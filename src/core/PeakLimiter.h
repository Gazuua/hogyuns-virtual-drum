#pragma once

#include <algorithm>
#include <cmath>

namespace hvd
{

/** 드럼 버스의 꼭대기를 천장 아래로 붙잡는 피크 리미터.

    전체 음량을 올리면 센 타격이 겹칠 때 합이 1.0을 넘는다. 넘는 부분을 그냥 잘라 내면(클립) 넘는 동안
    파형이 납작해지고 그 위에 실린 심벌 소리가 지워진다. 이 리미터는 천장을 넘는 샘플에서 게인을 바로
    내리고, 잠깐 붙잡아 둔 뒤 1로 되돌린다. 그래서 눌려서 모양이 바뀌는 것은 꼭대기로 올라가는 짧은
    구간뿐이고, 그 뒤의 소리는 모양은 그대로인 채 잠깐 작게 나온다.

    - 미리 보기(look-ahead)가 없다. 넘은 샘플을 그 샘플에서 바로 맞추므로 지연이 늘지 않는다.
    - 좌우 가운데 큰 쪽을 보고 양쪽을 똑같이 줄인다. 소리의 위치가 흔들리지 않는다.
    - 천장 아래의 신호는 건드리지 않는다.

    오디오 스레드에서 process()를 부른다. 메모리를 잡지 않고 잠금도 없다.

    값을 정한 근거 (2026-11-25, 기본 킷의 샘플을 엔진과 같은 게인으로 겹쳐 본 수치):
    전체 음량 1.6에서 킥 하나의 꼭대기가 1.38, 킥 + 스네어 + 크래시가 1.66까지 간다.
    - 천장 0.9: 뒤에 있는 softClip(AudioHost.cpp)이 누르기 시작하는 자리다. 드럼만으로는 softClip에 닿지 않는다.
    - 붙잡는 시간 3ms: 킥의 가장 큰 봉우리가 천장 위에 머무는 시간(약 3ms)이다. 그 봉우리가 내려가는
      동안 게인이 되돌아오면 같은 봉우리를 다시 눌러 납작해진다.
    - 되돌리는 시간 15ms(시간 상수. 약 50ms 뒤에는 거의 다 돌아온다): 길게 잡으면(붙잡기 10ms, 되돌리기
      60ms로 해 봄) 킥과 함께 친 하이햇과 심벌이 0.1초쯤 작게 나오고 전체 크기도 0.3 ~ 1 dB 더 잃는다.
      이보다 짧게 잡으면 잘라 내는 것(클립)에 가까워진다.
    리미터는 꼭대기를 낮추므로 킥이 중심인 연주는 전체 음량을 두 배로 해도 크기가 두 배(+6 dB)까지는
    늘지 않는다(잰 값으로 +3.3 ~ +5.7 dB). 소리가 좋은지는 귀로 확인하지 않았다. 고칠 때는 아래 세 값만
    바꾸면 된다(tests/CoreTests.cpp의 검사는 이 값들에서 기대값을 계산한다).
*/
class PeakLimiter
{
public:
    static constexpr float ceiling = 0.9f;             // 출력이 넘지 않는 크기
    static constexpr double holdSeconds = 0.003;       // 내린 게인을 붙잡아 두는 시간
    static constexpr double releaseSeconds = 0.015;    // 1로 되돌리는 시간 상수

    PeakLimiter() noexcept    { prepare (48000.0); }

    /** 샘플레이트에 맞춰 시간을 다시 계산하고 상태를 지운다. 오디오가 멈춰 있을 때 부른다. */
    void prepare (double sampleRate) noexcept
    {
        const auto rate = sampleRate > 0.0 ? sampleRate : 48000.0;

        holdFrames = static_cast<int> (holdSeconds * rate + 0.5);
        releaseStep = static_cast<float> (1.0 - std::exp (-1.0 / (releaseSeconds * rate)));
        reset();
    }

    void reset() noexcept
    {
        gain = 1.0f;
        holdLeft = 0;
    }

    /** left, right를 제자리에서 처리한다. */
    void process (float* left, float* right, int numFrames) noexcept
    {
        for (int i = 0; i < numFrames; ++i)
        {
            // 1. 붙잡아 두는 시간이 지났으면 게인을 1 쪽으로 되돌린다.
            if (holdLeft > 0)
            {
                --holdLeft;
            }
            else if (gain < 1.0f)
            {
                gain += (1.0f - gain) * releaseStep;

                // 1에 거의 다 왔으면 1로 맞춘다. 그래야 조용한 신호가 다시 손대지 않은 그대로 나간다.
                if (gain > almostUnity)
                    gain = 1.0f;
            }

            // 2. 그래도 천장을 넘으면 이 샘플에서 바로 필요한 만큼 내린다.
            const auto level = std::max (std::abs (left[i]), std::abs (right[i]));

            if (level * gain > ceiling)
            {
                gain = ceiling / level;
                holdLeft = holdFrames;
            }

            left[i] *= gain;
            right[i] *= gain;
        }
    }

    /** 지금 곱하고 있는 게인(0 초과 ~ 1). 1이면 아무것도 줄이지 않는 중이다. */
    float getGain() const noexcept    { return gain; }

private:
    static constexpr float almostUnity = 0.9999f;

    int holdFrames = 0;
    float releaseStep = 0.0f;
    float gain = 1.0f;
    int holdLeft = 0;
};

} // namespace hvd
