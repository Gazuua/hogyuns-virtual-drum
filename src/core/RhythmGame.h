#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Chart.h"

namespace hvd
{

/** 판정 구간(초, 실제 시간 기준). 노트의 시각에서 이만큼까지 벗어나도 그 판정을 준다. */
struct JudgeWindows
{
    double perfect = 0.050;
    double great = 0.100;
    double good = 0.160;
};

enum class Judgement : std::uint8_t { pending = 0, perfect, great, good, miss };

constexpr int numJudgements = 5;

/** 리듬 게임 한 판의 판정과 점수. 소리나 화면과는 상관없이 시각만 받아 계산한다.

    곡의 시각(songSeconds)은 곡을 느리게 틀어도 곡 안에서의 위치다. 판정 구간은 실제 시간 기준이므로
    안에서 재생 속도를 곱해 쓴다.
*/
class RhythmGame
{
public:
    /** relaxedLanes가 true이면 탐끼리, 심벌끼리(크래시, 하이햇, 라이드)는 서로 바꿔 쳐도 맞은 것으로 본다. */
    void start (std::vector<ChartNote> newNotes, const JudgeWindows& newWindows, double playbackSpeed, bool relaxedLanes);

    struct HitResult
    {
        Judgement judgement = Judgement::pending;    // pending이면 맞는 노트가 없었다 (감점 없음)
        int note = -1;
        double offsetSeconds = 0.0;                  // 실제 시간 기준. 음수는 빨리 침
    };

    /** lane을 songSeconds에 쳤다. */
    HitResult hit (Lane lane, double songSeconds) noexcept;

    /** 판정 구간이 지나간 노트를 놓친 것으로 처리한다. 새로 놓친 노트 수를 돌려준다. */
    int advance (double songSeconds) noexcept;

    const std::vector<ChartNote>& getNotes() const noexcept           { return notes; }
    Judgement getResult (std::size_t index) const noexcept            { return results[index]; }

    /** 아직 판정하지 않은 가장 앞 노트의 번호. 화면이 그릴 범위를 정할 때 쓴다. */
    std::size_t getFirstPending() const noexcept                      { return firstPending; }

    int getCount (Judgement judgement) const noexcept                 { return counts[static_cast<std::size_t> (judgement)]; }
    int getJudgedCount() const noexcept;
    int getCombo() const noexcept                                     { return combo; }
    int getMaxCombo() const noexcept                                  { return maxCombo; }

    /** 지금까지 판정한 노트 기준의 정확도 (0 ~ 1). */
    double getAccuracy() const noexcept;

    /** 전체 노트 기준의 점수 (0 ~ 1000000). */
    int getScore() const noexcept;

    /** 끝난 뒤의 등급: S, A, B, C, D */
    char getGrade() const noexcept;

    /** 맞힌 타격들의 평균 오차(초). 음수는 전반적으로 빨리 쳤다는 뜻이다. */
    double getMeanOffset() const noexcept                             { return timedHits > 0 ? offsetSum / timedHits : 0.0; }
    int getEarlyCount() const noexcept                                { return earlyHits; }
    int getLateCount() const noexcept                                 { return lateHits; }

    bool isFinished() const noexcept                                  { return firstPending >= notes.size(); }

private:
    bool laneMatches (Lane played, Lane wanted) const noexcept;
    void record (std::size_t index, Judgement judgement) noexcept;

    std::vector<ChartNote> notes;
    std::vector<Judgement> results;
    JudgeWindows windows;
    double speed = 1.0;
    bool relaxed = false;

    std::size_t firstPending = 0;
    std::array<int, numJudgements> counts {};
    int combo = 0;
    int maxCombo = 0;
    double offsetSum = 0.0;
    int timedHits = 0;
    int earlyHits = 0;
    int lateHits = 0;
};

} // namespace hvd
