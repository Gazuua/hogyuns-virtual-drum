#include "Performer.h"

#include <algorithm>
#include <chrono>

namespace hvd
{

namespace
{

constexpr std::uint64_t nanosPerMs = 1'000'000;
constexpr std::uint64_t followMinNs = 40 * nanosPerMs;     // 이보다 촘촘한 간격은 따라가지 않는다
constexpr std::uint64_t followMaxNs = 600 * nanosPerMs;    // 이보다 벌어진 간격은 연타로 보지 않는다
constexpr std::uint64_t tapMinNs = 200 * nanosPerMs;       // 300 BPM
constexpr std::uint64_t tapMaxNs = 2000 * nanosPerMs;      // 30 BPM

} // namespace

std::uint64_t Performer::now() noexcept
{
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (elapsed).count());
}

void Performer::setMapping (std::shared_ptr<const Mapping> newMapping)
{
    mapping.store (std::move (newMapping));
    generation.fetch_add (1);
}

bool Performer::sync()
{
    // 키 배열이 바뀌었으면 눌림 상태를 모두 버리고 새 배열로 시작한다.
    if (const auto current = generation.load(); current != activeGeneration)
    {
        active = mapping.load();
        activeGeneration = current;

        accentHeld = 0;
        ghostHeld = 0;
        pedalHeld.assign (active != nullptr ? active->pedals.size() : 0, 0);
        pedalPressTime.assign (pedalHeld.size(), 0);
        latched.fill (false);
        lastHitTime.fill (0);
        lastHitKey.fill (invalidKey);

        EngineCommand command;
        command.type = EngineCommand::Type::allRunnersStop;
        engine.postFromInput (command);
    }

    return active != nullptr;
}

float Performer::modified (float velocity) const noexcept
{
    if (accentHeld > 0 && ghostHeld == 0)
        velocity *= active->accentScale;
    else if (ghostHeld > 0 && accentHeld == 0)
        velocity *= active->ghostScale;

    return std::clamp (velocity, 0.0f, 1.0f);
}

namespace
{

/** 누르고 있는 페달의 바꿈을 먼저 보고, 없으면 떼고 있는 페달의 바꿈을 본다.
    풋스위치가 없으면 풋스위치 키(Pedal1~4)의 "떼고 있는 동안" 바꿈은 건너뛴다. */
template <typename IsHeld>
std::uint16_t swapForPedals (const Mapping& mapping, IsHeld&& isHeld, bool footswitch, std::uint16_t pad) noexcept
{
    for (std::size_t i = 0; i < mapping.pedals.size(); ++i)
        if (isHeld (i))
            for (const auto& [from, to] : mapping.pedals[i].swaps)
                if (from == pad)
                    return to;

    for (std::size_t i = 0; i < mapping.pedals.size(); ++i)
        if (! isHeld (i) && (footswitch || ! isPedalKey (mapping.pedals[i].key)))
            for (const auto& [from, to] : mapping.pedals[i].releasedSwaps)
                if (from == pad)
                    return to;

    return pad;
}

} // namespace

std::uint16_t Performer::swapped (std::uint16_t pad) const noexcept
{
    return swapForPedals (*active, [this] (std::size_t i) { return i < pedalHeld.size() && pedalHeld[i] > 0; },
                          footswitchConnected.load (std::memory_order_relaxed), pad);
}

std::uint16_t Performer::swappedForScreen (std::uint16_t pad) const
{
    const auto current = mapping.load();

    if (current == nullptr)
        return pad;

    // 키 배열이 막 바뀌었으면 입력 스레드는 다음 키에서 눌림 상태를 버리지만, 여기서는 실제로 눌린 키를 본다.
    return swapForPedals (*current, [this, &current] (std::size_t i)
    {
        const auto key = current->pedals[i].key;
        return key < numKeyCodes && keyHeld[key].load (std::memory_order_relaxed);
    }, footswitchConnected.load (std::memory_order_relaxed), pad);
}

void Performer::mouseHit (std::uint16_t pad, float velocity, bool followPedals)
{
    if (! sync())
        return;

    EngineCommand command;
    command.type = EngineCommand::Type::hit;
    command.pad = followPedals ? swapped (pad) : pad;
    command.velocity = std::clamp (velocity, 0.0f, 1.0f);
    command.origin = { HitSource::mouse, 0 };
    engine.postFromInput (command);
}

void Performer::choke (std::span<const std::uint16_t> pads)
{
    for (const auto pad : pads)
    {
        EngineCommand command;
        command.type = EngineCommand::Type::chokePad;
        command.pad = pad;
        engine.postFromInput (command);
    }
}

std::uint32_t Performer::toSamples (const Interval& interval) const noexcept
{
    const auto sampleRate = engine.getSampleRate();
    auto seconds = 0.0;

    if (interval.kind == Interval::Kind::milliseconds)
        seconds = static_cast<double> (interval.value) * 0.001;
    else if (interval.kind == Interval::Kind::beats)
        seconds = static_cast<double> (interval.value) * 60.0 / static_cast<double> (engine.getTempo());

    return static_cast<std::uint32_t> (std::max (0.0, seconds * sampleRate));
}

bool Performer::makeRunner (const Stroke& stroke, std::uint16_t pad, std::uint64_t timeNs, RunnerParams& params) const noexcept
{
    const auto sampleRate = engine.getSampleRate();
    const auto msToSamples = [sampleRate] (float milliseconds) { return static_cast<float> (milliseconds * 0.001 * sampleRate); };

    params = RunnerParams();

    // 간격
    if (stroke.type == Stroke::Type::pattern)
    {
        params.tempoSynced = true;
        params.intervalBeats = stroke.stepBeats;
    }
    else if (stroke.interval.kind == Interval::Kind::beats)
    {
        params.tempoSynced = true;
        params.intervalBeats = stroke.interval.value;
    }
    else if (stroke.interval.kind == Interval::Kind::milliseconds)
    {
        params.intervalSamples = static_cast<double> (stroke.interval.value) * 0.001 * sampleRate;
    }
    else
    {
        // follow: 같은 패드를 직전에 친 뒤로 흐른 시간을 간격으로 삼는다.
        const auto previous = pad < lastHitTime.size() ? lastHitTime[pad] : 0;
        const auto elapsed = previous != 0 && timeNs > previous ? timeNs - previous : 0;

        if (elapsed >= followMinNs && elapsed <= followMaxNs)
        {
            params.intervalSamples = static_cast<double> (elapsed) * 1.0e-9 * sampleRate
                                       / static_cast<double> (stroke.interval.followDivisor);
        }
        else if (stroke.interval.fallbackBeats > 0.0f)
        {
            params.tempoSynced = true;
            params.intervalBeats = stroke.interval.fallbackBeats;
        }
        else
        {
            return false;    // 따라갈 간격이 없다. 부르는 쪽이 한 번만 친다
        }
    }

    switch (stroke.type)
    {
        case Stroke::Type::repeat:
            params.kind = RunnerParams::Kind::repeat;
            params.accentEvery = stroke.accentEvery;
            params.softScale = stroke.softScale;
            params.rampPerHit = stroke.rampPerHit;
            params.swellSamples = msToSamples (stroke.swellMs);
            params.swellStart = stroke.swellStart;
            break;

        case Stroke::Type::buzz:
            params.kind = RunnerParams::Kind::buzz;
            params.bounces = stroke.bounces;
            params.bounceGapSamples = msToSamples (stroke.bounceGapMs);
            params.bounceGapShrink = stroke.bounceGapShrink;
            params.bounceDecay = stroke.bounceDecay;
            params.swellSamples = msToSamples (stroke.swellMs);
            params.swellStart = stroke.swellStart;
            break;

        case Stroke::Type::pattern:
            params.kind = RunnerParams::Kind::pattern;
            params.numSteps = stroke.numSteps;
            params.steps = stroke.steps;
            break;

        case Stroke::Type::single:
        case Stroke::Type::downUp:
        case Stroke::Type::multi:
            return false;
    }

    params.timingJitter = stroke.timingJitter;
    params.velocityJitter = stroke.velocityJitter;
    return true;
}

void Performer::postHit (std::uint16_t pad, float velocity, std::uint32_t delaySamples, KeyCode key, std::uint64_t timeNs,
                         std::uint32_t skipSamples)
{
    EngineCommand command;
    command.type = EngineCommand::Type::hit;
    command.pad = pad;
    command.velocity = velocity;
    command.delaySamples = delaySamples;
    command.skipSamples = skipSamples;
    command.origin = { HitSource::key, key };
    engine.postFromInput (command);

    if (delaySamples == 0 && pad < lastHitTime.size())
    {
        lastHitTime[pad] = timeNs;
        lastHitKey[pad] = key;
    }
}

void Performer::postLiveScale()
{
    EngineCommand command;
    command.type = EngineCommand::Type::setLiveVelocityScale;
    command.velocity = accentHeld > 0 && ghostHeld == 0 ? active->accentScale
                     : ghostHeld > 0 && accentHeld == 0 ? active->ghostScale
                                                        : 1.0f;
    engine.postFromInput (command);
}

void Performer::tapTempo (std::uint64_t timeNs)
{
    const auto elapsed = lastTapTime != 0 && timeNs > lastTapTime ? timeNs - lastTapTime : 0;
    lastTapTime = timeNs;

    if (elapsed < tapMinNs || elapsed > tapMaxNs)
    {
        numTapIntervals = 0;    // 너무 오랜만이거나 너무 빠르면 새로 센다
        return;
    }

    // 최근 간격 네 개의 평균으로 템포를 정한다.
    if (numTapIntervals < static_cast<int> (tapIntervals.size()))
    {
        tapIntervals[static_cast<std::size_t> (numTapIntervals++)] = elapsed;
    }
    else
    {
        std::rotate (tapIntervals.begin(), tapIntervals.begin() + 1, tapIntervals.end());
        tapIntervals.back() = elapsed;
    }

    std::uint64_t total = 0;

    for (int i = 0; i < numTapIntervals; ++i)
        total += tapIntervals[static_cast<std::size_t> (i)];

    engine.setTempo (static_cast<float> (60.0e9 * numTapIntervals / static_cast<double> (total)));
}

void Performer::keyDown (KeyCode key, std::uint64_t timeNs)
{
    if (key < numKeyCodes)
        keyHeld[key].store (true, std::memory_order_relaxed);

    if (! sync() || key >= numKeyCodes)
        return;

    const auto& actions = active->onPress[key];

    // 패턴은 누를 때마다 켜고 끈다. 한 키에 묶인 패턴들은 함께 움직인다.
    const auto turnPatternsOn = ! latched[key];
    auto hasPattern = false;

    for (std::size_t index = 0; index < actions.size(); ++index)
    {
        const auto& action = actions[index];

        switch (action.type)
        {
            case KeyAction::Type::accent:
                ++accentHeld;
                postLiveScale();
                break;

            case KeyAction::Type::ghost:
                ++ghostHeld;
                postLiveScale();
                break;

            case KeyAction::Type::pedal:
            {
                if (action.pedal >= pedalHeld.size())
                    break;

                ++pedalHeld[action.pedal];
                pedalPressTime[action.pedal] = timeNs;
                const auto& pedal = active->pedals[action.pedal];

                if (pedal.pressPad >= 0)
                    postHit (static_cast<std::uint16_t> (pedal.pressPad), modified (pedal.pressVelocity), 0, key, timeNs);

                break;
            }

            case KeyAction::Type::tapTempo:
                tapTempo (timeNs);
                break;

            case KeyAction::Type::metronome:
                engine.setMetronomeOn (! engine.isMetronomeOn());
                break;

            case KeyAction::Type::choke:
            {
                EngineCommand command;
                command.type = EngineCommand::Type::chokePad;
                command.pad = action.pad;
                engine.postFromInput (command);
                break;
            }

            case KeyAction::Type::hit:
            {
                const auto pad = swapped (action.pad);
                const auto& stroke = action.stroke;
                auto velocity = action.velocity;

                // 같은 패드의 다른 키를 거의 동시에 누르면 악센트로 친다.
                if (active->chordAccentMs > 0.0f && pad < lastHitTime.size()
                    && lastHitTime[pad] != 0 && lastHitKey[pad] != key && timeNs >= lastHitTime[pad]
                    && timeNs - lastHitTime[pad] <= static_cast<std::uint64_t> (active->chordAccentMs * static_cast<float> (nanosPerMs)))
                {
                    velocity = 1.0f;
                }

                switch (stroke.type)
                {
                    case Stroke::Type::single:
                    case Stroke::Type::downUp:
                        postHit (pad, modified (velocity), 0, key, timeNs);
                        break;

                    case Stroke::Type::multi:
                        postHit (pad, modified (velocity * stroke.firstScale), 0, key, timeNs);

                        for (std::size_t i = 0; i < stroke.numExtra; ++i)
                            postHit (pad, modified (velocity * stroke.extra[i].velocityScale),
                                     std::max<std::uint32_t> (1, toSamples (stroke.extra[i].delay)), key, timeNs);

                        break;

                    case Stroke::Type::repeat:
                    case Stroke::Type::buzz:
                    {
                        EngineCommand command;

                        if (makeRunner (stroke, pad, timeNs, command.runner))
                        {
                            // 이어 치기의 세기에는 보조 키를 미리 곱하지 않는다. 엔진이 누르는 동안의 값을 곱한다.
                            command.type = EngineCommand::Type::runnerStart;
                            command.runnerId = runnerId (key, index);
                            command.pad = pad;
                            command.velocity = velocity;
                            command.origin = { HitSource::key, key };
                            engine.postFromInput (command);

                            if (pad < lastHitTime.size())
                            {
                                lastHitTime[pad] = timeNs;
                                lastHitKey[pad] = key;
                            }
                        }
                        else
                        {
                            postHit (pad, modified (velocity), 0, key, timeNs);
                        }

                        break;
                    }

                    case Stroke::Type::pattern:
                    {
                        hasPattern = true;

                        EngineCommand command;
                        command.runnerId = runnerId (key, index);

                        if (turnPatternsOn && makeRunner (stroke, pad, timeNs, command.runner))
                        {
                            command.type = EngineCommand::Type::runnerStart;
                            command.pad = pad;
                            command.velocity = velocity;
                            command.origin = { HitSource::key, key };
                        }
                        else
                        {
                            command.type = EngineCommand::Type::runnerStop;
                        }

                        engine.postFromInput (command);
                        break;
                    }
                }

                break;
            }
        }
    }

    if (hasPattern)
        latched[key] = turnPatternsOn;
}

void Performer::keyUp (KeyCode key, std::uint64_t timeNs)
{
    if (key < numKeyCodes)
        keyHeld[key].store (false, std::memory_order_relaxed);

    if (! sync() || key >= numKeyCodes)
        return;

    const auto& actions = active->onPress[key];

    for (std::size_t index = 0; index < actions.size(); ++index)
    {
        const auto& action = actions[index];

        switch (action.type)
        {
            case KeyAction::Type::accent:
                accentHeld = std::max (0, accentHeld - 1);
                postLiveScale();
                break;

            case KeyAction::Type::ghost:
                ghostHeld = std::max (0, ghostHeld - 1);
                postLiveScale();
                break;

            case KeyAction::Type::pedal:
            {
                if (action.pedal >= pedalHeld.size() || pedalHeld[action.pedal] == 0)
                    break;

                --pedalHeld[action.pedal];
                const auto& pedal = active->pedals[action.pedal];
                const auto pressed = pedalPressTime[action.pedal];
                const auto heldNs = pressed != 0 && timeNs >= pressed ? timeNs - pressed : std::uint64_t (0);
                const auto quick = pressed != 0 && heldNs <= static_cast<std::uint64_t> (pedal.splashMs * static_cast<float> (nanosPerMs));

                if (pedal.splashPad >= 0 && quick)
                {
                    // 풋 스플래시: 밟을 때 이미 "칙"이 났으므로, 스플래시 녹음의 부딪치는 앞부분은 밟고 있던 시간만큼 건너뛰고
                    // 그 뒤의 열리며 울리는 소리부터 낸다. 같은 하이햇 그룹이라 칙 소리는 이때 끊긴다.
                    const auto skip = static_cast<std::uint32_t> (static_cast<double> (heldNs) * 1.0e-9 * engine.getSampleRate());
                    postHit (static_cast<std::uint16_t> (pedal.splashPad), modified (pedal.pressVelocity), 0, key, timeNs, skip);
                }
                else if (pedal.releasePad >= 0)
                {
                    postHit (static_cast<std::uint16_t> (pedal.releasePad), modified (pedal.releaseVelocity), 0, key, timeNs);
                }

                break;
            }

            case KeyAction::Type::hit:
            {
                const auto& stroke = action.stroke;

                if (stroke.type == Stroke::Type::downUp)
                {
                    postHit (swapped (action.pad), modified (action.velocity * stroke.releaseScale), 0, key, timeNs);
                }
                else if (stroke.type == Stroke::Type::repeat || stroke.type == Stroke::Type::buzz)
                {
                    EngineCommand command;
                    command.type = EngineCommand::Type::runnerStop;
                    command.runnerId = runnerId (key, index);
                    engine.postFromInput (command);
                }

                break;
            }

            case KeyAction::Type::choke:
            case KeyAction::Type::tapTempo:
            case KeyAction::Type::metronome:
                break;
        }
    }
}

} // namespace hvd
