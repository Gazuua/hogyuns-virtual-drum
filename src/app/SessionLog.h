#pragma once

#include <atomic>

#include "AudioHost.h"
#include "PlayLog.h"
#include "core/Engine.h"
#include "core/KeyCode.h"
#include "core/SpscQueue.h"
#include "core/TrackPlayer.h"

class Session;

/** 연주 로그에 무엇을 언제 적는지 정한다: 친 것(타격, 키)과 그때의 상황(장치, 킷, 키 배열, 템포, 곡).
    파일은 PlayLog가 맡는다. 상황은 Session에서 읽는다.

    noteKey()만 입력 스레드에서 불리고, 나머지는 화면 스레드에서 Session이 부른다.
    poll마다 logSituation() → logKeys() → (타격마다) logHit() → flushIfDue() 순서로 부른다.
    그래야 누른 키가 그 키가 낸 타격보다 앞에 온다.
*/
class SessionLog
{
public:
    SessionLog (const Session& sessionToRead, const hvd::Engine& engineToRead,
                const hvd::TrackPlayer& trackToRead, const AudioHost& audioToRead);

    /** folder 안에 적을 준비를 한다. 파일은 처음 칠 때 만든다. */
    void start (const juce::File& folder);

    /** "end" 줄을 적고 파일을 닫는다. */
    void stop();

    bool isActive() const noexcept                           { return playLog.isActive(); }
    juce::File getFile() const                               { return playLog.getFile(); }

    /** 지금 시각으로 줄을 시작한다. */
    PlayLog::Line beginLine (const char* event) const;

    /** 상황을 알리는 줄. 아직 친 것이 없어 파일이 없으면 버린다. */
    void writeNote (const PlayLog::Line& line)               { playLog.write (line); }

    /** 연주에 해당하는 줄. 파일이 없으면 만들고, 그때의 상황(장치, 킷, 곡, 템포)부터 적는다. */
    void writePlay (const PlayLog::Line& line);

    void flush()                                             { playLog.flush(); }

    /** 키 하나가 눌리거나 떼어졌다. 입력 스레드가 부른다. 화면 스레드의 logKeys()가 적는다. */
    void noteKey (hvd::KeyCode key, bool down) noexcept
    {
        if (keysWanted.load (std::memory_order_relaxed))
            loggedKeys.push (LoggedKey { engine.getClock(), key, down });
    }

    void logSituation();
    void logKeys();
    void logHit (const hvd::HitEvent& event, const hvd::Kit& kit, const AudioHost::TrackSync& sync);
    void logKit();
    void logKeymap();

    /** 1초에 한 번쯤 디스크에 넘긴다. */
    void flushIfDue();

private:
    struct LoggedKey
    {
        std::uint64_t clock = 0;    // 키가 들어왔을 때의 엔진 시계
        hvd::KeyCode key = hvd::invalidKey;
        bool down = false;
    };

    double logSecondsAt (std::uint64_t clock) const noexcept;
    void logWholeSituation();
    void logAudio (const AudioHost::Status& status);
    void logTempo();
    void logTrackFile();

    const Session& session;
    const hvd::Engine& engine;
    const hvd::TrackPlayer& track;
    const AudioHost& audio;

    PlayLog playLog;
    std::atomic<bool> keysWanted { false };              // 입력 스레드가 키를 큐에 넣을지
    hvd::SpscQueue<LoggedKey, 1024> loggedKeys;          // 입력 스레드 → 화면 스레드
    std::uint64_t logBaseClock = 0;                      // 로그의 시각 = logBaseSeconds + (엔진 시계 - logBaseClock) / logRate
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

    JUCE_DECLARE_NON_COPYABLE (SessionLog)
};
