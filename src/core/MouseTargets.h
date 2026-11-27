#pragma once

#include <cstdint>
#include <vector>

namespace hvd
{

/** 화면에서 마우스로 칠 수 있는 자리 하나. 좌표는 모니터의 실제 화소(물리 좌표)다. */
struct MouseTarget
{
    enum class Shape : std::uint8_t { ellipse, rectangle };

    Shape shape = Shape::rectangle;
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;    // 테두리 사각형. 타원은 이 안에 꽉 차게 그린다
    std::uint16_t pad = 0;
    float velocity = 0.9f;
    bool followPedals = true;                 // 키로 친 것처럼 페달을 따른다
    int spot = -1;                            // 화면이 이 자리를 알아보는 번호 (번쩍일 자리를 고를 때 쓴다)
    std::vector<std::uint16_t> chokes;        // 오른쪽 단추로 잡으면 함께 멈출 패드들

    bool contains (float x, float y) const noexcept
    {
        if (x < left || x > right || y < top || y > bottom)
            return false;

        if (shape == Shape::rectangle)
            return true;

        const auto halfWidth = 0.5f * (right - left), halfHeight = 0.5f * (bottom - top);

        if (halfWidth <= 0.0f || halfHeight <= 0.0f)
            return false;

        const auto dx = (x - (left + halfWidth)) / halfWidth;
        const auto dy = (y - (top + halfHeight)) / halfHeight;
        return dx * dx + dy * dy <= 1.0f;
    }

    bool operator== (const MouseTarget&) const = default;
};

/** 화면 스레드가 만들어 입력 스레드에 넘기는, 지금 보이는 자리들. 한 번 넘긴 뒤에는 바꾸지 않는다. */
struct MouseTargets
{
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;    // 자리들을 그린 부품. 이 밖은 다른 부품의 자리다
    std::vector<MouseTarget> targets;         // 앞에 있는 것이 먼저다 (겹치면 앞의 것을 친다)

    const MouseTarget* find (float x, float y) const noexcept
    {
        if (x < left || x >= right || y < top || y >= bottom)
            return nullptr;

        for (const auto& target : targets)
            if (target.contains (x, y))
                return &target;

        return nullptr;
    }

    bool operator== (const MouseTargets&) const = default;
};

} // namespace hvd
