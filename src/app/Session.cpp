#include "Session.h"

#include <algorithm>
#include <cmath>

#include "KeymapLoader.h"
#include "KitLoader.h"
#include "MusicFile.h"
#include "Text.h"

namespace
{

constexpr double maxTrackSeconds = 20.0 * 60.0;
constexpr double replayLookAheadSeconds = 0.25;

bool isNumber (const juce::var& value)
{
    return value.isDouble() || value.isInt() || value.isInt64();
}

double numberOr (const juce::var& value, double fallback)
{
    return isNumber (value) ? static_cast<double> (value) : fallback;
}

/** 파일이나 폴더 이름으로 쓸 수 있게 다듬는다. */
juce::String toFileName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).trim();
}

} // namespace

//==============================================================================
Session::Session (const Options& options)
    : dataRoot (options.dataRoot),
      remember (options.rememberSettings),
      charts (options.dataRoot, options.chartsFolder)
{
    for (auto& pad : midiPads)
        pad.store (-1, std::memory_order_relaxed);

    settings = juce::var (new juce::DynamicObject());

    if (remember)
        loadSettings();

    backups.setFolders (options.backupsFolder, dataRoot);

    // 연주 로그. 파일은 처음 칠 때 만든다.
    playLogFolder = options.playLogFolder;

    if (options.playLogFolder != juce::File() && options.writePlayLog && static_cast<bool> (getSetting ("playLog", true)))
    {
        playLog.start (options.playLogFolder);
        logKeysWanted.store (playLog.isActive(), std::memory_order_relaxed);
    }

    kitFolder = getKitsFolder().getChildFile (getSetting ("kit", "default").toString());

    if (! getKitFile().existsAsFile())
        kitFolder = getKitsFolder().getChildFile ("default");

    keymapFile = options.keymapFile;

    if (keymapFile == juce::File())
    {
        keymapFile = getKeymapsFolder().getChildFile (getSetting ("keymap", "default").toString() + ".json");

        if (! keymapFile.existsAsFile())
            keymapFile = getKeymapsFolder().getChildFile ("default.json");
    }

    // 음악 폴더는 여기서 정하기만 하고, 곡 목록은 리듬 게임 화면을 처음 열 때 만든다.
    if (options.musicFolder != juce::File())
        charts.setMusicFolderWithoutScan (options.musicFolder);
    else if (const auto saved = getSetting ("musicFolder", {}).toString(); saved.isNotEmpty() && juce::File (saved).isDirectory())
        charts.setMusicFolderWithoutScan (juce::File (saved));

    audio.setMasterGain (juce::jlimit (0.0f, AudioHost::maxMasterGain,
                                       static_cast<float> (numberOr (getSetting ("masterGain", {}), AudioHost::defaultMasterGain))));
    track.setGain (static_cast<float> (numberOr (getSetting ("trackGain", 0.7), 0.7)));

    audioError = audio.start (remember);
    audio.getDeviceManager().addMidiInputDeviceCallback ({}, this);

    reloadKit();

    // 등록해 둔 페달
    if (const auto saved = getSetting ("pedal", {}); saved.isObject())
    {
        PedalSetup setup;
        setup.device = saved["device"].toString();

        if (const auto* names = saved["keys"].getArray())
            for (const auto& name : *names)
                if (const auto key = hvd::keyFromName (name.toString().toStdString()))
                    setup.keys.push_back (*key);

        pedal = setup;
        applyPedal();
    }

    if (options.startKeyboard)
    {
        // 창 핸들을 알기 전에는 키를 받지 않는다.
        keyboard.setRequireActiveWindow (true);
        keyboardRegistered = keyboard.start ([this] (hvd::KeyCode key, bool down) { handleKey (key, down); },
                                             [this] (hvd::RawKeyboard::MouseButton button, int x, int y) { handleMouse (button, x, y); });
    }
    else
    {
        keyboardRegistered = true;    // 검사 모드에서는 입력 스레드를 쓰지 않는다
    }
}

Session::~Session()
{
    keyboard.stop();       // 입력 스레드가 엔진에 더 보내지 않게 먼저 멈춘다
    logKeysWanted.store (false, std::memory_order_relaxed);
    writeLogNote (beginLogLine ("end"));
    playLog.stop();
    audio.getDeviceManager().removeMidiInputDeviceCallback ({}, this);
    audio.stop();          // 오디오 콜백이 엔진과 킷을 더 쓰지 않게 멈춘다 (녹음 중이면 파일도 닫는다)
    engine.detachKit();    // 이제 ownedKits와 ownedTracks가 지워져도 안전하다
    track.detachTrack();
}

//==============================================================================
juce::File Session::getSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("HogyunsVirtualDrum")
               .getChildFile ("settings.json");
}

void Session::loadSettings()
{
    juce::var parsed;

    if (getSettingsFile().existsAsFile()
        && juce::JSON::parse (getSettingsFile().loadFileAsString(), parsed).wasOk()
        && parsed.isObject())
        settings = parsed;
}

void Session::saveSettings() const
{
    if (! remember)
        return;

    const auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (settings));
}

juce::var Session::getSetting (const juce::Identifier& name, const juce::var& fallback) const
{
    const auto& value = settings[name];
    return value.isVoid() ? fallback : value;
}

void Session::setSetting (const juce::Identifier& name, const juce::var& value)
{
    if (auto* object = settings.getDynamicObject(); object != nullptr && object->getProperty (name) != value)
    {
        object->setProperty (name, value);
        saveSettings();
    }
}

//==============================================================================
void Session::poll()
{
    releaseRetired();

    // 장치가 꽂히거나 빠졌으면 페달이 연결돼 있는지 다시 본다.
    if (keyboard.getDeviceChangeCount() != seenDeviceChanges)
        updatePedalConnection();

    // 로그에는 상황, 키, 타격의 순서로 적는다. 그래야 누른 키가 그 키가 낸 타격보다 앞에 온다.
    logSituation();
    logKeys();
    drainHitEvents();

    if (logPollCount % 60 == 0)
        playLog.flush();

    charts.poll();
    outputPeak = audio.takePeak();

    // 키 지정용으로 잡아 둔 키가 있으면 화면 쪽에 넘긴다.
    if (const auto key = capturedKey.exchange (-1, std::memory_order_relaxed); key >= 0 && captureCallback != nullptr)
    {
        auto callback = std::move (captureCallback);
        captureCallback = nullptr;
        callback (static_cast<hvd::KeyCode> (key));
    }

    if (replay.isActive())
    {
        const auto lookAhead = static_cast<std::uint32_t> (engine.getSampleRate() * replayLookAheadSeconds);

        if (! replay.pump (engine, engine.getClock(), lookAhead))
            sendChangeMessage();    // 다시 재생이 끝났다
    }

    // 킷이나 키 배열 파일을 밖에서 고쳐 저장하면 바로 다시 읽는다.
    if (++pollCount % 30 == 0)
    {
        if (getKitFile().getLastModificationTime() != kitFileTime)
            reloadKit();
        else if (keymapFile.getLastModificationTime() != keymapFileTime)
            reloadKeymap();
    }
}

void Session::releaseRetired()
{
    while (const auto* retired = engine.popRetiredKit())
        std::erase_if (ownedKits, [retired] (const auto& kit) { return kit.get() == retired; });

    while (const auto* retired = track.popRetiredTrack())
        std::erase_if (ownedTracks, [retired] (const auto& data) { return data.get() == retired; });
}

void Session::drainHitEvents()
{
    const auto sampleRate = engine.getSampleRate();
    const auto measuring = engine.isMetronomeOn() && ! replay.isActive();
    const auto logging = playLog.isActive();
    const auto sync = logging ? audio.getTrackSync() : AudioHost::TrackSync();
    hvd::HitEvent event;

    while (engine.popHitEvent (event))
    {
        if (shownKit == nullptr || event.pad >= shownKit->pads.size())
            continue;

        if (logging)
            logHit (event, sync);

        if (onHit != nullptr)
            onHit (event);

        if (takeRecording && ! replay.isActive() && event.time >= takeStartClock)
            take.push_back ({ static_cast<double> (event.time - takeStartClock) / sampleRate,
                              juce::String (shownKit->pads[event.pad].id),
                              event.velocity });

        if (measuring)
        {
            // 가장 가까운 16분음표 자리에서 얼마나 벗어났는지 잰다.
            const auto grid = sampleRate * 60.0 / static_cast<double> (engine.getTempo()) / 4.0;
            auto offset = std::fmod (static_cast<double> (event.time) - static_cast<double> (engine.getLastBeatTime()), grid);

            if (offset < 0.0)
                offset += grid;

            if (offset > grid * 0.5)
                offset -= grid;

            const auto ms = static_cast<float> (offset / sampleRate * 1000.0);
            timing.offsetsMs[static_cast<std::size_t> (timing.count % Timing::historySize)] = ms;
            timing.lastMs = ms;
            ++timing.count;

            const auto used = std::min (timing.count, Timing::historySize);
            auto sum = 0.0f;

            for (int i = 0; i < used; ++i)
                sum += std::abs (timing.offsetsMs[static_cast<std::size_t> (i)]);

            timing.averageAbsMs = sum / static_cast<float> (used);
        }
    }
}

//==============================================================================
juce::StringArray Session::getKitFolderNames() const
{
    juce::StringArray names;

    for (const auto& entry : juce::RangedDirectoryIterator (getKitsFolder(), false, "*", juce::File::findDirectories))
        if (entry.getFile().getChildFile ("kit.json").existsAsFile())
            names.add (entry.getFile().getFileName());

    names.sortNatural();

    // 기본 킷을 맨 앞에 둔다.
    if (const auto index = names.indexOf ("default"); index > 0)
        names.move (index, 0);

    return names;
}

void Session::selectKit (const juce::String& folderName)
{
    const auto folder = getKitsFolder().getChildFile (folderName);

    if (! folder.getChildFile ("kit.json").existsAsFile() || folder == kitFolder)
        return;

    kitFolder = folder;
    setSetting ("kit", folderName);
    muted.clear();
    solo.clear();
    reloadKit();
}

juce::String Session::duplicateKit (const juce::String& newName)
{
    const auto folderName = toFileName (newName);

    if (folderName.isEmpty())
        return ko ("이름을 적어 주세요");

    const auto target = getKitsFolder().getChildFile (folderName);

    if (target.exists())
        return ko ("같은 이름의 킷이 이미 있습니다: ") + folderName;

    if (! kitFolder.copyDirectoryTo (target))
        return ko ("킷 폴더를 복사하지 못했습니다");

    KitDocument copy;

    if (copy.load (target.getChildFile ("kit.json")))
    {
        copy.setName (newName.trim());
        copy.save();
    }

    selectKit (folderName);
    return {};
}

juce::String Session::exportKit (const juce::File& zipFile) const
{
    juce::ZipFile::Builder builder;
    const auto prefix = kitFolder.getFileName() + "/";

    for (const auto& entry : juce::RangedDirectoryIterator (kitFolder, true, "*", juce::File::findFiles))
    {
        const auto file = entry.getFile();

        // 샘플(FLAC, MP3 등)은 이미 압축돼 있으므로 다시 압축하지 않는다.
        const auto level = file.hasFileExtension ("json;txt;wav;aif;aiff") ? 6 : 0;
        builder.addFile (file, level, prefix + file.getRelativePathFrom (kitFolder).replaceCharacter ('\\', '/'));
    }

    zipFile.deleteFile();
    juce::FileOutputStream stream (zipFile);

    if (! stream.openedOk() || ! builder.writeToStream (stream, nullptr))
        return ko ("파일을 쓰지 못했습니다: ") + zipFile.getFullPathName();

    return {};
}

juce::String Session::importKit (const juce::File& zipFile)
{
    juce::ZipFile zip (zipFile);

    if (zip.getNumEntries() == 0)
        return ko ("ZIP 파일을 읽지 못했습니다");

    // 임시 폴더에 풀고, kit.json이 들어 있는 폴더를 kits/ 아래로 옮긴다.
    const auto temp = getKitsFolder().getChildFile (".importing");
    temp.deleteRecursively();

    if (const auto result = zip.uncompressTo (temp); result.failed())
    {
        temp.deleteRecursively();
        return ko ("압축을 풀지 못했습니다: ") + result.getErrorMessage();
    }

    juce::File found;

    for (const auto& entry : juce::RangedDirectoryIterator (temp, true, "kit.json", juce::File::findFiles))
        if (found == juce::File() || entry.getFile().getFullPathName().length() < found.getFullPathName().length())
            found = entry.getFile();

    if (found == juce::File())
    {
        temp.deleteRecursively();
        return ko ("ZIP 안에 kit.json이 없습니다");
    }

    const auto source = found.getParentDirectory();
    auto name = source == temp ? zipFile.getFileNameWithoutExtension() : source.getFileName();
    name = toFileName (name);

    const auto target = getKitsFolder().getNonexistentChildFile (name.isEmpty() ? "imported" : name, {}, false);
    const auto moved = source.moveFileTo (target);
    temp.deleteRecursively();

    if (! moved)
        return ko ("킷 폴더를 옮기지 못했습니다");

    selectKit (target.getFileName());
    return {};
}

void Session::reloadKit()
{
    kitMessages.clear();
    stopTakePlayback();

    const auto kitFile = getKitFile();
    auto result = loadKit (kitFile);

    for (const auto& warning : result.warnings)
        kitMessages.add (ko ("킷: ") + warning);

    if (result.kit != nullptr)
    {
        // 킷이 바뀌는 동안 예전 패드 번호로 치지 않도록 키 배열을 먼저 비운다.
        performer.setMapping (nullptr);
        shownMapping = nullptr;

        kitStats.numSamples = static_cast<int> (result.kit->samples.size());
        kitStats.totalSeconds = result.totalSeconds;
        kitStats.averageTrimMs = result.averageTrimMs;
        kitStats.memoryBytes = result.memoryBytes;

        shownKit = result.kit.get();
        ownedKits.emplace_back (result.kit.release());

        hvd::EngineCommand command;
        command.type = hvd::EngineCommand::Type::setKit;
        command.kit = shownKit;
        engine.postFromControl (command);

        kitDocument.load (kitFile);

        if (muted.size() != shownKit->pads.size())
        {
            muted.assign (shownKit->pads.size(), false);
            solo.assign (shownKit->pads.size(), false);
        }

        // 뮤트나 솔로가 걸려 있으면 새 킷에도 이어서 적용한다.
        if (std::find (muted.begin(), muted.end(), true) != muted.end() || std::find (solo.begin(), solo.end(), true) != solo.end())
            pushAllPadParams();

        applyFxFromDocument();
        rebuildMidiTable();
        logKit();
    }
    else
    {
        kitMessages.add (ko ("킷을 읽지 못했습니다: ") + result.error);
    }

    kitFileTime = kitFile.getLastModificationTime();
    noteContent (kitFile, rememberedKit);
    reloadKeymap();
}

juce::String Session::getPadId (int pad) const
{
    if (shownKit == nullptr || ! juce::isPositiveAndBelow (pad, static_cast<int> (shownKit->pads.size())))
        return {};

    return juce::String (shownKit->pads[static_cast<std::size_t> (pad)].id);
}

double Session::getPadValue (int pad, const juce::Identifier& field) const
{
    static const juce::Identifier volume ("volume"), reverbSend ("reverbSend"), velocityCurve ("velocityCurve");

    auto fallback = 0.0;

    if (field == volume || field == reverbSend)
        fallback = 1.0;
    else if (field == velocityCurve)
        fallback = numberOr (kitDocument.getProperty (velocityCurve), 1.0);

    return kitDocument.getPadNumber (getPadId (pad), field, fallback);
}

void Session::setPadValue (int pad, const juce::Identifier& field, double value)
{
    const auto id = getPadId (pad);

    if (id.isEmpty())
        return;

    kitDocument.setPadNumber (id, field, value);
    pushPadParams (pad);
}

void Session::pushPadParams (int pad)
{
    if (getPadId (pad).isEmpty())
        return;

    const auto index = static_cast<std::size_t> (pad);
    const auto anySolo = std::find (solo.begin(), solo.end(), true) != solo.end();

    hvd::PadParams params;
    params.volume = static_cast<float> (juce::jlimit (0.0, static_cast<double> (hvd::maxPadVolume), getPadValue (pad, "volume")));
    params.pan = static_cast<float> (juce::jlimit (-1.0, 1.0, getPadValue (pad, "pan")));
    params.pitchRatio = static_cast<float> (std::pow (2.0, juce::jlimit (-24.0, 24.0, getPadValue (pad, "pitch")) / 12.0));
    params.reverbSend = static_cast<float> (juce::jlimit (0.0, 1.0, getPadValue (pad, "reverbSend")));
    params.startSeconds = static_cast<float> (juce::jlimit (0.0, 5000.0, getPadValue (pad, "startMs")) * 0.001);
    params.velocityCurve = static_cast<float> (juce::jlimit (0.0, 4.0, getPadValue (pad, "velocityCurve")));
    params.mute = (index < muted.size() && muted[index]) || (anySolo && index < solo.size() && ! solo[index]);

    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::setPadParams;
    command.pad = static_cast<std::uint16_t> (pad);
    command.padParams = params;
    engine.postFromControl (command);
}

void Session::pushAllPadParams()
{
    if (shownKit != nullptr)
        for (int pad = 0; pad < static_cast<int> (shownKit->pads.size()); ++pad)
            pushPadParams (pad);
}

void Session::setPadMuted (int pad, bool shouldBeMuted)
{
    if (juce::isPositiveAndBelow (pad, static_cast<int> (muted.size())))
    {
        muted[static_cast<std::size_t> (pad)] = shouldBeMuted;
        pushPadParams (pad);
    }
}

void Session::setPadSolo (int pad, bool shouldBeSolo)
{
    if (juce::isPositiveAndBelow (pad, static_cast<int> (solo.size())))
    {
        solo[static_cast<std::size_t> (pad)] = shouldBeSolo;
        pushAllPadParams();    // 다른 패드들이 들리는지도 함께 바뀐다
    }
}

bool Session::isPadMuted (int pad) const
{
    return juce::isPositiveAndBelow (pad, static_cast<int> (muted.size())) && muted[static_cast<std::size_t> (pad)];
}

bool Session::isPadSolo (int pad) const
{
    return juce::isPositiveAndBelow (pad, static_cast<int> (solo.size())) && solo[static_cast<std::size_t> (pad)];
}

void Session::setFx (const hvd::FxSettings& fx)
{
    engine.setFxSettings (fx);

    auto* object = new juce::DynamicObject();
    object->setProperty ("eqLow", fx.eqLowDb);
    object->setProperty ("eqMid", fx.eqMidDb);
    object->setProperty ("eqHigh", fx.eqHighDb);
    object->setProperty ("comp", fx.compAmount);
    object->setProperty ("reverb", fx.reverbLevel);
    object->setProperty ("reverbSize", fx.reverbSize);
    object->setProperty ("reverbDamp", fx.reverbDamp);
    kitDocument.setProperty ("fx", juce::var (object));
}

void Session::applyFxFromDocument()
{
    const auto stored = kitDocument.getProperty ("fx");
    hvd::FxSettings fx;

    fx.eqLowDb = static_cast<float> (juce::jlimit (-18.0, 18.0, numberOr (stored["eqLow"], 0.0)));
    fx.eqMidDb = static_cast<float> (juce::jlimit (-18.0, 18.0, numberOr (stored["eqMid"], 0.0)));
    fx.eqHighDb = static_cast<float> (juce::jlimit (-18.0, 18.0, numberOr (stored["eqHigh"], 0.0)));
    fx.compAmount = static_cast<float> (juce::jlimit (0.0, 1.0, numberOr (stored["comp"], 0.0)));
    fx.reverbLevel = static_cast<float> (juce::jlimit (0.0, 1.0, numberOr (stored["reverb"], 0.0)));
    fx.reverbSize = static_cast<float> (juce::jlimit (0.0, 1.0, numberOr (stored["reverbSize"], 0.5)));
    fx.reverbDamp = static_cast<float> (juce::jlimit (0.0, 1.0, numberOr (stored["reverbDamp"], 0.5)));
    engine.setFxSettings (fx);
}

bool Session::saveKit (bool reload)
{
    if (! kitDocument.save())
    {
        kitMessages.add (ko ("킷 파일을 저장하지 못했습니다: ") + getKitFile().getFullPathName());
        messages = kitMessages;
        messages.addArray (keymapMessages);
        sendChangeMessage();
        return false;
    }

    if (reload)
    {
        reloadKit();
    }
    else
    {
        kitFileTime = getKitFile().getLastModificationTime();    // 방금 저장한 것을 바깥에서 고친 것으로 보지 않게 한다
        noteContent (getKitFile(), rememberedKit);
    }

    return true;
}

void Session::previewPad (int pad, float velocity, hvd::HitSource source, bool followPedals)
{
    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::hit;
    command.pad = static_cast<std::uint16_t> (pad);
    command.velocity = velocity;
    command.origin.source = source;

    // 연주 화면에서 마우스로 친 것은 키로 친 것처럼 페달을 따른다 (하이햇 페달을 떼면 반열림 등).
    if (source == hvd::HitSource::mouse && followPedals)
        command.pad = performer.swappedForScreen (command.pad);

    engine.postFromControl (command);
}

void Session::chokePads (const std::vector<std::uint16_t>& pads)
{
    for (const auto pad : pads)
    {
        hvd::EngineCommand command;
        command.type = hvd::EngineCommand::Type::chokePad;
        command.pad = pad;
        engine.postFromControl (command);
    }
}

void Session::setMouseTargets (std::shared_ptr<const hvd::MouseTargets> targets)
{
    mouseTargets.store (std::move (targets));
}

bool Session::isMouseTakenByInput() const
{
    return keyboard.isReadingMouse() && mouseTakeover.load (std::memory_order_relaxed) && mouseTargets.load() != nullptr;
}

void Session::giveUpMouseTakeover()
{
    mouseTakeover.store (false, std::memory_order_relaxed);
    mouseTargets.store (nullptr);
}

int Session::takeMouseSpot (int pad)
{
    if (! juce::isPositiveAndBelow (pad, hvd::Engine::maxPads))
        return -1;

    return mouseSpots[static_cast<std::size_t> (pad)].exchange (0, std::memory_order_acquire) - 1;
}

void Session::noteMouseSpot (int pad, int spot)
{
    if (juce::isPositiveAndBelow (pad, hvd::Engine::maxPads))
        mouseSpots[static_cast<std::size_t> (pad)].store (spot + 1, std::memory_order_release);
}

void Session::handleMouse (hvd::RawKeyboard::MouseButton button, int x, int y)
{
    // 입력 스레드에서 불린다. 화면이 넘겨 둔 자리에서 누른 자리를 찾아 바로 친다.
    const auto targets = mouseTargets.load();

    if (targets == nullptr || ! mouseTakeover.load (std::memory_order_relaxed))
        return;

    // 화소의 가운데를 누른 것으로 본다.
    const auto* target = targets->find (static_cast<float> (x) + 0.5f, static_cast<float> (y) + 0.5f);

    if (target == nullptr)
        return;

    const auto right = button == hvd::RawKeyboard::MouseButton::right;

    if (right)
    {
        performer.choke (target->chokes);
    }
    else
    {
        // 페달을 따르지 않는 자리는 화면이 그 자리를 번쩍이도록 적어 둔다. 타격보다 먼저 적는다.
        if (! target->followPedals)
            noteMouseSpot (target->pad, target->spot);

        performer.mouseHit (target->pad, target->velocity, target->followPedals);
    }

    rawClicks.push ({ target->spot, right, juce::Time::getMillisecondCounterHiRes() });    // 꽉 차면 버린다 (화면이 가려진 동안)
}

void Session::allSoundOff()
{
    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::allSoundOff;
    engine.postFromControl (command);
}

void Session::rebuildMidiTable()
{
    for (auto& pad : midiPads)
        pad.store (-1, std::memory_order_relaxed);

    if (shownKit == nullptr)
        return;

    for (std::size_t i = 0; i < shownKit->pads.size(); ++i)
    {
        const auto note = shownKit->pads[i].midiNote;

        if (juce::isPositiveAndBelow (note, 128) && midiPads[static_cast<std::size_t> (note)].load (std::memory_order_relaxed) < 0)
            midiPads[static_cast<std::size_t> (note)].store (static_cast<int> (i), std::memory_order_relaxed);
    }
}

void Session::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // MIDI 장치의 스레드에서 불린다. 전자 드럼 패드나 건반으로 패드를 친다.
    if (! message.isNoteOn())
        return;

    const auto pad = midiPads[static_cast<std::size_t> (message.getNoteNumber() & 127)].load (std::memory_order_relaxed);

    if (pad < 0)
        return;

    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::hit;
    command.pad = static_cast<std::uint16_t> (pad);
    command.velocity = message.getFloatVelocity();
    command.origin.source = hvd::HitSource::midi;

    // 장치가 여러 개면 스레드도 여러 개일 수 있으므로 한 번에 하나만 큐에 넣게 한다.
    const juce::SpinLock::ScopedLockType lock (midiLock);
    engine.postFromAux (command);
}

//==============================================================================
juce::StringArray Session::getKeymapNames() const
{
    juce::StringArray names;

    for (const auto& entry : juce::RangedDirectoryIterator (getKeymapsFolder(), false, "*.json", juce::File::findFiles))
        names.add (entry.getFile().getFileNameWithoutExtension());

    names.sortNatural();

    if (const auto index = names.indexOf ("default"); index > 0)
        names.move (index, 0);

    return names;
}

void Session::selectKeymap (const juce::String& name)
{
    const auto file = getKeymapsFolder().getChildFile (name + ".json");

    if (! file.existsAsFile() || file == keymapFile)
        return;

    keymapFile = file;
    setSetting ("keymap", name);
    reloadKeymap();
}

juce::String Session::duplicateKeymap (const juce::String& newName)
{
    const auto fileName = toFileName (newName);

    if (fileName.isEmpty())
        return ko ("이름을 적어 주세요");

    const auto target = getKeymapsFolder().getChildFile (fileName + ".json");

    if (target.exists())
        return ko ("같은 이름의 키 배열이 이미 있습니다: ") + fileName;

    if (! keymapFile.copyFileTo (target))
        return ko ("키 배열 파일을 복사하지 못했습니다");

    KeymapDocument copy;

    if (copy.load (target))
    {
        copy.setName (newName.trim());
        copy.save();
    }

    selectKeymap (fileName);
    return {};
}

bool Session::saveKeymap()
{
    const auto ok = keymapDocument.save();
    reloadKeymap();

    if (! ok)
    {
        keymapMessages.add (ko ("키 배열 파일을 저장하지 못했습니다: ") + keymapFile.getFullPathName());
        messages = kitMessages;
        messages.addArray (keymapMessages);
    }

    return ok;
}

void Session::reloadKeymap()
{
    keymapMessages.clear();
    keymapFileTime = keymapFile.getLastModificationTime();

    if (shownKit != nullptr)
    {
        const auto result = loadKeymap (keymapFile);

        for (const auto& warning : result.warnings)
            keymapMessages.add (ko ("키 배열: ") + warning);

        if (result.ok)
        {
            keymapDocument.load (keymapFile);

            std::vector<std::string> warnings;
            shownMapping = hvd::resolveMapping (result.spec, *shownKit, warnings);

            for (const auto& warning : warnings)
                keymapMessages.add (ko ("키 배열: ") + fromUtf8 (warning));

            performer.setMapping (shownMapping);
            engine.setBeatsPerBar (shownMapping->beatsPerBar);

            // 파일의 템포가 바뀌었을 때만 적용한다. 그래야 탭으로 맞춘 템포가 다시 읽을 때마다 되돌아가지 않는다.
            if (shownMapping->tempo != loadedTempo)
            {
                loadedTempo = shownMapping->tempo;
                engine.setTempo (loadedTempo);
            }

            logKeymap();
        }
        else
        {
            keymapMessages.add (ko ("키 배열을 읽지 못했습니다: ") + result.error);
        }
    }

    noteContent (keymapFile, rememberedKeymap);

    messages = kitMessages;
    messages.addArray (keymapMessages);
    sendChangeMessage();
}

//==============================================================================
// 백업
void Session::noteContent (const juce::File& file, Remembered& remembered, bool evenIfRecent)
{
    if (! backups.isEnabled() || ! file.existsAsFile())
        return;

    const auto text = file.loadFileAsString();

    // 같은 파일의 내용이 달라졌으면 달라지기 전의 것을 남긴다. 다른 파일로 바꾼 것(킷이나 키 배열을 고름)은 남길 것이 없다.
    if (remembered.file == file && remembered.text.isNotEmpty() && remembered.text != text)
        backups.keep (file, remembered.text, evenIfRecent);

    remembered.file = file;
    remembered.text = text;
}

juce::String Session::restoreBackup (const juce::File& backup, const juce::File& target, Remembered& remembered)
{
    if (! backup.existsAsFile())
        return ko ("백업 파일이 없습니다: ") + backup.getFileName();

    // 되돌리기 전의 상태를 먼저 남긴다. 방금 남긴 백업이 있어도 남겨서, 되돌린 것을 다시 되돌릴 수 있게 한다.
    if (target.existsAsFile())
        backups.keep (target, target.loadFileAsString(), true);

    if (! backup.copyFileTo (target))
        return ko ("백업을 복사하지 못했습니다: ") + target.getFullPathName();

    remembered.file = target;
    remembered.text = target.loadFileAsString();
    return {};
}

juce::String Session::restoreKitBackup (const juce::File& backup)
{
    const auto error = restoreBackup (backup, getKitFile(), rememberedKit);

    if (error.isEmpty())
        reloadKit();

    return error;
}

juce::String Session::restoreKeymapBackup (const juce::File& backup)
{
    const auto error = restoreBackup (backup, keymapFile, rememberedKeymap);

    if (error.isEmpty())
        reloadKeymap();

    return error;
}

//==============================================================================
void Session::handleKey (hvd::KeyCode key, bool down)
{
    // 입력 스레드에서 불린다. 화면에 손대지 않고 연주 로직에 넘기기만 한다.
    if (key >= hvd::numKeyCodes)
        return;

    const auto time = hvd::Performer::now();

    if (held[key].exchange (down, std::memory_order_relaxed) != down)
    {
        const auto count = heldCount.fetch_add (down ? 1 : -1, std::memory_order_relaxed) + (down ? 1 : -1);

        if (count > maxHeldCount.load (std::memory_order_relaxed))
            maxHeldCount.store (count, std::memory_order_relaxed);
    }

    if (down)
    {
        lastKey.store (key, std::memory_order_relaxed);
        keyPressCount.fetch_add (1, std::memory_order_relaxed);

        // 키 지정을 기다리는 중이면 이 키는 연주하지 않고 화면 쪽에 넘긴다.
        if (captureArmed.exchange (false, std::memory_order_relaxed))
        {
            capturedKey.store (key, std::memory_order_relaxed);
            return;
        }

        if (playingSuspended.load (std::memory_order_relaxed))
            return;

        forwarded[key] = true;

        if (logKeysWanted.load (std::memory_order_relaxed))
            loggedKeys.push (LoggedKey { engine.getClock(), key, true });

        performer.keyDown (key, time);
    }
    else if (forwarded[key])
    {
        // 누름을 넘긴 키만 뗌도 넘긴다. 그래야 뗄 때 치는 방식의 키가 엉뚱하게 울리지 않는다.
        forwarded[key] = false;

        if (logKeysWanted.load (std::memory_order_relaxed))
            loggedKeys.push (LoggedKey { engine.getClock(), key, false });

        performer.keyUp (key, time);
    }
}

bool Session::isKeyHeld (hvd::KeyCode key) const noexcept
{
    return key < hvd::numKeyCodes && held[key].load (std::memory_order_relaxed);
}

void Session::armKeyCapture (std::function<void (hvd::KeyCode)> callback)
{
    captureCallback = std::move (callback);
    capturedKey.store (-1, std::memory_order_relaxed);
    captureArmed.store (true, std::memory_order_relaxed);
}

void Session::cancelKeyCapture()
{
    captureArmed.store (false, std::memory_order_relaxed);
    capturedKey.store (-1, std::memory_order_relaxed);
    captureCallback = nullptr;
}

//==============================================================================
void Session::applyPedal()
{
    if (pedal.keys.size() > static_cast<std::size_t> (hvd::maxPedalKeys))
        pedal.keys.resize (static_cast<std::size_t> (hvd::maxPedalKeys));

    if (pedal.device.isEmpty() || pedal.keys.empty())
        pedal = {};

    keyboard.setPedal (hvd::RawKeyboard::deviceIdOf (pedal.device.toStdString()), pedal.keys);
    updatePedalConnection();
}

void Session::updatePedalConnection()
{
    seenDeviceChanges = keyboard.getDeviceChangeCount();
    performer.setFootswitchConnected (isPedalConnected());
}

void Session::setPedal (const PedalSetup& newPedal)
{
    pedal = newPedal;
    applyPedal();

    auto* saved = new juce::DynamicObject();
    juce::Array<juce::var> names;

    for (const auto key : pedal.keys)
        names.add (fromUtf8 (hvd::keyName (key)));

    saved->setProperty ("device", pedal.device);
    saved->setProperty ("keys", names);
    setSetting ("pedal", juce::var (saved));
}

bool Session::isPedalConnected() const
{
    const auto id = keyboard.getPedalDevice();

    if (id == 0)
        return false;

    const auto devices = hvd::RawKeyboard::listDevices();
    return std::any_of (devices.begin(), devices.end(), [id] (const auto& device) { return device.id == id; });
}

Session::PedalSetup Session::getPedalLearning (bool& cancelled) const
{
    const auto learned = keyboard.getPedalLearning();
    cancelled = learned.cancelled;

    PedalSetup setup;
    setup.keys = learned.keys;

    // 입력 스레드는 장치를 값으로만 알려 준다. 이름은 꽂혀 있는 장치들에서 찾는다.
    if (learned.device != 0)
        for (const auto& device : hvd::RawKeyboard::listDevices())
            if (device.id == learned.device)
                setup.device = juce::String (device.identity);

    return setup;
}

bool Session::endPedalLearning (bool keep)
{
    if (! keyboard.isPedalLearning())
        return false;

    auto cancelled = false;
    const auto learned = getPedalLearning (cancelled);
    keyboard.endPedalLearning();

    if (! keep || cancelled || learned.device.isEmpty() || learned.keys.empty())
        return false;

    setPedal (learned);
    return true;
}

hvd::ReportInterval Session::getReportInterval (bool ofPedal) const
{
    const auto times = keyboard.getReportTimes (ofPedal);
    return hvd::estimateReportInterval (times);
}

//==============================================================================
void Session::setMasterGain (float gain, bool save)
{
    audio.setMasterGain (juce::jlimit (0.0f, AudioHost::maxMasterGain, gain));

    if (save)
        rememberMasterGain();
}

juce::String Session::loadTrack (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto reader = createMusicReader (formats, file);

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples < 4)
        return ko ("읽을 수 없는 음악 파일입니다: ") + file.getFileName();

    const auto frames = std::min (reader->lengthInSamples, static_cast<juce::int64> (maxTrackSeconds * reader->sampleRate));

    auto data = std::make_unique<hvd::TrackData>();
    data->sampleRate = reader->sampleRate;
    data->samples.resize (static_cast<std::size_t> (frames) * 2);

    // 한 번에 조금씩 읽어 16비트로 바꿔 담는다. (긴 곡을 실수로 통째로 두면 메모리를 두 배로 쓴다)
    constexpr int chunk = 65536;
    juce::AudioBuffer<float> buffer (2, chunk);

    for (juce::int64 position = 0; position < frames; position += chunk)
    {
        const auto count = static_cast<int> (std::min<juce::int64> (chunk, frames - position));

        // MP3의 어림한 길이가 실제보다 길면 끝을 지나 읽을 때 실패가 돌아온다. 그때는 읽은 데까지만 쓴다.
        if (! reader->read (&buffer, 0, count, position, true, true))
        {
            if (position < static_cast<juce::int64> (reader->sampleRate))
                return ko ("음악 파일을 읽다가 실패했습니다: ") + file.getFileName();

            data->samples.resize (static_cast<std::size_t> (position) * 2);
            break;
        }

        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getReadPointer (1);
        auto* target = data->samples.data() + static_cast<std::size_t> (position) * 2;

        for (int i = 0; i < count; ++i)
        {
            target[i * 2] = static_cast<std::int16_t> (juce::jlimit (-32768.0f, 32767.0f, left[i] * 32767.0f));
            target[i * 2 + 1] = static_cast<std::int16_t> (juce::jlimit (-32768.0f, 32767.0f, right[i] * 32767.0f));
        }
    }

    track.setPlaying (false);
    track.setLoop (0.0, 0.0, false);

    if (! track.setTrack (data.get()))
        return ko ("반주를 바꾸지 못했습니다. 잠시 뒤에 다시 해 보세요.");

    ownedTracks.emplace_back (data.release());
    trackFile = file;
    sendChangeMessage();
    return {};
}

void Session::unloadTrack()
{
    track.setPlaying (false);
    track.setTrack (nullptr);
    trackFile = juce::File();
    sendChangeMessage();
}

void Session::setTrackGain (float gain)
{
    track.setGain (juce::jlimit (0.0f, 1.5f, gain));
    setSetting ("trackGain", track.getGain());
}

//==============================================================================
juce::String Session::startRecording (bool includeTrack)
{
    const auto name = "hvd-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".wav";
    const auto error = audio.startRecording (getRecordingsFolder().getChildFile (name), includeTrack);
    sendChangeMessage();
    return error;
}

void Session::stopRecording()
{
    audio.stopRecording();
    sendChangeMessage();
}

//==============================================================================
void Session::startTake()
{
    stopTakePlayback();
    take.clear();
    takeLength = 0.0;
    takeStartClock = engine.getClock();
    takeTrackPosition = track.isPlaying() ? track.getPosition() : -1.0;
    takeRecording = true;
    sendChangeMessage();
}

void Session::stopTake()
{
    if (! takeRecording)
        return;

    drainHitEvents();
    takeRecording = false;
    takeLength = static_cast<double> (engine.getClock() - takeStartClock) / engine.getSampleRate();
    sendChangeMessage();
}

void Session::playTake()
{
    stopTake();

    if (take.empty() || shownKit == nullptr)
        return;

    std::vector<hvd::ReplayEvent> events;
    events.reserve (take.size());

    for (const auto& hit : take)
        if (const auto pad = shownKit->findPad (hit.padId.toStdString()); pad >= 0)
            events.push_back ({ hit.seconds, static_cast<std::uint16_t> (pad), hit.velocity });

    // 기록할 때 반주가 나오고 있었으면 같은 자리에서 함께 재생한다.
    if (takeTrackPosition >= 0.0 && track.getLength() > 0.0)
    {
        track.seek (takeTrackPosition);
        track.setPlaying (true);
    }

    replay.start (std::move (events), engine.getClock(), engine.getSampleRate());
    sendChangeMessage();
}

void Session::stopTakePlayback()
{
    if (! replay.isActive())
        return;

    replay.stop();
    allSoundOff();    // 엔진에 미리 넘겨 둔 타격도 지운다

    if (takeTrackPosition >= 0.0)
        track.setPlaying (false);

    sendChangeMessage();
}

juce::String Session::exportTakeAsMidi (const juce::File& file) const
{
    if (take.empty() || shownKit == nullptr)
        return ko ("기록한 연주가 없습니다");

    constexpr int ticksPerQuarter = 480;
    const auto tempo = static_cast<double> (engine.getTempo());
    const auto ticksPerSecond = tempo / 60.0 * ticksPerQuarter;

    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / tempo)), 0.0);

    for (const auto& hit : take)
    {
        const auto pad = shownKit->findPad (hit.padId.toStdString());
        const auto note = pad >= 0 ? shownKit->pads[static_cast<std::size_t> (pad)].midiNote : -1;

        if (note < 0)
            continue;

        const auto tick = hit.seconds * ticksPerSecond;
        const auto velocity = static_cast<juce::uint8> (juce::jlimit (1, 127, juce::roundToInt (hit.velocity * 127.0f)));

        // 드럼은 10번 채널에 적는 것이 관례다.
        sequence.addEvent (juce::MidiMessage::noteOn (10, note, velocity), tick);
        sequence.addEvent (juce::MidiMessage::noteOff (10, note), tick + 60.0);
    }

    sequence.updateMatchedPairs();

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticksPerQuarter);
    midi.addTrack (sequence);

    file.deleteFile();
    juce::FileOutputStream stream (file);

    if (! stream.openedOk() || ! midi.writeTo (stream))
        return ko ("파일을 쓰지 못했습니다: ") + file.getFullPathName();

    return {};
}

juce::String Session::importTakeFromMidi (const juce::File& file)
{
    if (shownKit == nullptr)
        return ko ("킷이 없습니다");

    juce::FileInputStream stream (file);
    juce::MidiFile midi;

    if (! stream.openedOk() || ! midi.readFrom (stream))
        return ko ("MIDI 파일을 읽지 못했습니다: ") + file.getFileName();

    midi.convertTimestampTicksToSeconds();

    std::vector<TakeHit> hits;
    auto skipped = 0;

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        const auto* sequence = midi.getTrack (trackIndex);

        for (int i = 0; i < sequence->getNumEvents(); ++i)
        {
            const auto& message = sequence->getEventPointer (i)->message;

            if (! message.isNoteOn())
                continue;

            const auto pad = midiPads[static_cast<std::size_t> (message.getNoteNumber() & 127)].load (std::memory_order_relaxed);

            if (pad < 0)
            {
                ++skipped;
                continue;
            }

            hits.push_back ({ message.getTimeStamp(), getPadId (pad), message.getFloatVelocity() });
        }
    }

    if (hits.empty())
        return ko ("이 킷의 패드에 해당하는 노트가 없습니다");

    std::stable_sort (hits.begin(), hits.end(), [] (const TakeHit& a, const TakeHit& b) { return a.seconds < b.seconds; });

    stopTakePlayback();
    takeRecording = false;
    take = std::move (hits);
    takeLength = take.back().seconds + 1.0;
    takeTrackPosition = -1.0;
    sendChangeMessage();

    return skipped > 0 ? ko ("맞는 패드가 없어 건너뛴 노트: ") + juce::String (skipped) : juce::String();
}

//==============================================================================
// 연주 로그
//
// 줄마다 t(초)와 e(무슨 일인지)가 있다. t는 엔진 시계를 초로 바꾼 값이라 타격끼리는 샘플 단위로 정확하다.
//   hit     타격 하나: pad, v(세기), src(key / mouse / midi / screen / replay / auto), key(누른 키), song(곡 안의 위치)
//   down/up 키를 누르고 뗀 것: key
//   beat    메트로놈의 박: n
//   play / pause / seek / speed    반주의 진행: song, speed
//   start, audio, kit, keymap, tempo, track, game, end    그때의 상황
// song은 그 타격의 소리가 곡의 어느 자리에 놓였는지다 (녹음하면 그 자리에 들린다).

double Session::logSecondsAt (std::uint64_t clock) const noexcept
{
    const auto rate = logRate > 0.0 ? logRate : 48000.0;
    return logBaseSeconds + static_cast<double> (static_cast<std::int64_t> (clock - logBaseClock)) / rate;
}

PlayLog::Line Session::beginLogLine (const char* event) const
{
    return PlayLog::Line (logSecondsAt (engine.getClock()), event);
}

void Session::writeLogPlay (const PlayLog::Line& line)
{
    if (! playLog.hasFile())
    {
        if (! playLog.begin())
            return;

        logWholeSituation();
        playLog.writeKept();
    }

    playLog.write (line);
}

void Session::logWholeSituation()
{
    playLog.write (beginLogLine ("start").integer ("version", 1)
                                         .text ("time", juce::Time::getCurrentTime().toISO8601 (true)));
    logAudio (audio.getStatus());
    logKit();
    logKeymap();
    logTempo();
    logTrackFile();

    loggedSync = audio.getTrackSync();
    loggedSync.active = loggedSync.active && trackFile != juce::File();
    loggedBeatCount = engine.getBeatCount();

    if (loggedSync.active)
        playLog.write (PlayLog::Line (logSecondsAt (loggedSync.clock), "play").number ("song", loggedSync.position, 4)
                                                                                 .number ("speed", loggedSync.speed, 3));
}

void Session::logAudio (const AudioHost::Status& status)
{
    loggedAudio = status;

    const auto latencyMs = status.sampleRate > 0.0 ? 1000.0 * status.outputLatency / status.sampleRate : 0.0;

    playLog.write (beginLogLine ("audio").flag ("open", status.open)
                                         .text ("type", status.typeName)
                                         .text ("device", status.deviceName)
                                         .number ("rate", status.sampleRate, 0)
                                         .integer ("buffer", status.bufferSize)
                                         .number ("latencyMs", latencyMs, 2));
}

void Session::logKit()
{
    if (shownKit != nullptr)
        playLog.write (beginLogLine ("kit").text ("folder", getKitFolderName())
                                           .text ("name", fromUtf8 (shownKit->name))
                                           .integer ("pads", static_cast<juce::int64> (shownKit->pads.size())));
}

void Session::logKeymap()
{
    playLog.write (beginLogLine ("keymap").text ("name", getKeymapName()));
}

void Session::logTempo()
{
    loggedTempo = engine.getTempo();
    loggedBeatsPerBar = engine.getBeatsPerBar();
    loggedMetronome = engine.isMetronomeOn();

    playLog.write (beginLogLine ("tempo").number ("bpm", loggedTempo, 2)
                                         .integer ("beatsPerBar", loggedBeatsPerBar)
                                         .flag ("metronome", loggedMetronome));
}

void Session::logTrackFile()
{
    loggedTrackFile = trackFile;

    playLog.write (beginLogLine ("track").text ("file", trackFile.getFullPathName())
                                         .number ("length", trackFile != juce::File() ? track.getLength() : 0.0, 3));
}

void Session::logSituation()
{
    ++logPollCount;

    if (! playLog.isActive())
        return;

    // 장치의 샘플레이트가 바뀌면 그때부터 새 배율로 센다. 로그의 시각은 이어진다.
    if (const auto rate = engine.getSampleRate(); rate > 0.0 && rate != logRate)
    {
        const auto now = engine.getClock();
        logBaseSeconds = logSecondsAt (now);
        logBaseClock = now;
        logRate = rate;
    }

    if (! playLog.hasFile())
        return;    // 아직 친 것이 없다. 처음 칠 때 그때의 상황을 통째로 적는다

    // 장치와 템포는 0.5초에 한 번만 살핀다 (템포 슬라이더를 끄는 동안 줄이 쏟아지지 않게).
    if (logPollCount % 30 == 0)
    {
        const auto status = audio.getStatus();

        if (status.open != loggedAudio.open || status.typeName != loggedAudio.typeName || status.deviceName != loggedAudio.deviceName
            || status.sampleRate != loggedAudio.sampleRate || status.bufferSize != loggedAudio.bufferSize
            || status.outputLatency != loggedAudio.outputLatency)
            logAudio (status);

        if (engine.getTempo() != loggedTempo || engine.getBeatsPerBar() != loggedBeatsPerBar)
            logTempo();
    }

    if (engine.isMetronomeOn() != loggedMetronome)
        logTempo();

    // 메트로놈의 박. 타격이 박에서 얼마나 벗어났는지 나중에 잴 수 있게 한다.
    if (auto count = engine.getBeatCount(); count != loggedBeatCount)
    {
        auto time = engine.getLastBeatTime();

        // 읽는 사이에 다음 박이 쳐졌으면 다시 읽는다.
        for (int attempt = 0; attempt < 4 && engine.getBeatCount() != count; ++attempt)
        {
            count = engine.getBeatCount();
            time = engine.getLastBeatTime();
        }

        loggedBeatCount = count;

        if (loggedMetronome)
            playLog.write (PlayLog::Line (logSecondsAt (time), "beat").integer ("n", count));
    }

    // 반주. 곡을 연 것은 그 곡의 진행보다 먼저, 닫은 것은 멈춘 것 다음에 적는다.
    const auto loaded = trackFile != juce::File();
    const auto trackChanged = trackFile != loggedTrackFile;
    auto sync = audio.getTrackSync();
    sync.active = sync.active && loaded;    // 곡을 닫은 직후에는 오디오 쪽의 값이 닫기 전 그대로일 수 있다

    const auto at = logSecondsAt (sync.clock);

    if (trackChanged && loaded)
        logTrackFile();

    if (sync.active != loggedSync.active || (trackChanged && sync.active))
    {
        playLog.write (PlayLog::Line (at, sync.active ? "play" : "pause").number ("song", sync.position, 4)
                                                                         .number ("speed", sync.speed, 3));
    }
    else if (sync.active)
    {
        const auto expected = loggedSync.positionAt (sync.clock, engine.getSampleRate());

        if (std::abs (sync.speed - loggedSync.speed) > 1.0e-4f)
            playLog.write (PlayLog::Line (at, "speed").number ("song", sync.position, 4).number ("speed", sync.speed, 3));
        else if (std::abs (sync.position - expected) > 0.03)
            playLog.write (PlayLog::Line (at, "seek").number ("song", sync.position, 4).number ("from", expected, 4));
    }
    else if (loaded && ! trackChanged && std::abs (sync.position - loggedSync.position) > 0.03)
    {
        playLog.write (PlayLog::Line (at, "seek").number ("song", sync.position, 4).number ("from", loggedSync.position, 4));
    }

    if (trackChanged && ! loaded)
        logTrackFile();

    loggedSync = sync;
}

void Session::logKeys()
{
    LoggedKey entry;

    while (loggedKeys.pop (entry))
        playLog.writeOrKeep (PlayLog::Line (logSecondsAt (entry.clock), entry.down ? "down" : "up")
                                 .text ("key", fromUtf8 (hvd::keyName (entry.key))));
}

void Session::logHit (const hvd::HitEvent& event, const AudioHost::TrackSync& sync)
{
    static constexpr const char* sources[] = { "screen", "key", "midi", "replay", "auto", "mouse" };    // HitSource의 순서
    const auto source = static_cast<std::size_t> (event.origin.source);

    PlayLog::Line line (logSecondsAt (event.time), "hit");
    line.text ("pad", juce::String (shownKit->pads[event.pad].id))
        .number ("v", event.velocity, 3)
        .text ("src", source < std::size (sources) ? sources[source] : "screen");

    if (event.origin.source == hvd::HitSource::key && event.origin.key != hvd::invalidKey)
        line.text ("key", fromUtf8 (hvd::keyName (event.origin.key)));

    // 반주가 진행 중이면 이 타격의 소리가 곡의 어느 자리에 놓였는지 적는다. 준비 시간 동안에는 음수다.
    if (sync.active && trackFile != juce::File())
        line.number ("song", sync.positionAt (event.time, engine.getSampleRate()), 4);

    writeLogPlay (line);
}
