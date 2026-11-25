#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "Engine.h"

namespace hvd
{

/** 기록해 둔 타격 하나. */
struct ReplayEvent
{
    double seconds = 0.0;      // 기록을 시작한 뒤로 흐른 시간
    std::uint16_t pad = 0;
    float velocity = 1.0f;
};

/** 기록한 연주를 엔진으로 다시 친다.

    화면 스레드가 주기적으로 pump()를 불러, 곧 울릴 타격을 엔진 시계 기준의 정확한 시각으로 미리 넘겨 둔다.
    그래서 화면 스레드가 잠깐 늦어져도 타이밍은 샘플 단위로 맞는다.
*/
class ReplayPlayer
{
public:
    /** startClock은 재생을 시작할 엔진 시계 값이다. */
    void start (std::vector<ReplayEvent> newEvents, std::uint64_t startClockToUse, double sampleRateToUse)
    {
        events = std::move (newEvents);
        std::stable_sort (events.begin(), events.end(),
                          [] (const ReplayEvent& a, const ReplayEvent& b) { return a.seconds < b.seconds; });

        startClock = startClockToUse;
        sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 48000.0;
        next = 0;
        active = true;
    }

    void stop() noexcept                  { active = false; }
    bool isActive() const noexcept        { return active; }

    double getLengthSeconds() const noexcept
    {
        return events.empty() ? 0.0 : events.back().seconds;
    }

    /** 지금까지 재생한 시간(초). */
    double getElapsedSeconds (std::uint64_t clockNow) const noexcept
    {
        return clockNow > startClock ? static_cast<double> (clockNow - startClock) / sampleRate : 0.0;
    }

    /** clockNow부터 lookAheadSamples 안에 울릴 타격을 엔진에 넘긴다. 다 끝났으면 false를 돌려준다. */
    bool pump (Engine& engine, std::uint64_t clockNow, std::uint32_t lookAheadSamples)
    {
        if (! active)
            return false;

        while (next < events.size())
        {
            const auto& event = events[next];
            const auto time = startClock + static_cast<std::uint64_t> (std::llround (std::max (0.0, event.seconds) * sampleRate));

            if (time >= clockNow + lookAheadSamples)
                break;

            EngineCommand command;
            command.type = EngineCommand::Type::hitAt;
            command.pad = event.pad;
            command.velocity = event.velocity;
            command.time = time;
            command.origin.source = HitSource::replay;

            if (! engine.postFromControl (command))
                break;    // 큐가 꽉 찼으면 다음 번에 이어서 보낸다

            ++next;
        }

        // 마지막 타격이 울릴 시각이 지나면 끝난 것으로 본다.
        if (next >= events.size() && getElapsedSeconds (clockNow) > getLengthSeconds())
            active = false;

        return active;
    }

private:
    std::vector<ReplayEvent> events;
    std::uint64_t startClock = 0;
    double sampleRate = 48000.0;
    std::size_t next = 0;
    bool active = false;
};

} // namespace hvd
