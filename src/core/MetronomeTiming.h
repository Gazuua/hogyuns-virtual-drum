#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hvd
{

/** 가장 가까운 격자 자리에서 time이 얼마나 벗어났는지(ms). 음수는 빠름, 양수는 늦음.
    격자는 lastBeat(박이 쳐진 엔진 시계)에서 gridSamples 간격으로 놓인다. */
inline double gridOffsetMs (std::uint64_t time, std::uint64_t lastBeat, double gridSamples, double sampleRate) noexcept
{
    auto offset = std::fmod (static_cast<double> (time) - static_cast<double> (lastBeat), gridSamples);

    if (offset < 0.0)
        offset += gridSamples;

    if (offset > gridSamples * 0.5)
        offset -= gridSamples;

    return offset / sampleRate * 1000.0;
}

/** 메트로놈에 맞춰 친 타격이 격자에서 벗어난 정도의 최근 기록. 화면 스레드에서만 쓴다.
    값은 고리 버퍼에 쌓인다. 필드는 예전 Session::Timing과 같은 이름이다. */
struct MetronomeTiming
{
    static constexpr int historySize = 32;

    std::array<float, historySize> offsetsMs {};    // 음수는 빠름, 양수는 늦음. count % historySize 자리에 쌓는다
    int count = 0;                                  // 지금까지 잰 타격 수
    float lastMs = 0.0f;
    float averageAbsMs = 0.0f;                      // 남아 있는 기록의 절댓값 평균

    void add (float offsetMs) noexcept
    {
        offsetsMs[static_cast<std::size_t> (count % historySize)] = offsetMs;
        lastMs = offsetMs;
        ++count;

        const auto used = count < historySize ? count : historySize;
        auto sum = 0.0f;

        for (int i = 0; i < used; ++i)
            sum += std::abs (offsetsMs[static_cast<std::size_t> (i)]);

        averageAbsMs = sum / static_cast<float> (used);
    }

    void reset() noexcept                   { *this = {}; }

    /** 남아 있는 기록 수 (historySize 이하). */
    int stored() const noexcept             { return count < historySize ? count : historySize; }

    /** age번째로 최근 값. 0이 가장 최근이고 stored() 아래여야 한다. */
    float recent (int age) const noexcept
    {
        const auto index = ((count - 1 - age) % historySize + historySize) % historySize;
        return offsetsMs[static_cast<std::size_t> (index)];
    }
};

} // namespace hvd
