#pragma once

#include <span>

namespace hvd
{

/** 입력 장치가 컴퓨터에 상태를 알리는 간격(USB 보고 간격)을 어림한 결과. */
struct ReportInterval
{
    static constexpr int samplesNeeded = 20;

    int numSamples = 0;            // 쓸 수 있었던 간격의 수
    bool enough = false;           // 판단할 만큼 모였는지
    double intervalMs = 0.0;       // 찾은 간격. 0이면 1ms보다 짧거나 규칙이 보이지 않는다
    double shortestGapMs = 0.0;    // 연달아 온 두 보고 사이의 가장 짧은 시간
};

/** 키를 누르고 뗀 시각들(밀리초, 시간순)에서 장치의 보고 간격을 어림한다.

    USB 장치는 컴퓨터가 일정한 간격으로 물어볼 때만 상태를 알린다. 그래서 누르고 뗀 시각들은 그 간격의
    격자 위에 놓이고, 시각 사이의 차이는 간격의 배수가 된다. 차이들이 어떤 간격의 배수에 잘 맞는지를 보고,
    맞는 것 가운데 가장 긴 간격을 고른다.

    잴 수 있는 것은 간격뿐이다. 스위치가 닫힌 뒤 장치가 그것을 보고하기로 마음먹기까지의 시간(접점 떨림을
    걸러 내는 시간)은 컴퓨터 쪽에서는 알 수 없다.
*/
ReportInterval estimateReportInterval (std::span<const double> timesMs) noexcept;

} // namespace hvd
