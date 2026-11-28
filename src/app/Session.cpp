#include "Session.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Json.h"
#include "KeymapLoader.h"
#include "KitLoader.h"
#include "MusicFile.h"
#include "SampleCache.h"
#include "Text.h"

namespace
{

constexpr double maxTrackSeconds = 20.0 * 60.0;

using json::numberOr;

/** 파일이나 폴더 이름으로 쓸 수 있게 다듬는다. */
juce::String toFileName (const juce::String& name)
{
    return juce::File::createLegalFileName (name.trim()).trim();
}

/** folder 안의 문서 이름: 킷(folders)이면 kit.json이 든 폴더, 키 배열이면 .json 파일. 이름순이고 default가 맨 앞이다. */
juce::StringArray listDocuments (const juce::File& folder, bool folders)
{
    juce::StringArray names;

    if (folders)
    {
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findDirectories))
            if (entry.getFile().getChildFile ("kit.json").existsAsFile())
                names.add (entry.getFile().getFileName());
    }
    else
    {
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.json", juce::File::findFiles))
            names.add (entry.getFile().getFileNameWithoutExtension());
    }

    names.sortNatural();

    if (const auto index = names.indexOf ("default"); index > 0)
        names.move (index, 0);

    return names;
}

/** 킷 폴더나 키 배열 파일 from을 to로 복사하고, 복사본(document)의 이름을 newName으로 바꾼다. 복사하지 못하면 false. */
template <typename Document>
bool copyDocument (const juce::File& from, const juce::File& to, const juce::File& document, const juce::String& newName)
{
    if (! (from.isDirectory() ? from.copyDirectoryTo (to) : from.copyFileTo (to)))
        return false;

    Document copy;

    if (copy.load (document))
    {
        copy.setName (newName.trim());
        copy.save();
    }

    return true;
}

} // namespace

//==============================================================================
Session::Session (const Options& options)
    : dataRoot (options.dataRoot),
      remember (options.rememberSettings),
      appSettings (options.rememberSettings),
      charts (options.dataRoot, options.chartsFolder),
      loopTrainer (track, charts)
{
    for (auto& pad : midiPads)
        pad.store (-1, std::memory_order_relaxed);

    backups.setFolders (options.backupsFolder, dataRoot);

    // 연주 로그. 파일은 처음 칠 때 만든다.
    playLogFolder = options.playLogFolder;

    if (options.playLogFolder != juce::File() && options.writePlayLog && static_cast<bool> (getSetting ("playLog", true)))
    {
        sessionLog.start (options.playLogFolder);
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

    // 반주 화면의 속도 올리기 설정
    loopTrainer.setSettings (LoopTrainer::Settings::fromVar (getSetting ("speedTrainer", {})));

    // 세기 레이어의 경계에서 크기를 잇는다. 끄려면 settings.json에 "smoothLayers": false
    engine.setSmoothLayerSteps (static_cast<bool> (getSetting ("smoothLayers", true)));

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
    // 마우스를 놓기 전에 닫혀도 고친 것은 남긴다. 키 배열은 다시 적용하지 않고 파일에만 쓴다.
    if (kitEdited)
        saveKit (false);

    if (keymapEdited && keymapDocument.save())
        noteContent (keymapFile, rememberedKeymap);

    appSettings.saveIfPending();

    keyboard.stop();       // 입력 스레드가 엔진에 더 보내지 않게 먼저 멈춘다
    sessionLog.stop();
    audio.getDeviceManager().removeMidiInputDeviceCallback ({}, this);
    audio.stop();          // 오디오 콜백이 엔진과 킷을 더 쓰지 않게 멈춘다 (녹음 중이면 파일도 닫는다)
    engine.detachKit();    // 이제 ownedKits와 ownedTracks가 지워져도 안전하다
    track.detachTrack();
}

//==============================================================================
void Session::savePendingEdits()
{
    if (kitEdited && juce::Time::getMillisecondCounter() - kitEditedMs >= kitSaveDelayMs)
        saveKit (false);    // kitEdited를 지운다

    if (std::exchange (keymapEdited, false))
        saveKeymap();

    appSettings.saveIfPending();
}

void Session::updateMessages()
{
    messages = kitMessages;
    messages.addArray (keymapMessages);
}

//==============================================================================
void Session::poll()
{
    releaseRetired();

    // 장치가 꽂히거나 빠졌으면 페달이 연결돼 있는지 다시 본다.
    if (keyboard.getDeviceChangeCount() != seenDeviceChanges)
        updatePedalConnection();

    // 로그에는 상황, 키, 타격의 순서로 적는다. 그래야 누른 키가 그 키가 낸 타격보다 앞에 온다.
    sessionLog.logSituation();
    sessionLog.logKeys();
    drainHitEvents();
    loopTrainer.update (trackFile);
    sessionLog.flushIfDue();

    charts.poll();
    outputPeak = audio.takePeak();

    // 키 지정용으로 잡아 둔 키가 있으면 화면 쪽에 넘긴다.
    if (const auto key = capturedKey.exchange (-1, std::memory_order_relaxed); key >= 0 && captureCallback != nullptr)
    {
        auto callback = std::move (captureCallback);
        captureCallback = nullptr;
        callback (static_cast<hvd::KeyCode> (key));
    }

    if (takes.isPlaying() && ! takes.pump())
        sendChangeMessage();    // 다시 재생이 끝났다

    // 고친 문서와 설정은 마우스를 놓은 뒤에 저장한다 (킷은 잠깐 더 기다린다). 밖에서 고친 파일을 살피기 전에 한다.
    if ((kitEdited || keymapEdited || appSettings.isPending()) && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        savePendingEdits();

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
    auto releasedKit = false;

    while (const auto* retired = engine.popRetiredKit())
    {
        std::erase_if (ownedKits, [retired] (const auto& kit) { return kit.get() == retired; });
        releasedKit = true;
    }

    // 내려놓은 킷만 쓰던 샘플은 상한까지만 남겨 둔다 (그 킷이나 소리로 되돌아가면 다시 디코드하지 않게).
    if (releasedKit)
        trimSampleCache();

    while (const auto* retired = track.popRetiredTrack())
        std::erase_if (ownedTracks, [retired] (const auto& data) { return data.get() == retired; });
}

void Session::drainHitEvents()
{
    const auto sampleRate = engine.getSampleRate();
    const auto measuring = engine.isMetronomeOn() && ! takes.isPlaying();
    const auto logging = sessionLog.isActive();
    const auto training = loopTrainer.isOn();
    const auto sync = logging || training ? audio.getTrackSync() : AudioHost::TrackSync();
    hvd::HitEvent event;

    while (engine.popHitEvent (event))
    {
        if (shownKit == nullptr || event.pad >= shownKit->pads.size())
            continue;

        if (logging)
            sessionLog.logHit (event, *shownKit, sync);

        if (onHit != nullptr)
            onHit (event);

        if (training && sync.active)
            loopTrainer.addHit (event, *shownKit, sync.positionAt (event.time, sampleRate), static_cast<double> (event.time) / sampleRate);

        takes.add (event, *shownKit);

        if (measuring)
        {
            // 가장 가까운 16분음표 자리에서 얼마나 벗어났는지 잰다.
            const auto grid = sampleRate * 60.0 / static_cast<double> (engine.getTempo()) / 4.0;
            timing.add (static_cast<float> (hvd::gridOffsetMs (event.time, engine.getLastBeatTime(), grid, sampleRate)));
        }
    }
}

//==============================================================================
juce::StringArray Session::getKitFolderNames() const
{
    return listDocuments (getKitsFolder(), true);
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

    if (! copyDocument<KitDocument> (kitFolder, target, target.getChildFile ("kit.json"), newName))
        return ko ("킷 폴더를 복사하지 못했습니다");

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
    kitEdited = false;    // 고쳐 두고 아직 저장하지 않은 것은 다시 읽는 문서와 함께 버린다
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
        sessionLog.logKit();
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
    // 적지 않은 값은 표의 기본값이다. 세기 곡선은 킷 전체의 값을 먼저 본다. 표에 없는 이름이면 0.
    static const juce::Identifier velocityCurve (hvd::padField::velocityCurve.name);

    auto fallback = 0.0;

    if (field == velocityCurve)
        fallback = numberOr (kitDocument.getProperty (velocityCurve), hvd::padField::velocityCurve.fallback);
    else if (const auto* known = hvd::findPadField (field.toString().toStdString()))
        fallback = known->fallback;

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

    namespace field = hvd::padField;
    auto params = hvd::makePadParams (getPadValue (pad, field::volume.name), getPadValue (pad, field::pan.name),
                                      getPadValue (pad, field::pitch.name), getPadValue (pad, field::reverbSend.name),
                                      getPadValue (pad, field::startMs.name), getPadValue (pad, field::velocityCurve.name));
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
    kitDocument.setFx (fx);
}

void Session::applyFxFromDocument()
{
    engine.setFxSettings (kitDocument.getFx());
}

bool Session::saveKit (bool reload)
{
    kitEdited = false;

    if (! kitDocument.save())
    {
        kitMessages.add (ko ("킷 파일을 저장하지 못했습니다: ") + getKitFile().getFullPathName());
        updateMessages();
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
    return listDocuments (getKeymapsFolder(), false);
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

    if (! copyDocument<KeymapDocument> (keymapFile, target, target, newName))
        return ko ("키 배열 파일을 복사하지 못했습니다");

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
        updateMessages();
    }

    return ok;
}

void Session::reloadKeymap()
{
    keymapEdited = false;    // 고쳐 두고 아직 저장하지 않은 것은 다시 읽는 문서와 함께 버린다
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

            sessionLog.logKeymap();
        }
        else
        {
            keymapMessages.add (ko ("키 배열을 읽지 못했습니다: ") + result.error);
        }
    }

    noteContent (keymapFile, rememberedKeymap);

    updateMessages();
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
        sessionLog.noteKey (key, true);

        performer.keyDown (key, time);
    }
    else if (forwarded[key])
    {
        // 누름을 넘긴 키만 뗌도 넘긴다. 그래야 뗄 때 치는 방식의 키가 엉뚱하게 울리지 않는다.
        forwarded[key] = false;
        sessionLog.noteKey (key, false);

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
void Session::setMasterGain (float gain)
{
    audio.setMasterGain (juce::jlimit (0.0f, AudioHost::maxMasterGain, gain));
    markSettingEdited ("masterGain", audio.getMasterGain());
}

juce::String Session::loadTrack (const juce::File& file)
{
    auto data = std::make_unique<hvd::TrackData>();

    // 조금씩 읽어 16비트로 바꿔 담는다. (긴 곡을 float로 통째로 두면 메모리를 두 배로 쓴다)
    const auto error = readMusic (file, maxTrackSeconds,
        [&] (const MusicInfo& info)
        {
            data->sampleRate = info.sampleRate;
            data->samples.resize (static_cast<std::size_t> (info.numFrames) * 2);
            return juce::String();
        },
        [&] (const MusicChunk& chunk)
        {
            // MP3의 어림한 길이가 실제보다 길었다. 읽은 데까지만 쓴다.
            if (chunk.cutShort)
            {
                data->samples.resize (static_cast<std::size_t> (chunk.position) * 2);
                return juce::String();
            }

            const auto* left = chunk.buffer.getReadPointer (0);
            const auto* right = chunk.buffer.getReadPointer (1);
            auto* target = data->samples.data() + static_cast<std::size_t> (chunk.position) * 2;

            for (int i = 0; i < chunk.numFrames; ++i)
            {
                target[i * 2] = static_cast<std::int16_t> (juce::jlimit (-32768.0f, 32767.0f, left[i] * 32767.0f));
                target[i * 2 + 1] = static_cast<std::int16_t> (juce::jlimit (-32768.0f, 32767.0f, right[i] * 32767.0f));
            }

            return juce::String();
        });

    if (error.isNotEmpty())
        return error;

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
    markSettingEdited ("trackGain", track.getGain());
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
    takes.start();
    sendChangeMessage();
}

void Session::stopTake()
{
    if (! takes.isRecording())
        return;

    drainHitEvents();    // 그때까지 친 것을 마저 담는다
    takes.stop();
    sendChangeMessage();
}

void Session::playTake()
{
    stopTake();

    if (shownKit != nullptr && takes.play (*shownKit))
        sendChangeMessage();
}

void Session::stopTakePlayback()
{
    if (takes.stopPlayback())
        sendChangeMessage();
}

juce::String Session::exportTakeAsMidi (const juce::File& file) const
{
    if (shownKit == nullptr)
        return ko ("기록한 연주가 없습니다");

    return takes.exportMidi (file, *shownKit, engine.getTempo());
}

juce::String Session::importTakeFromMidi (const juce::File& file)
{
    if (shownKit == nullptr)
        return ko ("킷이 없습니다");

    const auto padOfNote = [this] (int note) { return midiPads[static_cast<std::size_t> (note & 127)].load (std::memory_order_relaxed); };
    auto imported = false;
    const auto message = takes.importMidi (file, padOfNote, *shownKit, imported);

    if (imported)
        sendChangeMessage();

    return message;
}
