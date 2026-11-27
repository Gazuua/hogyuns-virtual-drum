#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hvd
{

/** 메모리에 올린 샘플 하나. 만든 뒤에는 바꾸지 않는다. */
struct Sample
{
    std::vector<float> left;
    std::vector<float> right;      // 모노 샘플이면 비워 두고 left를 양쪽에 쓴다
    double sampleRate = 48000.0;

    std::size_t frames() const noexcept    { return left.size(); }
};

/** 한 패드 안에서 세기 구간 하나에 쓰는 샘플 묶음. */
struct Layer
{
    float upTo = 1.0f;                     // 세기가 이 값 이하이면 이 레이어를 쓴다
    float nominal = 1.0f;                  // 이 세기로 치면 샘플을 원래 크기로 낸다
    std::vector<std::uint32_t> samples;    // Kit::samples의 번호. 칠 때마다 번갈아 쓴다
};

/** 패드 음량(배율)의 한도. 킷 화면과 믹서의 슬라이더, 킷 불러오기가 모두 이 값을 쓴다.
    10배는 +20dB다. 드럼 버스의 리미터가 꼭대기를 붙잡으므로 크게 올려도 찢어지지는 않는다. */
constexpr float maxPadVolume = 10.0f;

/** 소리가 나는 단위 하나. 예: 스네어, 라이드 벨. */
struct Pad
{
    std::string id;                // 설정 파일에서 쓰는 이름
    std::string name;              // 화면에 보여 줄 이름
    float volume = 1.0f;           // 배율
    float pan = 0.0f;              // -1(왼쪽) ~ 1(오른쪽)
    float pitchRatio = 1.0f;       // 재생 속도 배율. 1이면 원래 음높이
    float velocityCurve = 1.0f;    // 세기를 음량으로 바꾸는 지수
    float chokeFadeMs = 20.0f;     // 초크될 때 소리가 사라지는 시간
    float reverbSend = 1.0f;       // 리버브로 보내는 양 0 ~ 1
    float startSeconds = 0.0f;     // 샘플의 앞부분을 이만큼 건너뛰고 재생한다
    int midiNote = -1;             // MIDI로 주고받을 때의 노트 번호. 없으면 -1
    int group = -1;                // 속한 초크 그룹 번호. 없으면 -1
    std::uint32_t cutsMask = 0;    // 이 패드를 치면 끊기는 그룹들의 비트 마스크
    std::vector<Layer> layers;     // upTo가 작은 것부터

    // 이 패드를 치면 함께 작게 울리는 패드 (예: 킥을 치면 스네어 줄이 떤다). 없으면 -1.
    // 같은 세기로 그 패드의 소리를 고르고, 음량에 sympatheticAmount를 곱한다. 타격으로 세지 않는다.
    int sympatheticPad = -1;
    float sympatheticAmount = 0.0f;
};

/** 킷 전체. 엔진에 넘긴 뒤에는 바꾸지 않는다. */
struct Kit
{
    static constexpr int maxGroups = 32;

    std::string name;
    std::string source;
    std::string license;

    // 휴머나이즈: 칠 때마다 음량과 음높이를 이 범위 안에서 무작위로 흔든다. 0이면 흔들지 않는다.
    float humanizeVolume = 0.0f;         // 비율. 0.05면 ±5%
    float humanizePitchCents = 0.0f;     // 센트(반음의 1/100)

    std::vector<Sample> samples;
    std::vector<Pad> pads;
    std::vector<std::string> groupNames;

    /** id가 같은 패드의 번호를 돌려준다. 없으면 -1. */
    int findPad (std::string_view id) const noexcept
    {
        for (std::size_t i = 0; i < pads.size(); ++i)
            if (pads[i].id == id)
                return static_cast<int> (i);

        return -1;
    }
};

} // namespace hvd
