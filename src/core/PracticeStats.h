#pragma once

#include <array>
#include <vector>

#include "Chart.h"

namespace hvd
{

/** 곡에 맞춰 친 타격 하나 (연주 로그에서 읽은 것). */
struct PracticeHit
{
    double seconds = 0.0;     // 곡 안의 위치
    Lane lane = Lane::snare;
    float speed = 1.0f;       // 그때의 재생 속도. 느리게 틀고 쳤으면 1보다 작다
};

/** 박자를 볼 때 묶는 악기. 탐 셋은 하나로 본다. */
enum class PracticeGroup : std::uint8_t { kick = 0, snare, hihat, ride, crash, toms };

constexpr int numPracticeGroups = 6;

PracticeGroup practiceGroupOf (Lane lane) noexcept;

/** 타격 무리의 박자 통계.

    차이 = 친 시각 - 박 격자에서 가장 가까운 자리. 음수는 빠름, 양수는 늦음이다.
    곡을 느리게 틀고 쳤어도 실제로 흐른 시간(ms)으로 센다.
*/
struct TimingStats
{
    int count = 0;
    float medianMs = 0.0f;         // 가운뎃값: 빠르거나 늦는 경향
    float lowMs = 0.0f;            // 10%째 값
    float highMs = 0.0f;           // 90%째 값
    float withinPercent = 0.0f;    // ±tightMs 안에 든 비율 (0 ~ 100)

    /** 흩어진 정도: 가운데 80%가 퍼져 있는 폭의 절반. */
    float spreadMs() const noexcept    { return (highMs - lowMs) * 0.5f; }
};

/** 곡의 한 구간(마디 몇 개)의 통계. */
struct PracticeSection
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    TimingStats stats;
};

struct PracticeReport
{
    static constexpr float tightMs = 20.0f;    // 이 안이면 박에 맞았다고 본다 (2026-11-28 30에서 좁힘)

    int gridPerBeat = 4;                       // 박 하나를 몇 칸으로 봤는지. 셋잇단 느낌의 곡은 3, 아니면 4(16분음표)
    std::vector<float> offsetsMs;              // 타격마다의 차이 (넘겨받은 타격과 같은 순서)
    TimingStats all;
    std::array<TimingStats, numPracticeGroups> groups;
    std::vector<PracticeSection> sections;     // 곡의 흐름. 친 것이 있는 구간만, 시간 순
};

/** 차이(ms)들의 통계를 낸다. */
TimingStats timingStatsOf (std::vector<float> offsetsMs, float tightMs = PracticeReport::tightMs);

/** 채보의 노트가 박 안의 어디에 놓였는지 보고 박 하나를 몇 칸으로 볼지 정한다 (셋잇단이 많으면 3, 아니면 4). */
int practiceGridOf (const Chart& chart) noexcept;

/** 친 것을 채보의 박 격자와 견준다. 채보에 박이 없으면(8개 미만) 빈 보고서를 돌려준다.
    barsPerSection은 "곡의 흐름"에서 한 구간으로 묶을 마디 수다. */
PracticeReport analysePractice (const std::vector<PracticeHit>& hits, const Chart& chart, int barsPerSection = 4);

} // namespace hvd
