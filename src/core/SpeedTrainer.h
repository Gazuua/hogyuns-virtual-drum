#pragma once

#include <algorithm>
#include <cmath>

namespace hvd
{

/** 반주의 구간을 반복하며 속도를 올리는 규칙. 한 바퀴를 마칠 때마다 finishLap()을 부르고, 바뀐 speed를 재생기에 준다.

    박자를 잴 수 있으면(곡의 채보가 있으면) 한 바퀴의 "박에 맞음" 비율이 passPercent 이상일 때 step만큼 올린다.
    잴 수 없으면 lapsWithoutTiming 바퀴마다 올린다. 거의 치지 않은 바퀴는 셈하지 않는다 (쉬어 간 바퀴).
*/
struct SpeedTrainer
{
    float step = 0.05f;
    float targetSpeed = 1.0f;
    float passPercent = 80.0f;
    float lowestSpeed = 0.5f;
    int minHits = 8;
    int lapsWithoutTiming = 2;

    float speed = 0.7f;
    int lapsAtSpeed = 0;          // 지금 속도에서 셈한 바퀴
    float lastPercent = -1.0f;    // 지난번에 셈한 바퀴의 박에 맞음 비율. 잴 수 없었으면 음수

    /** 시작한다. 지금 속도가 목표보다 느리면 거기서, 아니면 70%에서. 5% 단위로 맞춘다. */
    void start (float currentSpeed) noexcept
    {
        const auto from = currentSpeed < targetSpeed - 0.001f ? currentSpeed : 0.7f;
        speed = std::clamp (std::round (from / step) * step, lowestSpeed, targetSpeed);
        lapsAtSpeed = 0;
        lastPercent = -1.0f;
    }

    /** 한 바퀴를 마쳤다. hits는 그 바퀴에 친 타격 수, withinPercent는 박에 맞은 비율(0 ~ 100, 잴 수 없으면 음수).
        속도를 올렸으면 true. */
    bool finishLap (int hits, float withinPercent) noexcept
    {
        if (hits < minHits)
            return false;

        ++lapsAtSpeed;
        lastPercent = withinPercent;

        if (atTarget())
            return false;

        const auto passed = withinPercent >= 0.0f ? withinPercent >= passPercent : lapsAtSpeed >= lapsWithoutTiming;

        if (! passed)
            return false;

        speed = std::min (targetSpeed, speed + step);
        lapsAtSpeed = 0;
        return true;
    }

    bool atTarget() const noexcept    { return speed >= targetSpeed - 0.001f; }
};

} // namespace hvd
