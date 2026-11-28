#include "KeyCode.h"

#include <array>
#include <cstdio>

namespace hvd
{

namespace
{

struct KeyEntry
{
    KeyCode code;
    std::string_view name;
    std::string_view label;
};

constexpr KeyCode ext (unsigned scanCode) noexcept    { return makeKeyCode (scanCode, true); }

// 이름은 웹 표준 KeyboardEvent.code 를 따른다.
constexpr std::array keyTable {
    KeyEntry { 0x01, "Escape", "Esc" },
    KeyEntry { 0x02, "Digit1", "1" },
    KeyEntry { 0x03, "Digit2", "2" },
    KeyEntry { 0x04, "Digit3", "3" },
    KeyEntry { 0x05, "Digit4", "4" },
    KeyEntry { 0x06, "Digit5", "5" },
    KeyEntry { 0x07, "Digit6", "6" },
    KeyEntry { 0x08, "Digit7", "7" },
    KeyEntry { 0x09, "Digit8", "8" },
    KeyEntry { 0x0a, "Digit9", "9" },
    KeyEntry { 0x0b, "Digit0", "0" },
    KeyEntry { 0x0c, "Minus", "-" },
    KeyEntry { 0x0d, "Equal", "=" },
    KeyEntry { 0x0e, "Backspace", "Backspace" },
    KeyEntry { 0x0f, "Tab", "Tab" },
    KeyEntry { 0x10, "KeyQ", "Q" },
    KeyEntry { 0x11, "KeyW", "W" },
    KeyEntry { 0x12, "KeyE", "E" },
    KeyEntry { 0x13, "KeyR", "R" },
    KeyEntry { 0x14, "KeyT", "T" },
    KeyEntry { 0x15, "KeyY", "Y" },
    KeyEntry { 0x16, "KeyU", "U" },
    KeyEntry { 0x17, "KeyI", "I" },
    KeyEntry { 0x18, "KeyO", "O" },
    KeyEntry { 0x19, "KeyP", "P" },
    KeyEntry { 0x1a, "BracketLeft", "[" },
    KeyEntry { 0x1b, "BracketRight", "]" },
    KeyEntry { 0x1c, "Enter", "Enter" },
    KeyEntry { 0x1d, "ControlLeft", "L-Ctrl" },
    KeyEntry { 0x1e, "KeyA", "A" },
    KeyEntry { 0x1f, "KeyS", "S" },
    KeyEntry { 0x20, "KeyD", "D" },
    KeyEntry { 0x21, "KeyF", "F" },
    KeyEntry { 0x22, "KeyG", "G" },
    KeyEntry { 0x23, "KeyH", "H" },
    KeyEntry { 0x24, "KeyJ", "J" },
    KeyEntry { 0x25, "KeyK", "K" },
    KeyEntry { 0x26, "KeyL", "L" },
    KeyEntry { 0x27, "Semicolon", ";" },
    KeyEntry { 0x28, "Quote", "'" },
    KeyEntry { 0x29, "Backquote", "`" },
    KeyEntry { 0x2a, "ShiftLeft", "L-Shift" },
    KeyEntry { 0x2b, "Backslash", "\\" },
    KeyEntry { 0x2c, "KeyZ", "Z" },
    KeyEntry { 0x2d, "KeyX", "X" },
    KeyEntry { 0x2e, "KeyC", "C" },
    KeyEntry { 0x2f, "KeyV", "V" },
    KeyEntry { 0x30, "KeyB", "B" },
    KeyEntry { 0x31, "KeyN", "N" },
    KeyEntry { 0x32, "KeyM", "M" },
    KeyEntry { 0x33, "Comma", "," },
    KeyEntry { 0x34, "Period", "." },
    KeyEntry { 0x35, "Slash", "/" },
    KeyEntry { 0x36, "ShiftRight", "R-Shift" },
    KeyEntry { 0x37, "NumpadMultiply", "Num *" },
    KeyEntry { 0x38, "AltLeft", "L-Alt" },
    KeyEntry { 0x39, "Space", "Space" },
    KeyEntry { 0x3a, "CapsLock", "Caps" },
    KeyEntry { 0x3b, "F1", "F1" },
    KeyEntry { 0x3c, "F2", "F2" },
    KeyEntry { 0x3d, "F3", "F3" },
    KeyEntry { 0x3e, "F4", "F4" },
    KeyEntry { 0x3f, "F5", "F5" },
    KeyEntry { 0x40, "F6", "F6" },
    KeyEntry { 0x41, "F7", "F7" },
    KeyEntry { 0x42, "F8", "F8" },
    KeyEntry { 0x43, "F9", "F9" },
    KeyEntry { 0x44, "F10", "F10" },
    KeyEntry { 0x46, "ScrollLock", "ScrLk" },
    KeyEntry { 0x47, "Numpad7", "Num 7" },
    KeyEntry { 0x48, "Numpad8", "Num 8" },
    KeyEntry { 0x49, "Numpad9", "Num 9" },
    KeyEntry { 0x4a, "NumpadSubtract", "Num -" },
    KeyEntry { 0x4b, "Numpad4", "Num 4" },
    KeyEntry { 0x4c, "Numpad5", "Num 5" },
    KeyEntry { 0x4d, "Numpad6", "Num 6" },
    KeyEntry { 0x4e, "NumpadAdd", "Num +" },
    KeyEntry { 0x4f, "Numpad1", "Num 1" },
    KeyEntry { 0x50, "Numpad2", "Num 2" },
    KeyEntry { 0x51, "Numpad3", "Num 3" },
    KeyEntry { 0x52, "Numpad0", "Num 0" },
    KeyEntry { 0x53, "NumpadDecimal", "Num ." },
    KeyEntry { 0x56, "IntlBackslash", "Intl \\" },
    KeyEntry { 0x57, "F11", "F11" },
    KeyEntry { 0x58, "F12", "F12" },
    KeyEntry { 0x71, "Lang2", "Hanja" },
    KeyEntry { 0x72, "Lang1", "Han/Eng" },
    KeyEntry { ext (0x1c), "NumpadEnter", "Num Enter" },
    KeyEntry { ext (0x1d), "ControlRight", "R-Ctrl" },
    KeyEntry { ext (0x35), "NumpadDivide", "Num /" },
    KeyEntry { ext (0x38), "AltRight", "R-Alt" },
    KeyEntry { ext (0x47), "Home", "Home" },
    KeyEntry { ext (0x48), "ArrowUp", "Up" },
    KeyEntry { ext (0x49), "PageUp", "PgUp" },
    KeyEntry { ext (0x4b), "ArrowLeft", "Left" },
    KeyEntry { ext (0x4d), "ArrowRight", "Right" },
    KeyEntry { ext (0x4f), "End", "End" },
    KeyEntry { ext (0x50), "ArrowDown", "Down" },
    KeyEntry { ext (0x51), "PageDown", "PgDn" },
    KeyEntry { ext (0x52), "Insert", "Ins" },
    KeyEntry { ext (0x53), "Delete", "Del" },
    KeyEntry { ext (0x5b), "MetaLeft", "L-Win" },
    KeyEntry { ext (0x5c), "MetaRight", "R-Win" },
    KeyEntry { ext (0x5d), "ContextMenu", "Menu" },

    // 풋스위치의 스위치 (왼쪽부터)
    KeyEntry { pedalKey (0), "Pedal1", "Pedal 1" },
    KeyEntry { pedalKey (1), "Pedal2", "Pedal 2" },
    KeyEntry { pedalKey (2), "Pedal3", "Pedal 3" },
    KeyEntry { pedalKey (3), "Pedal4", "Pedal 4" },

    // 마우스 옆 단추
    KeyEntry { mouseKey (0), "MouseBack", "M4 뒤" },
    KeyEntry { mouseKey (1), "MouseForward", "M5 앞" },
};

const KeyEntry* findEntry (KeyCode key) noexcept
{
    for (const auto& entry : keyTable)
        if (entry.code == key)
            return &entry;

    return nullptr;
}

char lower (char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char> (c - 'A' + 'a') : c;
}

bool equalsIgnoreCase (std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;

    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower (a[i]) != lower (b[i]))
            return false;

    return true;
}

int hexDigit (char c) noexcept
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string fallbackName (KeyCode key)
{
    char text[16] {};
    const auto scan = static_cast<unsigned> (key & 0xff);

    if ((key & 0x100) != 0)
        std::snprintf (text, sizeof (text), "Scan_E0_%02X", scan);
    else
        std::snprintf (text, sizeof (text), "Scan_%02X", scan);

    return text;
}

} // namespace

std::string keyName (KeyCode key)
{
    if (const auto* entry = findEntry (key))
        return std::string (entry->name);

    return fallbackName (key);
}

std::optional<KeyCode> keyFromName (std::string_view name) noexcept
{
    for (const auto& entry : keyTable)
        if (equalsIgnoreCase (entry.name, name))
            return entry.code;

    // "Scan_5E" 또는 "Scan_E0_5E"
    constexpr std::string_view prefix = "scan_";

    if (name.size() > prefix.size() && equalsIgnoreCase (name.substr (0, prefix.size()), prefix))
    {
        auto rest = name.substr (prefix.size());
        auto extended = false;

        if (rest.size() > 3 && equalsIgnoreCase (rest.substr (0, 3), "e0_"))
        {
            extended = true;
            rest = rest.substr (3);
        }

        if (rest.size() == 2)
        {
            const auto high = hexDigit (rest[0]);
            const auto low = hexDigit (rest[1]);

            if (high >= 0 && low >= 0)
            {
                const auto scan = static_cast<unsigned> (high * 16 + low);

                if (scan != 0)
                    return makeKeyCode (scan, extended);
            }
        }
    }

    return std::nullopt;
}

std::string keyLabel (KeyCode key)
{
    if (const auto* entry = findEntry (key))
        return std::string (entry->label);

    return fallbackName (key);
}

} // namespace hvd
