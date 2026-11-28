#pragma once

#include <algorithm>
#include <cmath>

namespace hvd
{

/** 반주의 구간을 반복하며 속도를 바꾸는 규칙. 한 바퀴를 마칠 때마다 finishLap()을 부르고, 바뀐 speed를 재생기에 준다.

    startSpeed에서 시작해 targetSpeed까지 step씩 올린다. 박자를 잴 수 있으면(곡의 채보가 있으면) 한 바퀴의
    "박에 맞음" 비율이 passPercent 이상일 때, 잴 수 없으면 lapsWithoutTiming 바퀴마다 올린다.
    거의 치지 않은 바퀴는 셈하지 않는다 (쉬어 간 바퀴). 켜 둔 동안 손으로 속도를 바꾸면 거기서 이어 간다.
*/
struct SpeedTrainer
{
    static constexpr float lowestSpeed = 0.5f, highestSpeed = 1.5f;    // 반주 재생기가 내는 속도의 한도

    // 설정 (clampSettings가 한도 안으로 맞춘다)
    float startSpeed = 0.7f;
    float targetSpeed = 1.0f;
    float step = 0.05f;
    float passPercent = 80.0f;
    int minHits = 8;
    int lapsWithoutTiming = 2;

    // 진행
    float speed = 0.7f;
    int lapsAtSpeed = 0;          // 지금 속도에서 셈한 바퀴
    float lastPercent = -1.0f;    // 지난번에 셈한 바퀴의 박에 맞음 비율. 잴 수 없었으면 음수

    /** 속도는 0.5 ~ 1.5, 폭은 0.01 ~ 0.25, 기준은 50 ~ 100. 끝은 시작보다 작지 않게. */
    void clampSettings() noexcept
    {
        startSpeed = std::clamp (startSpeed, lowestSpeed, highestSpeed);
        targetSpeed = std::clamp (targetSpeed, startSpeed, highestSpeed);
        step = std::clamp (step, 0.01f, 0.25f);
        passPercent = std::clamp (passPercent, 50.0f, 100.0f);
    }

    /** 시작한다: startSpeed부터. */
    void start() noexcept
    {
        clampSettings();
        speed = startSpeed;
        lapsAtSpeed = 0;
        lastPercent = -1.0f;
    }

    /** 켜 둔 동안 손으로 속도를 바꿨다. 그 속도에서 이어 간다. */
    void noteSpeedChanged (float newSpeed) noexcept
    {
        speed = std::clamp (newSpeed, lowestSpeed, highestSpeed);
        lapsAtSpeed = 0;
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
