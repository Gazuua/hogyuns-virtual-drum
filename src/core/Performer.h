#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "Engine.h"
#include "Mapping.h"

namespace hvd
{

/** 키 입력을 키 배열에 따라 엔진 지시로 바꾼다.

    세기 보조 키, 페달, 동시 누름 악센트, 패턴 켜고 끄기, 탭 템포처럼
    "지금 무엇이 눌려 있는가"에 따라 달라지는 일을 여기서 처리한다.

    스레드 규칙: setMapping()은 화면 스레드에서, keyDown()과 keyUp()은 입력 스레드 하나에서 부른다.
*/
class Performer
{
public:
    explicit Performer (Engine& engineToUse) noexcept    : engine (engineToUse) {}

    /** 키 배열을 바꾼다. 눌려 있던 키와 켜 둔 패턴은 다음 키 입력 때 정리된다. */
    void setMapping (std::shared_ptr<const Mapping> newMapping);

    std::shared_ptr<const Mapping> getMapping() const    { return mapping.load(); }

    /** timeNs는 now()로 잰, 뒤로 가지 않는 시각(나노초)이다. */
    void keyDown (KeyCode key, std::uint64_t timeNs);
    void keyUp (KeyCode key, std::uint64_t timeNs);

    static std::uint64_t now() noexcept;

private:
    bool sync();
    float modified (float velocity) const noexcept;
    std::uint16_t swapped (std::uint16_t pad) const noexcept;
    std::uint32_t toSamples (const Interval& interval) const noexcept;
    bool makeRunner (const Stroke& stroke, std::uint16_t pad, std::uint64_t timeNs, RunnerParams& params) const noexcept;

    void postHit (std::uint16_t pad, float velocity, std::uint32_t delaySamples, KeyCode key, std::uint64_t timeNs);
    void postLiveScale();
    void tapTempo (std::uint64_t timeNs);

    static std::uint32_t runnerId (KeyCode key, std::size_t actionIndex) noexcept
    {
        return (static_cast<std::uint32_t> (key) << 4) | static_cast<std::uint32_t> (actionIndex & 0xf);
    }

    Engine& engine;
    std::atomic<std::shared_ptr<const Mapping>> mapping;
    std::atomic<std::uint32_t> generation { 0 };

    // 아래는 입력 스레드에서만 쓴다.
    std::shared_ptr<const Mapping> active;
    std::uint32_t activeGeneration = 0;
    int accentHeld = 0;
    int ghostHeld = 0;
    std::vector<std::uint8_t> pedalHeld;
    std::array<bool, numKeyCodes> latched {};
    std::array<std::uint64_t, Engine::maxPads> lastHitTime {};
    std::array<KeyCode, Engine::maxPads> lastHitKey {};
    std::array<std::uint64_t, 4> tapIntervals {};
    int numTapIntervals = 0;
    std::uint64_t lastTapTime = 0;
};

} // namespace hvd
