#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "AudioHost.h"
#include "Backups.h"
#include "ChartLibrary.h"
#include "Documents.h"
#include "PlayLog.h"
#include "RawKeyboard.h"
#include "core/Engine.h"
#include "core/InputTiming.h"
#include "core/Mapping.h"
#include "core/Performer.h"
#include "core/Replay.h"
#include "core/SpscQueue.h"
#include "core/TrackPlayer.h"

/** 화면 뒤에서 돌아가는 프로그램 전체. 엔진, 오디오 출력, 키보드 입력, 킷과 키 배열 파일을 가지고 서로 잇는다.

    화면 쪽은 이 객체의 함수를 화면 스레드에서 부르고, 바뀐 것이 있으면 ChangeListener로 알림을 받는다.
    handleKey()만 입력 스레드에서 불린다.
*/
class Session final : public juce::ChangeBroadcaster,
                      private juce::MidiInputCallback
{
public:
    struct Options
    {
        juce::File dataRoot;                // kits/ 와 keymaps/ 가 들어 있는 폴더
        juce::File keymapFile;              // 비어 있으면 설정에 저장된 키 배열(없으면 keymaps/default.json)
        bool rememberSettings = true;       // 장치 설정과 프로그램 설정을 불러오고 저장할지
        bool startKeyboard = true;          // 키보드 입력 스레드를 시작할지
        juce::File musicFolder;             // 리듬 게임의 곡을 찾을 폴더. 비어 있으면 설정에 저장된 폴더(없으면 사용자의 음악 폴더)
        juce::File chartsFolder;            // 채보를 둘 폴더. 비어 있으면 dataRoot/charts
        juce::File playLogFolder;           // 연주 로그를 두는 폴더. 비어 있으면 로그를 남기지 않는다
        bool writePlayLog = true;           // false이면 그 폴더의 로그를 읽기만 하고(연습 기록 화면) 새로 남기지는 않는다
        juce::File backupsFolder;           // 킷과 키 배열의 이전 내용을 둘 폴더. 비어 있으면 남기지 않는다
    };

    explicit Session (const Options& options);
    ~Session() override;

    /** 화면 타이머가 1초에 60번쯤 부른다. 엔진이 알려 온 일을 처리하고 파일이 바뀌었는지 살핀다. */
    void poll();

    //==============================================================================
    hvd::Engine& getEngine() noexcept                        { return engine; }
    hvd::TrackPlayer& getTrack() noexcept                    { return track; }
    AudioHost& getAudio() noexcept                           { return audio; }
    hvd::RawKeyboard& getKeyboard() noexcept                 { return keyboard; }
    ChartLibrary& getCharts() noexcept                       { return charts; }
    juce::File getDataRoot() const                           { return dataRoot; }

    const hvd::Kit* getKit() const noexcept                  { return shownKit; }
    std::shared_ptr<const hvd::Mapping> getMapping() const   { return shownMapping; }
    KitDocument& getKitDocument() noexcept                   { return kitDocument; }
    KeymapDocument& getKeymapDocument() noexcept             { return keymapDocument; }

    const juce::StringArray& getMessages() const noexcept    { return messages; }
    juce::String getAudioError() const                       { return audioError; }
    bool isKeyboardRegistered() const noexcept               { return keyboardRegistered; }

    struct KitStats
    {
        int numSamples = 0;
        double totalSeconds = 0.0;
        double averageTrimMs = 0.0;
        juce::int64 memoryBytes = 0;
    };

    const KitStats& getKitStats() const noexcept             { return kitStats; }

    //==============================================================================
    // 킷
    juce::File getKitsFolder() const                         { return dataRoot.getChildFile ("kits"); }
    juce::File getKitFile() const                            { return kitFolder.getChildFile ("kit.json"); }
    juce::StringArray getKitFolderNames() const;
    juce::String getKitFolderName() const                    { return kitFolder.getFileName(); }

    /** 다른 킷으로 바꾼다. */
    void selectKit (const juce::String& folderName);

    /** 지금 킷을 통째로 복사해 새 킷을 만들고 그 킷으로 바꾼다. 실패하면 이유를 돌려준다. */
    juce::String duplicateKit (const juce::String& newName);

    /** 지금 킷을 ZIP 파일 하나로 내보낸다 / ZIP 파일에서 킷을 가져와 그 킷으로 바꾼다. */
    juce::String exportKit (const juce::File& zipFile) const;
    juce::String importKit (const juce::File& zipFile);

    /** 킷 파일과 샘플을 다시 읽는다. 샘플을 디코딩하므로 1초쯤 걸릴 수 있다. */
    void reloadKit();

    /** 패드 설정 하나를 바꾼다. 소리에는 바로 반영되고, 파일에는 saveKit()을 불러야 저장된다.
        field: volume, pan, pitch(반음), reverbSend, startMs, velocityCurve */
    void setPadValue (int pad, const juce::Identifier& field, double value);
    double getPadValue (int pad, const juce::Identifier& field) const;
    juce::String getPadId (int pad) const;

    void setPadMuted (int pad, bool shouldBeMuted);
    void setPadSolo (int pad, bool shouldBeSolo);
    bool isPadMuted (int pad) const;
    bool isPadSolo (int pad) const;

    /** 드럼 전체에 거는 이펙트. 킷 파일에 함께 저장된다. */
    hvd::FxSettings getFx() const                            { return engine.getFxSettings(); }
    void setFx (const hvd::FxSettings& settings);

    /** 고친 킷 문서를 저장한다. 샘플 구성이 바뀌었으면 reload를 true로 준다. */
    bool saveKit (bool reload);

    /** 패드를 한 번 친다. source는 연주 로그에 남길 출처다 (연주 화면에서 마우스로 친 것은 mouse). */
    void previewPad (int pad, float velocity, hvd::HitSource source = hvd::HitSource::screen);
    void allSoundOff();

    //==============================================================================
    // 백업: 킷 파일과 키 배열 파일이 바뀔 때마다(프로그램이 저장했든 밖에서 고쳤든) 바뀌기 전의 내용을 남긴다.
    const FileBackups& getBackups() const noexcept           { return backups; }

    /** 백업 하나로 지금 킷(또는 키 배열) 파일을 되돌린다. 되돌리기 전의 상태도 백업으로 남는다. 실패하면 이유를 돌려준다. */
    juce::String restoreKitBackup (const juce::File& backup);
    juce::String restoreKeymapBackup (const juce::File& backup);

    //==============================================================================
    // 키 배열
    juce::File getKeymapsFolder() const                      { return dataRoot.getChildFile ("keymaps"); }
    juce::File getKeymapFile() const                         { return keymapFile; }
    juce::StringArray getKeymapNames() const;
    juce::String getKeymapName() const                       { return keymapFile.getFileNameWithoutExtension(); }

    void selectKeymap (const juce::String& name);
    juce::String duplicateKeymap (const juce::String& newName);

    /** 고친 키 배열 문서를 저장하고 바로 적용한다. */
    bool saveKeymap();
    void reloadKeymap();

    //==============================================================================
    // 키 입력
    /** 키 하나가 눌리거나 떼어졌을 때. 입력 스레드가 부른다. */
    void handleKey (hvd::KeyCode key, bool down);

    bool isKeyHeld (hvd::KeyCode key) const noexcept;
    int getHeldKeyCount() const noexcept                     { return heldCount.load (std::memory_order_relaxed); }
    int getMaxHeldKeyCount() const noexcept                  { return maxHeldCount.load (std::memory_order_relaxed); }
    void resetMaxHeldKeyCount() noexcept                     { maxHeldCount.store (0, std::memory_order_relaxed); }
    hvd::KeyCode getLastKey() const noexcept                 { return lastKey.load (std::memory_order_relaxed); }
    std::uint32_t getKeyPressCount() const noexcept          { return keyPressCount.load (std::memory_order_relaxed); }

    /** 글자를 입력하는 동안에는 키를 눌러도 연주하지 않게 한다. */
    void setPlayingSuspended (bool shouldSuspend) noexcept   { playingSuspended.store (shouldSuspend, std::memory_order_relaxed); }

    /** 다음에 누르는 키 하나를 연주하지 않고 callback으로 넘긴다 (키 지정용). */
    void armKeyCapture (std::function<void (hvd::KeyCode)> callback);
    void cancelKeyCapture();
    bool isKeyCaptureArmed() const noexcept                  { return captureArmed.load (std::memory_order_relaxed); }

    //==============================================================================
    // 페달 (풋스위치). 키보드로 인식되는 장치 하나를 골라, 그 스위치들을 키보드의 키와 따로 다룬다.
    // 스위치는 키 배열에서 Pedal1, Pedal2… 라는 키로 지정한다.
    struct PedalSetup
    {
        juce::String device;                // 장치의 제조사·제품 번호. 비어 있으면 등록된 페달이 없다
        std::vector<hvd::KeyCode> keys;     // 스위치가 보내는 키. 차례로 Pedal1, Pedal2…
    };

    const PedalSetup& getPedal() const noexcept              { return pedal; }
    void setPedal (const PedalSetup& newPedal);
    bool isPedalConnected() const;

    /** 페달 등록을 시작한다. 끝낼 때까지 누르는 키는 연주하지 않는다.
        처음 누른 키의 장치가 페달이 되고, 그 장치의 스위치를 밟은 순서대로 Pedal1, Pedal2…로 삼는다. */
    void beginPedalLearning()                                { keyboard.beginPedalLearning(); }
    bool isPedalLearning() const noexcept                    { return keyboard.isPedalLearning(); }

    /** 등록하는 동안 지금까지 밟은 것. Esc를 눌렀으면 cancelled가 true가 된다. */
    PedalSetup getPedalLearning (bool& cancelled) const;

    /** 등록을 마친다. keep이 true이고 밟은 스위치가 있으면 그것을 페달로 삼고 true를 돌려준다. */
    bool endPedalLearning (bool keep);

    /** 장치가 누름과 뗌을 알려 오는 간격을 최근 입력에서 어림한다. ofPedal이 false이면 키보드의 것. */
    hvd::ReportInterval getReportInterval (bool ofPedal) const;

    //==============================================================================
    // 전체 음량
    void setMasterGain (float gain);
    float getMasterGain() const noexcept                     { return audio.getMasterGain(); }

    /** 마지막 poll() 사이에 내보낸 소리의 최대 크기(0 ~ 1). 레벨 미터와 상태 표시가 함께 쓴다. */
    float getOutputPeak() const noexcept                     { return outputPeak; }

    //==============================================================================
    // 반주
    /** 음악 파일을 읽어 반주로 쓴다. 통째로 메모리에 올리므로 몇 초 걸릴 수 있다. 실패하면 이유를 돌려준다. */
    juce::String loadTrack (const juce::File& file);
    void unloadTrack();
    juce::File getTrackFile() const                          { return trackFile; }
    void setTrackGain (float gain);

    //==============================================================================
    // 녹음 (WAV)
    juce::String startRecording (bool includeTrack);
    void stopRecording();
    juce::File getRecordingsFolder() const                   { return dataRoot.getChildFile ("recordings"); }

    //==============================================================================
    // 연주 기록과 다시 재생
    struct TakeHit
    {
        double seconds = 0.0;
        juce::String padId;
        float velocity = 1.0f;
    };

    void startTake();
    void stopTake();
    bool isTakeRecording() const noexcept                    { return takeRecording; }
    bool hasTake() const noexcept                            { return ! take.empty(); }
    int getTakeHitCount() const noexcept                     { return static_cast<int> (take.size()); }
    double getTakeLength() const noexcept                    { return takeLength; }

    void playTake();
    void stopTakePlayback();
    bool isTakePlaying() const noexcept                      { return replay.isActive(); }
    double getTakePlaybackSeconds() const noexcept           { return replay.getElapsedSeconds (engine.getClock()); }

    juce::String exportTakeAsMidi (const juce::File& file) const;
    juce::String importTakeFromMidi (const juce::File& file);

    //==============================================================================
    // 타이밍: 메트로놈이 켜져 있을 때 타격이 16분음표 격자에서 얼마나 벗어났는지
    struct Timing
    {
        static constexpr int historySize = 32;

        std::array<float, historySize> offsetsMs {};    // 음수는 빠름, 양수는 늦음
        int count = 0;                                  // 지금까지 잰 타격 수
        float lastMs = 0.0f;
        float averageAbsMs = 0.0f;
    };

    /** 엔진이 낸 타격마다 불린다 (화면 스레드, poll() 안에서). 리듬 게임이 판정에 쓴다. */
    std::function<void (const hvd::HitEvent&)> onHit;

    const Timing& getTiming() const noexcept                 { return timing; }
    void resetTiming()                                       { timing = {}; }

    //==============================================================================
    // 연주 로그: 켜져 있는 동안 친 것(타격, 키)과 그때의 상황(곡의 위치, 템포, 킷 등)을 파일에 적어 둔다.
    // 파일은 처음으로 무언가를 쳤을 때 만든다. 설정의 "playLog"를 false로 두면 남기지 않는다.
    bool isPlayLogActive() const noexcept                    { return playLog.isActive(); }
    juce::File getPlayLogFolder() const                      { return playLogFolder; }
    juce::File getPlayLogFile() const                        { return playLog.getFile(); }

    /** 지금 시각으로 로그 줄을 시작한다. 화면 쪽이 상황(게임을 시작했다 등)을 적을 때 쓴다. */
    PlayLog::Line beginLogLine (const char* event) const;

    /** 상황을 알리는 줄을 적는다. 아직 친 것이 없어 파일이 없으면 버린다. */
    void writeLogNote (const PlayLog::Line& line)            { playLog.write (line); }

    /** 연주에 해당하는 줄을 적는다. 파일이 없으면 만들고, 그때의 상황(장치, 킷, 곡, 템포)부터 적는다. */
    void writeLogPlay (const PlayLog::Line& line);

    /** 아직 디스크에 넘기지 않은 로그를 넘긴다. */
    void flushPlayLog()                                      { playLog.flush(); }

    //==============================================================================
    // 프로그램 설정 (화면 쪽이 기억해 둘 값)
    juce::var getSetting (const juce::Identifier& name, const juce::var& fallback) const;
    void setSetting (const juce::Identifier& name, const juce::var& value);

private:
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    void loadSettings();
    void saveSettings() const;
    juce::File getSettingsFile() const;

    void applyKeymap();
    void applyPedal();
    void pushPadParams (int pad);
    void pushAllPadParams();
    void applyFxFromDocument();
    void releaseRetired();
    void drainHitEvents();
    void rebuildMidiTable();
    void rememberFileTimes();

    /** 파일의 지금 내용을 기억한다. 전에 기억한 것과 달라졌으면 전의 내용을 백업으로 남긴다. */
    struct Remembered
    {
        juce::File file;
        juce::String text;
    };

    void noteContent (const juce::File& file, Remembered& remembered, bool evenIfRecent = false);
    juce::String restoreBackup (const juce::File& backup, const juce::File& target, Remembered& remembered);

    double logSecondsAt (std::uint64_t clock) const noexcept;
    void logSituation();
    void logWholeSituation();
    void logKeys();
    void logHit (const hvd::HitEvent& event, const AudioHost::TrackSync& sync);
    void logAudio (const AudioHost::Status& status);
    void logKit();
    void logKeymap();
    void logTempo();
    void logTrackFile();

    juce::File dataRoot;
    juce::File kitFolder;
    juce::File keymapFile;
    bool remember = true;

    hvd::Engine engine;
    hvd::TrackPlayer track;
    AudioHost audio { engine, track };
    hvd::Performer performer { engine };
    hvd::RawKeyboard keyboard;
    bool keyboardRegistered = false;
    juce::String audioError;

    // 엔진에 넘긴 킷들. 엔진이 내려놓았다고 알려 올 때까지 지우지 않는다.
    std::vector<std::unique_ptr<const hvd::Kit>> ownedKits;
    const hvd::Kit* shownKit = nullptr;
    KitStats kitStats;
    KitDocument kitDocument;
    std::vector<bool> muted, solo;

    std::shared_ptr<const hvd::Mapping> shownMapping;
    KeymapDocument keymapDocument;
    float loadedTempo = 0.0f;    // 키 배열 파일에서 마지막으로 읽은 템포

    juce::Time kitFileTime, keymapFileTime;
    juce::StringArray kitMessages, keymapMessages, messages;
    int pollCount = 0;

    // 키 입력 (입력 스레드와 주고받는 값은 atomic)
    std::array<std::atomic<bool>, hvd::numKeyCodes> held {};
    std::atomic<int> heldCount { 0 };
    std::atomic<int> maxHeldCount { 0 };
    std::atomic<std::uint32_t> keyPressCount { 0 };
    std::atomic<hvd::KeyCode> lastKey { hvd::invalidKey };
    std::atomic<bool> playingSuspended { false };
    std::atomic<bool> captureArmed { false };
    std::atomic<int> capturedKey { -1 };
    std::function<void (hvd::KeyCode)> captureCallback;
    std::array<bool, hvd::numKeyCodes> forwarded {};    // 누름을 연주 로직에 넘긴 키. handleKey()를 부르는 스레드에서만 쓴다
    PedalSetup pedal;

    // MIDI 입력: 노트 번호 → 패드 번호 (-1이면 없음)
    std::array<std::atomic<int>, 128> midiPads {};
    juce::SpinLock midiLock;

    // 반주
    std::vector<std::unique_ptr<const hvd::TrackData>> ownedTracks;
    juce::File trackFile;

    // 연주 기록
    std::vector<TakeHit> take;
    bool takeRecording = false;
    std::uint64_t takeStartClock = 0;
    double takeLength = 0.0;
    double takeTrackPosition = -1.0;    // 기록을 시작할 때 반주가 재생 중이었으면 그 위치
    hvd::ReplayPlayer replay;

    Timing timing;
    float outputPeak = 0.0f;
    juce::var settings;

    // 백업
    FileBackups backups;
    Remembered rememberedKit, rememberedKeymap;

    // 연주 로그
    struct LoggedKey
    {
        std::uint64_t clock = 0;    // 키가 들어왔을 때의 엔진 시계
        hvd::KeyCode key = hvd::invalidKey;
        bool down = false;
    };

    PlayLog playLog;
    juce::File playLogFolder;
    std::atomic<bool> logKeysWanted { false };          // 입력 스레드가 키를 큐에 넣을지
    hvd::SpscQueue<LoggedKey, 1024> loggedKeys;         // 입력 스레드 → 화면 스레드
    std::uint64_t logBaseClock = 0;                     // 로그의 시각 = logBaseSeconds + (엔진 시계 - logBaseClock) / logRate
    double logBaseSeconds = 0.0;
    double logRate = 0.0;

    // 마지막으로 로그에 적은 상황. 달라지면 새 줄을 적는다.
    AudioHost::Status loggedAudio;
    AudioHost::TrackSync loggedSync;
    juce::File loggedTrackFile;
    float loggedTempo = 0.0f;
    int loggedBeatsPerBar = 0;
    bool loggedMetronome = false;
    std::uint32_t loggedBeatCount = 0;
    int logPollCount = 0;

    // 리듬 게임의 곡 목록과 채보
    ChartLibrary charts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Session)
};
