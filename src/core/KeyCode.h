#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hvd
{

/** 키보드의 물리 키 하나를 가리키는 번호.

    Windows 스캔코드(세트 1)의 아래 8비트에, 확장 키(E0 접두)이면 0x100을 더한 값이다.
    글자가 아니라 키의 위치를 가리키므로 한/영 상태나 자판 배열과 상관없이 같다.
*/
using KeyCode = std::uint16_t;

inline constexpr std::size_t numKeyCodes = 512;
inline constexpr KeyCode invalidKey = 0;

constexpr KeyCode makeKeyCode (unsigned scanCode, bool extended) noexcept
{
    return static_cast<KeyCode> ((scanCode & 0xffu) | (extended ? 0x100u : 0u));
}

/** 풋스위치(페달)의 스위치를 가리키는 키 번호. 설정 파일에서는 "Pedal1", "Pedal2"…로 쓴다.

    풋스위치는 키보드로 인식되어 보통 글자 키를 보낸다. 그대로 두면 키보드의 같은 글자와 구분되지 않으므로,
    페달로 등록한 장치에서 온 키는 이 번호로 바꿔서 다룬다 (RawKeyboard). 키보드의 스캔코드가 쓰지 않는 번호다.
*/
inline constexpr int maxPedalKeys = 4;
inline constexpr KeyCode firstPedalKey = 0xc1;

constexpr KeyCode pedalKey (int index) noexcept
{
    return static_cast<KeyCode> (firstPedalKey + index);
}

constexpr bool isPedalKey (KeyCode key) noexcept
{
    return key >= firstPedalKey && key < firstPedalKey + maxPedalKeys;
}

/** 설정 파일에 쓰는 이름. 예: "KeyJ", "Space", "Numpad1", "ShiftLeft".
    표에 없는 키는 "Scan_5E", "Scan_E0_5E" 꼴로 돌려준다. */
std::string keyName (KeyCode key);

/** keyName()의 반대. 대소문자를 가리지 않는다. 모르는 이름이면 비어 있는 값을 돌려준다. */
std::optional<KeyCode> keyFromName (std::string_view name) noexcept;

/** 화면에 보여 줄 짧은 이름. 예: "J", "Space", "Num 1", "L-Shift". */
std::string keyLabel (KeyCode key);

} // namespace hvd
