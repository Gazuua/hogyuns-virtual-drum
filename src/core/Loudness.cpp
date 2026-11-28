#include "Loudness.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace hvd
{

namespace
{

/** 2차 IIR 필터 하나 (직접형 I). */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    double process (double x) noexcept
    {
        const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

/** K 가중의 두 단계: 머리 효과를 흉내 내는 고음 선반, 아주 낮은 음을 빼는 고역 통과 (BS.1770, 샘플레이트에 맞춰 계산). */
void makeKWeighting (double sampleRate, Biquad& shelf, Biquad& highPass) noexcept
{
    {
        constexpr double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const auto k = std::tan (std::numbers::pi * f0 / sampleRate);
        const auto vh = std::pow (10.0, gainDb / 20.0);
        const auto vb = std::pow (vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;
        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2.0 * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2.0 * (k * k - 1.0) / a0;
        shelf.a2 = (1.0 - k / q + k * k) / a0;
    }

    {
        constexpr double f0 = 38.13547087602444, q = 0.5003270373238773;
        const auto k = std::tan (std::numbers::pi * f0 / sampleRate);
        const auto a0 = 1.0 + k / q + k * k;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (k * k - 1.0) / a0;
        highPass.a2 = (1.0 - k / q + k * k) / a0;
    }
}

constexpr float silentLoudness = -100.0f;

/** 레이어 k가 맡는 세기의 아래 경계와 위 경계. 맨 위 레이어는 1까지 맡는다 (엔진이 넘치는 세기도 맨 위 레이어로 낸다). */
float lowEdgeOf (const Pad& pad, std::size_t k) noexcept     { return k == 0 ? 0.0f : pad.layers[k - 1].upTo; }
float highEdgeOf (const Pad& pad, std::size_t k) noexcept    { return k + 1 == pad.layers.size() ? std::max (pad.layers[k].upTo, 1.0f) : pad.layers[k].upTo; }

float centreOf (const Pad& pad, std::size_t k) noexcept
{
    const auto low = lowEdgeOf (pad, k), high = std::max (highEdgeOf (pad, k), 0.01f);
    return low > 0.0f ? std::sqrt (low * high) : high * 0.5f;
}

/** 보정하지 않았을 때 레이어 k를 velocity로 친 크기 (엔진의 음량 계산: (세기 / nominal)^curve). */
float levelOf (const Pad& pad, std::size_t k, float velocity, float curve) noexcept
{
    const auto& layer = pad.layers[k];
    return layer.loudness + 20.0f * curve * std::log10 (std::max (velocity, 1.0e-4f) / std::max (layer.nominal, 0.01f));
}

} // namespace

float momentaryMaxLoudness (const float* left, const float* right, std::size_t frames, double sampleRate, double maxSeconds) noexcept
{
    if (left == nullptr || frames == 0 || sampleRate <= 0.0)
        return silentLoudness;

    frames = std::min (frames, static_cast<std::size_t> (maxSeconds * sampleRate));
    const auto window = std::max<std::size_t> (1, static_cast<std::size_t> (std::lround (0.4 * sampleRate)));
    const auto hop = std::max<std::size_t> (1, static_cast<std::size_t> (std::lround (0.01 * sampleRate)));

    // 채널마다 K 가중을 거친 제곱을 더해 둔다 (누적 합으로 창의 평균을 바로 구한다).
    std::vector<double> sums (frames + 1, 0.0);
    Biquad shelfL, passL, shelfR, passR;
    makeKWeighting (sampleRate, shelfL, passL);
    makeKWeighting (sampleRate, shelfR, passR);

    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto l = passL.process (shelfL.process (left[i]));
        const auto r = right != nullptr ? passR.process (shelfR.process (right[i])) : l;
        sums[i + 1] = sums[i] + l * l + r * r;
    }

    // 창은 소리의 앞에서 시작한다. 소리가 창보다 짧으면 남는 부분은 조용한 것으로 본다.
    auto loudest = 0.0;

    for (std::size_t start = 0; start == 0 || start + window <= frames; start += hop)
    {
        const auto end = std::min (frames, start + window);
        loudest = std::max (loudest, (sums[end] - sums[start]) / static_cast<double> (window));
    }

    return loudest > 1.0e-10 ? static_cast<float> (-0.691 + 10.0 * std::log10 (loudest)) : silentLoudness;
}

float sampleLoudness (const Sample& sample) noexcept
{
    return momentaryMaxLoudness (sample.left.data(), sample.right.empty() ? nullptr : sample.right.data(), sample.frames(), sample.sampleRate);
}

float layerStepCorrectionDb (const Pad& pad, std::size_t layer, float velocity, float curve) noexcept
{
    const auto count = pad.layers.size();

    if (count < 2 || layer >= count || velocity <= 0.0f)
        return 0.0f;

    // 이 세기가 놓인 구간: 이 레이어의 가운데보다 세면 위 레이어 쪽, 약하면 아래 레이어 쪽.
    std::size_t lower = layer, upper = layer;

    if (velocity >= centreOf (pad, layer))
    {
        if (layer + 1 >= count)
            return 0.0f;

        upper = layer + 1;
    }
    else
    {
        if (layer == 0)
            return 0.0f;

        lower = layer - 1;
    }

    if (! pad.layers[lower].measured || ! pad.layers[upper].measured)
        return 0.0f;

    const auto lowCentre = centreOf (pad, lower), highCentre = centreOf (pad, upper);
    const auto lowLevel = levelOf (pad, lower, lowCentre, curve), highLevel = levelOf (pad, upper, highCentre, curve);

    if (highCentre <= lowCentre || highLevel <= lowLevel)
        return 0.0f;

    const auto t = std::log (velocity / lowCentre) / std::log (highCentre / lowCentre);
    const auto wanted = lowLevel + (highLevel - lowLevel) * t;
    return std::clamp (wanted - levelOf (pad, layer, velocity, curve), -12.0f, 12.0f);
}

} // namespace hvd
