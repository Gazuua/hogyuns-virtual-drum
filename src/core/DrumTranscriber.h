#pragma once

#include <atomic>
#include <cstddef>
#include <string>

#include "Chart.h"

namespace hvd
{

struct TranscribeOptions
{
    /** 드럼만 분리해 둔 소리이면 true. 완성곡을 그대로 주면 false로 두고, 그때는 안에서 타악기 성분을 먼저 골라낸다. */
    bool isolatedDrums = false;

    /** true이면 찾은 타격마다 잰 값을 debugLog에 한 줄씩 적는다 (조정용). */
    bool logEvents = false;
};

/** 소리에서 드럼 타격을 찾아 채보로 받아 적는다.

    left와 right는 왼쪽과 오른쪽 채널이다. 모노이면 right에 nullptr을 준다. 스테레오이면 심벌의 좌우 위치로
    하이햇과 라이드를 가르는 데 쓴다. sampleRate는 32000 ~ 50000 Hz를 기대한다.
    몇 초가 걸릴 수 있으므로 화면 스레드가 아닌 곳에서 부른다.
    progress에는 진행 정도(0 ~ 1)를 적고, cancel이 true가 되면 그만두고 빈 채보를 돌려준다.
*/
Chart transcribeDrums (const float* left, const float* right, std::size_t numSamples, double sampleRate,
                       const TranscribeOptions& options,
                       std::atomic<float>* progress = nullptr,
                       const std::atomic<bool>* cancel = nullptr,
                       std::string* debugLog = nullptr);

} // namespace hvd
