#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hvd
{

namespace
{

constexpr double twoPi = 6.283185307179586;
constexpr double clickSeconds = 0.04;
constexpr double clickFrequency = 1320.0;
constexpr double accentClickFrequency = 1760.0;

} // namespace

void Engine::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    sampleRateForOthers.store (sampleRate, std::memory_order_relaxed);

    stopAllVoices();
    stopAllRunners();
    numScheduled = 0;

    metronomeWasOn = false;
    samplesToNextBeat = 0.0;
    clickRemaining = 0;

    fx.prepare (sampleRate);
}

void Engine::setFxSettings (const FxSettings& settings) noexcept
{
    const float values[numFxValues] = { settings.eqLowDb, settings.eqMidDb, settings.eqHighDb, settings.compAmount,
                                        settings.reverbLevel, settings.reverbSize, settings.reverbDamp };

    for (std::size_t i = 0; i < numFxValues; ++i)
        fxValues[i].store (values[i], std::memory_order_relaxed);

    fxCustom.store (true, std::memory_order_relaxed);
}

FxSettings Engine::getFxSettings() const noexcept
{
    FxSettings settings;

    if (! fxCustom.load (std::memory_order_relaxed))
        return settings;    // 아직 아무도 정하지 않았으면 기본값

    settings.eqLowDb = fxValues[0].load (std::memory_order_relaxed);
    settings.eqMidDb = fxValues[1].load (std::memory_order_relaxed);
    settings.eqHighDb = fxValues[2].load (std::memory_order_relaxed);
    settings.compAmount = fxValues[3].load (std::memory_order_relaxed);
    settings.reverbLevel = fxValues[4].load (std::memory_order_relaxed);
    settings.reverbSize = fxValues[5].load (std::memory_order_relaxed);
    settings.reverbDamp = fxValues[6].load (std::memory_order_relaxed);
    return settings;
}

bool Engine::postFromInput (const EngineCommand& command) noexcept
{
    if (inputQueue->push (command))
        return true;

    droppedCommands.fetch_add (1, std::memory_order_relaxed);
    return false;
}

bool Engine::postFromControl (const EngineCommand& command) noexcept
{
    if (controlQueue->push (command))
        return true;

    droppedCommands.fetch_add (1, std::memory_order_relaxed);
    return false;
}

bool Engine::postFromAux (const EngineCommand& command) noexcept
{
    if (auxQueue->push (command))
        return true;

    droppedCommands.fetch_add (1, std::memory_order_relaxed);
    return false;
}

const Kit* Engine::popRetiredKit() noexcept
{
    const Kit* retired = nullptr;
    return retiredKits->pop (retired) ? retired : nullptr;
}

const Kit* Engine::detachKit() noexcept
{
    stopAllVoices();
    stopAllRunners();
    numScheduled = 0;
    return std::exchange (kit, nullptr);
}

void Engine::setTempo (float beatsPerMinute) noexcept
{
    tempo.store (std::clamp (beatsPerMinute, 30.0f, 300.0f), std::memory_order_relaxed);
}

void Engine::setBeatsPerBar (int beats) noexcept
{
    beatsPerBar.store (std::clamp (beats, 1, 16), std::memory_order_relaxed);
}

std::uint32_t Engine::getHitCount (int pad) const noexcept
{
    if (pad < 0 || pad >= maxPads)
        return 0;

    return hitCounts[static_cast<std::size_t> (pad)].load (std::memory_order_relaxed);
}

std::uint32_t Engine::getChokeCount (int pad) const noexcept
{
    if (pad < 0 || pad >= maxPads)
        return 0;

    return chokeCounts[static_cast<std::size_t> (pad)].load (std::memory_order_relaxed);
}

bool Engine::popHitEvent (HitEvent& event) noexcept
{
    return hitEvents->pop (event);
}

float Engine::takePadPeak (int pad) noexcept
{
    if (pad < 0 || pad >= maxPads)
        return 0.0f;

    return padPeaks[static_cast<std::size_t> (pad)].exchange (0.0f, std::memory_order_relaxed);
}

float Engine::getLastVelocity (int pad) const noexcept
{
    if (pad < 0 || pad >= maxPads)
        return 0.0f;

    return lastVelocities[static_cast<std::size_t> (pad)].load (std::memory_order_relaxed);
}

double Engine::samplesPerBeat() const noexcept
{
    return sampleRate * 60.0 / static_cast<double> (tempo.load (std::memory_order_relaxed));
}

float Engine::nextRandom() noexcept
{
    // xorshift32. 오디오 스레드에서만 쓴다.
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return static_cast<float> (static_cast<std::int32_t> (randomState)) / 2147483648.0f;
}

void Engine::render (float* const* outputs, int numChannels, int numFrames, float* clickOutput) noexcept
{
    EngineCommand command;

    while (controlQueue->pop (command))
        handle (command);

    while (inputQueue->pop (command))
        handle (command);

    while (auxQueue->pop (command))
        handle (command);

    auto* left = numChannels > 0 ? outputs[0] : nullptr;
    auto* right = numChannels > 1 ? outputs[1] : nullptr;

    // 내부 버스의 크기에 맞춰 나눠서 처리한다. 보통의 장치 버퍼(128 ~ 1024)는 한 번에 끝난다.
    for (int done = 0; done < numFrames;)
    {
        const auto count = std::min (numFrames - done, maxBlockFrames);

        renderBlock (left != nullptr ? left + done : nullptr,
                     right != nullptr ? right + done : nullptr,
                     clickOutput != nullptr ? clickOutput + done : nullptr,
                     count);
        done += count;
    }

    clockForOthers.store (clock, std::memory_order_relaxed);
}

void Engine::renderBlock (float* left, float* right, float* click, int numFrames) noexcept
{
    runRunners (numFrames);
    fireScheduled (numFrames);

    const auto count = static_cast<std::size_t> (numFrames);
    std::fill_n (buses->left.begin(), count, 0.0f);
    std::fill_n (buses->right.begin(), count, 0.0f);
    std::fill_n (buses->sendLeft.begin(), count, 0.0f);
    std::fill_n (buses->sendRight.begin(), count, 0.0f);

    auto active = 0;

    for (auto& voice : voices)
    {
        if (voice.sample == nullptr)
            continue;

        mixVoice (voice, numFrames);

        if (voice.sample != nullptr)
            ++active;
    }

    activeVoices.store (active, std::memory_order_relaxed);

    // 이펙트는 드럼 소리에만 건다. 메트로놈은 그 뒤에 더한다.
    fx.process (buses->left.data(), buses->right.data(), buses->sendLeft.data(), buses->sendRight.data(),
                numFrames, getFxSettings());
    gainReduction.store (fx.getGainReductionDb(), std::memory_order_relaxed);

    if (left != nullptr && right != nullptr)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            left[i] += buses->left[i];
            right[i] += buses->right[i];
        }
    }
    else if (auto* mono = left != nullptr ? left : right)
    {
        // 출력이 한 채널뿐이면 양쪽을 반씩 섞어 낸다.
        for (std::size_t i = 0; i < count; ++i)
            mono[i] += 0.5f * (buses->left[i] + buses->right[i]);
    }

    if (click != nullptr)
        renderMetronome (click, nullptr, numFrames);
    else
        renderMetronome (left, right, numFrames);

    clock += static_cast<std::uint64_t> (numFrames);
}

void Engine::handle (const EngineCommand& command) noexcept
{
    switch (command.type)
    {
        case EngineCommand::Type::hit:
        {
            if (command.delaySamples == 0)
                startVoice (command.pad, command.velocity, 0, command.origin, command.skipSamples);
            else
                scheduleHit (clock + command.delaySamples, command.pad, command.velocity, command.origin);

            break;
        }

        case EngineCommand::Type::hitAt:
        {
            if (command.time <= clock)
                startVoice (command.pad, command.velocity, 0, command.origin);
            else
                scheduleHit (command.time, command.pad, command.velocity, command.origin);

            break;
        }

        case EngineCommand::Type::chokePad:
            chokePad (command.pad, 0);

            if (command.pad < maxPads)
                chokeCounts[command.pad].fetch_add (1, std::memory_order_relaxed);

            break;

        case EngineCommand::Type::allSoundOff:
            for (auto& voice : voices)
                if (voice.sample != nullptr)
                    beginFade (voice, 0);

            stopAllRunners();
            numScheduled = 0;
            break;

        case EngineCommand::Type::setKit:
        {
            // 이전 킷의 샘플을 가리키는 보이스가 남지 않게 바로 멈춘다.
            stopAllVoices();
            stopAllRunners();
            numScheduled = 0;
            roundRobin.fill (0);

            if (kit != nullptr && kit != command.kit)
                retiredKits->push (kit);

            kit = command.kit;

            // 패드 설정은 킷 파일의 값에서 시작한다.
            padParams.fill (PadParams());

            if (kit != nullptr)
            {
                const auto numPads = std::min (kit->pads.size(), static_cast<std::size_t> (maxPads));

                for (std::size_t i = 0; i < numPads; ++i)
                {
                    padParams[i].volume = kit->pads[i].volume;
                    padParams[i].pan = kit->pads[i].pan;
                    padParams[i].pitchRatio = kit->pads[i].pitchRatio;
                    padParams[i].reverbSend = kit->pads[i].reverbSend;
                    padParams[i].startSeconds = kit->pads[i].startSeconds;
                    padParams[i].velocityCurve = kit->pads[i].velocityCurve;
                }
            }

            break;
        }

        case EngineCommand::Type::setPadParams:
            if (command.pad < maxPads)
                padParams[command.pad] = command.padParams;

            break;

        case EngineCommand::Type::runnerStart:
        {
            Runner* slot = nullptr;

            // 같은 번호가 이미 돌고 있으면 그 자리를 다시 쓴다.
            for (auto& runner : runners)
                if (runner.active && runner.id == command.runnerId)
                    slot = &runner;

            if (slot == nullptr)
                for (auto& runner : runners)
                    if (! runner.active && slot == nullptr)
                        slot = &runner;

            if (slot == nullptr)
            {
                droppedCommands.fetch_add (1, std::memory_order_relaxed);
                break;
            }

            slot->active = true;
            slot->id = command.runnerId;
            slot->pad = command.pad;
            slot->velocity = command.velocity;
            slot->origin = command.origin;
            slot->params = command.runner;
            slot->nextTime = static_cast<double> (clock);
            slot->startTime = slot->nextTime;
            slot->count = 0;
            break;
        }

        case EngineCommand::Type::runnerStop:
            for (auto& runner : runners)
                if (runner.id == command.runnerId)
                    runner.active = false;

            break;

        case EngineCommand::Type::allRunnersStop:
            stopAllRunners();
            break;

        case EngineCommand::Type::setLiveVelocityScale:
            liveVelocityScale = std::clamp (command.velocity, 0.0f, 4.0f);
            break;
    }
}

void Engine::runRunners (int numFrames) noexcept
{
    const auto blockStart = static_cast<double> (clock);
    const auto blockEnd = blockStart + static_cast<double> (numFrames);
    auto active = 0;

    for (auto& runner : runners)
    {
        if (! runner.active)
            continue;

        // 한 블록에서 낼 수 있는 타격 수에 한도를 둬서, 설정이 잘못돼도 오디오 스레드가 묶이지 않게 한다.
        for (int guard = 0; runner.active && runner.nextTime < blockEnd && guard < 64; ++guard)
        {
            const auto offset = std::max (0, static_cast<int> (runner.nextTime - blockStart));
            emitRunnerHit (runner, offset);

            auto interval = runner.params.tempoSynced ? static_cast<double> (runner.params.intervalBeats) * samplesPerBeat()
                                                      : runner.params.intervalSamples;

            if (runner.params.timingJitter > 0.0f)
                interval *= 1.0 + static_cast<double> (runner.params.timingJitter * nextRandom());

            runner.nextTime += std::max (interval, 32.0);
            ++runner.count;
        }

        if (runner.active)
            ++active;
    }

    activeRunners.store (active, std::memory_order_relaxed);
}

/** 점점 커지기(스웰)의 배율: 시작한 뒤 swellSamples에 걸쳐 swellStart배에서 1배로 올리고, 그 뒤로는 1배다.
    들리는 크기가 고르게 커지도록 dB로 곧게 올린다 (세기를 곧게 올리면 앞에서 확 커지고 뒤에서는 거의 그대로다). */
static float swellScale (const RunnerParams& params, double elapsedSamples) noexcept
{
    if (params.swellSamples <= 0.0f)
        return 1.0f;

    const auto progress = std::clamp (elapsedSamples / static_cast<double> (params.swellSamples), 0.0, 1.0);
    const auto start = std::clamp (params.swellStart, 0.001f, 1.0f);
    return std::pow (start, static_cast<float> (1.0 - progress));
}

void Engine::emitRunnerHit (Runner& runner, int offset) noexcept
{
    const auto& params = runner.params;

    switch (params.kind)
    {
        case RunnerParams::Kind::repeat:
        {
            const auto accented = params.accentEvery > 0 ? (runner.count % params.accentEvery) == 0
                                                         : runner.count == 0;

            auto velocity = runner.velocity * (accented ? 1.0f : params.softScale) * liveVelocityScale;
            velocity *= swellScale (params, runner.nextTime - runner.startTime);

            if (params.rampPerHit != 1.0f)
                velocity *= std::pow (params.rampPerHit, static_cast<float> (runner.count));

            if (params.velocityJitter > 0.0f)
                velocity *= 1.0f + params.velocityJitter * nextRandom();

            startVoice (runner.pad, velocity, offset, runner.origin);
            break;
        }

        case RunnerParams::Kind::buzz:
        {
            // 손 스트로크 하나가 여러 번 튀면서 점점 약해지고 촘촘해진다.
            auto velocity = runner.velocity * swellScale (params, runner.nextTime - runner.startTime) * liveVelocityScale;
            auto gap = static_cast<double> (params.bounceGapSamples);
            auto time = runner.nextTime;

            for (int bounce = 0; bounce < params.bounces; ++bounce)
            {
                auto bounceVelocity = velocity;

                if (params.velocityJitter > 0.0f)
                    bounceVelocity *= 1.0f + params.velocityJitter * nextRandom();

                if (bounce == 0)
                    startVoice (runner.pad, bounceVelocity, offset, runner.origin);
                else
                    scheduleHit (static_cast<std::uint64_t> (time), runner.pad, bounceVelocity, runner.origin);

                time += gap;
                gap *= static_cast<double> (params.bounceGapShrink);
                velocity *= params.bounceDecay;
            }

            break;
        }

        case RunnerParams::Kind::pattern:
        {
            if (params.numSteps == 0)
            {
                runner.active = false;
                break;
            }

            const auto level = params.steps[runner.count % params.numSteps];

            if (level > 0)
                startVoice (runner.pad, runner.velocity * static_cast<float> (level) / 255.0f, offset, runner.origin);

            break;
        }
    }
}

void Engine::scheduleHit (std::uint64_t time, std::uint16_t pad, float velocity, HitOrigin origin) noexcept
{
    if (numScheduled >= maxScheduled)
    {
        droppedCommands.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    auto& slot = scheduled[static_cast<std::size_t> (numScheduled++)];
    slot.time = time;
    slot.pad = pad;
    slot.velocity = velocity;
    slot.origin = origin;
}

void Engine::fireScheduled (int numFrames) noexcept
{
    const auto blockEnd = clock + static_cast<std::uint64_t> (numFrames);
    auto i = 0;

    while (i < numScheduled)
    {
        const auto hit = scheduled[static_cast<std::size_t> (i)];

        if (hit.time < blockEnd)
        {
            const auto offset = hit.time > clock ? static_cast<int> (hit.time - clock) : 0;
            startVoice (hit.pad, hit.velocity, offset, hit.origin);

            // 마지막 항목을 이 자리로 옮기고 같은 자리를 다시 본다.
            scheduled[static_cast<std::size_t> (i)] = scheduled[static_cast<std::size_t> (--numScheduled)];
        }
        else
        {
            ++i;
        }
    }
}

void Engine::beginFade (Voice& voice, int offset) noexcept
{
    if (voice.fadeStep > 0.0f)
        return;    // 이미 사라지는 중

    voice.fadeStep = voice.chokeFadeStep;
    voice.fadeDelay = offset;
}

void Engine::chokePad (std::uint16_t padIndex, int offset) noexcept
{
    for (auto& voice : voices)
        if (voice.sample != nullptr && voice.pad == padIndex)
            beginFade (voice, offset);
}

void Engine::stopAllVoices() noexcept
{
    for (auto& voice : voices)
        voice.sample = nullptr;

    activeVoices.store (0, std::memory_order_relaxed);
}

void Engine::stopAllRunners() noexcept
{
    for (auto& runner : runners)
        runner.active = false;

    liveVelocityScale = 1.0f;
    activeRunners.store (0, std::memory_order_relaxed);
}

Engine::Voice& Engine::allocateVoice() noexcept
{
    Voice* oldest = &voices[0];

    for (auto& voice : voices)
    {
        if (voice.sample == nullptr)
            return voice;

        if (voice.serial < oldest->serial)
            oldest = &voice;
    }

    // 빈 보이스가 없으면 가장 오래된 것을 빼앗는다.
    return *oldest;
}

void Engine::startVoice (std::uint16_t padIndex, float velocity, int offset, HitOrigin origin, std::uint32_t skipSamples) noexcept
{
    if (kit == nullptr || padIndex >= kit->pads.size() || padIndex >= maxPads)
        return;

    const auto& pad = kit->pads[padIndex];
    velocity = std::clamp (velocity, 0.0f, 1.0f);

    hitCounts[padIndex].fetch_add (1, std::memory_order_relaxed);
    lastVelocities[padIndex].store (velocity, std::memory_order_relaxed);

    if (velocity > 0.0f)
    {
        HitEvent event;
        event.time = clock + static_cast<std::uint64_t> (std::max (0, offset));
        event.pad = padIndex;
        event.velocity = velocity;
        event.origin = origin;
        hitEvents->push (event);    // 아무도 꺼내 가지 않아 꽉 차면 버려진다
    }

    // 이 패드가 끊는 그룹에 속한 소리를 먼저 줄인다. (예: 하이햇을 닫으면 열린 소리가 끊긴다)
    if (pad.cutsMask != 0)
        for (auto& voice : voices)
            if (voice.sample != nullptr && voice.group >= 0 && ((pad.cutsMask >> voice.group) & 1u) != 0)
                beginFade (voice, offset);

    if (! playPad (padIndex, velocity, offset, 1.0f, skipSamples))
        return;

    // 함께 울리는 패드 (킥을 치면 스네어 줄이 떤다). 그 패드의 타격으로 세지 않고, 그 패드가 끊는 그룹도 건드리지 않는다.
    if (pad.sympatheticPad >= 0 && pad.sympatheticPad != padIndex && pad.sympatheticAmount > 0.0f)
        playPad (static_cast<std::uint16_t> (pad.sympatheticPad), velocity, offset, pad.sympatheticAmount, 0);
}

bool Engine::playPad (std::uint16_t padIndex, float velocity, int offset, float gainScale, std::uint32_t skipSamples) noexcept
{
    if (kit == nullptr || padIndex >= kit->pads.size() || padIndex >= maxPads)
        return false;

    const auto& pad = kit->pads[padIndex];
    const auto& params = padParams[padIndex];

    if (pad.layers.empty() || velocity <= 0.0f || params.mute)
        return false;

    // 세기에 맞는 레이어를 고른다.
    const Layer* layer = &pad.layers.back();

    for (const auto& candidate : pad.layers)
    {
        if (velocity <= candidate.upTo)
        {
            layer = &candidate;
            break;
        }
    }

    if (layer->samples.empty())
        return false;

    // 같은 레이어 안에서는 칠 때마다 다음 샘플로 넘어간다.
    const auto turn = roundRobin[padIndex]++;
    const auto sampleIndex = layer->samples[turn % layer->samples.size()];

    if (sampleIndex >= kit->samples.size())
        return false;

    const auto& sample = kit->samples[sampleIndex];

    if (sample.frames() < 4)
        return false;

    const auto nominal = std::max (layer->nominal, 0.01f);
    auto gain = params.volume * gainScale * std::pow (velocity / nominal, params.velocityCurve);
    auto step = sample.sampleRate / sampleRate * static_cast<double> (params.pitchRatio);

    // 휴머나이즈: 칠 때마다 음량과 음높이를 조금씩 다르게 한다.
    if (kit->humanizeVolume > 0.0f)
        gain *= 1.0f + kit->humanizeVolume * nextRandom();

    if (kit->humanizePitchCents > 0.0f)
        step *= std::pow (2.0, static_cast<double> (kit->humanizePitchCents * nextRandom()) / 1200.0);

    const auto pan = std::clamp (params.pan, -1.0f, 1.0f);
    const auto chokeFrames = std::max (1.0, static_cast<double> (pad.chokeFadeMs) * 0.001 * sampleRate);

    // 앞부분을 건너뛰는 설정이 샘플보다 길면 낼 소리가 없다. 지시가 건너뛰라고 한 만큼은 재생 속도대로 더 건너뛴다.
    const auto startFrame = std::max (0.0, static_cast<double> (params.startSeconds) * sample.sampleRate)
                          + static_cast<double> (skipSamples) * step;

    if (startFrame + 4.0 >= static_cast<double> (sample.frames()))
        return false;

    // 소리 중간부터 내면 첫 샘플에서 딸깍 소리가 나므로 3ms 동안 키운다.
    constexpr double rampSeconds = 0.003;

    auto& voice = allocateVoice();
    voice.sample = &sample;
    voice.position = startFrame;
    voice.step = step;
    voice.gainLeft = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
    voice.gainRight = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
    voice.send = std::clamp (params.reverbSend, 0.0f, 1.0f);
    voice.fade = skipSamples > 0 ? 0.0f : 1.0f;
    voice.fadeStep = skipSamples > 0 ? static_cast<float> (-1.0 / std::max (1.0, rampSeconds * sampleRate)) : 0.0f;
    voice.chokeFadeStep = static_cast<float> (1.0 / chokeFrames);
    voice.startOffset = std::max (0, offset);
    voice.fadeDelay = 0;
    voice.group = pad.group;
    voice.pad = padIndex;
    voice.serial = ++serialCounter;
    return true;
}

void Engine::mixVoice (Voice& voice, int numFrames) noexcept
{
    auto* const left = buses->left.data();
    auto* const right = buses->right.data();
    auto* const sendLeft = buses->sendLeft.data();
    auto* const sendRight = buses->sendRight.data();
    auto i = voice.startOffset;

    if (i >= numFrames)
    {
        voice.startOffset -= numFrames;
        return;
    }

    voice.startOffset = 0;

    const auto& sample = *voice.sample;
    const auto* sourceLeft = sample.left.data();
    const auto* sourceRight = sample.right.empty() ? sourceLeft : sample.right.data();
    const auto numSourceFrames = sample.frames();
    const auto plainCopy = voice.step == 1.0;
    auto peak = 0.0f;

    // 이 보이스가 이번 블록에서 낸 가장 큰 크기를 패드의 레벨 미터에 반영한다.
    const auto reportPeak = [this, &voice, &peak]() noexcept
    {
        auto& padPeak = padPeaks[voice.pad];

        if (peak > padPeak.load (std::memory_order_relaxed))
            padPeak.store (peak, std::memory_order_relaxed);
    };

    for (; i < numFrames; ++i)
    {
        if (voice.fadeStep > 0.0f)
        {
            if (voice.fadeDelay > 0)
            {
                --voice.fadeDelay;
            }
            else
            {
                voice.fade -= voice.fadeStep;

                if (voice.fade <= 0.0f)
                {
                    voice.sample = nullptr;
                    reportPeak();
                    return;
                }
            }
        }
        else if (voice.fadeStep < 0.0f)
        {
            voice.fade -= voice.fadeStep;

            if (voice.fade >= 1.0f)
            {
                voice.fade = 1.0f;
                voice.fadeStep = 0.0f;
            }
        }

        const auto index = static_cast<std::size_t> (voice.position);

        // 보간에 뒤쪽 두 샘플이 필요하므로 끝에서 두 프레임 앞에서 마친다.
        if (index + 2 >= numSourceFrames)
        {
            voice.sample = nullptr;
            reportPeak();
            return;
        }

        float valueLeft, valueRight;

        if (plainCopy)
        {
            valueLeft = sourceLeft[index];
            valueRight = sourceRight[index];
        }
        else
        {
            // 4점 에르미트 보간 (샘플레이트가 다르거나 피치를 바꿀 때)
            const auto t = static_cast<float> (voice.position - static_cast<double> (index));
            const auto before = index > 0 ? index - 1 : 0;

            const auto interpolate = [t] (float y0, float y1, float y2, float y3) noexcept
            {
                const auto c1 = 0.5f * (y2 - y0);
                const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                return ((c3 * t + c2) * t + c1) * t + y1;
            };

            valueLeft = interpolate (sourceLeft[before], sourceLeft[index], sourceLeft[index + 1], sourceLeft[index + 2]);
            valueRight = interpolate (sourceRight[before], sourceRight[index], sourceRight[index + 1], sourceRight[index + 2]);
        }

        const auto outLeft = valueLeft * voice.gainLeft * voice.fade;
        const auto outRight = valueRight * voice.gainRight * voice.fade;

        left[i] += outLeft;
        right[i] += outRight;
        sendLeft[i] += outLeft * voice.send;
        sendRight[i] += outRight * voice.send;

        peak = std::max ({ peak, std::abs (outLeft), std::abs (outRight) });
        voice.position += voice.step;
    }

    reportPeak();
}

void Engine::renderMetronome (float* left, float* right, int numFrames) noexcept
{
    const auto on = metronomeOn.load (std::memory_order_relaxed);

    // 켜는 순간 첫 박부터 시작한다.
    if (on && ! metronomeWasOn)
    {
        samplesToNextBeat = 0.0;
        beatInBar = 0;
    }

    metronomeWasOn = on;

    if (! on && clickRemaining <= 0)
        return;

    for (int i = 0; i < numFrames; ++i)
    {
        if (on)
        {
            if (samplesToNextBeat <= 0.0)
            {
                const auto accent = beatInBar == 0;

                clickRemaining = static_cast<int> (clickSeconds * sampleRate);
                clickPhase = 0.0;
                clickPhaseStep = twoPi * (accent ? accentClickFrequency : clickFrequency) / sampleRate;
                clickGain = metronomeGain.load (std::memory_order_relaxed) * (accent ? 1.0f : 0.7f);
                clickDecay = static_cast<float> (std::exp (-1.0 / (0.008 * sampleRate)));

                beatCount.fetch_add (1, std::memory_order_relaxed);
                lastBeatTime.store (clock + static_cast<std::uint64_t> (i), std::memory_order_relaxed);
                beatInBar = (beatInBar + 1) % std::max (1, beatsPerBar.load (std::memory_order_relaxed));
                samplesToNextBeat += samplesPerBeat();
            }

            samplesToNextBeat -= 1.0;
        }

        if (clickRemaining > 0)
        {
            const auto value = static_cast<float> (std::sin (clickPhase)) * clickGain;
            clickPhase += clickPhaseStep;
            clickGain *= clickDecay;
            --clickRemaining;

            if (left != nullptr)
                left[i] += value;

            if (right != nullptr)
                right[i] += value;
        }
    }
}

} // namespace hvd
