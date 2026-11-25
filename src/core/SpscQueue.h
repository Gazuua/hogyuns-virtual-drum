#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace hvd
{

/** 생산자 스레드 하나와 소비자 스레드 하나가 잠금 없이 쓰는 고정 크기 큐.

    Capacity는 2의 거듭제곱이어야 하고, 실제로 담을 수 있는 개수는 Capacity - 1 이다.
    push()는 생산자 스레드에서만, pop()은 소비자 스레드에서만 부른다.
*/
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    /** 꽉 차 있으면 false를 돌려주고 아무것도 넣지 않는다. */
    bool push (const T& item) noexcept
    {
        const auto tail = writeIndex.load (std::memory_order_relaxed);
        const auto next = (tail + 1) & mask;

        if (next == readIndex.load (std::memory_order_acquire))
            return false;

        items[tail] = item;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    /** 비어 있으면 false를 돌려준다. */
    bool pop (T& item) noexcept
    {
        const auto head = readIndex.load (std::memory_order_relaxed);

        if (head == writeIndex.load (std::memory_order_acquire))
            return false;

        item = items[head];
        readIndex.store ((head + 1) & mask, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t mask = Capacity - 1;

    std::array<T, Capacity> items {};
    std::atomic<std::size_t> readIndex { 0 };
    std::atomic<std::size_t> writeIndex { 0 };
};

} // namespace hvd
