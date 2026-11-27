#include "ChartTools.h"
#include "Documents.h"
#include "GamePage.h"
#include "KeymapLoader.h"
#include "KitLoader.h"
#include "MainComponent.h"
#include "MusicFile.h"
#include "PracticePage.h"
#include "SfzImport.h"
#include "Text.h"

#include "../../tests/CoreTests.h"

namespace
{

/** kits/default/kit.json 이 들어 있는 폴더를 실행 파일 위치에서 위로 올라가며 찾는다. */
juce::File findDataRoot()
{
    auto folder = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

    for (int depth = 0; depth < 8; ++depth)
    {
        if (folder.getChildFile ("kits").getChildFile ("default").getChildFile ("kit.json").existsAsFile())
            return folder;

        const auto parent = folder.getParentDirectory();

        if (parent == folder)
            break;

        folder = parent;
    }

    return juce::File::getCurrentWorkingDirectory();
}

juce::File resolvePath (const juce::String& path)
{
    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

/** 키보드 메시지 처리 규칙을 OS를 거치지 않고 확인한다. 문제가 없으면 true. */
bool checkKeyboardRules (juce::String& report)
{
    struct Event
    {
        hvd::KeyCode key;
        bool down;
    };

    std::vector<Event> events;
    hvd::RawKeyboard probe;
    probe.setHandlerForTesting ([&events] (hvd::KeyCode key, bool down) { events.push_back ({ key, down }); });

    constexpr unsigned released = 1;    // RI_KEY_BREAK
    constexpr unsigned extended = 2;    // RI_KEY_E0
    constexpr unsigned pauseKey = 4;    // RI_KEY_E1

    probe.processRawKey (0x24, 0);                      // J 누름
    probe.processRawKey (0x24, 0);                      // 자동 반복: 무시
    probe.processRawKey (0x24, released);               // J 뗌
    probe.processRawKey (0x24, released);               // 이미 뗀 키: 무시
    probe.processRawKey (0x2a, extended);               // 가짜 Shift: 무시
    probe.processRawKey (0x1d, extended);               // 오른쪽 Ctrl 누름
    probe.processRawKey (0x1d, 0);                      // 왼쪽 Ctrl 누름: 오른쪽과 다른 키
    probe.processRawKey (0x1d, pauseKey);               // Pause 키 시퀀스: 무시
    probe.processRawKey (0x00, 0);                      // 잘못된 코드: 무시
    probe.processRawKey (0x1d, extended | released);    // 오른쪽 Ctrl 뗌

    const std::vector<Event> expected {
        { 0x24, true }, { 0x24, false }, { 0x11d, true }, { 0x1d, true }, { 0x11d, false }
    };

    auto ok = events.size() == expected.size();

    for (std::size_t i = 0; ok && i < events.size(); ++i)
        ok = events[i].key == expected[i].key && events[i].down == expected[i].down;

    report << "[keyboard rules]\n" << (ok ? "ok" : "FAILED") << " (" << static_cast<int> (events.size()) << " events)\n";

    //==============================================================================
    // 페달(풋스위치): 등록한 장치에서 온 키만 페달 키로 바뀌는지
    constexpr hvd::RawKeyboard::DeviceId mainKeyboard = 11, footSwitch = 22;
    constexpr unsigned scanA = 0x1e, scanB = 0x30, scanZ = 0x2c, scanEscape = 0x01;

    const auto matches = [&events] (const std::vector<Event>& wanted)
    {
        auto same = events.size() == wanted.size();

        for (std::size_t i = 0; same && i < events.size(); ++i)
            same = events[i].key == wanted[i].key && events[i].down == wanted[i].down;

        return same;
    };

    // 1. 등록: 처음 밟은 장치의 키만 차례로 적고, 그동안 누른 키는 연주에 넘기지 않는다
    events.clear();
    probe.beginPedalLearning();
    probe.processRawKey (scanA, 0, footSwitch);
    probe.processRawKey (scanA, 0, footSwitch);            // 자동 반복
    probe.processRawKey (scanA, released, footSwitch);
    probe.processRawKey (0x20, 0, mainKeyboard);           // 다른 장치의 키: 적지 않는다
    probe.processRawKey (0x20, released, mainKeyboard);
    probe.processRawKey (scanB, 0, footSwitch);
    probe.processRawKey (scanA, 0, footSwitch);            // 이미 적은 스위치
    probe.processRawKey (scanA, released, footSwitch);

    auto learned = probe.getPedalLearning();
    auto pedalOk = events.empty() && probe.isPedalLearning() && ! learned.cancelled && learned.device == footSwitch
                     && learned.keys == std::vector<hvd::KeyCode> { hvd::KeyCode { scanA }, hvd::KeyCode { scanB } };

    // 등록을 마칠 때 아직 밟고 있던 스위치(B)는 뗄 때까지 아무 일도 하지 않는다
    probe.endPedalLearning();
    probe.setPedal (learned.device, learned.keys);
    probe.processRawKey (scanB, 0, footSwitch);            // 자동 반복
    probe.processRawKey (scanB, released, footSwitch);
    pedalOk = pedalOk && events.empty();

    // 2. 같은 키라도 장치에 따라 다른 키가 된다. 한쪽이 눌려 있어도 다른 쪽이 자동 반복으로 버려지지 않는다
    std::int64_t time = 5'000'000;
    const auto step = [&time] { return time += 8000 * (3 + (time / 8000) % 7); };    // 8ms의 배수만큼씩

    probe.processRawKey (scanA, 0, mainKeyboard, step());
    probe.processRawKey (scanA, 0, footSwitch, step());
    probe.processRawKey (scanA, 0, footSwitch, step());           // 자동 반복
    probe.processRawKey (scanB, 0, footSwitch, step());
    probe.processRawKey (scanA, released, footSwitch, step());
    probe.processRawKey (scanA, released, mainKeyboard, step());
    probe.processRawKey (scanB, released, footSwitch, step());
    probe.processRawKey (scanZ, 0, footSwitch, step());           // 등록하지 않은 키는 그대로 지나간다
    probe.processRawKey (scanZ, released, footSwitch, step());
    probe.processRawKey (scanB, 0, 0, step());                    // 장치를 모르는 입력은 키보드로 본다
    probe.processRawKey (scanB, released, 0, step());

    pedalOk = pedalOk && matches ({ { scanA, true }, { hvd::pedalKey (0), true }, { hvd::pedalKey (1), true },
                                    { hvd::pedalKey (0), false }, { scanA, false }, { hvd::pedalKey (1), false },
                                    { scanZ, true }, { scanZ, false }, { scanB, true }, { scanB, false } });

    // 3. 보고 간격: 8ms 격자 위의 시각으로 스위치를 여러 번 밟으면 8ms로 읽힌다
    for (int i = 0; i < 20; ++i)
    {
        probe.processRawKey (scanA, 0, footSwitch, step());
        probe.processRawKey (scanA, released, footSwitch, step());
    }

    const auto pedalTimes = probe.getReportTimes (true);
    const auto keyboardTimes = probe.getReportTimes (false);
    const auto interval = hvd::estimateReportInterval (pedalTimes);
    pedalOk = pedalOk && pedalTimes.size() == 46 && keyboardTimes.size() == 9 && interval.enough && interval.intervalMs == 8.0;

    // 4. 페달을 지우면 눌린 채 남은 페달 키가 떼어지고, 그 장치의 키는 다시 키보드의 키가 된다
    events.clear();
    probe.processRawKey (scanA, 0, footSwitch);
    probe.setPedal (0, {});
    probe.processRawKey (scanA, released, footSwitch);
    probe.processRawKey (scanA, 0, footSwitch);
    probe.processRawKey (scanA, released, footSwitch);
    pedalOk = pedalOk && matches ({ { hvd::pedalKey (0), true }, { hvd::pedalKey (0), false }, { scanA, true }, { scanA, false } });

    // 5. Esc는 등록 취소
    events.clear();
    probe.beginPedalLearning();
    probe.processRawKey (scanEscape, 0, mainKeyboard);
    probe.processRawKey (scanEscape, released, mainKeyboard);
    learned = probe.getPedalLearning();
    probe.endPedalLearning();
    pedalOk = pedalOk && events.empty() && learned.cancelled && learned.keys.empty();

    // 장치 이름이 같으면 같은 값, 다르면 다른 값
    pedalOk = pedalOk && hvd::RawKeyboard::deviceIdOf ("") == 0
                && hvd::RawKeyboard::deviceIdOf ("VID_046D&PID_C31C") == hvd::RawKeyboard::deviceIdOf ("VID_046D&PID_C31C")
                && hvd::RawKeyboard::deviceIdOf ("VID_046D&PID_C31C") != hvd::RawKeyboard::deviceIdOf ("VID_046D&PID_C31D");

    report << "pedal device rules: " << (pedalOk ? "ok" : "FAILED") << " (interval " << interval.intervalMs << " ms from "
           << interval.numSamples << " gaps)\n";

    // 지금 꽂혀 있는 키보드 장치들. 풋스위치도 여기에 나온다.
    const auto devices = hvd::RawKeyboard::listDevices();
    report << "keyboard devices: " << static_cast<int> (devices.size()) << "\n";

    for (const auto& device : devices)
        report << "  " << juce::String (device.identity) << "\n";

    return ok && pedalOk;
}

/** MP3의 음악 데이터가 시작하는 자리를 찾는 규칙을 만든 바이트로 확인한다. 문제가 없으면 true. */
bool checkMp3Start (juce::String& report)
{
    // 192kbps, 44.1kHz 레이어 3 프레임(626바이트)의 머리만 있고 속은 비어 있는 가짜 프레임들
    const auto appendFrames = [] (juce::MemoryBlock& block, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const juce::uint8 frame[626] = { 0xff, 0xfb, 0xb0, 0x00 };
            block.append (frame, sizeof (frame));
        }
    };

    const auto appendId3 = [] (juce::MemoryBlock& block, int bodySize)
    {
        const juce::uint8 head[10] = { 'I', 'D', '3', 3, 0, 0,
                                       0, 0, static_cast<juce::uint8> (bodySize >> 7), static_cast<juce::uint8> (bodySize & 0x7f) };
        block.append (head, sizeof (head));
        block.setSize (block.getSize() + static_cast<std::size_t> (bodySize), true);
    };

    const auto startOf = [] (const juce::MemoryBlock& block)
    {
        juce::MemoryInputStream stream (block, false);
        return findMp3AudioStart (stream);
    };

    // 1. 태그 바로 뒤에 음악이 오는 보통 파일: 손댈 것이 없다
    juce::MemoryBlock plain;
    appendId3 (plain, 300);
    appendFrames (plain, 4);

    // 2. 태그와 음악 사이에 덩어리가 끼어 있고, 그 안에 프레임 머리처럼 보이는 글자(FF FE …)가 있는 파일
    juce::MemoryBlock stuffed;
    appendId3 (stuffed, 300);
    const juce::uint8 junk[] = { 'I', 'T', 'A', 'G', 0, 0, 0, 0, 0x01, 0xff, 0xfe, 0xb3, 0x30, 0xf3, 0x30, 0, 0, 0xff, 0xfb, 0xb0, 0x00, 0, 0 };
    stuffed.append (junk, sizeof (junk));
    stuffed.setSize (stuffed.getSize() + 2000, true);
    const auto expected = static_cast<juce::int64> (stuffed.getSize());
    appendFrames (stuffed, 4);

    // 3. 태그가 없고 처음부터 음악인 파일, 4. 음악이 아닌 파일
    juce::MemoryBlock bare, noise;
    appendFrames (bare, 4);
    noise.setSize (5000, true);

    const auto ok = startOf (plain) == 0 && startOf (stuffed) == expected && startOf (bare) == 0 && startOf (noise) == 0;

    report << "\n[mp3 start]\n" << (ok ? "ok" : "FAILED") << " (stuffed file: audio found at " << startOf (stuffed)
           << ", expected " << expected << ")\n";
    return ok;
}

/** 킷의 모든 패드를 장치 없이 직접 재생해 보고 약한 타격과 센 타격의 크기를 적는다. */
bool checkKitOffline (const juce::File& kitFile, juce::String& report)
{
    report << "\n[kit offline render]\n";

    auto loaded = loadKit (kitFile);

    if (loaded.kit == nullptr)
    {
        report << "FAILED: " << loaded.error << "\n";
        return false;
    }

    hvd::Engine engine;
    engine.prepare (48000.0);

    hvd::EngineCommand setKit;
    setKit.type = hvd::EngineCommand::Type::setKit;
    setKit.kit = loaded.kit.get();
    engine.postFromControl (setKit);

    constexpr int numFrames = 9600;    // 0.2초
    std::vector<float> left (numFrames), right (numFrames);
    auto ok = true;

    const auto playPeak = [&] (int pad, float velocity)
    {
        hvd::EngineCommand allOff;
        allOff.type = hvd::EngineCommand::Type::allSoundOff;
        engine.postFromControl (allOff);

        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        float* channels[] = { left.data(), right.data() };
        engine.render (channels, 2, numFrames);    // 앞 소리를 지운다

        hvd::EngineCommand hit;
        hit.pad = static_cast<std::uint16_t> (pad);
        hit.velocity = velocity;
        engine.postFromInput (hit);

        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        engine.render (channels, 2, numFrames);

        auto peak = 0.0f;

        for (int i = 0; i < numFrames; ++i)
            peak = std::max ({ peak, std::abs (left[static_cast<std::size_t> (i)]), std::abs (right[static_cast<std::size_t> (i)]) });

        return peak;
    };

    for (int pad = 0; pad < static_cast<int> (loaded.kit->pads.size()); ++pad)
    {
        const auto soft = playPeak (pad, 0.3f);
        const auto hard = playPeak (pad, 1.0f);
        const auto padOk = soft > 0.0f && hard > soft;
        ok = ok && padOk;

        report << juce::String (loaded.kit->pads[static_cast<std::size_t> (pad)].id).paddedRight (' ', 14)
               << " soft " << juce::String (juce::Decibels::gainToDecibels (soft), 1).paddedLeft (' ', 6) << " dB"
               << "   hard " << juce::String (juce::Decibels::gainToDecibels (hard), 1).paddedLeft (' ', 6) << " dB"
               << (padOk ? "" : "   FAILED") << "\n";
    }

    engine.detachKit();
    return ok;
}

/** 440 Hz 사인파가 든 WAV 파일을 만든다. */
bool writeSineWav (const juce::File& file, double seconds, double sampleRate)
{
    const auto frames = static_cast<int> (seconds * sampleRate);
    juce::AudioBuffer<float> buffer (2, frames);

    for (int i = 0; i < frames; ++i)
    {
        const auto value = 0.5f * static_cast<float> (std::sin (6.283185307179586 * 440.0 * i / sampleRate));
        buffer.setSample (0, i, value);
        buffer.setSample (1, i, value);
    }

    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    const auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                     .withNumChannels (2)
                                                                                     .withBitsPerSample (16));

    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, frames);
}

/** 소리 파일의 길이(초)와 최대 크기를 잰다. 못 읽으면 길이가 0이다. */
std::pair<double, float> measureAudioFile (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return { 0.0, 0.0f };

    const auto frames = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> buffer (2, frames);
    reader->read (&buffer, 0, frames, 0, true, true);
    return { frames / reader->sampleRate, buffer.getMagnitude (0, frames) };
}

/** 편집 화면이 쓰는 문서 클래스로 킷과 키 배열 파일을 고쳐 저장하고, 다시 읽어 그대로인지 확인한다. */
bool checkDocuments (const juce::File& dataRoot, const juce::File& temp, juce::String& report)
{
    report << "\n[documents]\n";
    auto ok = true;

    const auto expect = [&ok, &report] (bool condition, const char* what)
    {
        ok = ok && condition;
        report << (condition ? "ok      " : "FAILED  ") << what << "\n";
    };

    // 키 배열: 줄을 하나 넣고 저장한 뒤 다시 읽는다.
    const auto keymapCopy = temp.getChildFile ("keymap.json");
    dataRoot.getChildFile ("keymaps").getChildFile ("default.json").copyFileTo (keymapCopy);

    KeymapDocument keymap;
    expect (keymap.load (keymapCopy), "keymap: load");
    const auto numBindings = keymap.getNumBindings();

    auto* binding = new juce::DynamicObject();
    binding->setProperty ("key", "F9");
    binding->setProperty ("pad", "kick");
    const auto added = keymap.addBinding (juce::var (binding));
    keymap.setBindingProperty (added, "velocity", 0.5);
    expect (keymap.save(), "keymap: save");

    const auto reloaded = loadKeymap (keymapCopy);
    expect (reloaded.ok && static_cast<int> (reloaded.spec.bindings.size()) == numBindings + 1, "keymap: one more binding after reload");
    expect (reloaded.ok && ! reloaded.spec.bindings.empty() && reloaded.spec.bindings.back().key == "F9"
                && std::abs (reloaded.spec.bindings.back().velocity - 0.5f) < 1.0e-6f,
            "keymap: new binding kept its values");

    // 킷: 값을 바꾸고, 패드를 추가해 킷 폴더 밖의 샘플을 넣는다.
    const auto kitFolder = temp.getChildFile ("kit");
    kitFolder.createDirectory();
    const auto kitCopy = kitFolder.getChildFile ("kit.json");
    dataRoot.getChildFile ("kits").getChildFile ("default").getChildFile ("kit.json").copyFileTo (kitCopy);

    const auto outsideSample = temp.getChildFile ("outside.wav");
    expect (writeSineWav (outsideSample, 0.2, 44100.0), "kit: write a sample outside the kit folder");

    KitDocument kit;
    expect (kit.load (kitCopy), "kit: load");
    const auto numPads = kit.getPadIds().size();
    kit.setPadNumber ("snare", "volume", 0.5);
    kit.setPadNumber ("kick", "volume", 7.5);    // 예전 한도(4)를 넘는 값도 그대로 읽혀야 한다

    const auto newPad = kit.addPad ("test pad");
    expect (kit.setPadSamples (newPad, juce::Array<juce::File> (outsideSample), false).isEmpty(), "kit: set samples of the new pad");
    expect (kitFolder.getChildFile ("samples").getChildFile (newPad).getChildFile ("outside.wav").existsAsFile(),
            "kit: outside sample was copied into the kit folder");
    expect (kit.save(), "kit: save");

    // 다른 패드의 샘플은 이 임시 폴더에 없으므로 경고가 나오는 것이 맞다. 새 패드만 소리가 난다.
    const auto loaded = loadKit (kitCopy);
    expect (loaded.kit != nullptr && static_cast<int> (loaded.kit->pads.size()) == numPads + 1, "kit: one more pad after reload");

    if (loaded.kit != nullptr)
    {
        const auto snare = loaded.kit->findPad ("snare");
        const auto added2 = loaded.kit->findPad (newPad.toStdString());
        expect (snare >= 0 && std::abs (loaded.kit->pads[static_cast<std::size_t> (snare)].volume - 0.5f) < 1.0e-6f, "kit: changed volume was saved");
        const auto kick = loaded.kit->findPad ("kick");
        expect (kick >= 0 && std::abs (loaded.kit->pads[static_cast<std::size_t> (kick)].volume - 7.5f) < 1.0e-6f, "kit: a volume above 4 is kept (limit 10)");
        expect (added2 >= 0 && loaded.kit->pads[static_cast<std::size_t> (added2)].layers.size() == 1, "kit: new pad plays its sample");
    }

    // 모아 둔 소리: 패드의 "sounds" 가운데 하나를 고르면 그 소리의 샘플 구성이 "layers"가 된다.
    {
        const auto soundsFolder = temp.getChildFile ("sounds-kit");
        soundsFolder.createDirectory();
        const auto soundsFile = soundsFolder.getChildFile ("kit.json");

        soundsFile.replaceWithText (R"({
          "name": "sounds",
          "pads": [
            { "id": "hihat_closed", "name": "closed", "sound": "a",
              "layers": [ { "upTo": 1.0, "samples": [ "a_closed.wav" ] } ],
              "sounds": [
                { "id": "a", "name": "A", "family": "lib-a/hat", "layers": [ { "upTo": 1.0, "samples": [ "a_closed.wav" ] } ] },
                { "id": "b", "name": "B", "family": "lib-b/hat",
                  "layers": [ { "upTo": 0.5, "samples": [ "b_closed_soft.wav" ] }, { "upTo": 1.0, "samples": [ "b_closed.wav" ] } ] } ] },
            { "id": "hihat_open", "name": "open", "sound": "a",
              "layers": [ { "upTo": 1.0, "samples": [ "a_open.wav" ] } ],
              "sounds": [
                { "id": "a", "name": "A", "family": "lib-a/hat", "layers": [ { "upTo": 1.0, "samples": [ "a_open.wav" ] } ] },
                { "id": "b", "name": "B", "family": "lib-b/hat", "layers": [ { "upTo": 1.0, "samples": [ "b_open.wav" ] } ] } ] },
            { "id": "kick", "name": "kick", "samples": [ "kick.wav" ] }
          ]
        })");

        KitDocument sounds;
        expect (sounds.load (soundsFile), "sounds: load");
        expect (sounds.getPadSounds ("hihat_closed").size() == 2 && sounds.getPadSounds ("kick").isEmpty(), "sounds: listed per pad");
        expect (sounds.getPadSoundIndex ("hihat_closed") == 0 && sounds.getPadSoundIndex ("kick") == -1, "sounds: current sound found");

        // 같은 악기에서 나온 소리를 가진 다른 패드도 함께 바뀐다
        expect (sounds.selectPadSound ("hihat_closed", 1, true) == 2, "sounds: choosing one changes its family too");
        expect (sounds.getPadSoundIndex ("hihat_closed") == 1 && sounds.getPadSoundIndex ("hihat_open") == 1
                    && sounds.countLayers ("hihat_closed") == 2 && sounds.countSamples ("hihat_open") == 1
                    && sounds.findPad ("hihat_open")["layers"][0]["samples"][0].toString() == "b_open.wav",
                "sounds: the pads now play the chosen sound");

        // 혼자만 바꿀 수도 있고, 모아 둔 원본은 그대로 남는다
        expect (sounds.selectPadSound ("hihat_closed", 0, false) == 1 && sounds.getPadSoundIndex ("hihat_open") == 1
                    && sounds.findPad ("hihat_closed")["sounds"][1]["layers"].size() == 2,
                "sounds: one pad can be changed alone");
        expect (sounds.selectPadSound ("hihat_closed", 5, true) == 0 && sounds.selectPadSound ("kick", 0, true) == 0, "sounds: bad choices are ignored");

        // 저장했다가 다시 읽어도 그대로이고, 샘플을 직접 고르면 "모아 둔 소리"가 아니게 된다
        expect (sounds.save() && sounds.load (soundsFile) && sounds.getPadSoundIndex ("hihat_open") == 1, "sounds: saved and read back");
        expect (sounds.setPadSamples ("hihat_open", juce::Array<juce::File> (outsideSample), false).isEmpty()
                    && sounds.getPadSoundIndex ("hihat_open") == -1 && sounds.getPadSounds ("hihat_open").size() == 2,
                "sounds: choosing files by hand leaves the collection");
    }

    // SFZ 가져오기: 건반 둘, 세기 구간 둘, 초크 그룹이 든 작은 SFZ를 만들어 읽는다.
    const auto sfzFolder = temp.getChildFile ("sfz");
    sfzFolder.getChildFile ("samples").createDirectory();
    writeSineWav (sfzFolder.getChildFile ("samples").getChildFile ("kick 1.wav"), 0.2, 44100.0);
    writeSineWav (sfzFolder.getChildFile ("samples").getChildFile ("snare.wav"), 0.2, 44100.0);

    const auto sfzFile = sfzFolder.getChildFile ("test kit.sfz");
    sfzFile.replaceWithText ("// test\n<control> default_path=samples/\n<global> volume=-6\n"
                             "<group> key=36\n<region> sample=kick 1.wav lovel=0 hivel=63\n"
                             "<region> sample=kick 1.wav lovel=64 hivel=127\n"
                             "<group> key=d2 group=1 off_by=2\n<region> sample=snare.wav\n"
                             "<region> sample=missing.wav key=60 trigger=release\n");

    const auto kitsFolder = temp.getChildFile ("kits");
    kitsFolder.createDirectory();
    const auto imported = importSfz (sfzFile, kitsFolder);
    expect (imported.error.isEmpty() && imported.numPads == 2 && imported.numSamples == 2 && imported.numMissing == 0, "sfz: two pads, two samples");

    const auto sfzKit = loadKit (kitsFolder.getChildFile (imported.kitFolderName).getChildFile ("kit.json"));
    expect (sfzKit.kit != nullptr && sfzKit.warnings.isEmpty(), "sfz: imported kit loads without warnings");

    if (sfzKit.kit != nullptr)
    {
        const auto kick = sfzKit.kit->findPad ("kick");
        const auto snare = sfzKit.kit->findPad ("snare");
        expect (kick >= 0 && sfzKit.kit->pads[static_cast<std::size_t> (kick)].layers.size() == 2, "sfz: note 36 became the kick with two velocity layers");
        expect (kick >= 0 && std::abs (sfzKit.kit->pads[static_cast<std::size_t> (kick)].volume - 0.5f) < 0.01f, "sfz: volume opcode applied");
        expect (snare >= 0 && sfzKit.kit->pads[static_cast<std::size_t> (snare)].group >= 0
                    && sfzKit.kit->pads[static_cast<std::size_t> (snare)].cutsMask != 0, "sfz: note d2 became the snare with choke settings");
    }

    return ok;
}

/** 킷과 키 배열의 백업: 내용이 바뀔 때만 남기고, 잇달아 저장할 때는 하나만 남기고, 오래된 것은 솎아 내는지 본다. */
bool checkBackups (const juce::File& temp, juce::String& report)
{
    report << "\n[backups]\n";
    auto ok = true;

    const auto expect = [&ok, &report] (bool condition, const char* what)
    {
        ok = ok && condition;
        report << (condition ? "ok      " : "FAILED  ") << what << "\n";
    };

    const auto dataRoot = temp.getChildFile ("backup-data");
    const auto original = dataRoot.getChildFile ("kits").getChildFile ("default").getChildFile ("kit.json");

    FileBackups backups;
    expect (! backups.keep (original, "a"), "nothing is kept when no folder is set");
    backups.setFolders (temp.getChildFile ("backup-store"), dataRoot);

    const juce::Time start (2026, 10, 20, 10, 10, 0);    // 10시 10분: 아래의 세 백업이 같은 한 시간 안에 든다
    const auto after = [start] (double seconds) { return start + juce::RelativeTime::seconds (seconds); };

    expect (backups.getFolderFor (original).getFileName() == "kits__default__kit.json", "one folder for each file, named after its path");
    expect (backups.keep (original, "one", false, after (0)), "first content is kept");
    expect (! backups.keep (original, "one", false, after (60)), "same content is not kept again");
    expect (! backups.keep (original, "two", false, after (5)), "a save right after another is not kept");
    expect (backups.keep (original, "two", true, after (5)), "unless it must be (before a restore)");
    expect (backups.keep (original, "three", false, after (60)), "later content is kept");

    auto entries = backups.list (original);
    expect (entries.size() == 3 && entries.front().file.loadFileAsString() == "three" && entries.back().file.loadFileAsString() == "one",
            "backups are listed newest first");
    expect (entries.size() == 3 && entries.front().time == after (60) && entries.front().file.getFileName() == "20261120-101100.json",
            "file names carry the time");

    // 다섯 시간 뒤: 앞의 셋은 같은 한 시간 안의 것이므로 가장 새것 하나만 남는다.
    expect (backups.keep (original, "four", false, after (5 * 3600)), "content five hours later is kept");
    entries = backups.list (original);
    expect (entries.size() == 2 && entries.back().file.loadFileAsString() == "three", "older than an hour: one per hour remains");

    // 닷새 뒤: 첫날의 둘 가운데 가장 새것 하나만 남는다.
    expect (backups.keep (original, "five", false, after (5 * 86400)), "content five days later is kept");
    entries = backups.list (original);
    expect (entries.size() == 2 && entries.back().file.loadFileAsString() == "four", "older than two days: one per day remains");

    return ok;
}

/** 프로그램에 물려 있는 키 배열 파일을 밖에서 고쳐 보고, 고치기 전의 내용이 남는지와 그것으로 되돌아가는지 본다.
    (자체 검사의 키 배열은 임시 폴더에 따로 만든 것이라 사용자의 파일은 건드리지 않는다.) */
bool checkBackupRoundTrip (Session& session, juce::String& report)
{
    report << "\n[backup and restore]\n";
    auto ok = true;

    const auto expect = [&ok, &report] (bool condition, const char* what)
    {
        ok = ok && condition;
        report << (condition ? "ok      " : "FAILED  ") << what << "\n";
    };

    const auto file = session.getKeymapFile();
    const auto before = file.loadFileAsString();
    const auto changed = before.replace ("\"tempo\": 120", "\"tempo\": 121");

    expect (changed != before && file.replaceWithData (changed.toRawUTF8(), changed.getNumBytesAsUTF8()), "keymap file changed from outside");
    session.reloadKeymap();    // 화면 타이머가 파일이 바뀐 것을 알아챘을 때 부르는 것과 같은 길

    auto entries = session.getBackups().list (file);
    expect (entries.size() == 1 && entries.front().file.loadFileAsString() == before, "the content before the change was kept");
    expect (std::abs (session.getEngine().getTempo() - 121.0f) < 0.01f, "the changed file is in use");

    expect (! entries.empty() && session.restoreKeymapBackup (entries.front().file).isEmpty(), "restored from the backup");
    expect (file.loadFileAsString() == before, "the file is back to what it was");
    expect (std::abs (session.getEngine().getTempo() - 120.0f) < 0.01f, "the restored file is in use");

    entries = session.getBackups().list (file);
    expect (entries.size() == 2 && entries.front().file.loadFileAsString() == changed, "the state before the restore was kept too");

    return ok;
}

/** 연습 기록: 아는 내용의 로그를 로그 기록기로 직접 써 놓고, 연주로 제대로 읽히는지와 박자가 맞게 재지는지 본다.
    song은 채보가 있는 곡, chart는 그 채보다. 로그는 folder에 남긴다 (연습 기록 화면이 이어서 읽는다). */
bool checkPracticeLog (const juce::File& folder, const juce::File& song, const hvd::Chart& chart, juce::String& report)
{
    report << "\n[practice log]\n";
    auto ok = true;

    const auto expect = [&ok, &report] (bool condition, const char* what)
    {
        ok = ok && condition;
        report << (condition ? "ok      " : "FAILED  ") << what << "\n";
    };

    if (chart.beats.size() < 40)
    {
        expect (false, "the test song has a beat grid");
        return false;
    }

    const auto before = folder.findChildFiles (juce::File::findFiles, false, "play-*.jsonl");

    PlayLog log;
    log.start (folder);
    expect (log.begin(), "a log file for the practice check was made");
    const auto file = log.getFile();

    auto clock = 100.0;    // 로그의 시각(초). 곡의 위치와는 따로 흐른다
    const auto hit = [&log, &clock] (const char* pad, const char* source, const char* key, double songSeconds)
    {
        PlayLog::Line line (clock, "hit");
        line.text ("pad", pad).number ("v", 0.9, 3).text ("src", source);

        if (key != nullptr)
            line.text ("key", key);

        if (songSeconds > -1000.0)
            line.number ("song", songSeconds, 4);

        log.write (line);
    };

    log.write (PlayLog::Line (clock, "start").integer ("version", 1).text ("time", "2026-11-25T21:00:00.000+09:00"));
    hit ("snare", "key", "KeyD", -2000.0);    // 곡 없이 친 것: 연주에 들지 않는다
    log.write (PlayLog::Line (clock, "track").text ("file", song.getFullPathName()).number ("length", chart.duration, 3));
    log.write (PlayLog::Line (clock, "play").number ("song", 0.0, 4).number ("speed", 1.0, 3));

    // 첫 연주: 박마다 10ms 늦게 서른 번 (키보드와 마우스를 번갈아), 프로그램이 대신 친 것 셋, 키를 누르고 있어 이어진 연타 다섯
    const char* const pads[] = { "kick", "snare", "hihat_closed" };

    for (int i = 0; i < 30; ++i)
    {
        clock += 0.5;
        hit (pads[i % 3], i % 2 == 0 ? "key" : "mouse", i % 2 == 0 ? "KeyX" : nullptr, chart.beats[static_cast<std::size_t> (i + 4)] + 0.010);
    }

    for (int i = 0; i < 3; ++i)
        hit ("kick", "auto", nullptr, chart.beats[static_cast<std::size_t> (i + 4)]);

    for (int i = 0; i < 5; ++i)
    {
        clock += 0.06;
        hit ("crash1", "key", "KeyE", chart.beats[35] + 0.06 * i);
    }

    // 곡을 처음으로 되감아 다시 틀면 새 연주다. 이번에는 절반 속도로, 박마다 곡의 시간으로 10ms 빠르게(실제로는 20ms) 스물다섯 번.
    clock += 1.0;
    log.write (PlayLog::Line (clock, "pause").number ("song", chart.beats[36], 4).number ("speed", 1.0, 3));
    log.write (PlayLog::Line (clock, "play").number ("song", 0.0, 4).number ("speed", 0.5, 3));

    for (int i = 0; i < 25; ++i)
    {
        clock += 1.0;
        hit ("snare", "key", "KeyD", chart.beats[static_cast<std::size_t> (i + 4)] - 0.010);
    }

    log.stop();

    const auto takes = readPracticeTakes (file);
    expect (takes.size() == 2, "the log reads as two takes (the song was restarted)");

    if (takes.size() != 2)
        return false;

    const auto& first = takes[0];
    const auto& second = takes[1];

    expect (first.song == song && first.track == song && ! first.fromGame, "the take knows its song");
    expect (first.numPlayed == 35 && first.numByKey == 20 && first.numByMouse == 15, "hits are counted by how they were played");
    expect (first.numRolled == 4 && first.hits.size() == 31, "hits of a held key are left out of the timing, autoplay is not counted");
    expect (std::abs (first.time.toMilliseconds() - juce::Time::fromISO8601 ("2026-11-25T21:00:00.000+09:00").toMilliseconds() - 500) < 5,
            "the take knows when it was played");

    const auto firstReport = hvd::analysePractice (first.hits, chart);
    const auto secondReport = hvd::analysePractice (second.hits, chart);
    report << "first take: median " << juce::String (firstReport.all.medianMs, 1) << " ms, within 30 ms " << juce::String (firstReport.all.withinPercent, 0)
           << "%, sections " << static_cast<int> (firstReport.sections.size()) << "; second take: median " << juce::String (secondReport.all.medianMs, 1)
           << " ms at speed " << juce::String (second.slowestSpeed, 2) << "\n";

    expect (std::abs (firstReport.all.medianMs - 10.0f) < 1.0f && firstReport.all.withinPercent > 90.0f, "ten milliseconds late reads as ten milliseconds late");
    expect (second.numPlayed == 25 && std::abs (second.slowestSpeed - 0.5f) < 0.01f, "the second take was played at half speed");
    expect (std::abs (secondReport.all.medianMs + 20.0f) < 1.0f, "at half speed, ten milliseconds of the song early is twenty milliseconds early");
    expect (firstReport.sections.size() >= 2, "the take is split into sections of the song");

    const auto after = folder.findChildFiles (juce::File::findFiles, false, "play-*.jsonl");
    expect (after.size() == before.size() + 1, "the check left one more log in the folder");
    return ok;
}

/** 자체 검사가 남긴 연주 로그를 읽어, 친 것과 그때의 상황이 제대로 적혔는지 본다.
    takeHits는 키로 친 타격의 수(단타 8번 + 반복 키), songNotes는 마지막에 시범 연주한 곡의 채보다. */
bool checkPlayLog (const juce::File& folder, int takeHits, const std::vector<hvd::ChartNote>& songNotes, juce::String& report)
{
    report << "\n[play log]\n";
    auto ok = true;

    const auto expect = [&ok, &report] (bool condition, const juce::String& what)
    {
        ok = ok && condition;
        report << (condition ? "ok      " : "FAILED  ") << what << "\n";
    };

    const auto files = folder.findChildFiles (juce::File::findFiles, false, "*.jsonl");
    expect (files.size() == 1, "one log file was made");

    if (files.isEmpty())
        return false;

    juce::StringArray lines;
    lines.addLines (files[0].loadFileAsString());
    lines.removeEmptyStrings();

    juce::Array<juce::var> entries;
    auto allParsed = true;

    for (const auto& line : lines)
    {
        juce::var parsed;

        if (juce::JSON::parse (line, parsed).wasOk() && parsed.isObject() && parsed.hasProperty ("t") && parsed["e"].isString())
            entries.add (parsed);
        else
            allParsed = false;
    }

    report << "file: " << files[0].getFileName() << ", " << lines.size() << " lines, " << files[0].getSize() << " bytes\n";
    expect (allParsed && ! entries.isEmpty(), "every line is a JSON object with t and e");

    if (entries.isEmpty())
        return false;

    // event인 줄 가운데 name의 값이 value인 것의 수. name을 주지 않으면 event인 줄 모두.
    const auto count = [&entries] (const char* event, const char* name = nullptr, const juce::String& value = {})
    {
        auto total = 0;

        for (const auto& entry : entries)
            if (entry["e"].toString() == event && (name == nullptr || entry[name].toString() == value))
                ++total;

        return total;
    };

    expect (entries.getFirst()["e"].toString() == "start", "the file begins when the first hit is played");
    expect (count ("audio", "open", "1") >= 1 || count ("audio", "open", "true") >= 1, "audio device is written down");
    expect (count ("kit", "folder", "default") >= 1 && count ("keymap") >= 1 && count ("tempo") >= 1, "kit, keymap and tempo are written down");

    // 키로 친 것: 단타 8번(스페이스 두 번 포함)과 반복 키
    const auto repeats = count ("hit", "key", "Digit2");
    report << "hits by key " << count ("hit", "src", "key") << " (Space " << count ("hit", "key", "Space") << ", repeat key " << repeats
           << "), replayed " << count ("hit", "src", "replay") << ", autoplay " << count ("hit", "src", "auto")
           << ", key downs " << count ("down") << ", ups " << count ("up") << "\n";

    expect (count ("hit", "src", "key") == takeHits && count ("hit", "key", "Space") == 2, "hits carry the key that played them");
    expect (repeats == takeHits - 8 && repeats >= 3, "hits of a held repeat key carry that key");
    expect (count ("down") == 9 && count ("up") == 9, "key presses and releases are logged");
    expect (count ("hit", "src", "replay") == takeHits, "replayed hits are marked as replay");
    expect (count ("hit", "src", "auto") >= 25, "autoplay hits are marked as auto");

    // 누른 키는 그 키가 낸 타격보다 앞에, 바로 가까이에 있다.
    auto downAt = -1.0, hitAt = -1.0;

    for (const auto& entry : entries)
    {
        if (entry["key"].toString() != "Space")
            continue;

        if (entry["e"].toString() == "down" && downAt < 0.0)
            downAt = static_cast<double> (entry["t"]);
        else if (entry["e"].toString() == "hit" && hitAt < 0.0)
            hitAt = static_cast<double> (entry["t"]);
    }

    report << "first Space: down at " << juce::String (downAt, 4) << " s, hit at " << juce::String (hitAt, 4) << " s\n";
    expect (downAt >= 0.0 && hitAt >= downAt && hitAt - downAt < 0.05, "a key press is followed at once by its hit");

    // 곡과 함께 친 타격에는 곡 안의 위치가 붙는다. 시범 연주는 채보의 시각에 정확히 치므로 채보와 맞아야 한다.
    auto withSong = 0, onNote = 0;
    auto worstMs = 0.0;

    for (const auto& entry : entries)
    {
        if (entry["e"].toString() != "hit" || ! entry.hasProperty ("song"))
            continue;

        ++withSong;
        const auto position = static_cast<double> (entry["song"]);
        auto nearest = 1.0e9;

        for (const auto& note : songNotes)
            nearest = std::min (nearest, std::abs (note.seconds - position));

        worstMs = std::max (worstMs, nearest * 1000.0);
        onNote += nearest < 0.005 ? 1 : 0;
    }

    report << "hits with a song position: " << withSong << ", on a chart note: " << onNote
           << ", farthest from a note: " << juce::String (worstMs, 2) << " ms\n";
    expect (withSong >= 5 && onNote == withSong, "hits made while a song plays carry the position in the song");

    // 반주와 게임의 진행
    auto sawTrack = false, sawSong = false, sawHalfSpeed = false, sawSongGame = false;

    for (const auto& entry : entries)
    {
        const auto event = entry["e"].toString();

        if (event == "track")
        {
            sawTrack = sawTrack || entry["file"].toString().endsWith ("track.wav");
            sawSong = sawSong || entry["file"].toString().endsWith ("test song.wav");
        }
        else if (event == "play")
        {
            sawHalfSpeed = sawHalfSpeed || std::abs (static_cast<double> (entry["speed"]) - 0.5) < 0.001;
        }
        else if (event == "game" && entry["state"].toString() == "start")
        {
            sawSongGame = sawSongGame || (entry["song"].toString().endsWith ("test song.wav") && static_cast<int> (entry["level"]) == 0);
        }
    }

    report << "track lines " << count ("track") << ", play " << count ("play") << ", pause " << count ("pause")
           << ", seek " << count ("seek") << ", game " << count ("game") << "\n";

    expect (sawTrack && sawSong, "opened tracks are written down");
    expect (sawHalfSpeed && count ("pause") >= 1, "playing and stopping a track is written down with its speed");
    expect (count ("game", "state", "start") == 2 && sawSongGame, "games are written down with the song and level");
    expect (count ("game", "state", "stop") == 2 && count ("game", "state", "auto") == 1, "ending a game and switching auto mode are written down");

    return ok;
}

} // namespace

class DrumApplication final : public juce::JUCEApplication
{
public:
    DrumApplication() = default;

    const juce::String getApplicationName() override       { return "Hogyun's Virtual Drum"; }
    const juce::String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        // 한글이 깨지지 않도록 기본 글꼴을 맑은 고딕으로 둔다.
        juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName ("Malgun Gothic");

        const auto args = getCommandLineParameterArray();

        // --keymap <파일>: 저장해 둔 키 배열 대신 이 키 배열을 쓴다.
        if (const auto index = args.indexOf ("--keymap"); index >= 0 && args[index + 1].isNotEmpty())
            keymapFile = resolvePath (args[index + 1]);

        // --coretests <보고서 파일>: 코어 자동 테스트만 돌리고 끝낸다.
        if (const auto index = args.indexOf ("--coretests"); index >= 0)
        {
            std::string log;
            const auto failures = hvd::runCoreTests (log);
            writeReport (args[index + 1], juce::String (log));
            setApplicationReturnValue (failures == 0 ? 0 : 1);
            quit();
            return;
        }

        // --charttest <보고서 파일>: 기본 킷으로 아는 패턴을 연주해 그 소리를 받아 적고 얼마나 맞는지 잰다.
        if (const auto index = args.indexOf ("--charttest"); index >= 0)
        {
            juce::String report;
            const auto ok = runChartTest (findDataRoot(), report);
            writeReport (args[index + 1], report + "\nresult: " + (ok ? "ALL OK" : "FAILED") + "\n");
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
            return;
        }

        // --transcribe <음악 파일> <보고서 파일> [--stem] [--events] [--chart <채보 파일>]: 파일 하나를 받아 적어 본다.
        // --stem은 드럼만 분리해 둔 소리일 때, --events는 찾은 타격마다 잰 값을 모두 적을 때 쓴다.
        if (const auto index = args.indexOf ("--transcribe"); index >= 0)
        {
            const auto chartIndex = args.indexOf ("--chart");
            writeReport (args[index + 2], transcribeToReport (resolvePath (args[index + 1]), args.contains ("--stem"), args.contains ("--events"),
                                                              chartIndex >= 0 ? resolvePath (args[chartIndex + 1]) : juce::File()));
            quit();
            return;
        }

        // --analyse-folder <진행 파일> [--folder <음악 폴더>] [--workers <수>] [--force]:
        // 폴더(기본은 사용자의 음악 폴더)의 모든 곡의 채보를 만든다. 이미 채보가 있는 곡은 건너뛴다(--force면 다시 만든다).
        // 진행 상황을 2초마다 진행 파일에 적고, "<진행 파일>.stop" 파일이 생기면 하던 곡을 그만두고 끝낸다.
        // 다시 실행하면 남은 곡부터 이어서 한다. scripts/make-charts.ps1이 이 명령을 쓴다.
        if (const auto index = args.indexOf ("--analyse-folder"); index >= 0)
        {
            const auto folderIndex = args.indexOf ("--folder");
            const auto workersIndex = args.indexOf ("--workers");

            runBatch (args[index + 1],
                      folderIndex >= 0 ? resolvePath (args[folderIndex + 1]) : juce::File(),
                      workersIndex >= 0 ? args[workersIndex + 1].getIntValue() : 3,
                      args.contains ("--force"));
            return;
        }

        // --testsong <WAV 파일> <템포>: 기본 킷으로 아는 패턴(16마디 록)을 연주한 소리를 쓴다.
        if (const auto index = args.indexOf ("--testsong"); index >= 0)
        {
            setApplicationReturnValue (writeTestSong (findDataRoot(), resolvePath (args[index + 1]), args[index + 2].getDoubleValue()) ? 0 : 1);
            quit();
            return;
        }

        // --chartscore <드럼만 분리한 WAV> <템포> <보고서 파일>: --testsong의 패턴이 든 소리를 받아 적고 패턴과 견줘 채점한다.
        if (const auto index = args.indexOf ("--chartscore"); index >= 0)
        {
            juce::String report;
            const auto ok = scoreStemAgainstPattern (resolvePath (args[index + 1]), args[index + 2].getDoubleValue(), report);
            writeReport (args[index + 3], report);
            setApplicationReturnValue (ok ? 0 : 1);
            quit();
            return;
        }

        // --analyse <보고서 파일> <음악 파일>...: 프로그램 안에서 "채보 만들기"를 누른 것과 같은 길로 채보를 만든다.
        // 드럼 분리 도구가 있으면 쓴다. 채보는 평소와 같은 자리(charts/)에 저장되므로 프로그램에서 바로 쓸 수 있다.
        if (const auto index = args.indexOf ("--analyse"); index >= 0)
        {
            juce::Array<juce::File> files;

            for (int i = index + 2; i < args.size(); ++i)
                files.add (resolvePath (args[i]));

            runAnalysis (args[index + 1], files);
            return;
        }

        // --selftest <보고서 파일>: 창 없이 킷, 키 처리, 편집, 오디오 장치, 녹음, 반주, 리플레이를 검사하고 끝낸다.
        // 장치는 실제로 돌리지만 무음을 내보내므로 소리는 나지 않는다.
        if (const auto index = args.indexOf ("--selftest"); index >= 0)
        {
            runSelfTest (args[index + 1]);
            return;
        }

        // --screenshot <PNG 파일> [--page play|kit|keys|mixer|track|game|gameplay] [--after <ms>] [--press <키,키>]: 화면을 그려 PNG로 저장하고 끝낸다.
        // gameplay는 리듬 게임의 연습 패턴을 시범 연주로 돌리는 모습이다. --after로 찍을 때까지 기다릴 시간을 정한다.
        // --press는 찍기 전에 누를 키 이름이다(예: Pedal1). 주지 않으면 Space, J, D를 누른다.
        // --footswitch는 풋스위치가 꽂혀 있는 것으로 친다.
        if (const auto index = args.indexOf ("--screenshot"); index >= 0)
        {
            const auto pageIndex = args.indexOf ("--page");
            const auto afterIndex = args.indexOf ("--after");
            const auto pressIndex = args.indexOf ("--press");
            const auto pageName = pageIndex >= 0 ? args[pageIndex + 1] : juce::String ("play");
            const auto demo = pageName == "gameplay";

            std::vector<hvd::KeyCode> pressKeys;

            if (pressIndex >= 0)
                for (const auto& name : juce::StringArray::fromTokens (args[pressIndex + 1], ",", {}))
                    if (const auto key = hvd::keyFromName (name.trim().toStdString()))
                        pressKeys.push_back (*key);

            saveScreenshot (args[index + 1], demo ? MainComponent::pageIndexFromName ("game") : MainComponent::pageIndexFromName (pageName),
                            demo, afterIndex >= 0 ? args[afterIndex + 1].getIntValue() : (demo ? 6500 : 360), pressKeys,
                            args.contains ("--footswitch"));
            return;
        }

        MainComponent::Options options;
        options.dataRoot = findDataRoot();
        options.keymapFile = keymapFile;
        options.playLogFolder = options.dataRoot.getChildFile ("logs");    // 연주 로그. 검사와 도구 실행에서는 남기지 않는다
        options.backupsFolder = options.dataRoot.getChildFile ("backups"); // 킷과 키 배열의 이전 내용. 마찬가지다
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), options);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        headless = nullptr;
        analysis = nullptr;
        batch = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, const MainComponent::Options& options)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                          .findColour (juce::ResizableWindow::backgroundColourId),
                              allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (options), true);
            setResizable (true, true);
            setResizeLimits (1000, 680, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);

            if (auto* content = getContentComponent())
                content->grabKeyboardFocus();
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    static void writeReport (const juce::String& path, const juce::String& text)
    {
        if (path.isNotEmpty())
            resolvePath (path).replaceWithText (text);
    }

    MainComponent::Options headlessOptions() const
    {
        MainComponent::Options options;
        options.dataRoot = findDataRoot();
        options.keymapFile = keymapFile;
        options.rememberSettings = false;    // 검사는 사용자의 설정을 읽지도 바꾸지도 않는다
        options.startKeyboard = false;
        return options;
    }

    /** 자체 검사가 단계 사이에 들고 다니는 것들. */
    struct SelfTest
    {
        juce::String report;
        juce::String reportPath;
        juce::File temp;
        bool ok = true;
        int time = 0;                // 다음 단계를 실행할 시각 (ms)
        int takeHits = 0;
        int hitsBeforeReplay = 0;
        int songIndex = -1;
        int waited = 0;

        void expect (bool condition, const juce::String& what)
        {
            ok = ok && condition;
            report << (condition ? "ok      " : "FAILED  ") << what << "\n";
        }
    };

    /** delayMs 뒤에(앞 단계 기준) action을 실행하도록 걸어 둔다. */
    void schedule (const std::shared_ptr<SelfTest>& test, int delayMs, std::function<void (Session&)> action)
    {
        test->time += delayMs;

        juce::Timer::callAfterDelay (test->time, [this, action = std::move (action)]
        {
            if (headless != nullptr)
                action (headless->getSession());
        });
    }

    static int totalHits (Session& session)
    {
        auto hits = 0;

        if (const auto* kit = session.getKit())
            for (int pad = 0; pad < static_cast<int> (kit->pads.size()); ++pad)
                hits += static_cast<int> (session.getEngine().getHitCount (pad));

        return hits;
    }

    void runSelfTest (const juce::String& reportPath)
    {
        auto test = std::make_shared<SelfTest>();
        test->reportPath = reportPath;
        test->temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("hvd-selftest-" + juce::String (juce::Time::currentTimeMillis()));
        test->temp.createDirectory();

        // 1. 코어 자동 테스트
        std::string coreLog;
        test->ok = hvd::runCoreTests (coreLog) == 0 && test->ok;
        test->report << "[core tests]\n" << juce::String (coreLog) << "\n";

        // 2. 키보드 메시지 처리 규칙
        test->ok = checkKeyboardRules (test->report) && test->ok;
        test->ok = checkMp3Start (test->report) && test->ok;

        // 3. 킷을 장치 없이 재생, 편집용 문서로 고쳐 저장
        auto options = headlessOptions();

        // 키 배열은 사용자가 고쳐 쓰는 파일이다. 아래에서 누르는 키들이 무엇을 치는지가 검사와 어긋나지 않도록,
        // 검사는 자기 키 배열을 따로 만들어 쓴다 (실행할 때 --keymap을 주었으면 그것을 따른다).
        if (options.keymapFile == juce::File())
        {
            options.keymapFile = test->temp.getChildFile ("selftest-keymap.json");
            options.keymapFile.replaceWithText (R"({
              "name": "selftest",
              "tempo": 120,
              "beatsPerBar": 4,
              "bindings": [
                {"key": "Space", "pad": "kick", "velocity": 1.0},
                {"key": "KeyS", "pad": "snare", "velocity": 1.0},
                {"key": "KeyJ", "pad": "hihat_closed", "velocity": 0.9},
                {"key": "KeyQ", "pad": "crash1", "velocity": 1.0},
                {"key": "Digit2", "pad": "snare", "velocity": 0.9, "stroke": "repeat", "rate": "1/16", "accentEvery": 4, "softVelocity": 0.6}
              ]
            })");
        }

        options.musicFolder = test->temp.getChildFile ("music");    // 사용자의 음악 폴더와 채보 폴더는 건드리지 않는다
        options.musicFolder.createDirectory();
        options.chartsFolder = test->temp.getChildFile ("charts");
        options.playLogFolder = test->temp.getChildFile ("logs");    // 연주 로그도 임시 폴더에 남겨 끝에서 읽어 본다
        options.backupsFolder = test->temp.getChildFile ("backups"); // 백업도 마찬가지
        const auto kitFile = options.dataRoot.getChildFile ("kits").getChildFile ("default").getChildFile ("kit.json");
        test->ok = checkKitOffline (kitFile, test->report) && test->ok;
        test->ok = checkDocuments (options.dataRoot, test->temp, test->report) && test->ok;
        test->ok = checkBackups (test->temp, test->report) && test->ok;

        // 4. 실제 화면 구성 요소로 킷, 키 배열, 오디오 장치를 연다
        headless = std::make_unique<MainComponent> (options);
        auto& session = headless->getSession();

        test->report << "\n[kit and keymap]\ndata root: " << options.dataRoot.getFullPathName() << "\n";

        if (const auto* kit = session.getKit())
        {
            const auto& stats = session.getKitStats();
            test->report << "kit: " << fromUtf8 (kit->name) << "\n"
                         << "license: " << fromUtf8 (kit->license) << "\n"
                         << "pads: " << static_cast<int> (kit->pads.size()) << ", samples: " << stats.numSamples
                         << ", total " << juce::String (stats.totalSeconds, 1) << " s, "
                         << juce::String (static_cast<double> (stats.memoryBytes) / 1.0e6, 1) << " MB in memory\n"
                         << "leading silence trimmed: " << juce::String (stats.averageTrimMs, 2) << " ms on average\n";
        }
        else
        {
            test->ok = false;
            test->report << "FAILED: no kit\n";
        }

        for (const auto& message : session.getMessages())
            test->report << "message: " << message << "\n";

        test->ok = session.getMessages().isEmpty() && test->ok;

        // 모든 화면을 한 번씩 만들어 그려 본다 (그리다가 죽는 일이 없는지).
        for (int page = 0; page < MainComponent::numPages; ++page)
        {
            headless->showPage (page);
            const auto image = headless->createComponentSnapshot (headless->getLocalBounds(), true, 1.0f);
            test->ok = image.isValid() && test->ok;
        }

        headless->showPage (0);
        test->report << "pages drawn: " << MainComponent::numPages << "\n";

        // 5. Raw Input 등록이 되는지 (받은 키는 버린다)
        const auto registered = session.getKeyboard().start ([] (hvd::KeyCode, bool) {});
        session.getKeyboard().stop();
        test->ok = registered && test->ok;
        test->report << "\n[raw input]\nregistration: " << (registered ? "ok" : "FAILED") << "\n";

        // 6. 실제 장치로 키 → 소리. 장치에는 무음을 내보내고, 그 앞 단계의 소리 크기를 잰다.
        //    동시에 WAV 녹음과 연주 기록도 돌린다.
        schedule (test, 300, [test] (Session& s)
        {
            s.getAudio().setOutputMuted (true);
            s.getAudio().resetCounters();
            s.startTake();
            test->expect (s.getAudio().startRecording (test->temp.getChildFile ("recording.wav"), true).isEmpty(), "recording started");
        });

        // 킥, 스네어, 하이햇, 크래시 키를 순서대로 눌러 본다.
        const hvd::KeyCode keys[] = { 0x39 /* Space */, 0x1f /* S */, 0x24 /* J */, 0x1f, 0x39, 0x1f, 0x24, 0x10 /* Q */ };

        for (const auto key : keys)
            schedule (test, 140, [key] (Session& s) { s.handleKey (key, true); s.handleKey (key, false); });

        // 이어서 반복 키(스네어 16분음표)를 0.5초 동안 누르고 있다가 뗀다. 템포 120에서 네댓 번 울려야 한다.
        constexpr hvd::KeyCode repeatKey = 0x03;    // Digit2
        schedule (test, 340, [] (Session& s) { s.handleKey (repeatKey, true); });
        schedule (test, 500, [] (Session& s) { s.handleKey (repeatKey, false); });

        schedule (test, 700, [test] (Session& s)
        {
            s.poll();
            s.stopTake();
            s.getAudio().stopRecording();

            auto& audio = s.getAudio();
            const auto peak = audio.getMaxPeak();
            const auto hits = totalHits (s);
            const auto repeats = hits - 8;
            test->takeHits = s.getTakeHitCount();

            test->report << "\n[audio device]\n" << audio.describe() << "\n"
                         << "callbacks: " << audio.getCallbackCount()
                         << ", late callbacks: " << audio.getLateCallbackCount()
                         << ", single hits: 8, repeat hits while held 0.5 s: " << repeats
                         << ", output peak before muting: " << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dB"
                         << ", dropped commands: " << static_cast<int> (s.getEngine().getDroppedCommandCount()) << "\n";

            // 단타 8번에 반복 타격 3~6번. 반복은 키를 뗀 뒤에 멈춰 있어야 한다.
            test->expect (audio.getStatus().open && audio.getCallbackCount() > 0, "device is running");
            test->expect (peak > 0.01f, "drums reached the output stage");
            // 여기까지는 드럼만 냈다(반주와 메트로놈 없음). 리미터가 걸려 있으면 천장을 넘지 않는다.
            test->expect (peak <= hvd::PeakLimiter::ceiling + 0.001f, "drum limiter keeps the drums under the ceiling");
            test->expect (repeats >= 3 && repeats <= 6, "repeat key played 3 to 6 hits");
            test->expect (s.getEngine().getActiveRunnerCount() == 0, "repeat stopped after key up");

            test->report << "\n[recording and take]\n";
            const auto [seconds, recordedPeak] = measureAudioFile (test->temp.getChildFile ("recording.wav"));
            test->report << "recorded " << juce::String (seconds, 2) << " s, peak "
                         << juce::String (juce::Decibels::gainToDecibels (recordedPeak), 1) << " dB\n";
            test->expect (seconds > 1.5 && recordedPeak > 0.01f, "WAV recording contains the drums");
            test->expect (test->takeHits == hits, "take recorded every hit");

            // 7. 반주: 사인파 파일을 만들어 열고 재생한다.
            test->report << "\n[backing track]\n";
            const auto trackFile = test->temp.getChildFile ("track.wav");
            test->expect (writeSineWav (trackFile, 3.0, 44100.0) && s.loadTrack (trackFile).isEmpty(), "track loaded");
            audio.resetCounters();
            s.getTrack().setSpeed (0.5f);
            s.getTrack().setPlaying (true);
        });

        schedule (test, 600, [test] (Session& s)
        {
            const auto position = s.getTrack().getPosition();
            const auto peak = s.getAudio().getMaxPeak();
            test->report << "after 0.6 s at half speed: position " << juce::String (position, 2) << " s, peak "
                         << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dB\n";
            test->expect (position > 0.15 && position < 0.50, "track advanced at half speed");
            test->expect (peak > 0.05f, "track reached the output stage");
            s.unloadTrack();

            // 8. 연주 기록을 MIDI로 내보내고 다시 읽은 뒤, 엔진으로 다시 친다.
            test->report << "\n[midi and replay]\n";
            const auto midiFile = test->temp.getChildFile ("take.mid");
            test->expect (s.exportTakeAsMidi (midiFile).isEmpty() && midiFile.getSize() > 0, "take exported as MIDI");
            test->expect (s.importTakeFromMidi (midiFile).isEmpty() && s.getTakeHitCount() == test->takeHits, "MIDI import gives the same hits");

            s.poll();
            test->hitsBeforeReplay = totalHits (s);
            s.playTake();
            test->expect (s.isTakePlaying(), "replay started");
        });

        // 기록은 2초 남짓이다. 넉넉히 기다린 뒤 다 쳤는지 본다.
        schedule (test, 3600, [this, test] (Session& s)
        {
            s.poll();
            const auto replayed = totalHits (s) - test->hitsBeforeReplay;
            test->report << "replayed hits: " << replayed << " of " << test->takeHits << "\n";
            test->expect (replayed == test->takeHits, "replay played every hit");
            test->expect (! s.isTakePlaying(), "replay finished");

            // 9. 리듬 게임: 곡 없이 연습 패턴을 시범 연주로 돌린다. 프로그램이 정확한 시각에 치므로 모두 퍼펙트여야 한다.
            test->report << "\n[rhythm game]\n";
            headless->showPage (MainComponent::pageIndexFromName ("game"));
            gamePage().startDemo (true);
            test->expect (gamePage().getRunner().getState() == GameRunner::State::playing, "practice pattern started");
        });

        schedule (test, 7000, [this, test] (Session&)
        {
            const auto& game = gamePage().getRunner().getGame();
            test->report << "practice pattern after 7 s: perfect " << game.getCount (hvd::Judgement::perfect)
                         << ", great " << game.getCount (hvd::Judgement::great) << ", good " << game.getCount (hvd::Judgement::good)
                         << ", miss " << game.getCount (hvd::Judgement::miss) << ", combo " << game.getCombo() << "\n";
            test->expect (game.getCount (hvd::Judgement::perfect) >= 20, "autoplay hits were judged perfect");
            test->expect (game.getCount (hvd::Judgement::miss) == 0 && game.getCount (hvd::Judgement::good) == 0, "no misses in autoplay");

            // 도중에 오토 모드를 끄면 더 치지 않고, 그 판은 오토 모드를 쓴 판으로 남는다.
            gamePage().getRunner().setAutoplay (false);
            test->expect (! gamePage().getRunner().isAutoplay() && gamePage().getRunner().wasAssisted(), "auto mode can be switched off while playing");
            gamePage().endPlay();

            // 10. 기본 킷으로 연주한 곡을 음악 폴더에 넣고, 받아 적게 한다 (드럼 분리 도구는 쓰지 않는다).
            const auto musicFolder = test->temp.getChildFile ("music");
            const auto song = musicFolder.getChildFile ("test song.wav");
            test->expect (writeTestSong (findDataRoot(), song, 120.0), "test song written");

            auto& charts = headless->getSession().getCharts();
            charts.setUseSeparator (false);
            charts.setMusicFolder (musicFolder);
            test->songIndex = charts.findSong (song);
            test->expect (charts.getNumSongs() == 1 && test->songIndex == 0, "music folder lists the song");
            charts.analyse ({ test->songIndex }, true);
            waitForChart (test);
        });

        // 리플레이와 리듬 게임은 화면 타이머가 돌아야 진행된다. 창이 없어도 MainComponent의 타이머는 돈다.
    }

    GamePage& gamePage()
    {
        return dynamic_cast<GamePage&> (headless->getPage (MainComponent::pageIndexFromName ("game")));
    }

    /** 채보가 만들어질 때까지 0.25초마다 살핀다. 다 되면 그 곡으로 한 판을 시범 연주한다. */
    void waitForChart (const std::shared_ptr<SelfTest>& test)
    {
        juce::Timer::callAfterDelay (250, [this, test]
        {
            if (headless == nullptr)
                return;

            auto& charts = headless->getSession().getCharts();
            const auto& song = charts.getSong (test->songIndex);

            if (! song.hasChart && song.error.isEmpty() && ++test->waited < 240)
            {
                waitForChart (test);
                return;
            }

            test->report << "analysis took " << juce::String (test->waited * 0.25, 1) << " s: tempo " << juce::String (song.tempo, 1)
                         << ", stars " << juce::String (song.stars[0], 1) << " / " << juce::String (song.stars[1], 1) << " / " << juce::String (song.stars[2], 1)
                         << " / " << juce::String (song.stars[3], 1)
                         << ", notes " << song.noteCounts[0] << " / " << song.noteCounts[1] << " / " << song.noteCounts[2] << " / " << song.noteCounts[3]
                         << (song.error.isNotEmpty() ? "  error: " + song.error : juce::String()) << "\n";

            test->expect (song.hasChart, "chart was made from the song");
            test->expect (std::abs (song.tempo - 120.0f) < 3.0f, "tempo of the song found");
            test->expect (song.stars[0] > 0.0f && song.stars[0] <= song.stars[1] && song.stars[1] <= song.stars[2]
                              && song.stars[2] <= song.stars[3] && song.stars[3] <= 5.0f,
                          "star ratings rise with the level");

            hvd::Chart chart;
            test->expect (charts.loadChart (test->songIndex, chart) && static_cast<int> (chart.notes.size()) == song.noteCounts[3], "chart file reads back");

            // 11. 그 곡으로 한 판: 곡을 틀고 채보를 시범 연주한다.
            const auto error = gamePage().startSong (test->songIndex, true);
            test->expect (error.isEmpty(), "song started" + (error.isNotEmpty() ? ": " + error : juce::String()));

            juce::Timer::callAfterDelay (7000, [this, test]
            {
                if (headless == nullptr)
                    return;

                auto& session = headless->getSession();
                const auto& runner = gamePage().getRunner();
                const auto& game = runner.getGame();
                const auto sync = session.getAudio().getTrackSync();

                test->report << "song after 7 s: position " << juce::String (runner.getSongSeconds(), 2) << " s, perfect "
                             << game.getCount (hvd::Judgement::perfect) << ", great " << game.getCount (hvd::Judgement::great)
                             << ", miss " << game.getCount (hvd::Judgement::miss) << "\n";

                test->expect (sync.active && runner.getSongSeconds() > 3.0 && runner.getSongSeconds() < 4.6, "song position follows the audio clock");
                // 기본 단계(가볍게)는 킥과 스네어의 4분음표뿐이라 4초 동안 대여섯 개다.
                test->expect (game.getCount (hvd::Judgement::perfect) >= 5 && game.getCount (hvd::Judgement::miss) == 0, "autoplay follows the chart in time with the song");

                gamePage().endPlay();
                test->expect (! session.getTrack().isPlaying(), "song stopped with the game");
                test->expect (session.getAudio().getLateCallbackCount() < 5, "almost no late audio callbacks");

                // 12. 연주 로그: 지금까지 한 것이 파일에 제대로 적혔는지 읽어 본다.
                hvd::Chart songChart;
                session.getCharts().loadChart (test->songIndex, songChart);
                session.poll();
                session.flushPlayLog();
                test->ok = checkPlayLog (test->temp.getChildFile ("logs"), test->takeHits, songChart.notes, test->report) && test->ok;

                // 13. 백업과 되돌리기
                test->ok = checkBackupRoundTrip (session, test->report) && test->ok;

                // 14. 연습 기록: 로그에서 연주를 읽어 박자를 재고, 화면이 그것을 보여 주는지
                auto& practice = dynamic_cast<PracticePage&> (headless->getPage (MainComponent::pageIndexFromName ("practice")));
                test->ok = checkPracticeLog (test->temp.getChildFile ("logs"), session.getCharts().getSong (test->songIndex).file, songChart, test->report) && test->ok;
                headless->showPage (MainComponent::pageIndexFromName ("practice"));
                test->expect (practice.getNumTakes() == 2, "practice page lists the takes found in the log folder");
                test->expect (headless->createComponentSnapshot (headless->getLocalBounds(), true, 1.0f).isValid(), "practice page draws a take");

                test->report << "\nresult: " << (test->ok ? "ALL OK" : "FAILED") << "\n";
                writeReport (test->reportPath, test->report);
                headless = nullptr;
                test->temp.deleteRecursively();

                setApplicationReturnValue (test->ok ? 0 : 1);
                quit();
            });
        });
    }

    void saveScreenshot (const juce::String& path, int page, bool gameDemo, int afterMs, std::vector<hvd::KeyCode> pressKeys, bool footswitch)
    {
        // 연습 기록 화면은 사용자의 연주 로그를 읽어서 그린다. 읽기만 하고, 이 실행에서는 로그를 남기지 않는다.
        auto options = headlessOptions();
        options.playLogFolder = options.dataRoot.getChildFile ("logs");
        options.writePlayLog = false;

        headless = std::make_unique<MainComponent> (options);
        headless->getSession().getAudio().setOutputMuted (true);
        headless->showPage (page);

        if (footswitch)
            headless->getSession().pretendFootswitchConnected();

        juce::Timer::callAfterDelay (250, [this, gameDemo, pressKeys = std::move (pressKeys)]
        {
            if (headless == nullptr)
                return;

            if (gameDemo)
            {
                gamePage().startDemo (true);
                return;
            }

            // 패드가 켜진 모습도 보이도록 키 몇 개를 누른 뒤에 찍는다. 기본은 Space, J, D.
            auto& session = headless->getSession();

            for (const auto key : pressKeys.empty() ? std::vector<hvd::KeyCode> { 0x39, 0x24, 0x20 } : pressKeys)
                session.handleKey (key, true);
        });

        juce::Timer::callAfterDelay (juce::jmax (300, afterMs), [this, path]
        {
            auto file = resolvePath (path);
            file.deleteFile();

            if (headless != nullptr)
            {
                headless->refreshStatus();
                const auto image = headless->createComponentSnapshot (headless->getLocalBounds(), true, 2.0f);

                if (auto stream = file.createOutputStream())
                {
                    juce::PNGImageFormat png;
                    png.writeImageToStream (image, *stream);
                }
            }

            setApplicationReturnValue (file.existsAsFile() ? 0 : 1);
            headless = nullptr;
            quit();
        });
    }

    /** --analyse: 곡을 하나씩 받아 적는다. 곡마다 그 곡이 든 폴더를 음악 폴더로 삼아 목록에서 찾는다. */
    struct Analysis
    {
        std::unique_ptr<ChartLibrary> library;
        juce::Array<juce::File> files;
        juce::String report, reportPath;
        int next = 0, current = -1, failures = 0;
        double started = 0.0;
    };

    void runAnalysis (const juce::String& reportPath, const juce::Array<juce::File>& files)
    {
        analysis = std::make_unique<Analysis>();
        analysis->library = std::make_unique<ChartLibrary> (findDataRoot());
        analysis->files = files;
        analysis->reportPath = reportPath;
        analysis->report << "separator installed: " << (analysis->library->isSeparatorInstalled() ? "yes" : "no") << "\n";
        stepAnalysis();
    }

    void stepAnalysis()
    {
        if (analysis == nullptr)
            return;

        auto& state = *analysis;
        auto& library = *state.library;
        library.poll();

        if (state.current >= 0)
        {
            const auto& song = library.getSong (state.current);

            if (! song.hasChart && song.error.isEmpty())
            {
                juce::Timer::callAfterDelay (250, [this] { stepAnalysis(); });
                return;
            }

            const auto seconds = (juce::Time::getMillisecondCounterHiRes() - state.started) / 1000.0;
            state.report << song.title << ": " << (song.hasChart ? "ok" : "FAILED " + song.error) << " in " << juce::String (seconds, 1) << " s";

            if (song.hasChart)
            {
                state.report << ", " << (song.precise ? "separated drums" : "full mix") << ", tempo " << juce::String (song.tempo, 1) << ", stars";

                for (int level = 0; level < hvd::numChartLevels; ++level)
                    state.report << " " << juce::String (song.stars[static_cast<std::size_t> (level)], 1);

                state.report << ", notes";

                for (int level = 0; level < hvd::numChartLevels; ++level)
                    state.report << " " << song.noteCounts[static_cast<std::size_t> (level)];

                state.report << (song.hasBacking ? ", backing saved" : "");
            }
            else
            {
                ++state.failures;
            }

            state.report << "\n";
            writeReport (state.reportPath, state.report);    // 중간에 끊겨도 그때까지의 결과가 남게 한다
            state.current = -1;
        }

        if (state.next >= state.files.size())
        {
            writeReport (state.reportPath, state.report);
            setApplicationReturnValue (state.failures == 0 ? 0 : 1);
            analysis = nullptr;
            quit();
            return;
        }

        const auto file = state.files[state.next++];
        library.setMusicFolder (file.getParentDirectory());
        state.current = library.findSong (file);

        if (state.current < 0)
        {
            state.report << file.getFullPathName() << ": FAILED (not found among the " << library.getNumSongs()
                         << " songs of " << file.getParentDirectory().getFullPathName() << ")\n";
            ++state.failures;
        }
        else
        {
            state.started = juce::Time::getMillisecondCounterHiRes();
            library.analyse ({ state.current }, true);
        }

        juce::Timer::callAfterDelay (250, [this] { stepAnalysis(); });
    }

    /** --analyse-folder: 폴더의 곡을 모두 받아 적는다. */
    struct Batch
    {
        std::unique_ptr<ChartLibrary> library;
        juce::String reportPath;
        juce::File stopFile;
        double started = 0.0;
        int total = 0, workers = 1, ticks = 0;
    };

    void runBatch (const juce::String& reportPath, const juce::File& folder, int workers, bool force)
    {
        batch = std::make_unique<Batch>();
        batch->library = std::make_unique<ChartLibrary> (findDataRoot());
        batch->reportPath = reportPath;
        batch->stopFile = resolvePath (reportPath + ".stop");
        batch->stopFile.deleteFile();
        batch->workers = juce::jlimit (1, 8, workers);
        batch->started = juce::Time::getMillisecondCounterHiRes();

        auto& library = *batch->library;

        if (folder != juce::File())
            library.setMusicFolder (folder);
        else
            library.rescan();

        std::vector<int> wanted;

        for (int i = 0; i < library.getNumSongs(); ++i)
            if (force || ! library.getSong (i).hasChart)
                wanted.push_back (i);

        batch->total = static_cast<int> (wanted.size());
        library.setNumWorkers (batch->workers);
        library.analyse (wanted, force);
        stepBatch();
    }

    void stepBatch()
    {
        if (batch == nullptr)
            return;

        auto& state = *batch;
        auto& library = *state.library;
        library.poll();

        const auto progress = library.getProgress();
        const auto finished = progress.done >= state.total;
        const auto stopRequested = state.stopFile.existsAsFile();

        if (finished || stopRequested || state.ticks++ % 4 == 0)
        {
            const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - state.started) / 1000.0;
            const auto perSong = progress.done > 0 ? elapsed / progress.done : 0.0;
            const auto clock = [] (double seconds)
            {
                const auto total = static_cast<int> (seconds);
                return juce::String (total / 3600) + ":" + juce::String ((total / 60) % 60).paddedLeft ('0', 2) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
            };

            juce::String report;
            report << ko ("채보 만들기: ") << progress.done << " / " << state.total << ko ("곡 완료 (실패 ") << progress.failed << ")\n"
                   << ko ("음악 폴더: ") << library.getMusicFolder().getFullPathName() << ko ("  ·  전체 ") << library.getNumSongs() << ko ("곡") << "\n"
                   << ko ("경과 ") << clock (elapsed);

            if (progress.done > 0)
                report << ko ("  ·  곡당 평균 ") << juce::String (perSong, 1) << ko ("초  ·  남은 시간 약 ") << clock (perSong * (state.total - progress.done));

            report << "\n" << ko ("드럼 분리: ") << (library.getSeparatorDevice().isNotEmpty() ? library.getSeparatorDevice() : ko ("준비 중"))
                   << ko ("  ·  함께 처리하는 곡 ") << state.workers << "\n";

            if (! finished && ! stopRequested && progress.title.isNotEmpty())
                report << ko ("방금 시작한 곡: ") << progress.title << "\n";

            report << ko ("상태: ") << (finished ? ko ("끝남") : (stopRequested ? ko ("멈춤 (다시 실행하면 남은 곡부터 이어서 합니다)") : ko ("진행 중"))) << "\n";

            juce::StringArray failures;

            for (int i = 0; i < library.getNumSongs(); ++i)
                if (const auto& song = library.getSong (i); song.error.isNotEmpty())
                    failures.add ("  " + song.title + ": " + song.error);

            if (! failures.isEmpty())
                report << "\n" << ko ("실패한 곡:") << "\n" << failures.joinIntoString ("\n") << "\n";

            writeReport (state.reportPath, report);
        }

        if (finished || stopRequested)
        {
            state.stopFile.deleteFile();
            setApplicationReturnValue (0);
            batch = nullptr;    // 하던 곡을 그만두고 분리 도구를 끝낸다
            quit();
            return;
        }

        juce::Timer::callAfterDelay (500, [this] { stepBatch(); });
    }

    std::unique_ptr<Batch> batch;
    std::unique_ptr<Analysis> analysis;
    juce::File keymapFile;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<MainComponent> headless;
};

START_JUCE_APPLICATION (DrumApplication)
