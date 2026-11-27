#include "Chart.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "PadKinds.h"

namespace hvd
{

namespace
{

constexpr const char* laneNames[numLanes] = { "crash", "hihat", "snare", "tom_high", "tom_mid", "tom_floor", "ride", "kick" };

/** 박 안에서의 자리를 1/24박 단위로 돌려준다 (0 ~ 23). 격자에 없는 노트는 -1. */
int subdivisionOf (const ChartNote& note)
{
    if (note.beat < 0.0)
        return -1;

    const auto fraction = note.beat - std::floor (note.beat);
    return static_cast<int> (std::lround (fraction * 24.0)) % 24;
}

bool isOneOf (int value, std::initializer_list<int> list)
{
    return std::find (list.begin(), list.end(), value) != list.end();
}

} // namespace

const char* laneName (Lane lane) noexcept
{
    const auto index = static_cast<int> (lane);
    return index >= 0 && index < numLanes ? laneNames[index] : "";
}

bool laneFromName (std::string_view name, Lane& lane) noexcept
{
    for (int i = 0; i < numLanes; ++i)
    {
        if (name == laneNames[i])
        {
            lane = static_cast<Lane> (i);
            return true;
        }
    }

    return false;
}

bool laneForPadId (std::string_view padId, Lane& lane) noexcept
{
    switch (instrumentOfPad (padId))
    {
        case Instrument::kick:     lane = Lane::kick;   return true;
        case Instrument::snare:    lane = Lane::snare;  return true;
        case Instrument::hihat:    lane = Lane::hihat;  return true;
        case Instrument::ride:     lane = Lane::ride;   return true;
        case Instrument::crash:
        case Instrument::china:    lane = Lane::crash;  return true;
        case Instrument::other:    return false;

        case Instrument::tom:
        {
            const auto has = [padId] (std::string_view part) { return containsIgnoringCase (padId, part); };

            if (has ("floor") || has ("low") || has ("3") || has ("4"))
                lane = Lane::tomFloor;
            else if (has ("mid") || has ("2"))
                lane = Lane::tomMid;
            else
                lane = Lane::tomHigh;

            return true;
        }
    }

    return false;
}

//==============================================================================
std::vector<ChartNote> makeChartVariant (const Chart& chart, ChartLevel level)
{
    if (level == ChartLevel::full)
        return chart.notes;

    // 셋잇단 느낌의 곡인지: 박 안의 1/3, 2/3 자리에 놓인 노트가 16분음표 엇박 자리보다 많은지 본다.
    auto onTriplets = 0, onSixteenths = 0;

    for (const auto& note : chart.notes)
    {
        const auto sub = subdivisionOf (note);
        onTriplets += isOneOf (sub, { 8, 16 }) ? 1 : 0;
        onSixteenths += isOneOf (sub, { 6, 18 }) ? 1 : 0;
    }

    const auto triplets = onTriplets > onSixteenths * 2 && onTriplets > 8;
    const auto easy = level == ChartLevel::easy;
    const auto normal = level == ChartLevel::normal;

    const auto onQuarter = [] (int sub) { return sub == 0; };
    const auto onEighth = [triplets] (int sub) { return triplets ? isOneOf (sub, { 0, 16 }) : isOneOf (sub, { 0, 12 }); };
    const auto onSixteenth = [triplets] (int sub) { return triplets ? isOneOf (sub, { 0, 8, 16 }) : isOneOf (sub, { 0, 6, 12, 18 }); };

    std::vector<ChartNote> result;
    result.reserve (chart.notes.size());

    // 박 격자가 없는 채보는 레인마다 최소 간격으로만 덜어 낸다.
    const auto hasGrid = chart.tempo > 0.0f && ! chart.beats.empty();
    std::array<double, numLanes> lastKept;
    lastKept.fill (-1000.0);

    for (const auto& note : chart.notes)
    {
        const auto sub = subdivisionOf (note);
        const auto timekeeping = note.lane == Lane::hihat || note.lane == Lane::ride;
        auto keep = true;

        if (! hasGrid || sub < 0)
        {
            // 격자에 없는 노트: 레인마다 최소 간격으로만 남긴다. 격자가 있는 곡에서는 가장 어려운 단계에서만 남긴다.
            const auto gap = easy ? 0.45 : (normal ? (timekeeping ? 0.45 : 0.28) : (timekeeping ? 0.22 : 0.12));
            const auto spaced = note.seconds - lastKept[static_cast<std::size_t> (note.lane)] >= gap;

            if (hasGrid)
                keep = ! easy && ! normal && note.velocity >= 0.5f && spaced;
            else
                keep = spaced && ! (easy && timekeeping);
        }
        else if (note.lane == Lane::crash)
        {
            keep = easy ? onQuarter (sub) : (normal ? onEighth (sub) : true);
        }
        else if (timekeeping)
        {
            keep = easy ? false : (normal ? onQuarter (sub) : onEighth (sub));
        }
        else
        {
            // 킥, 스네어, 탐
            keep = easy ? onQuarter (sub) : (normal ? onEighth (sub) : onSixteenth (sub));

            if (note.lane == Lane::snare && note.velocity < (easy || normal ? 0.4f : 0.28f))
                keep = false;    // 고스트 노트
        }

        if (keep)
        {
            result.push_back (note);
            lastKept[static_cast<std::size_t> (note.lane)] = note.seconds;
        }
    }

    return result;
}

float rateChartDifficulty (const std::vector<ChartNote>& notes)
{
    if (notes.size() < 4)
        return 0.0f;

    constexpr double window = 4.0;
    constexpr double step = 1.0;
    constexpr double chordGap = 0.02;

    // 같은 순간에 치는 노트 묶음은 첫 노트를 1, 나머지를 0.5로 센다.
    std::vector<std::pair<double, double>> weights;    // (시각, 무게)
    weights.reserve (notes.size());

    auto offGrid = 0;
    std::array<bool, numLanes> used {};

    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto chordMember = i > 0 && notes[i].seconds - notes[i - 1].seconds < chordGap;
        weights.emplace_back (notes[i].seconds, chordMember ? 0.5 : 1.0);
        used[static_cast<std::size_t> (notes[i].lane)] = true;

        const auto sub = subdivisionOf (notes[i]);
        offGrid += (sub != 0 && sub != 12) ? 1 : 0;
    }

    const auto first = notes.front().seconds;
    const auto last = notes.back().seconds;
    std::vector<double> densities;

    std::size_t begin = 0, end = 0;
    auto sum = 0.0;

    for (auto start = first; start <= std::max (first, last - window * 0.5); start += step)
    {
        while (end < weights.size() && weights[end].first < start + window)
            sum += weights[end++].second;

        while (begin < end && weights[begin].first < start)
            sum -= weights[begin++].second;

        densities.push_back (sum / window);
    }

    if (densities.empty())
        return 0.0f;

    auto sorted = densities;
    std::sort (sorted.begin(), sorted.end());
    const auto peak = sorted[static_cast<std::size_t> (static_cast<double> (sorted.size() - 1) * 0.9)];

    auto average = 0.0;

    for (const auto density : densities)
        average += density;

    average /= static_cast<double> (densities.size());

    const auto syncopation = static_cast<double> (offGrid) / static_cast<double> (notes.size());
    const auto lanes = static_cast<double> (std::count (used.begin(), used.end(), true));

    auto load = 0.65 * peak + 0.35 * average;
    load *= 1.0 + 0.15 * syncopation;
    load *= 0.9 + 0.025 * lanes;

    const auto stars = 5.0 * std::min (1.0, std::pow (load / 19.0, 0.7));
    return static_cast<float> (std::round (stars * 10.0) / 10.0);
}

} // namespace hvd
