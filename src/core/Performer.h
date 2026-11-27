#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "Engine.h"
#include "Mapping.h"

namespace hvd
{

/** 키 입력을 키 배열에 따라 엔진 지시로 바꾼다.

    세기 보조 키, 페달, 동시 누름 악센트, 패턴 켜고 끄기, 탭 템포처럼
    "지금 무엇이 눌려 있는가"에 따라 달라지는 일을 여기서 처리한다.

    스레드 규칙: setMapping()과 swappedForScreen()은 화면 스레드에서, keyDown(), keyUp(), mouseHit(), choke()는
    입력 스레드 하나에서 부른다.
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

    /** 마우스로 패드를 친다. 입력 스레드에서 부른다. followPedals이면 키로 친 것처럼 페달을 따른다. */
    void mouseHit (std::uint16_t pad, float velocity, bool followPedals);

    /** 패드들에서 울리고 있는 소리를 멈춘다 (마우스로 심벌을 잡는 것). 입력 스레드에서 부른다. */
    void choke (std::span<const std::uint16_t> pads);

    /** 마우스로 친 패드를 지금의 페달 상태에 맞게 바꾼다. 키로 친 것과 같은 규칙이다. 화면 스레드에서 부른다. */
    std::uint16_t swappedForScreen (std::uint16_t pad) const;

    /** 풋스위치가 등록돼 있고 꽂혀 있는지. 화면 스레드에서 알려 준다.
        풋스위치가 없으면 Pedal1~4 키의 "떼고 있는 동안" 바꿈은 쓰지 않는다. 밟을 수 없는 페달을
        늘 뗀 것으로 보면, 페달 없이 칠 때 소리가 바뀌어 버리기 때문이다. */
    void setFootswitchConnected (bool connected) noexcept    { footswitchConnected.store (connected, std::memory_order_relaxed); }

private:
    bool sync();
    float modified (float velocity) const noexcept;
    std::uint16_t swapped (std::uint16_t pad) const noexcept;
    std::uint32_t toSamples (const Interval& interval) const noexcept;
    bool makeRunner (const Stroke& stroke, std::uint16_t pad, std::uint64_t timeNs, RunnerParams& params) const noexcept;

    void postHit (std::uint16_t pad, float velocity, std::uint32_t delaySamples, KeyCode key, std::uint64_t timeNs,
                  std::uint32_t skipSamples = 0);
    void postLiveScale();
    void tapTempo (std::uint64_t timeNs);

    static std::uint32_t runnerId (KeyCode key, std::size_t actionIndex) noexcept
    {
        return (static_cast<std::uint32_t> (key) << 4) | static_cast<std::uint32_t> (actionIndex & 0xf);
    }

    Engine& engine;
    std::atomic<std::shared_ptr<const Mapping>> mapping;
    std::atomic<std::uint32_t> generation { 0 };

    // 입력 스레드가 적고 화면 스레드가 읽는, 지금 눌려 있는 키. 마우스로 친 패드에 페달을 적용할 때 쓴다.
    std::array<std::atomic<bool>, numKeyCodes> keyHeld {};
    std::atomic<bool> footswitchConnected { false };

    // 아래는 입력 스레드에서만 쓴다.
    std::shared_ptr<const Mapping> active;
    std::uint32_t activeGeneration = 0;
    int accentHeld = 0;
    int ghostHeld = 0;
    std::vector<std::uint8_t> pedalHeld;
    std::vector<std::uint64_t> pedalPressTime;    // 페달을 마지막으로 밟은 시각. 빨리 뗐는지(풋 스플래시) 볼 때 쓴다
    std::array<bool, numKeyCodes> latched {};
    std::array<std::uint64_t, Engine::maxPads> lastHitTime {};
    std::array<KeyCode, Engine::maxPads> lastHitKey {};
    std::array<std::uint64_t, 4> tapIntervals {};
    int numTapIntervals = 0;
    std::uint64_t lastTapTime = 0;
};

} // namespace hvd
