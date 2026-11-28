#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/KeyCode.h"

namespace hvd
{

/** Windows Raw Input으로 키보드를 직접 읽는 입력 스레드.

    화면 이벤트를 거치지 않으므로 한/영 입력기의 영향을 받지 않고, 좌우 Shift나
    숫자패드처럼 위치가 다른 키를 구분한다. Windows의 키 자동 반복은 걸러 낸다.

    풋스위치(페달)처럼 키보드로 인식되는 장치 하나를 "페달"로 등록할 수 있다. 그 장치에서 온 키는
    키보드의 같은 키와 섞이지 않게 Pedal1, Pedal2… 로 바꿔서 넘긴다.

    마우스 단추를 누른 것도 같은 스레드에서 받는다. 화면 스레드가 바쁠 때(그림을 그리는 중 등) 클릭이 밀리지 않게 하려는 것이다.
    Windows가 화면에 보내는 보통의 마우스 메시지는 그대로 간다.
*/
class RawKeyboard
{
public:
    /** 입력 스레드에서 불린다. down이 false면 키를 뗀 것이다. */
    using Handler = std::function<void (KeyCode key, bool down)>;

    enum class MouseButton { left, right };

    /** 입력 스레드에서 불린다. 등록한 창이 맨 앞에 있을 때만 불린다. 누른 자리가 그 창의 어디인지는 받는 쪽이 판단한다.
        x, y는 누른 자리의 화면 좌표(모니터의 실제 화소)다. 왼손잡이 설정으로 단추를 바꿨으면 바뀐 대로 넘긴다. */
    using MouseHandler = std::function<void (MouseButton button, int x, int y)>;

    /** 입력 장치를 가리키는 값. 장치의 제조사·제품 번호로 만들고, 0은 "알 수 없음"이다.
        같은 제품이면 어느 USB 단자에 꽂아도 같은 값이 나온다. */
    using DeviceId = std::uint64_t;

    struct Device
    {
        std::string identity;    // 예: "VID_046D&PID_C31C"
        DeviceId id = 0;
    };

    /** 페달을 등록하는 동안 밟은 것. */
    struct Learned
    {
        DeviceId device = 0;
        std::vector<KeyCode> keys;    // 스위치가 보낸 키. 밟은 순서대로
        bool cancelled = false;       // Esc를 눌렀다
    };

    RawKeyboard() = default;
    ~RawKeyboard();

    RawKeyboard (const RawKeyboard&) = delete;
    RawKeyboard& operator= (const RawKeyboard&) = delete;

    /** 입력 스레드를 시작한다. 키보드의 Raw Input 등록에 성공하면 true. mouseHandler를 주면 마우스 단추도 받는다. */
    bool start (Handler newHandler, MouseHandler newMouseHandler = {});
    void stop();

    /** 마우스 단추를 입력 스레드에서 받고 있는지. */
    bool isReadingMouse() const noexcept                        { return mouseRegistered.load (std::memory_order_relaxed); }

    /** 이 창이 맨 앞에 있을 때만 키 누름을 넘긴다. 값은 Windows 창 핸들(HWND)이다. */
    void setActiveWindow (void* nativeWindowHandle) noexcept    { activeWindow.store (nativeWindowHandle); }

    /** true이면 창 핸들을 아직 모를 때 키 누름을 넘기지 않는다. 다른 프로그램에 치는 키가
        드럼을 울리지 않게 하는 안전장치다. false이면 창 핸들이 없을 때 항상 넘긴다. */
    void setRequireActiveWindow (bool shouldRequire) noexcept   { requireActiveWindow.store (shouldRequire); }

    //==============================================================================
    // 페달. 아래 함수들은 화면 스레드에서 부른다.

    /** 검사용: 창의 클라이언트 영역 왼쪽 위의 화면 좌표(모니터의 실제 화소)와 그 창의 화면 배율(DPI / 96). 실패하면 false. */
    static bool clientOriginOf (void* nativeWindowHandle, int& x, int& y, double& scale);

    /** 검사용: 이 스레드에서 WindowFromPoint를 부르는 데 걸린 시간(ms). 입력 스레드가 클릭마다 부른다.
        그 자리의 창이 expectedWindow(의 자식)이면 foundExpected가 true다. x, y는 모니터의 실제 화소. */
    static double windowFromPointMs (int x, int y, void* expectedWindow, bool& foundExpected);

    /** 이 스레드가 지금 처리하는 메시지(마우스 클릭 등)가 생긴 뒤 흐른 시간(ms, 약 16ms 단위). 모르면 0.
        화면 스레드가 바빠 클릭을 늦게 받았을 때, 누른 때를 어림하는 데 쓴다. */
    static double currentMessageAgeMs() noexcept;

    /** 지금 꽂혀 있는 키보드 장치들 (풋스위치도 키보드로 잡힌다). */
    static std::vector<Device> listDevices();

    /** identity(Device::identity와 같은 꼴)가 가리키는 장치의 값. 비어 있으면 0. */
    static DeviceId deviceIdOf (std::string_view identity) noexcept;

    /** 페달로 다룰 장치와, 그 장치의 스위치들이 보내는 키. keys[i]가 Pedal(i+1)이 된다.
        device가 0이면 페달이 없는 것이다. */
    void setPedal (DeviceId device, std::span<const KeyCode> keys) noexcept;
    DeviceId getPedalDevice() const noexcept                    { return pedalDevice.load (std::memory_order_relaxed); }

    /** 페달 등록을 시작한다. 끝낼 때까지 누르는 키는 연주에 쓰지 않고 적어 둔다.
        처음 눌린 키의 장치가 페달이 되고, 그 장치에서 온 키만 차례로 적는다. Esc는 취소다. */
    void beginPedalLearning() noexcept;
    void endPedalLearning() noexcept                            { learning.store (false, std::memory_order_relaxed); }
    bool isPedalLearning() const noexcept                       { return learning.load (std::memory_order_relaxed); }
    Learned getPedalLearning() const;

    /** 장치가 누름과 뗌을 알려 온 시각들(밀리초, 시간순). 보고 간격을 어림하는 데 쓴다.
        fromPedal이 false이면 페달이 아닌 장치들(키보드)의 것이다. */
    std::vector<double> getReportTimes (bool fromPedal) const;

    //==============================================================================
    /** 키보드 메시지 하나를 처리한다. 입력 스레드가 부르며, 검사할 때는 직접 부를 수 있다.
        makeCode와 flags는 RAWKEYBOARD의 MakeCode, Flags 값이다. timeUs는 메시지를 받은 시각(마이크로초)이고
        음수이면 지금 시각을 쓴다. */
    void processRawKey (unsigned makeCode, unsigned flags, DeviceId device = 0, std::int64_t timeUs = -1);

    /** 입력 스레드가 부른다. 장치 핸들(HANDLE)로 장치를 알아내 processRawKey()에 넘긴다. */
    void processRawInput (void* deviceHandle, unsigned makeCode, unsigned flags);

    /** 입력 스레드가 부른다. buttonFlags는 RAWMOUSE의 usButtonFlags, x와 y는 그 메시지가 생긴 때의 커서 자리다. */
    void processRawMouse (unsigned buttonFlags, int x, int y);

    /** 마우스 옆 단추(뒤, 앞)를 키처럼 넘긴다 (mouseKey). 키와 같이 이 창이 맨 앞일 때만 누르고, 커서 자리는 보지 않는다.
        입력 스레드가 부르며, 검사할 때는 직접 부를 수 있다. buttonFlags는 RAWMOUSE의 usButtonFlags다. */
    void processMouseSideButtons (unsigned buttonFlags);

    /** 입력 스레드가 부른다. 장치가 꽂히거나 빠졌으니 핸들과 장치의 짝을 다시 알아내게 한다. */
    void forgetDeviceHandles() noexcept
    {
        numKnownHandles = 0;
        deviceChanges.fetch_add (1, std::memory_order_relaxed);
    }

    /** 장치가 꽂히거나 빠질 때마다 하나씩 는다. 화면 스레드가 페달 연결을 다시 확인할 때 쓴다. */
    std::uint32_t getDeviceChangeCount() const noexcept         { return deviceChanges.load (std::memory_order_relaxed); }

    /** 검사용: 스레드를 시작하지 않고 처리기만 지정한다. */
    void setHandlerForTesting (Handler newHandler)    { handler = std::move (newHandler); }
    void setMouseHandlerForTesting (MouseHandler newHandler)    { mouseHandler = std::move (newHandler); }

private:
    void threadMain();
    bool isInFront() const noexcept;
    void learn (KeyCode rawKey, DeviceId device) noexcept;
    void noteReport (bool fromPedal, std::int64_t timeUs) noexcept;

    Handler handler;
    MouseHandler mouseHandler;
    std::thread thread;
    std::atomic<unsigned long> threadId { 0 };
    std::atomic<int> startState { 0 };    // 0: 준비 중, 1: 등록 성공, -1: 등록 실패
    std::atomic<bool> mouseRegistered { false };
    std::atomic<void*> activeWindow { nullptr };
    std::atomic<bool> requireActiveWindow { false };
    std::array<bool, numKeyCodes> isDown {};

    // 페달을 등록하는 동안 눌린 키. 뗄 때까지 연주에 쓰지 않는다. 입력 스레드만 쓴다.
    std::array<bool, numKeyCodes> swallowed {};

    // 장치 핸들 → 장치. 입력 스레드만 쓴다.
    struct KnownHandle
    {
        void* handle = nullptr;
        DeviceId device = 0;
    };

    std::array<KnownHandle, 16> knownHandles {};
    int numKnownHandles = 0;
    int nextHandleSlot = 0;

    // 페달 (화면 스레드가 정하고 입력 스레드가 읽는다)
    std::atomic<DeviceId> pedalDevice { 0 };
    std::array<std::atomic<KeyCode>, maxPedalKeys> pedalSourceKeys {};
    std::atomic<bool> pedalChanged { false };
    std::atomic<std::uint32_t> deviceChanges { 0 };

    // 페달 등록 (입력 스레드가 적고 화면 스레드가 읽는다)
    std::atomic<bool> learning { false };
    std::atomic<bool> learnCancelled { false };
    std::atomic<DeviceId> learnDevice { 0 };
    std::atomic<int> learnCount { 0 };
    std::array<std::atomic<KeyCode>, maxPedalKeys> learnKeys {};

    // 보고 시각 기록. [0]은 키보드, [1]은 페달.
    struct ReportLog
    {
        static constexpr std::size_t size = 128;

        std::array<std::atomic<std::int64_t>, size> timesUs {};
        std::atomic<std::uint32_t> count { 0 };
    };

    std::array<ReportLog, 2> reportLogs;
};

} // namespace hvd
