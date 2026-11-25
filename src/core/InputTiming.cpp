#include "InputTiming.h"

#include <algorithm>
#include <cmath>

namespace hvd
{

namespace
{

// 이보다 가까운 두 시각은 같은 보고에 함께 실려 온 것이다 (키 둘을 동시에 누른 경우).
constexpr double sameReportMs = 0.25;

// 이보다 먼 두 시각은 쓰지 않는다. 장치를 물어보는 시계와 컴퓨터의 시계가 조금씩 어긋나서,
// 사이가 멀수록 차이가 격자에서 벗어난다.
constexpr double longestGapMs = 400.0;

// 긴 것부터 본다. 짧은 간격의 격자는 긴 간격의 격자를 포함하기 때문이다.
// Windows는 USB 장치를 2의 거듭제곱 간격으로 물어보지만, 그렇지 않은 장치를 엉뚱하게 읽지 않도록 5와 10도 본다.
constexpr double candidatesMs[] = { 32.0, 16.0, 10.0, 8.0, 5.0, 4.0, 2.0, 1.0 };

// 차이가 배수에서 간격의 이 비율 안에 들면 "맞는다"고 본다. 규칙이 없는 입력이면 40%쯤만 맞는다.
constexpr double tolerance = 0.2;
constexpr double fractionNeeded = 0.8;

bool isUsable (double gapMs) noexcept
{
    return gapMs >= sameReportMs && gapMs <= longestGapMs;
}

} // namespace

ReportInterval estimateReportInterval (std::span<const double> timesMs) noexcept
{
    ReportInterval result;

    for (std::size_t i = 1; i < timesMs.size(); ++i)
    {
        const auto gap = timesMs[i] - timesMs[i - 1];

        if (! isUsable (gap))
            continue;

        result.shortestGapMs = result.numSamples == 0 ? gap : std::min (result.shortestGapMs, gap);
        ++result.numSamples;
    }

    result.enough = result.numSamples >= ReportInterval::samplesNeeded;

    if (! result.enough)
        return result;

    for (const auto candidate : candidatesMs)
    {
        auto fits = 0;

        for (std::size_t i = 1; i < timesMs.size(); ++i)
        {
            const auto gap = timesMs[i] - timesMs[i - 1];

            if (isUsable (gap) && std::abs (gap - std::round (gap / candidate) * candidate) < tolerance * candidate)
                ++fits;
        }

        if (static_cast<double> (fits) >= fractionNeeded * static_cast<double> (result.numSamples))
        {
            result.intervalMs = candidate;
            break;
        }
    }

    return result;
}

} // namespace hvd
