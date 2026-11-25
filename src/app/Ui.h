#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "Text.h"

/** 화면 여러 곳에서 함께 쓰는 색과 작은 도우미들. */
namespace ui
{

inline const juce::Colour quietText { 0xff9fb0bd };
inline const juce::Colour warningText { 0xffffcc80 };
inline const juce::Colour accent { 0xffffb74d };
inline const juce::Colour panel { 0xff222b33 };
inline const juce::Colour panelEdge { 0xff3d4a55 };
inline const juce::Colour danger { 0xffe57373 };
inline const juce::Colour good { 0xff81c784 };

/** 화면 한 장. 탭으로 바꿔 가며 보여 준다. */
class Page : public juce::Component
{
public:
    /** 보이는 동안 1초에 60번쯤 불린다. */
    virtual void tick() {}

    /** 탭을 눌러 이 화면이 보이게 됐을 때 불린다. */
    virtual void pageShown() {}

    /** 다른 화면으로 넘어가 이 화면이 가려질 때 불린다. */
    virtual void pageHidden() {}

    /** Esc를 눌렀을 때 불린다. 이 화면이 처리했으면 true를 돌려준다. */
    virtual bool escapePressed() { return false; }
};

/** 연주 중에 스페이스나 엔터가 조작 부품을 건드리지 않도록 키보드 포커스를 갖지 않게 한다. */
inline void noFocus (juce::Component& component)
{
    component.setWantsKeyboardFocus (false);
    component.setMouseClickGrabsKeyboardFocus (false);
}

inline void setupButton (juce::Component& parent, juce::Button& button, const juce::String& text, std::function<void()> onClick)
{
    button.setButtonText (text);
    button.onClick = std::move (onClick);
    noFocus (button);
    parent.addAndMakeVisible (button);
}

inline void setupLabel (juce::Component& parent, juce::Label& label, const juce::String& text,
                        float fontSize = 14.0f, juce::Colour colour = juce::Colours::white, bool bold = false)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (fontSize, bold ? juce::Font::bold : juce::Font::plain));
    label.setColour (juce::Label::textColourId, colour);
    label.setMinimumHorizontalScale (0.8f);
    parent.addAndMakeVisible (label);
}

/** 가로 슬라이더: 오른쪽에 값 상자, 더블 클릭하면 기본값으로 돌아간다. */
inline void setupSlider (juce::Component& parent, juce::Slider& slider, double minimum, double maximum, double step,
                         double defaultValue, const juce::String& suffix = {}, int decimals = 2)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    slider.setRange (minimum, maximum, step);
    slider.setNumDecimalPlacesToDisplay (decimals);
    slider.setTextValueSuffix (suffix);
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setValue (defaultValue, juce::dontSendNotification);
    slider.setScrollWheelEnabled (false);    // 화면을 굴리다가 값이 바뀌는 일을 막는다 (값은 바로 저장되므로)
    noFocus (slider);
    parent.addAndMakeVisible (slider);
}

/** 작은 돌림 손잡이. */
inline void setupKnob (juce::Component& parent, juce::Slider& slider, double minimum, double maximum, double step, double defaultValue)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRange (minimum, maximum, step);
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setValue (defaultValue, juce::dontSendNotification);
    slider.setPopupDisplayEnabled (true, true, nullptr);
    slider.setScrollWheelEnabled (false);
    noFocus (slider);
    parent.addAndMakeVisible (slider);
}

inline void setupCombo (juce::Component& parent, juce::ComboBox& combo, std::function<void()> onChange)
{
    combo.onChange = std::move (onChange);
    noFocus (combo);
    parent.addAndMakeVisible (combo);
}

/** 패드 id로 악기 종류를 짐작해 색을 고른다. */
inline juce::Colour padColour (const juce::String& padId)
{
    const auto id = padId.toLowerCase();

    if (id.startsWith ("kick") || id.startsWith ("bass"))                  return juce::Colour (0xffe57373);
    if (id.startsWith ("snare") || id.startsWith ("sidestick"))            return juce::Colour (0xffffb74d);
    if (id.startsWith ("hihat") || id.startsWith ("hat"))                  return juce::Colour (0xff4dd0e1);
    if (id.startsWith ("tom"))                                             return juce::Colour (0xff81c784);
    if (id.startsWith ("ride"))                                            return juce::Colour (0xff64b5f6);
    if (id.startsWith ("crash") || id.startsWith ("china")
        || id.startsWith ("splash") || id.startsWith ("cymbal"))           return juce::Colour (0xffba68c8);

    return juce::Colour (0xff90a4ae);
}

inline bool isCymbalPad (const juce::String& padId)
{
    const auto id = padId.toLowerCase();
    return id.startsWith ("hihat") || id.startsWith ("hat") || id.startsWith ("ride") || id.startsWith ("crash")
        || id.startsWith ("china") || id.startsWith ("splash") || id.startsWith ("cymbal");
}

/** 초를 "1:23.4" 꼴로 적는다. */
inline juce::String formatTime (double seconds)
{
    seconds = juce::jmax (0.0, seconds);
    const auto minutes = static_cast<int> (seconds / 60.0);
    const auto rest = seconds - minutes * 60.0;
    return juce::String (minutes) + ":" + juce::String (rest, 1).paddedLeft ('0', 4);
}

/** 소리 파일로 읽을 수 있는 확장자인지. */
inline bool isAudioFile (const juce::File& file)
{
    return file.hasFileExtension ("wav;flac;mp3;ogg;aif;aiff;wma");
}

inline const char* audioFilePatterns()    { return "*.wav;*.flac;*.mp3;*.ogg;*.aif;*.aiff"; }

//==============================================================================
inline void showMessage (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, title, text, ko ("확인"));
}

/** 글 한 줄을 물어본다. 확인을 누르면 onOk가 불린다. */
inline void promptForText (juce::Component* parent, const juce::String& title, const juce::String& message,
                           const juce::String& initial, std::function<void (juce::String)> onOk)
{
    auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, parent);
    window->addTextEditor ("text", initial);
    window->addButton (ko ("확인"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (ko ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create ([window, onOk = std::move (onOk)] (int result)
                             {
                                 if (result == 1)
                                     onOk (window->getTextEditorContents ("text"));
                             }),
                             true);
}

/** 예/아니오를 물어본다. 예를 누르면 onYes가 불린다. */
inline void confirm (juce::Component* parent, const juce::String& title, const juce::String& message, std::function<void()> onYes)
{
    auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon, parent);
    window->addButton (ko ("예"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (ko ("아니오"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create ([onYes = std::move (onYes)] (int result)
                             {
                                 if (result == 1)
                                     onYes();
                             }),
                             true);
}

} // namespace ui
