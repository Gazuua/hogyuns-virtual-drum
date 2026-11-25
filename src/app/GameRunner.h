#pragma once

#include <array>
#include <vector>

#include "Session.h"
#include "core/RhythmGame.h"

/** 리듬 게임 한 판을 진행한다: 곡을 틀고, 엔진이 낸 타격을 곡 안의 시각으로 바꿔 판정한다.

    화면과는 떨어져 있어서 창 없이도 돌릴 수 있다. 화면 스레드에서만 쓴다.
*/
class GameRunner
{
public:
    explicit GameRunner (Session& sessionToUse);
    ~GameRunner();

    struct Setup
    {
        std::vector<hvd::ChartNote> notes;    // 칠 노트 (고른 단계의 것)
        std::vector<double> beats;            // 박의 시각 (화면의 박 줄)
        int firstDownbeat = 0;
        double duration = 0.0;                // 곡 길이(초)
        double speed = 1.0;                   // 재생 속도 배율 (0.5 ~ 1)
        bool relaxedLanes = false;
        bool autoplay = false;                // 오토 모드: 프로그램이 채보대로 대신 친다
        double offsetMs = 0.0;                // 사용자가 정한 판정 보정. 양수면 늦게 쳐도 맞게 본다
        hvd::JudgeWindows windows;

        // 아래 둘은 연주 로그에 적을 뿐, 진행에는 쓰지 않는다.
        juce::File song;                      // 어느 곡의 채보인지 (원곡 파일). 연습 패턴이면 빈 값
        int level = -1;                       // 채보의 단계 (0 ~ 3). 연습 패턴이면 -1
    };

    /** audio를 틀면서 한 판을 시작한다. audio가 빈 File이면 곡 없이 엔진 시계만으로 진행한다.
        실패하면 이유를 돌려준다. */
    juce::String start (const juce::File& audio, Setup newSetup);

    /** 그만둔다. 곡도 멈춘다. */
    void stop();

    void setPaused (bool shouldPause);

    /** 연주 도중에 오토 모드를 켜거나 끈다. 켜면 그때부터 프로그램이 채보대로 대신 친다. */
    void setAutoplay (bool shouldAutoplay);
    bool isAutoplay() const noexcept                      { return setup.autoplay; }

    /** 이 판에서 오토 모드를 한 번이라도 썼는지. 썼으면 기록으로 남기지 않는다. */
    bool wasAssisted() const noexcept                     { return assisted; }

    enum class State { idle, playing, paused, finished };

    State getState() const noexcept                       { return state; }
    const Setup& getSetup() const noexcept                { return setup; }
    const hvd::RhythmGame& getGame() const noexcept       { return game; }

    /** 화면 타이머가 1초에 60번쯤 부른다. */
    void tick();

    /** 지금 스피커에서 들리는 곡 안의 위치(초). 시작 전 준비 시간에는 음수다. */
    double getSongSeconds() const noexcept                { return songSeconds; }

    /** 장치의 출력 지연(초). 판정과 화면이 이만큼을 보정한다. */
    double getLatencySeconds() const noexcept             { return latency; }

    struct Feedback
    {
        hvd::Judgement judgement = hvd::Judgement::pending;    // pending이면 노트 없이 친 것
        hvd::Lane lane = hvd::Lane::snare;
        float offsetMs = 0.0f;
        float velocity = 0.0f;
    };

    /** 마지막으로 부른 뒤에 생긴 타격과 판정을 돌려준다. */
    std::vector<Feedback> takeFeedback();

    /** 킷이 바뀌었을 때 패드와 레인의 짝을 다시 만든다. */
    void refreshPads();

    /** 패드가 속한 레인. 없으면 -1. */
    int laneOfPad (int pad) const noexcept;

    /** 레인을 대표하는 패드 (시범 연주가 치는 패드). 없으면 -1. */
    int getPreferredPad (hvd::Lane lane) const noexcept    { return lanePads[static_cast<std::size_t> (lane)]; }

private:
    void handleHit (const hvd::HitEvent& event);
    void pumpAutoplay();
    double renderedSecondsAt (std::uint64_t clock) const noexcept;
    void finish();
    void logResult (const char* state);

    Session& session;
    hvd::RhythmGame game;
    Setup setup;
    State state = State::idle;

    bool assisted = false;
    bool usingTrack = false;
    bool trackStarted = false;          // 곡이 진행 중인 것을 한 번이라도 봤는지
    std::uint64_t startClock = 0;       // 곡 없이 할 때: 준비 시간이 시작된 엔진 시계
    double pausedSeconds = 0.0;         // 곡 없이 할 때: 멈춰 있던 시간의 합
    std::uint64_t pauseClock = 0;
    double endSeconds = 0.0;            // 이 위치를 지나면 끝낸다
    double songSeconds = 0.0;
    double latency = 0.0;
    float savedTrackGain = -1.0f;       // 끝에서 소리를 줄이기 전의 반주 음량

    std::vector<int> padLanes;                         // 패드 번호 → 레인 (-1이면 없음)
    std::array<int, hvd::numLanes> lanePads {};        // 레인 → 시범 연주에 쓸 패드 (-1이면 없음)
    int openHatPad = -1;
    std::size_t nextAutoNote = 0;
    std::vector<Feedback> feedback;
};
