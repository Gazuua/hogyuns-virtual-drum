#include "RhythmGame.h"

#include <algorithm>
#include <cmath>

namespace hvd
{

namespace
{

constexpr double scoreWeights[numJudgements] = { 0.0, 1.0, 0.7, 0.4, 0.0 };

} // namespace

void RhythmGame::start (std::vector<ChartNote> newNotes, const JudgeWindows& newWindows, double playbackSpeed, bool relaxedLanes)
{
    notes = std::move (newNotes);
    std::stable_sort (notes.begin(), notes.end(), [] (const ChartNote& a, const ChartNote& b) { return a.seconds < b.seconds; });

    results.assign (notes.size(), Judgement::pending);
    windows = newWindows;
    speed = playbackSpeed > 0.0 ? playbackSpeed : 1.0;
    relaxed = relaxedLanes;

    firstPending = 0;
    counts.fill (0);
    combo = 0;
    maxCombo = 0;
    offsetSum = 0.0;
    timedHits = 0;
    earlyHits = 0;
    lateHits = 0;
}

bool RhythmGame::laneMatches (Lane played, Lane wanted) const noexcept
{
    if (played == wanted)
        return true;

    return relaxed && ((isTomLane (played) && isTomLane (wanted)) || (isCymbalLane (played) && isCymbalLane (wanted)));
}

void RhythmGame::record (std::size_t index, Judgement judgement) noexcept
{
    results[index] = judgement;
    ++counts[static_cast<std::size_t> (judgement)];

    if (judgement == Judgement::miss)
    {
        combo = 0;
    }
    else
    {
        ++combo;
        maxCombo = std::max (maxCombo, combo);
    }

    while (firstPending < notes.size() && results[firstPending] != Judgement::pending)
        ++firstPending;
}

RhythmGame::HitResult RhythmGame::hit (Lane lane, double songSeconds) noexcept
{
    const auto reach = windows.good * speed;

    // 판정 구간 안에 있는, 아직 치지 않은 노트 가운데 가장 가까운 것을 고른다.
    // 같은 레인의 노트를 먼저 찾고, 없을 때만(relaxed) 비슷한 레인의 노트를 찾는다.
    auto best = notes.size();
    auto bestDistance = reach;
    auto bestExact = false;

    for (auto i = firstPending; i < notes.size() && notes[i].seconds <= songSeconds + reach; ++i)
    {
        if (results[i] != Judgement::pending || ! laneMatches (lane, notes[i].lane))
            continue;

        const auto distance = std::abs (notes[i].seconds - songSeconds);
        const auto exact = notes[i].lane == lane;

        if (distance > reach)
            continue;

        if ((exact && ! bestExact) || (exact == bestExact && distance <= bestDistance))
        {
            best = i;
            bestDistance = distance;
            bestExact = exact;
        }
    }

    HitResult result;

    if (best == notes.size())
        return result;

    const auto offset = (songSeconds - notes[best].seconds) / speed;
    const auto distance = std::abs (offset);

    result.note = static_cast<int> (best);
    result.offsetSeconds = offset;
    result.judgement = distance <= windows.perfect ? Judgement::perfect
                     : distance <= windows.great   ? Judgement::great
                                                   : Judgement::good;

    offsetSum += offset;
    ++timedHits;

    if (offset < -windows.perfect)
        ++earlyHits;
    else if (offset > windows.perfect)
        ++lateHits;

    record (best, result.judgement);
    return result;
}

int RhythmGame::advance (double songSeconds) noexcept
{
    const auto reach = windows.good * speed;
    auto missed = 0;

    for (auto i = firstPending; i < notes.size() && notes[i].seconds + reach < songSeconds; ++i)
    {
        if (results[i] == Judgement::pending)
        {
            record (i, Judgement::miss);
            ++missed;
        }
    }

    return missed;
}

int RhythmGame::getJudgedCount() const noexcept
{
    return counts[1] + counts[2] + counts[3] + counts[4];
}

double RhythmGame::getAccuracy() const noexcept
{
    const auto judged = getJudgedCount();

    if (judged == 0)
        return 1.0;

    auto sum = 0.0;

    for (int i = 1; i < numJudgements; ++i)
        sum += scoreWeights[i] * counts[static_cast<std::size_t> (i)];

    return sum / judged;
}

int RhythmGame::getScore() const noexcept
{
    if (notes.empty())
        return 0;

    auto sum = 0.0;

    for (int i = 1; i < numJudgements; ++i)
        sum += scoreWeights[i] * counts[static_cast<std::size_t> (i)];

    const auto total = static_cast<double> (notes.size());
    return static_cast<int> (std::lround (1000000.0 * (0.9 * sum / total + 0.1 * maxCombo / total)));
}

char RhythmGame::getGrade() const noexcept
{
    if (notes.empty())
        return 'D';

    auto sum = 0.0;

    for (int i = 1; i < numJudgements; ++i)
        sum += scoreWeights[i] * counts[static_cast<std::size_t> (i)];

    const auto accuracy = sum / static_cast<double> (notes.size());

    return accuracy >= 0.95 ? 'S'
         : accuracy >= 0.88 ? 'A'
         : accuracy >= 0.78 ? 'B'
         : accuracy >= 0.65 ? 'C'
                            : 'D';
}

} // namespace hvd
