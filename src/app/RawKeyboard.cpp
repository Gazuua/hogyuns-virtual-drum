#include "RawKeyboard.h"

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <iterator>

namespace hvd
{

namespace
{

constexpr wchar_t windowClassName[] = L"HvdRawKeyboardWindow";
constexpr KeyCode escapeKey = 0x01;

LRESULT CALLBACK windowProc (HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_INPUT)
    {
        if (auto* self = reinterpret_cast<RawKeyboard*> (GetWindowLongPtrW (window, GWLP_USERDATA)))
        {
            RAWINPUT input {};
            UINT size = sizeof (input);

            const auto copied = GetRawInputData (reinterpret_cast<HRAWINPUT> (lParam), RID_INPUT,
                                                 &input, &size, sizeof (RAWINPUTHEADER));

            if (copied != static_cast<UINT> (-1) && input.header.dwType == RIM_TYPEKEYBOARD)
            {
                self->processRawInput (input.header.hDevice, input.data.keyboard.MakeCode, input.data.keyboard.Flags);
            }
            else if (copied != static_cast<UINT> (-1) && input.header.dwType == RIM_TYPEMOUSE)
            {
                const auto buttons = static_cast<unsigned> (input.data.mouse.usButtonFlags);

                if ((buttons & (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN)) != 0)
                {
                    // 이 메시지가 생긴 때의 커서 자리. 움직임만 알리는 메시지는 지나간다.
                    const auto position = GetMessagePos();
                    self->processRawMouse (buttons, static_cast<short> (LOWORD (position)), static_cast<short> (HIWORD (position)));
                }

                if ((buttons & (RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP)) != 0)
                    self->processMouseSideButtons (buttons);
            }
        }
    }
    else if (message == WM_INPUT_DEVICE_CHANGE)
    {
        // 장치가 꽂히거나 빠졌다. 핸들이 다른 장치를 가리키게 될 수 있다.
        if (auto* self = reinterpret_cast<RawKeyboard*> (GetWindowLongPtrW (window, GWLP_USERDATA)))
            self->forgetDeviceHandles();
    }

    return DefWindowProcW (window, message, wParam, lParam);
}

/** 장치 이름에서 제조사·제품 번호를 뽑는다.

    이름은 "\\?\HID#VID_046D&PID_C31C&MI_00#8&1f0a2b3c&0&0000#{...}" 꼴이다. '#'으로 나눈 둘째 토막이 제품을,
    셋째 토막이 꽂힌 자리를 가리킨다. 자리는 다른 단자에 꽂으면 달라지므로 쓰지 않는다.
*/
std::string identityFromName (std::wstring_view name)
{
    std::string text;
    text.reserve (name.size());

    for (const auto c : name)
    {
        if (c >= L'a' && c <= L'z')
            text.push_back (static_cast<char> (c - L'a' + L'A'));
        else if (c > 0x20 && c < 0x7f)
            text.push_back (static_cast<char> (c));
        else
            text.push_back ('?');
    }

    if (const auto first = text.find ('#'); first != std::string::npos)
    {
        const auto second = text.find ('#', first + 1);
        text = text.substr (first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
    }

    // 인터페이스 번호(MI_00)와 컬렉션 번호(COL01)는 뗀다. 한 제품이 키보드 역할을 여럿 가지기도 한다.
    const auto vendor = text.find ("VID_");
    const auto product = text.find ("PID_");

    if (vendor != std::string::npos && product != std::string::npos
        && vendor + 8 <= text.size() && product + 8 <= text.size())
        return text.substr (vendor, 8) + "&" + text.substr (product, 8);

    if (text.size() > 80)
        text.resize (80);

    return text;
}

std::string identityOfHandle (HANDLE device)
{
    if (device == nullptr)
        return {};

    wchar_t name[512] {};
    UINT size = static_cast<UINT> (std::size (name));    // 글자 수

    const auto copied = GetRawInputDeviceInfoW (device, RIDI_DEVICENAME, name, &size);

    if (copied == static_cast<UINT> (-1) || copied == 0)
        return {};

    return identityFromName (std::wstring_view (name, wcsnlen (name, std::size (name))));
}

std::int64_t nowUs() noexcept
{
    return std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

RawKeyboard::~RawKeyboard()
{
    stop();
}

bool RawKeyboard::start (Handler newHandler, MouseHandler newMouseHandler)
{
    stop();

    handler = std::move (newHandler);
    mouseHandler = std::move (newMouseHandler);
    mouseRegistered.store (false);
    isDown.fill (false);
    swallowed.fill (false);
    numKnownHandles = 0;
    startState.store (0);
    thread = std::thread ([this] { threadMain(); });

    // 입력 스레드가 창을 만들고 등록을 마칠 때까지 잠깐 기다린다.
    for (int i = 0; i < 2000 && startState.load() == 0; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));

    return startState.load() == 1;
}

void RawKeyboard::stop()
{
    if (! thread.joinable())
        return;

    // 스레드 번호가 정해질 때까지 기다렸다가 종료 메시지를 보낸다.
    for (int i = 0; i < 2000 && threadId.load() == 0; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));

    while (PostThreadMessageW (threadId.load(), WM_QUIT, 0, 0) == FALSE)
    {
        // 메시지 큐가 아직 없으면 잠시 뒤 다시 보낸다.
        if (GetLastError() != ERROR_INVALID_THREAD_ID)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        else
            break;
    }

    thread.join();
    threadId.store (0);
}

void RawKeyboard::threadMain()
{
    const auto instance = GetModuleHandleW (nullptr);

    // 커서 자리를 모니터의 실제 화소로 받는다. (화면 쪽이 그 좌표로 자리를 넘겨준다)
    SetThreadDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 스레드의 메시지 큐를 먼저 만들어 둔다.
    MSG message {};
    PeekMessageW (&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    threadId.store (GetCurrentThreadId());

    WNDCLASSEXW windowClass {};
    windowClass.cbSize = sizeof (windowClass);
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = windowClassName;
    RegisterClassExW (&windowClass);

    // 화면에 보이지 않는 메시지 전용 창. 키보드 입력이 이 창으로 온다.
    const auto window = CreateWindowExW (0, windowClassName, L"", 0, 0, 0, 0, 0,
                                         HWND_MESSAGE, nullptr, instance, nullptr);
    auto registered = false;

    if (window != nullptr)
    {
        SetWindowLongPtrW (window, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (this));

        RAWINPUTDEVICE device {};
        device.usUsagePage = 0x01;           // 일반 데스크톱 장치
        device.usUsage = 0x06;               // 키보드
        // 포커스와 상관없이 받는다 (맨 앞 창인지는 직접 확인한다). 장치가 꽂히고 빠지는 것도 알려 달라고 한다.
        device.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
        device.hwndTarget = window;
        registered = RegisterRawInputDevices (&device, 1, sizeof (device)) != FALSE;

        // 마우스는 따로 등록한다. 실패해도 키보드는 쓴다 (마우스 클릭은 화면 스레드가 받는다).
        if (registered && mouseHandler)
        {
            RAWINPUTDEVICE mouse {};
            mouse.usUsagePage = 0x01;
            mouse.usUsage = 0x02;                // 마우스
            mouse.dwFlags = RIDEV_INPUTSINK;
            mouse.hwndTarget = window;
            mouseRegistered.store (RegisterRawInputDevices (&mouse, 1, sizeof (mouse)) != FALSE);
        }
    }

    // 키 입력이 다른 작업에 밀리지 않도록 우선순위를 올린다.
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    startState.store (registered ? 1 : -1);

    while (GetMessageW (&message, nullptr, 0, 0) > 0)
        DispatchMessageW (&message);

    if (window != nullptr)
    {
        RAWINPUTDEVICE device {};
        device.usUsagePage = 0x01;
        device.usUsage = 0x06;
        device.dwFlags = RIDEV_REMOVE;
        device.hwndTarget = nullptr;
        RegisterRawInputDevices (&device, 1, sizeof (device));

        if (mouseRegistered.exchange (false))
        {
            device.usUsage = 0x02;
            RegisterRawInputDevices (&device, 1, sizeof (device));
        }

        DestroyWindow (window);
    }

    UnregisterClassW (windowClassName, instance);
}

//==============================================================================
bool RawKeyboard::clientOriginOf (void* nativeWindowHandle, int& x, int& y, double& scale)
{
    auto* window = static_cast<HWND> (nativeWindowHandle);

    if (window == nullptr)
        return false;

    // 입력 스레드와 같은 좌표계(모니터의 실제 화소)로 묻는다.
    const auto previous = SetThreadDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT origin { 0, 0 };
    const auto ok = ClientToScreen (window, &origin) != FALSE;
    const auto dpi = GetDpiForWindow (window);
    SetThreadDpiAwarenessContext (previous);

    x = origin.x;
    y = origin.y;
    scale = dpi > 0 ? static_cast<double> (dpi) / 96.0 : 1.0;
    return ok && dpi > 0;
}

double RawKeyboard::windowFromPointMs (int x, int y, void* expectedWindow, bool& foundExpected)
{
    const auto previous = SetThreadDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto start = std::chrono::steady_clock::now();
    const auto under = WindowFromPoint (POINT { x, y });
    const auto elapsed = std::chrono::steady_clock::now() - start;
    SetThreadDpiAwarenessContext (previous);

    foundExpected = under != nullptr && GetAncestor (under, GA_ROOT) == static_cast<HWND> (expectedWindow);
    return std::chrono::duration<double, std::milli> (elapsed).count();
}

double RawKeyboard::currentMessageAgeMs() noexcept
{
    // GetMessageTime()은 이 스레드가 마지막으로 꺼낸 메시지가 생긴 시각이다 (GetTickCount와 같은 시계, 약 16ms 단위).
    const auto age = static_cast<std::int32_t> (GetTickCount() - static_cast<DWORD> (GetMessageTime()));
    return age > 0 && age < 60000 ? static_cast<double> (age) : 0.0;
}

std::vector<RawKeyboard::Device> RawKeyboard::listDevices()
{
    std::vector<Device> devices;
    UINT count = 0;

    if (GetRawInputDeviceList (nullptr, &count, sizeof (RAWINPUTDEVICELIST)) != 0 || count == 0)
        return devices;

    std::vector<RAWINPUTDEVICELIST> list (count);
    const auto copied = GetRawInputDeviceList (list.data(), &count, sizeof (RAWINPUTDEVICELIST));

    if (copied == static_cast<UINT> (-1))
        return devices;

    for (UINT i = 0; i < copied; ++i)
    {
        if (list[i].dwType != RIM_TYPEKEYBOARD)
            continue;

        auto identity = identityOfHandle (list[i].hDevice);
        const auto id = deviceIdOf (identity);

        // 한 제품이 키보드 역할을 여럿 가지면 한 번만 적는다.
        if (id != 0 && std::none_of (devices.begin(), devices.end(), [id] (const Device& device) { return device.id == id; }))
            devices.push_back ({ std::move (identity), id });
    }

    return devices;
}

RawKeyboard::DeviceId RawKeyboard::deviceIdOf (std::string_view identity) noexcept
{
    if (identity.empty())
        return 0;

    // FNV-1a
    DeviceId hash = 14695981039346656037ull;

    for (const auto c : identity)
    {
        hash ^= static_cast<unsigned char> (c);
        hash *= 1099511628211ull;
    }

    return hash != 0 ? hash : 1;
}

void RawKeyboard::setPedal (DeviceId device, std::span<const KeyCode> keys) noexcept
{
    // 장치를 먼저 지워 두고 키를 바꾼다. 입력 스레드가 반쯤 바뀐 표로 키를 바꾸지 않게 한다.
    pedalDevice.store (0, std::memory_order_relaxed);

    for (std::size_t i = 0; i < pedalSourceKeys.size(); ++i)
        pedalSourceKeys[i].store (i < keys.size() ? keys[i] : invalidKey, std::memory_order_relaxed);

    pedalDevice.store (device, std::memory_order_relaxed);

    // 눌린 채로 남은 페달 키는 입력 스레드가 다음 키를 받을 때 뗀다. 예전 페달의 보고 시각도 그때 버린다.
    pedalChanged.store (true, std::memory_order_relaxed);
}

void RawKeyboard::beginPedalLearning() noexcept
{
    learnDevice.store (0, std::memory_order_relaxed);
    learnCount.store (0, std::memory_order_relaxed);
    learnCancelled.store (false, std::memory_order_relaxed);
    learning.store (true, std::memory_order_relaxed);
}

RawKeyboard::Learned RawKeyboard::getPedalLearning() const
{
    Learned result;
    result.device = learnDevice.load (std::memory_order_relaxed);
    result.cancelled = learnCancelled.load (std::memory_order_relaxed);

    const auto count = std::min (learnCount.load (std::memory_order_relaxed), maxPedalKeys);

    for (int i = 0; i < count; ++i)
        result.keys.push_back (learnKeys[static_cast<std::size_t> (i)].load (std::memory_order_relaxed));

    return result;
}

std::vector<double> RawKeyboard::getReportTimes (bool fromPedal) const
{
    const auto& log = reportLogs[fromPedal ? 1 : 0];
    const auto count = log.count.load (std::memory_order_relaxed);
    const auto available = std::min (count, static_cast<std::uint32_t> (ReportLog::size));

    std::vector<double> times;
    times.reserve (available);

    for (auto i = count - available; i != count; ++i)
        times.push_back (static_cast<double> (log.timesUs[i % ReportLog::size].load (std::memory_order_relaxed)) / 1000.0);

    return times;
}

//==============================================================================
bool RawKeyboard::isInFront() const noexcept
{
    if (auto* window = activeWindow.load())
        return GetForegroundWindow() == static_cast<HWND> (window);

    return ! requireActiveWindow.load();
}

void RawKeyboard::learn (KeyCode rawKey, DeviceId device) noexcept
{
    if (rawKey == escapeKey)
    {
        learnCancelled.store (true, std::memory_order_relaxed);
        return;
    }

    // 어느 장치인지 알 수 없는 입력(다른 프로그램이 만들어 넣은 키 등)은 페달이 될 수 없다.
    if (device == 0)
        return;

    auto current = learnDevice.load (std::memory_order_relaxed);

    if (current == 0)
    {
        learnDevice.store (device, std::memory_order_relaxed);
        current = device;
    }

    if (current != device)
        return;

    const auto count = learnCount.load (std::memory_order_relaxed);

    for (int i = 0; i < count; ++i)
        if (learnKeys[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) == rawKey)
            return;

    if (count < maxPedalKeys)
    {
        learnKeys[static_cast<std::size_t> (count)].store (rawKey, std::memory_order_relaxed);
        learnCount.store (count + 1, std::memory_order_relaxed);
    }
}

void RawKeyboard::noteReport (bool fromPedal, std::int64_t timeUs) noexcept
{
    auto& log = reportLogs[fromPedal ? 1 : 0];
    const auto index = log.count.load (std::memory_order_relaxed);

    log.timesUs[index % ReportLog::size].store (timeUs >= 0 ? timeUs : nowUs(), std::memory_order_relaxed);
    log.count.store (index + 1, std::memory_order_relaxed);
}

void RawKeyboard::processRawInput (void* deviceHandle, unsigned makeCode, unsigned flags)
{
    DeviceId device = 0;

    if (deviceHandle != nullptr)
    {
        auto known = false;

        for (int i = 0; i < numKnownHandles && ! known; ++i)
        {
            if (knownHandles[static_cast<std::size_t> (i)].handle == deviceHandle)
            {
                device = knownHandles[static_cast<std::size_t> (i)].device;
                known = true;
            }
        }

        if (! known)
        {
            device = deviceIdOf (identityOfHandle (static_cast<HANDLE> (deviceHandle)));

            // 알아내지 못한 장치는 적어 두지 않고 다음에 다시 물어본다.
            if (device != 0)
            {
                const auto capacity = static_cast<int> (knownHandles.size());
                const auto slot = numKnownHandles < capacity ? numKnownHandles++ : (nextHandleSlot++ % capacity);
                knownHandles[static_cast<std::size_t> (slot)] = { deviceHandle, device };
            }
        }
    }

    processRawKey (makeCode, flags, device);
}

void RawKeyboard::processRawMouse (unsigned buttonFlags, int x, int y)
{
    // 맨 앞 창이 등록한 창일 때만 넘긴다. 누른 자리가 연주 자리인지는 처리기가 화면이 넘긴 자리 목록으로 본다.
    // 우리 팝업이나 대화 상자가 떠 있으면 화면 스레드가 자리 목록을 비워 둔다 (PlayPage::publishMouseTargets).
    if (! mouseHandler || ! isInFront())
        return;

    // 다른 프로그램의 "항상 위" 창이 드럼 위에 겹쳐 있으면 그 창을 누른 것이다.
    // WindowFromPoint는 다른 스레드의 창에 메시지를 보내지 않아 화면 스레드가 바빠도 기다리지 않는다 (--mousecheck로 잼).
    if (auto* window = activeWindow.load())
        if (const auto under = WindowFromPoint (POINT { x, y }); under == nullptr || GetAncestor (under, GA_ROOT) != static_cast<HWND> (window))
            return;

    // 왼손잡이 설정에서는 오른쪽 단추가 주 단추다. Raw Input은 실제 단추를 알려 준다.
    const auto swapped = GetSystemMetrics (SM_SWAPBUTTON) != 0;

    if ((buttonFlags & RI_MOUSE_LEFT_BUTTON_DOWN) != 0)
        mouseHandler (swapped ? MouseButton::right : MouseButton::left, x, y);

    if ((buttonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0)
        mouseHandler (swapped ? MouseButton::left : MouseButton::right, x, y);
}

void RawKeyboard::processMouseSideButtons (unsigned buttonFlags)
{
    // 뒤 단추(XBUTTON1)와 앞 단추(XBUTTON2). 뗌을 먼저 본다 (한 메시지에 둘 다 올 수 있다).
    constexpr struct { unsigned downFlag, upFlag; } buttons[numMouseKeys] = {
        { RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP },
        { RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP },
    };

    for (int i = 0; i < numMouseKeys; ++i)
    {
        const auto key = mouseKey (i);
        auto& down = isDown[key];

        if ((buttonFlags & buttons[i].upFlag) != 0 && down)
        {
            down = false;

            if (handler)
                handler (key, false);
        }

        // 다른 프로그램이 맨 앞에 있거나 페달을 등록하는 중이면 누르지 않는다.
        if ((buttonFlags & buttons[i].downFlag) != 0 && ! down && isInFront() && ! learning.load (std::memory_order_relaxed))
        {
            down = true;

            if (handler)
                handler (key, true);
        }
    }
}

void RawKeyboard::processRawKey (unsigned makeCode, unsigned flags, DeviceId device, std::int64_t timeUs)
{
    const auto extended = (flags & RI_KEY_E0) != 0;
    const auto released = (flags & RI_KEY_BREAK) != 0;

    if (makeCode == 0 || makeCode == KEYBOARD_OVERRUN_MAKE_CODE)
        return;

    // Pause 키가 보내는 E1 시퀀스는 쓰지 않는다.
    if ((flags & RI_KEY_E1) != 0)
        return;

    // PrintScreen이나 방향키와 함께 오는 가짜 Shift는 버린다.
    if (extended && (makeCode == 0x2a || makeCode == 0x36))
        return;

    // 페달이 바뀌었으면, 예전 페달에서 눌린 채로 남은 키를 뗀다. (그 키의 뗌은 이제 다른 번호로 온다)
    if (pedalChanged.exchange (false, std::memory_order_relaxed))
    {
        for (int i = 0; i < maxPedalKeys; ++i)
        {
            if (auto& pedalDown = isDown[pedalKey (i)]; pedalDown)
            {
                pedalDown = false;

                if (handler)
                    handler (pedalKey (i), false);
            }
        }

        reportLogs[1].count.store (0, std::memory_order_relaxed);
    }

    const auto rawKey = makeKeyCode (makeCode, extended);

    // 페달을 등록하면서 누른 키는 뗄 때까지 아무 일도 하지 않는다.
    if (swallowed[rawKey])
    {
        if (released)
            swallowed[rawKey] = false;

        return;
    }

    // 페달로 등록한 장치의 스위치는 키보드의 같은 키와 섞이지 않게 페달 키로 바꾼다.
    const auto fromPedal = device != 0 && device == pedalDevice.load (std::memory_order_relaxed);
    auto key = rawKey;

    if (fromPedal)
    {
        for (int i = 0; i < maxPedalKeys; ++i)
        {
            if (pedalSourceKeys[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) == rawKey)
            {
                key = pedalKey (i);
                break;
            }
        }
    }

    auto& down = isDown[key];

    if (released)
    {
        if (! down)
            return;

        down = false;
        noteReport (fromPedal, timeUs);

        if (handler)
            handler (key, false);

        return;
    }

    // 이미 눌린 키가 또 오면 Windows의 자동 반복이다.
    if (down)
        return;

    // 다른 프로그램이 맨 앞에 있을 때는 치지 않는다.
    if (! isInFront())
        return;

    if (learning.load (std::memory_order_relaxed))
    {
        swallowed[rawKey] = true;
        learn (rawKey, device);
        return;
    }

    down = true;
    noteReport (fromPedal, timeUs);

    if (handler)
        handler (key, true);
}

} // namespace hvd
