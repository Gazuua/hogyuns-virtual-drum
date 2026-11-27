#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "LookAndFeel.h"
#include "Text.h"
#include "Theme.h"

/** 화면 여러 곳에서 함께 쓰는 작은 도우미들. 색과 크기는 Theme.h의 토큰을 쓴다. */
namespace ui
{

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

/** 글 꼬리표의 크기, 굵기, 색을 쓰임에 맞춘다. */
inline void setTextStyle (juce::Label& label, Text style)
{
    const auto& t = theme();
    auto size = type::body;
    auto bold = false;
    auto colour = t.text;

    switch (style)
    {
        case Text::title:      size = type::title;      bold = true;    break;
        case Text::section:    size = type::section;    bold = true;    break;
        case Text::heading:    size = type::heading;    bold = true;    break;
        case Text::body:       break;
        case Text::detail:     size = type::caption;                    break;
        case Text::caption:    size = type::caption;    colour = t.textMuted;    break;
        case Text::hint:       colour = t.textMuted;                    break;
        case Text::small:      size = type::small;      colour = t.textMuted;    break;
        case Text::warning:    size = type::caption;    colour = t.warning;      break;
    }

    label.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    label.setColour (juce::Label::textColourId, colour);
}

inline void setupLabel (juce::Component& parent, juce::Label& label, const juce::String& text, Text style = Text::body)
{
    label.setText (text, juce::dontSendNotification);
    setTextStyle (label, style);
    label.setMinimumHorizontalScale (0.8f);
    parent.addAndMakeVisible (label);
}

/** 큰 칸(패널): 면으로 채운 둥근 네모. 테두리는 없다. */
inline void fillPanel (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (theme().surface);
    g.fillRoundedRectangle (area, theme().panelRadius);
}

inline void fillPanel (juce::Graphics& g, juce::Rectangle<int> area)    { fillPanel (g, area.toFloat()); }

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
