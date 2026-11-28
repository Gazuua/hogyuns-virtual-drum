#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace ui
{

/** 정수 하나를 보여 주는 칸 ("120 BPM", "70%"). 위나 오른쪽으로 끌면 커지고, 두 번 누르면 숫자를 적는다.
    윗줄의 템포 칸과 반주 화면의 속도 올리기 설정이 쓴다. 키보드 포커스는 적는 동안에만 갖는다. */
class NumberField : public juce::Label
{
public:
    /** pixelsPerStep: 이만큼 끌 때마다 1씩 바뀐다. boxed이면 입력 칸처럼 바탕을 그린다. */
    NumberField (int minimum, int maximum, juce::String suffix, float pixelsPerStep = 4.0f, bool boxed = false);

    /** 보여 줄 값. 끄는 중이거나 적는 중에는 바꾸지 않는다. */
    void setValue (int newValue);
    int getValue() const noexcept    { return value; }

    /** 범위를 바꾼다. 지금 값이 벗어나면 안으로 넣는다 (알리지 않는다). */
    void setRange (int newMinimum, int newMaximum);

    /** 끌거나 적어서 값을 바꿨을 때 불린다. */
    std::function<void (int)> onValueChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

protected:
    juce::TextEditor* createEditorComponent() override;
    void editorShown (juce::TextEditor*) override;
    void textWasEdited() override;

private:
    void showValue();
    void change (int newValue);

    int minimum, maximum, value;
    juce::String suffix;
    float pixelsPerStep;
    bool boxed;
    int dragStartValue = 0;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NumberField)
};

} // namespace ui
