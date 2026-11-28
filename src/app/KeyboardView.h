#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "core/KeyCode.h"

/** 화면에 그린 키보드. 키마다 지정된 동작을 보여 주고, 눌려 있는 키와 고른 키를 표시한다. */
class KeyboardView final : public juce::Component,
                           public juce::TooltipClient
{
public:
    struct Cap
    {
        juce::String text;       // 지정된 동작. 비어 있으면 지정되지 않은 키
        juce::String tip;        // 마우스를 올렸을 때 보일 글 (동작이 여럿이면 모두)
        juce::Colour colour;
        int count = 0;           // 이 키에 지정된 동작 수
    };

    KeyboardView();

    void setCaps (std::vector<Cap> newCaps);
    void setSelected (hvd::KeyCode key);
    hvd::KeyCode getSelected() const noexcept    { return selected; }

    /** 그림에 있는 키인지. */
    static bool hasKey (hvd::KeyCode key);

    /** 폭이 width일 때 그림 전체가 들어가는 높이. */
    static int heightForWidth (int width);

    /** 키가 없는 빈자리(가운데 줄 오른쪽, 이동 키 위). 화면이 작은 표시를 겹쳐 둘 때 쓴다. */
    juce::Rectangle<int> getSpareArea() const;

    /** 기능 키 줄 가운데 페달 스위치 왼쪽 (Esc ~ ScrLk). 화면이 잠깐 띠를 겹쳐 둘 때 쓴다. */
    juce::Rectangle<int> getFunctionRowArea() const;

    /** 눌린 키가 바뀌었으면 다시 그린다. */
    void refreshHeld();

    std::function<void (hvd::KeyCode)> onKeyClicked;
    std::function<bool (hvd::KeyCode)> isKeyHeld;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    struct Shape
    {
        hvd::KeyCode code;
        float x, y, w, h;
    };

    static const std::vector<Shape>& shapes();
    float unit() const;
    juce::Rectangle<float> boundsOf (float x, float y, float w, float h) const;
    juce::Rectangle<float> boundsOf (const Shape& shape) const    { return boundsOf (shape.x, shape.y, shape.w, shape.h); }
    const Shape* shapeAt (juce::Point<float> position) const;

    std::vector<Cap> caps;
    std::vector<bool> heldShown;
    hvd::KeyCode selected = hvd::invalidKey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardView)
};
