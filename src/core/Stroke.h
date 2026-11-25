#pragma once

#include <array>
#include <cstdint>

namespace hvd
{

/** 시간 간격을 적는 방법. */
struct Interval
{
    enum class Kind : std::uint8_t
    {
        milliseconds,    // value 밀리초
        beats,           // value 박 (4분음표가 1박). 템포를 따라간다
        follow           // 같은 패드를 직전에 친 간격을 따라간다
    };

    Kind kind = Kind::milliseconds;
    float value = 100.0f;
    float followDivisor = 1.0f;     // follow: 잰 간격을 이 값으로 나눈다 (2면 두 배 빠르게)
    float fallbackBeats = 0.0f;     // follow: 잴 간격이 없을 때 쓸 박 단위 간격. 0이면 반복하지 않는다
};

/** 키 하나를 눌렀을 때 타격이 어떻게 나가는지. */
struct Stroke
{
    enum class Type : std::uint8_t
    {
        single,     // 누르면 1타
        downUp,     // 누를 때 1타, 뗄 때 1타
        multi,      // 누르면 정해진 간격으로 여러 타 (더블, 플램, 드래그)
        repeat,     // 누르고 있는 동안 일정 간격으로 연타
        buzz,       // 누르고 있는 동안 잘게 튀는 롤
        pattern     // 누를 때마다 켜고 끈다. 켜져 있는 동안 스텝 패턴을 되풀이한다
    };

    struct ExtraHit
    {
        Interval delay;                 // 첫 타로부터의 시간
        float velocityScale = 1.0f;
    };

    Type type = Type::single;

    // downUp: 뗄 때의 세기 배율
    float releaseScale = 0.8f;

    // multi: 첫 타의 세기 배율과 뒤따르는 타격들
    float firstScale = 1.0f;
    std::uint8_t numExtra = 0;
    std::array<ExtraHit, 3> extra {};

    // repeat, buzz: 타격(buzz는 손 스트로크) 간격
    Interval interval;

    // repeat: accentEvery번째마다 원래 세기로, 나머지는 softScale을 곱해서 친다.
    // accentEvery가 0이면 맨 처음 한 번만 원래 세기다.
    std::uint8_t accentEvery = 0;
    float softScale = 0.8f;
    float rampPerHit = 1.0f;            // 타격마다 세기에 곱한다. 1보다 크면 점점 세게
    float timingJitter = 0.0f;          // 간격을 이 비율만큼 무작위로 흔든다
    float velocityJitter = 0.0f;        // 세기를 이 비율만큼 무작위로 흔든다

    // buzz: 손 스트로크 하나가 여러 번 튄다
    std::uint8_t bounces = 4;
    float bounceGapMs = 24.0f;          // 첫 번째와 두 번째 튐 사이
    float bounceGapShrink = 0.82f;      // 튈 때마다 간격에 곱한다
    float bounceDecay = 0.72f;          // 튈 때마다 세기에 곱한다

    // repeat, buzz: 누르기 시작한 뒤 swellMs에 걸쳐 swellStart배에서 1배로 점점 커지고, 그 뒤로는 그 세기로 이어진다
    // (심벌을 연타해 소리를 키워 가는 스웰, 점점 커지는 스네어 롤). 들리는 크기가 고르게 커지도록 dB로 곧게 올린다.
    float swellMs = 0.0f;               // 0이면 처음부터 원래 세기
    float swellStart = 0.4f;            // 커지기 시작할 때의 세기 배율

    // pattern: 스텝마다의 세기 (0은 쉼, 255가 원래 세기)
    std::uint8_t numSteps = 0;
    std::array<std::uint8_t, 32> steps {};
    float stepBeats = 0.25f;
};

/** 엔진이 스스로 타격을 이어 내는 일 하나(반복, 버즈 롤, 패턴)의 설정.
    입력 스레드가 간격을 샘플 수로 풀어서 엔진에 넘긴다. */
struct RunnerParams
{
    enum class Kind : std::uint8_t { repeat, buzz, pattern };

    Kind kind = Kind::repeat;

    bool tempoSynced = false;           // true면 intervalBeats와 현재 템포로 간격을 정한다
    float intervalBeats = 0.25f;
    double intervalSamples = 4800.0;    // tempoSynced가 false일 때의 간격

    std::uint8_t accentEvery = 0;
    float softScale = 0.8f;
    float rampPerHit = 1.0f;
    float timingJitter = 0.0f;
    float velocityJitter = 0.0f;

    std::uint8_t bounces = 4;
    float bounceGapSamples = 1152.0f;
    float bounceGapShrink = 0.82f;
    float bounceDecay = 0.72f;
    float swellSamples = 0.0f;
    float swellStart = 0.4f;

    std::uint8_t numSteps = 0;
    std::array<std::uint8_t, 32> steps {};
};

} // namespace hvd
