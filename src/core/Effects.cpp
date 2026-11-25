#include "Effects.h"

#include <algorithm>
#include <cmath>

namespace hvd
{

namespace
{

constexpr double pi = 3.141592653589793;

// Freeverb(퍼블릭 도메인, Jezar)의 지연 길이. 44.1 kHz 기준이며 다른 샘플레이트에서는 비율로 늘린다.
constexpr int combLengths[] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
constexpr int allpassLengths[] = { 556, 441, 341, 225 };
constexpr int stereoSpread = 23;

struct Coefficients
{
    float b0, b1, b2, a1, a2;
};

/** RBJ 오디오 EQ 쿡북의 셸프와 피크 필터. kind: 0 저음 셸프, 1 피크, 2 고음 셸프 */
Coefficients makeFilter (int kind, double sampleRate, double frequency, double gainDb, double q)
{
    const auto a = std::pow (10.0, gainDb / 40.0);
    const auto w = 2.0 * pi * std::min (frequency, sampleRate * 0.45) / sampleRate;
    const auto cosW = std::cos (w);
    const auto sinW = std::sin (w);
    const auto alpha = sinW / (2.0 * q);
    double b0, b1, b2, a0, a1, a2;

    if (kind == 1)
    {
        b0 = 1.0 + alpha * a;
        b1 = -2.0 * cosW;
        b2 = 1.0 - alpha * a;
        a0 = 1.0 + alpha / a;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha / a;
    }
    else
    {
        const auto sign = kind == 0 ? 1.0 : -1.0;    // 저음 셸프와 고음 셸프는 cos 항의 부호만 다르다
        const auto beta = 2.0 * std::sqrt (a) * alpha;

        b0 = a * ((a + 1.0) - sign * (a - 1.0) * cosW + beta);
        b1 = sign * 2.0 * a * ((a - 1.0) - sign * (a + 1.0) * cosW);
        b2 = a * ((a + 1.0) - sign * (a - 1.0) * cosW - beta);
        a0 = (a + 1.0) + sign * (a - 1.0) * cosW + beta;
        a1 = -sign * 2.0 * ((a - 1.0) + sign * (a + 1.0) * cosW);
        a2 = (a + 1.0) + sign * (a - 1.0) * cosW - beta;
    }

    return { static_cast<float> (b0 / a0), static_cast<float> (b1 / a0), static_cast<float> (b2 / a0),
             static_cast<float> (a1 / a0), static_cast<float> (a2 / a0) };
}

} // namespace

void MasterFx::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    const auto scale = sampleRate / 44100.0;

    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        const auto spread = channel == 1 ? stereoSpread : 0;

        for (std::size_t i = 0; i < numCombs; ++i)
        {
            auto& comb = combs[channel][i];
            comb.buffer.assign (static_cast<std::size_t> ((combLengths[i] + spread) * scale) + 1, 0.0f);
            comb.index = 0;
            comb.store = 0.0f;
        }

        for (std::size_t i = 0; i < numAllpasses; ++i)
        {
            auto& allpass = allpasses[channel][i];
            allpass.buffer.assign (static_cast<std::size_t> ((allpassLengths[i] + spread) * scale) + 1, 0.0f);
            allpass.index = 0;
        }
    }

    lowShelf = {};
    midPeak = {};
    highShelf = {};
    envelope = 0.0f;
    gainReductionDb = 0.0f;
    reverbWasActive = false;

    current = {};
    updateEq (current);
}

void MasterFx::updateEq (const FxSettings& settings) noexcept
{
    const auto apply = [] (Biquad& filter, const Coefficients& c)
    {
        filter.b0 = c.b0;
        filter.b1 = c.b1;
        filter.b2 = c.b2;
        filter.a1 = c.a1;
        filter.a2 = c.a2;
    };

    apply (lowShelf, makeFilter (0, sampleRate, 150.0, settings.eqLowDb, 0.707));
    apply (midPeak, makeFilter (1, sampleRate, 1000.0, settings.eqMidDb, 0.8));
    apply (highShelf, makeFilter (2, sampleRate, 6000.0, settings.eqHighDb, 0.707));

    eqActive = settings.eqLowDb != 0.0f || settings.eqMidDb != 0.0f || settings.eqHighDb != 0.0f;
}

void MasterFx::clearReverb() noexcept
{
    for (auto& channel : combs)
    {
        for (auto& comb : channel)
        {
            std::fill (comb.buffer.begin(), comb.buffer.end(), 0.0f);
            comb.store = 0.0f;
        }
    }

    for (auto& channel : allpasses)
        for (auto& allpass : channel)
            std::fill (allpass.buffer.begin(), allpass.buffer.end(), 0.0f);
}

void MasterFx::process (float* left, float* right, const float* sendLeft, const float* sendRight,
                        int numFrames, const FxSettings& settings) noexcept
{
    if (settings != current)
    {
        if (settings.eqLowDb != current.eqLowDb || settings.eqMidDb != current.eqMidDb || settings.eqHighDb != current.eqHighDb)
            updateEq (settings);

        current = settings;
    }

    float* const channels[] = { left, right };

    // 1. EQ
    if (eqActive)
    {
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            auto* samples = channels[channel];

            for (int i = 0; i < numFrames; ++i)
                samples[i] = highShelf.process (midPeak.process (lowShelf.process (samples[i], channel), channel), channel);
        }
    }

    // 2. 컴프레서. 양쪽 채널 중 큰 쪽을 보고 똑같이 줄여 소리의 위치가 흔들리지 않게 한다.
    if (current.compAmount > 0.0f)
    {
        const auto amount = std::min (current.compAmount, 1.0f);
        const auto thresholdDb = -24.0f * amount;
        const auto slope = 1.0f - 1.0f / (1.0f + 3.0f * amount);             // 비율 1:1 ~ 4:1
        const auto makeup = std::pow (10.0f, -thresholdDb * slope * 0.5f / 20.0f);
        const auto attack = static_cast<float> (std::exp (-1.0 / (0.004 * sampleRate)));
        const auto release = static_cast<float> (std::exp (-1.0 / (0.120 * sampleRate)));
        auto reduction = 0.0f;

        for (int i = 0; i < numFrames; ++i)
        {
            const auto level = std::max (std::abs (left[i]), std::abs (right[i]));
            const auto coefficient = level > envelope ? attack : release;
            envelope = level + coefficient * (envelope - level);

            auto gain = makeup;

            if (envelope > 1.0e-6f)
            {
                const auto overDb = 20.0f * std::log10 (envelope) - thresholdDb;

                if (overDb > 0.0f)
                {
                    const auto reductionDb = overDb * slope;
                    gain *= std::pow (10.0f, -reductionDb / 20.0f);
                    reduction = std::max (reduction, reductionDb);
                }
            }

            left[i] *= gain;
            right[i] *= gain;
        }

        gainReductionDb = reduction;
    }
    else
    {
        envelope = 0.0f;
        gainReductionDb = 0.0f;
    }

    // 3. 리버브. 컴프레서 뒤에 더해 잔향이 눌려 펌핑되지 않게 한다.
    const auto reverbActive = current.reverbLevel > 0.0f;

    if (reverbActive && ! reverbWasActive)
        clearReverb();    // 꺼져 있던 동안의 오래된 잔향을 지운다

    reverbWasActive = reverbActive;

    if (! reverbActive)
        return;

    const auto feedback = 0.70f + 0.28f * std::clamp (current.reverbSize, 0.0f, 1.0f);
    const auto damp = 0.05f + 0.40f * std::clamp (current.reverbDamp, 0.0f, 1.0f);
    const auto wet = 3.0f * 0.5f * std::min (current.reverbLevel, 1.0f);
    const float* const sends[] = { sendLeft, sendRight };

    for (int i = 0; i < numFrames; ++i)
    {
        // 양쪽 입력을 섞어 넣고, 채널마다 길이가 조금 다른 지연선으로 좌우가 다른 잔향을 만든다.
        const auto input = (sends[0][i] + sends[1][i]) * 0.015f;

        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            auto output = 0.0f;

            for (auto& comb : combs[channel])
            {
                const auto delayed = comb.buffer[comb.index];
                comb.store = delayed * (1.0f - damp) + comb.store * damp;
                comb.buffer[comb.index] = input + comb.store * feedback;
                comb.index = comb.index + 1 < comb.buffer.size() ? comb.index + 1 : 0;
                output += delayed;
            }

            for (auto& allpass : allpasses[channel])
            {
                const auto delayed = allpass.buffer[allpass.index];
                allpass.buffer[allpass.index] = output + delayed * 0.5f;
                allpass.index = allpass.index + 1 < allpass.buffer.size() ? allpass.index + 1 : 0;
                output = delayed - output;
            }

            channels[channel][i] += output * wet;
        }
    }
}

} // namespace hvd
