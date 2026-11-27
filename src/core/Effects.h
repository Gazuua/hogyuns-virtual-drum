#pragma once

#include <array>
#include <vector>

namespace hvd
{

/** 드럼 전체에 거는 이펙트 설정. 모두 0이면 아무것도 하지 않는다. */
struct FxSettings
{
    float eqLowDb = 0.0f;         // 저음 (150 Hz 아래) 올리고 내리기, dB
    float eqMidDb = 0.0f;         // 중음 (1 kHz 근처), dB
    float eqHighDb = 0.0f;        // 고음 (6 kHz 위), dB
    float compAmount = 0.0f;      // 컴프레서 세기 0 ~ 1. 0이면 끈다
    float reverbLevel = 0.0f;     // 리버브 크기 0 ~ 1. 0이면 끈다
    float reverbSize = 0.5f;      // 공간 크기 0 ~ 1
    float reverbDamp = 0.5f;      // 고음 감쇠 0 ~ 1

    bool operator== (const FxSettings&) const = default;
};

/** 킷 파일의 "fx"에 적는 값 하나: 이름, 한도, 적지 않았을 때의 값, FxSettings의 어느 값인지. */
struct FxField
{
    const char* name;
    float minimum, maximum, fallback;
    float FxSettings::* member;
};

inline constexpr FxField fxFields[] = {
    { "eqLow",      -18.0f, 18.0f, 0.0f, &FxSettings::eqLowDb },
    { "eqMid",      -18.0f, 18.0f, 0.0f, &FxSettings::eqMidDb },
    { "eqHigh",     -18.0f, 18.0f, 0.0f, &FxSettings::eqHighDb },
    { "comp",         0.0f,  1.0f, 0.0f, &FxSettings::compAmount },
    { "reverb",       0.0f,  1.0f, 0.0f, &FxSettings::reverbLevel },
    { "reverbSize",   0.0f,  1.0f, 0.5f, &FxSettings::reverbSize },
    { "reverbDamp",   0.0f,  1.0f, 0.5f, &FxSettings::reverbDamp },
};

/** 드럼 버스에 거는 3밴드 EQ, 컴프레서, 리버브.

    오디오 스레드에서 process()를 부른다. prepare()는 오디오가 멈춰 있을 때 부르며 여기서만 메모리를 잡는다.
*/
class MasterFx
{
public:
    void prepare (double newSampleRate);

    /** left, right를 제자리에서 처리한다. sendLeft, sendRight는 리버브로 보낼 신호다. */
    void process (float* left, float* right, const float* sendLeft, const float* sendRight,
                  int numFrames, const FxSettings& settings) noexcept;

    /** 컴프레서가 지금 줄이고 있는 양(dB, 0 이상). 화면 표시용. */
    float getGainReductionDb() const noexcept    { return gainReductionDb; }

private:
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        std::array<float, 2> z1 {}, z2 {};

        float process (float input, std::size_t channel) noexcept
        {
            const auto output = b0 * input + z1[channel];
            z1[channel] = b1 * input - a1 * output + z2[channel];
            z2[channel] = b2 * input - a2 * output;
            return output;
        }
    };

    struct Comb
    {
        std::vector<float> buffer;
        std::size_t index = 0;
        float store = 0.0f;
    };

    struct Allpass
    {
        std::vector<float> buffer;
        std::size_t index = 0;
    };

    static constexpr int numCombs = 8;
    static constexpr int numAllpasses = 4;

    void updateEq (const FxSettings& settings) noexcept;
    void clearReverb() noexcept;

    double sampleRate = 48000.0;
    FxSettings current;
    bool eqActive = false;
    bool reverbWasActive = false;

    Biquad lowShelf, midPeak, highShelf;

    float envelope = 0.0f;           // 컴프레서가 따라가는 신호 크기
    float gainReductionDb = 0.0f;

    std::array<std::array<Comb, numCombs>, 2> combs;
    std::array<std::array<Allpass, numAllpasses>, 2> allpasses;
};

} // namespace hvd
