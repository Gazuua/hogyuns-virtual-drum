#include "PracticeStats.h"

#include <algorithm>
#include <cmath>

namespace hvd
{

namespace
{

/** 정렬된 값들에서 fraction(0 ~ 1)째 값. 사이에 놓이면 이웃한 두 값을 잇는다. */
float quantile (const std::vector<float>& sorted, double fraction) noexcept
{
    if (sorted.empty())
        return 0.0f;

    const auto position = fraction * static_cast<double> (sorted.size() - 1);
    const auto below = static_cast<std::size_t> (position);
    const auto above = std::min (below + 1, sorted.size() - 1);
    const auto weight = static_cast<float> (position - static_cast<double> (below));
    return sorted[below] + (sorted[above] - sorted[below]) * weight;
}

/** seconds가 박 격자의 어디인지 (박 번호 + 박 안에서의 위치)와, 그 자리의 박 하나 길이(초)를 돌려준다.
    격자 밖이면 가장 가까운 박의 길이로 이어서 센다. */
double beatPositionOf (const std::vector<double>& beats, double seconds, double& beatSeconds) noexcept
{
    const auto next = std::upper_bound (beats.begin(), beats.end(), seconds);
    auto index = static_cast<std::ptrdiff_t> (next - beats.begin()) - 1;
    index = std::clamp<std::ptrdiff_t> (index, 0, static_cast<std::ptrdiff_t> (beats.size()) - 2);

    const auto start = beats[static_cast<std::size_t> (index)];
    beatSeconds = std::max (1.0e-3, beats[static_cast<std::size_t> (index) + 1] - start);
    return static_cast<double> (index) + (seconds - start) / beatSeconds;
}

} // namespace

PracticeGroup practiceGroupOf (Lane lane) noexcept
{
    switch (lane)
    {
        case Lane::kick:     return PracticeGroup::kick;
        case Lane::snare:    return PracticeGroup::snare;
        case Lane::hihat:    return PracticeGroup::hihat;
        case Lane::ride:     return PracticeGroup::ride;
        case Lane::crash:    return PracticeGroup::crash;
        case Lane::tomHigh:
        case Lane::tomMid:
        case Lane::tomFloor: return PracticeGroup::toms;
    }

    return PracticeGroup::toms;
}

TimingStats timingStatsOf (std::vector<float> offsetsMs, float tightMs)
{
    TimingStats stats;
    stats.count = static_cast<int> (offsetsMs.size());

    if (offsetsMs.empty())
        return stats;

    std::sort (offsetsMs.begin(), offsetsMs.end());
    stats.medianMs = quantile (offsetsMs, 0.5);
    stats.lowMs = quantile (offsetsMs, 0.1);
    stats.highMs = quantile (offsetsMs, 0.9);

    const auto within = std::count_if (offsetsMs.begin(), offsetsMs.end(), [tightMs] (float value) { return std::abs (value) <= tightMs; });
    stats.withinPercent = 100.0f * static_cast<float> (within) / static_cast<float> (offsetsMs.size());
    return stats;
}

int practiceGridOf (const Chart& chart) noexcept
{
    // 단계 덜어 내기(makeChartVariant)와 같은 기준이다.
    return hasTripletFeel (chart.notes) ? 3 : 4;
}

PracticeReport analysePractice (const std::vector<PracticeHit>& hits, const Chart& chart, int barsPerSection)
{
    PracticeReport report;

    if (chart.beats.size() < 8)
        return report;

    report.gridPerBeat = practiceGridOf (chart);
    report.offsetsMs.reserve (hits.size());

    const auto grid = static_cast<double> (report.gridPerBeat);
    const auto beatsPerSection = static_cast<double> (std::max (1, barsPerSection) * 4);

    struct Placed
    {
        long section = 0;
        float offsetMs = 0.0f;
    };

    std::vector<Placed> placed;
    placed.reserve (hits.size());
    std::array<std::vector<float>, numPracticeGroups> byGroup;

    for (const auto& hit : hits)
    {
        auto beatSeconds = 0.0;
        const auto position = beatPositionOf (chart.beats, hit.seconds, beatSeconds);
        const auto cells = position * grid;
        const auto speed = static_cast<double> (std::clamp (hit.speed, 0.25f, 4.0f));

        // 격자 칸에서 벗어난 만큼을 곡의 시간으로 바꾸고, 재생 속도로 나눠 실제로 흐른 시간으로 만든다.
        const auto offsetMs = static_cast<float> ((cells - std::round (cells)) / grid * beatSeconds * 1000.0 / speed);

        report.offsetsMs.push_back (offsetMs);
        byGroup[static_cast<std::size_t> (practiceGroupOf (hit.lane))].push_back (offsetMs);
        placed.push_back ({ static_cast<long> (std::floor ((position - static_cast<double> (chart.firstDownbeat)) / beatsPerSection)), offsetMs });
    }

    report.all = timingStatsOf (report.offsetsMs);

    for (std::size_t group = 0; group < byGroup.size(); ++group)
        report.groups[group] = timingStatsOf (byGroup[group]);

    // 곡의 흐름: 구간(마디 몇 개)마다 따로 센다.
    std::vector<long> sectionNumbers;

    for (const auto& item : placed)
        sectionNumbers.push_back (item.section);

    std::sort (sectionNumbers.begin(), sectionNumbers.end());
    sectionNumbers.erase (std::unique (sectionNumbers.begin(), sectionNumbers.end()), sectionNumbers.end());

    const auto secondsAtBeat = [&chart] (double beat)
    {
        // 박 번호(소수 가능)를 곡의 시각으로. 격자 밖은 끝의 박 길이로 이어 간다.
        const auto last = static_cast<double> (chart.beats.size() - 1);
        const auto index = static_cast<std::size_t> (std::clamp (std::floor (beat), 0.0, last - 1.0));
        return chart.beats[index] + (beat - static_cast<double> (index)) * (chart.beats[index + 1] - chart.beats[index]);
    };

    for (const auto number : sectionNumbers)
    {
        std::vector<float> offsets;

        for (const auto& item : placed)
            if (item.section == number)
                offsets.push_back (item.offsetMs);

        PracticeSection section;
        const auto firstBeat = static_cast<double> (chart.firstDownbeat) + static_cast<double> (number) * beatsPerSection;
        section.startSeconds = secondsAtBeat (firstBeat);
        section.endSeconds = secondsAtBeat (firstBeat + beatsPerSection);
        section.stats = timingStatsOf (std::move (offsets));
        report.sections.push_back (section);
    }

    return report;
}

} // namespace hvd
