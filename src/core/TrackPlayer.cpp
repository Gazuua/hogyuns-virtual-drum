#include "TrackPlayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace hvd
{

namespace
{

constexpr double twoPi = 6.283185307179586;
constexpr float toFloat = 1.0f / 32768.0f;

} // namespace

void TrackPlayer::prepare (double deviceSampleRate)
{
    deviceRate = deviceSampleRate > 0.0 ? deviceSampleRate : 48000.0;

    windowTable.resize (maxWindow);

    for (int i = 0; i < maxWindow; ++i)
        windowTable[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (twoPi * i / maxWindow));

    for (auto& channel : stretched)
        channel.assign (capacity, 0.0f);

    for (auto& channel : tail)
        channel.assign (maxHop, 0.0f);

    templateMono.assign (maxHop, 0.0f);
    candidateMono.assign (maxHop * 2 + 8, 0.0f);

    hasTail = false;
    skipSearch = true;
    rewindPending = false;
    currentGain = 0.0f;
    readPosition = 1.0;
    filled = 0;
}

bool TrackPlayer::setTrack (const TrackData* newTrack) noexcept
{
    return incoming.push (newTrack);
}

const TrackData* TrackPlayer::popRetiredTrack() noexcept
{
    const TrackData* old = nullptr;
    return retired.pop (old) ? old : nullptr;
}

const TrackData* TrackPlayer::detachTrack() noexcept
{
    playing.store (false, std::memory_order_relaxed);
    length.store (0.0, std::memory_order_relaxed);
    position.store (0.0, std::memory_order_relaxed);
    return std::exchange (track, nullptr);
}

void TrackPlayer::setSpeed (float newSpeed) noexcept
{
    speed.store (std::clamp (newSpeed, 0.5f, 1.5f), std::memory_order_relaxed);
}

void TrackPlayer::setLoop (double startSeconds, double endSeconds, bool enabled) noexcept
{
    loopStart.store (std::max (0.0, startSeconds), std::memory_order_relaxed);
    loopEnd.store (std::max (0.0, endSeconds), std::memory_order_relaxed);
    loopEnabled.store (enabled && endSeconds > startSeconds, std::memory_order_relaxed);
}

void TrackPlayer::applyTrack (const TrackData* newTrack) noexcept
{
    if (track != nullptr && track != newTrack)
        retired.push (track);

    track = newTrack;
    analysisPosition = 0.0;
    previousStart = 0;
    hasTail = false;
    skipSearch = true;
    rewindPending = false;
    currentGain = 0.0f;
    readPosition = 1.0;
    filled = 0;
    numMarks = 0;

    // 창 길이는 약 46ms. 샘플레이트에 맞춰 2의 거듭제곱으로 고른다.
    const auto rate = track != nullptr ? track->sampleRate : 44100.0;
    window = rate < 30000.0 ? 1024 : (rate < 70000.0 ? 2048 : 4096);
    hop = window / 2;

    // 앞의 한 프레임은 보간용 여유로 비워 둔다.
    stretched[0][0] = 0.0f;
    stretched[1][0] = 0.0f;
    filled = 1;

    length.store (track != nullptr ? track->seconds() : 0.0, std::memory_order_relaxed);
    position.store (0.0, std::memory_order_relaxed);
}

int TrackPlayer::findBestOffset (std::int64_t templateStart, std::int64_t nominal) noexcept
{
    // 직전 조각에 자연스럽게 이어지는 부분(template)과 가장 닮은 곳을 nominal 주변에서 찾는다.
    const auto range = hop / 2;
    const auto numFrames = static_cast<std::int64_t> (track->frames());
    const auto* data = track->samples.data();

    const auto monoAt = [data, numFrames] (std::int64_t frame) noexcept
    {
        if (frame < 0 || frame >= numFrames)
            return 0.0f;

        const auto index = static_cast<std::size_t> (frame) * 2;
        return static_cast<float> (static_cast<int> (data[index]) + static_cast<int> (data[index + 1]));
    };

    for (int i = 0; i < hop; ++i)
        templateMono[static_cast<std::size_t> (i)] = monoAt (templateStart + i);

    const auto candidateLength = hop + 2 * range;

    for (int i = 0; i < candidateLength; ++i)
        candidateMono[static_cast<std::size_t> (i)] = monoAt (nominal - range + i);

    const auto* reference = templateMono.data();
    const auto* candidates = candidateMono.data();

    // 닮은 정도: 정규화한 상관. offset은 -range ~ range.
    const auto score = [reference, candidates, range, this] (int offset, int step) noexcept
    {
        const auto* candidate = candidates + (offset + range);
        auto correlation = 0.0f;
        auto energy = 1.0e-3f;

        for (int i = 0; i < hop; i += step)
        {
            correlation += reference[i] * candidate[i];
            energy += candidate[i] * candidate[i];
        }

        return correlation / std::sqrt (energy);
    };

    // 먼저 성기게 훑고, 가장 좋은 곳 주변만 촘촘하게 다시 본다.
    auto best = 0;
    auto bestScore = score (0, 4);

    for (int offset = -range; offset <= range; offset += 4)
    {
        if (const auto value = score (offset, 4); value > bestScore)
        {
            bestScore = value;
            best = offset;
        }
    }

    const auto coarse = best;
    bestScore = score (coarse, 1);

    for (int offset = std::max (-range, coarse - 3); offset <= std::min (range, coarse + 3); ++offset)
    {
        if (const auto value = score (offset, 1); value > bestScore)
        {
            bestScore = value;
            best = offset;
        }
    }

    return best;
}

void TrackPlayer::generateFrame() noexcept
{
    const auto numFrames = static_cast<std::int64_t> (track->frames());
    const auto* data = track->samples.data();
    const auto currentSpeed = static_cast<double> (speed.load (std::memory_order_relaxed));
    const auto unchanged = std::abs (currentSpeed - 1.0) < 1.0e-4;

    const auto nominal = static_cast<std::int64_t> (std::llround (analysisPosition));
    auto chosen = nominal;

    if (hasTail && ! skipSearch)
    {
        if (unchanged)
            chosen = previousStart + hop;    // 원래 속도에서는 그대로 이어 붙여 원본과 똑같이 낸다
        else
            chosen = nominal + findBestOffset (previousStart + hop, nominal);
    }

    skipSearch = false;

    const auto tableStep = static_cast<std::size_t> (maxWindow / window);
    const auto base = static_cast<std::size_t> (filled);

    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        auto* output = stretched[channel].data() + base;
        auto* overlap = tail[channel].data();

        for (int i = 0; i < hop; ++i)
        {
            const auto first = chosen + i;
            const auto second = chosen + hop + i;

            const auto firstValue = (first >= 0 && first < numFrames)
                                        ? static_cast<float> (data[static_cast<std::size_t> (first) * 2 + channel]) * toFloat
                                        : 0.0f;
            const auto secondValue = (second >= 0 && second < numFrames)
                                        ? static_cast<float> (data[static_cast<std::size_t> (second) * 2 + channel]) * toFloat
                                        : 0.0f;

            const auto index = static_cast<std::size_t> (i);

            // 앞 조각의 뒤쪽 절반과 이번 조각의 앞쪽 절반을 겹쳐 더한다.
            output[i] = (hasTail ? overlap[i] : 0.0f) + firstValue * windowTable[index * tableStep];
            overlap[i] = secondValue * windowTable[(index + static_cast<std::size_t> (hop)) * tableStep];
        }
    }

    // 이 조각이 원본의 어디에서 왔는지 적어 둔다.
    if (numMarks == maxMarks)
    {
        std::copy (marks.begin() + 1, marks.end(), marks.begin());
        --numMarks;
    }

    marks[static_cast<std::size_t> (numMarks++)] = { filled, chosen };

    hasTail = true;
    filled += hop;
    previousStart = chosen;
    analysisPosition = (unchanged ? static_cast<double> (chosen) : analysisPosition) + static_cast<double> (hop) * currentSpeed;

    // 구간 반복: 끝 지점을 지나면 시작 지점으로 돌아간다. 겹쳐 더하는 과정에서 자연스럽게 이어진다.
    if (loopEnabled.load (std::memory_order_relaxed))
    {
        const auto start = loopStart.load (std::memory_order_relaxed) * track->sampleRate;
        const auto end = std::min (loopEnd.load (std::memory_order_relaxed) * track->sampleRate, static_cast<double> (numFrames));

        if (end > start + static_cast<double> (window) && analysisPosition >= end)
        {
            analysisPosition = std::floor (start);
            skipSearch = true;
            return;
        }
    }

    // 마지막 조각의 뒤쪽 절반까지 다 내보낸 뒤에 끝난 것으로 본다. (끝을 넘은 부분은 무음으로 읽힌다)
    if (analysisPosition >= static_cast<double> (numFrames + hop) && ! rewindPending)
    {
        // 끝까지 재생했다. 남은 소리가 사라지면 render()가 처음으로 되돌린다.
        playing.store (false, std::memory_order_relaxed);
        rewindPending = true;
    }
}

void TrackPlayer::render (float* left, float* right, int numFrames) noexcept
{
    const TrackData* newTrack = nullptr;

    while (incoming.pop (newTrack))
        applyTrack (newTrack);

    if (track == nullptr || track->frames() < 4)
    {
        // 곡이 없으면 진행 중이 아니다. 이 표시를 그대로 두면 곡을 닫은 뒤에도 "재생 중"으로 남는다.
        renderActive.store (false, std::memory_order_relaxed);
        return;
    }

    if (const auto target = seekRequest.exchange (noSeek, std::memory_order_relaxed); target > noSeek * 0.5)
    {
        const auto last = static_cast<double> (track->frames() - 1);
        analysisPosition = std::min (std::floor (target * track->sampleRate), last);
        skipSearch = true;
        rewindPending = false;

        if (flushOnSeek.exchange (false, std::memory_order_relaxed))
        {
            // 만들어 둔 조각을 버린다. 새 조각은 창의 앞쪽 절반으로 부드럽게 시작한다.
            stretched[0][0] = 0.0f;
            stretched[1][0] = 0.0f;
            filled = 1;
            readPosition = 1.0;
            hasTail = false;
            numMarks = 0;
            currentGain = 0.0f;
        }
    }

    const auto wantPlay = playing.load (std::memory_order_relaxed);
    const auto targetGain = wantPlay ? gain.load (std::memory_order_relaxed) : 0.0f;

    renderStart.store (audibleFrame() / track->sampleRate, std::memory_order_relaxed);
    renderActive.store (wantPlay || currentGain > 1.0e-5f, std::memory_order_relaxed);

    if (! wantPlay && currentGain <= 1.0e-5f)
    {
        currentGain = 0.0f;

        if (rewindPending)
        {
            // 끝까지 재생한 뒤에는 처음(반복 중이면 반복 시작 지점)으로 돌아가 기다린다.
            analysisPosition = loopEnabled.load (std::memory_order_relaxed)
                                   ? std::floor (loopStart.load (std::memory_order_relaxed) * track->sampleRate)
                                   : 0.0;
            hasTail = false;
            skipSearch = true;
            rewindPending = false;
        }

        position.store (std::clamp (analysisPosition / track->sampleRate, 0.0, track->seconds()), std::memory_order_relaxed);
        return;
    }

    const auto ratio = track->sampleRate / deviceRate;
    const auto slew = static_cast<float> (1.0 / (0.01 * deviceRate));    // 켜고 끌 때 10ms에 걸쳐 바뀐다

    for (int i = 0; i < numFrames; ++i)
    {
        auto index = static_cast<int> (readPosition);

        // 보간에 뒤쪽 두 프레임이 필요하다. 모자라면 조각을 더 만든다.
        while (index + 3 > filled)
        {
            if (filled + hop > capacity)
            {
                // 이미 읽은 앞부분을 버리고 당긴다.
                const auto drop = index - 1;
                const auto keep = static_cast<std::size_t> (filled - drop);

                for (auto& channel : stretched)
                    std::memmove (channel.data(), channel.data() + drop, keep * sizeof (float));

                filled -= drop;
                readPosition -= static_cast<double> (drop);
                index -= drop;

                // 조각 표시도 함께 당기고, 통째로 버려진 조각은 지운다.
                auto kept = 0;

                for (int m = 0; m < numMarks; ++m)
                {
                    auto mark = marks[static_cast<std::size_t> (m)];
                    mark.base -= drop;

                    if (mark.base + hop > 0)
                        marks[static_cast<std::size_t> (kept++)] = mark;
                }

                numMarks = kept;
            }

            generateFrame();
        }

        const auto t = static_cast<float> (readPosition - static_cast<double> (index));

        const auto interpolate = [t] (const float* y) noexcept
        {
            const auto c1 = 0.5f * (y[1] - y[-1]);
            const auto c2 = y[-1] - 2.5f * y[0] + 2.0f * y[1] - 0.5f * y[2];
            const auto c3 = 0.5f * (y[2] - y[-1]) + 1.5f * (y[0] - y[1]);
            return ((c3 * t + c2) * t + c1) * t + y[0];
        };

        currentGain += std::clamp (targetGain - currentGain, -slew, slew);

        if (left != nullptr)
            left[i] += interpolate (stretched[0].data() + index) * currentGain;

        if (right != nullptr)
            right[i] += interpolate (stretched[1].data() + index) * currentGain;

        readPosition += ratio;
    }

    position.store (std::clamp (analysisPosition / track->sampleRate, 0.0, track->seconds()), std::memory_order_relaxed);
}

double TrackPlayer::audibleFrame() const noexcept
{
    // 지금 읽는 자리가 들어 있는 조각을 찾는다. 아직 만든 조각이 없으면 다음에 읽을 원본 위치가 곧 나갈 자리다.
    for (int m = numMarks - 1; m >= 0; --m)
    {
        const auto& mark = marks[static_cast<std::size_t> (m)];

        if (readPosition >= static_cast<double> (mark.base))
            return static_cast<double> (mark.source) + (readPosition - static_cast<double> (mark.base));
    }

    return numMarks > 0 ? static_cast<double> (marks[0].source) : analysisPosition;
}

} // namespace hvd
