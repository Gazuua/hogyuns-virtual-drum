#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <span>

#include "Format.h"
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

    /** true이면 윗줄에서 템포, 탭, 메트로놈을 숨긴다 (리듬 게임을 하는 동안 채보의 BPM과 헷갈리지 않게). */
    virtual bool hidesTransport() const { return false; }

    /** 다른 화면으로 넘어간다 ("play", "track" 등 MainComponent의 화면 이름). MainComponent가 채운다. */
    std::function<void (const juce::String& pageName)> showPageNamed;
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

/** 꼬리표의 글이 지금 글꼴로 차지하는 폭 (테두리 여백은 빼고). 글 길이에 맞춰 자리를 줄 때 쓴다. */
inline int textWidth (const juce::Label& label)
{
    return juce::GlyphArrangement::getStringWidthInt (label.getFont(), label.getText());
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

/** 켜고 끄는 설정: 체크 상자. */
inline void setupToggle (juce::Component& parent, juce::ToggleButton& toggle, const juce::String& text,
                         std::function<void()> onClick, bool initial = false)
{
    toggle.setButtonText (text);
    toggle.setToggleState (initial, juce::dontSendNotification);
    toggle.onClick = std::move (onClick);
    noFocus (toggle);
    parent.addAndMakeVisible (toggle);
}

/** 보기 전환: 붙은 버튼 묶음. 하나를 누르면 onSelect (번호)가 불린다. 고른 칸은 setToggleState로 정한다. */
inline void setupSegmented (juce::Component& parent, std::span<juce::TextButton> buttons, const juce::StringArray& texts,
                            int radioGroup, std::function<void (int)> onSelect)
{
    const auto last = static_cast<int> (buttons.size()) - 1;

    for (int i = 0; i <= last; ++i)
    {
        auto& button = buttons[static_cast<std::size_t> (i)];
        setupButton (parent, button, texts[i], [onSelect, i] { if (onSelect) onSelect (i); });
        button.setClickingTogglesState (true);
        button.setRadioGroupId (radioGroup);
        button.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < last ? juce::Button::ConnectedOnRight : 0));
        setButtonKind (button, ButtonKind::segment);
    }
}

/** 한 줄 배치: 왼쪽에 항목 이름(captionWidth), 나머지에 조절 부품. area의 위에서 height만큼 떼어 쓴다. */
inline void placeLabelled (juce::Rectangle<int>& area, juce::Label& caption, juce::Component& control,
                           int captionWidth, int height = space::control, int gapBelow = space::tight)
{
    auto row = area.removeFromTop (height);
    caption.setBounds (row.removeFromLeft (captionWidth));
    control.setBounds (row);
    area.removeFromTop (gapBelow);
}

/** 오래 걸리는 일을 하는 동안 모래시계 커서를 보인다. */
struct WaitCursor
{
    WaitCursor()     { juce::MouseCursor::showWaitCursor(); }
    ~WaitCursor()    { juce::MouseCursor::hideWaitCursor(); }

    JUCE_DECLARE_NON_COPYABLE (WaitCursor)
};

/** 파일 고르기 창을 띄운다. 고르면(취소가 아니면) onChosen이 불린다. holder는 창이 떠 있는 동안 살아 있어야 한다. */
inline void chooseFile (std::unique_ptr<juce::FileChooser>& holder, const juce::String& title, const juce::File& start,
                        const juce::String& patterns, int flags, std::function<void (const juce::File&)> onChosen)
{
    holder = std::make_unique<juce::FileChooser> (title, start, patterns);
    holder->launchAsync (flags, [onChosen = std::move (onChosen)] (const juce::FileChooser& chooser)
    {
        if (const auto file = chooser.getResult(); file != juce::File() && onChosen)
            onChosen (file);
    });
}

/** chooseFile과 같고, 고른 파일을 모두 넘긴다 (여럿 고르기, 저장 창). */
inline void chooseFiles (std::unique_ptr<juce::FileChooser>& holder, const juce::String& title, const juce::File& start,
                         const juce::String& patterns, int flags, std::function<void (const juce::Array<juce::File>&)> onChosen)
{
    holder = std::make_unique<juce::FileChooser> (title, start, patterns);
    holder->launchAsync (flags, [onChosen = std::move (onChosen)] (const juce::FileChooser& chooser)
    {
        if (const auto files = chooser.getResults(); ! files.isEmpty() && files.getFirst() != juce::File() && onChosen != nullptr)
            onChosen (files);
    });
}

//==============================================================================
/** 글자 없이 작은 그림만 있는 조용한 버튼 (새로 고침, 폴더 열기). 무엇인지는 툴팁으로 알린다. */
class IconButton final : public juce::Button
{
public:
    enum class Icon
    {
        refresh,
        folder
    };

    IconButton (Icon iconToDraw, const juce::String& tooltip)
        : juce::Button (tooltip), icon (iconToDraw)
    {
        setTooltip (tooltip);
        noFocus (*this);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto& t = theme();
        const auto bounds = getLocalBounds().toFloat();

        if (isEnabled() && (highlighted || down))
        {
            g.setColour (down ? t.pressed : t.hover);
            g.fillRoundedRectangle (bounds, t.controlRadius);
        }

        const auto box = bounds.withSizeKeepingCentre (16.0f, 16.0f);
        juce::Path path;

        if (icon == Icon::refresh)
        {
            // 거의 한 바퀴 도는 화살표
            const auto c = box.getCentre();
            path.addCentredArc (c.x, c.y, 6.0f, 6.0f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.2f, true);
            const auto tip = c.getPointOnCircumference (6.0f, juce::MathConstants<float>::twoPi - 0.2f);
            path.startNewSubPath (tip.translated (-4.0f, -1.5f));
            path.lineTo (tip);
            path.lineTo (tip.translated (0.5f, -4.5f));
        }
        else
        {
            // 폴더: 탭이 달린 네모
            const auto r = box.reduced (0.5f, 2.0f);
            path.startNewSubPath (r.getX(), r.getBottom());
            path.lineTo (r.getX(), r.getY());
            path.lineTo (r.getX() + 5.0f, r.getY());
            path.lineTo (r.getX() + 7.0f, r.getY() + 2.0f);
            path.lineTo (r.getRight(), r.getY() + 2.0f);
            path.lineTo (r.getRight(), r.getBottom());
            path.closeSubPath();
        }

        g.setColour ((highlighted ? t.text : t.textMuted).withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f));
        g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    Icon icon;
};

//==============================================================================
/** 글 옆에 꺾쇠(⌄, ›)를 그리는 버튼.
    menu: 누르면 메뉴가 뜨는 버튼(꺾쇠는 늘 아래). disclosure: 아래 묶음을 펼치고 접는 버튼(펼쳐지면 아래, 접히면 오른쪽).
    펼침 상태는 setOpen()으로 정한다. 모양(보조, 조용한 등)은 setButtonKind를 따른다. */
class ChevronButton final : public juce::TextButton
{
public:
    enum class Style
    {
        menu,
        disclosure
    };

    explicit ChevronButton (Style styleToUse = Style::menu) : style (styleToUse) {}

    void setOpen (bool shouldBeOpen)    { if (open != shouldBeOpen) { open = shouldBeOpen; repaint(); } }
    bool isOpen() const noexcept        { return open; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& lf = getLookAndFeel();
        const auto& t = theme();

        lf.drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);

        const auto font = juce::Font (juce::FontOptions (juce::jmin (type::body, static_cast<float> (getHeight()) * 0.56f)));
        const auto textWidth = juce::GlyphArrangement::getStringWidth (font, getButtonText());
        constexpr float chevron = 8.0f, gap = 6.0f;

        auto area = getLocalBounds().toFloat().reduced (getButtonKind (*this) == ButtonKind::quiet ? 4.0f : 10.0f, 0.0f);
        const auto contentWidth = juce::jmin (area.getWidth(), textWidth + gap + chevron);
        auto content = style == Style::menu ? area.withSizeKeepingCentre (contentWidth, area.getHeight()) : area.withWidth (contentWidth);

        g.setColour ((getButtonKind (*this) == ButtonKind::quiet && ! highlighted ? t.textMuted : t.text).withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f));
        g.setFont (font);
        g.drawText (getButtonText(), content.removeFromLeft (contentWidth - gap - chevron), juce::Justification::centredLeft, true);

        const auto c = content.removeFromRight (chevron).getCentre();
        juce::Path path;

        if (style == Style::menu || open)
        {
            path.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
            path.lineTo (c.x, c.y + 2.0f);
            path.lineTo (c.x + 4.0f, c.y - 2.0f);
        }
        else
        {
            path.startNewSubPath (c.x - 2.0f, c.y - 4.0f);
            path.lineTo (c.x + 2.0f, c.y);
            path.lineTo (c.x - 2.0f, c.y + 4.0f);
        }

        g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    Style style;
    bool open = false;
};

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

/** 할 일을 물어본다. 버튼은 동작 이름(confirmText, 예: "삭제")과 "취소"다. 동작 버튼을 누르면 onConfirm이 불린다.
    dangerous이면(되돌리기 어려운 일) 경고 그림을 붙이고, 동작 버튼을 위험 모양(빨간 글자)으로 그리고, 엔터로는 실행하지 않는다. */
inline void confirm (juce::Component* parent, const juce::String& title, const juce::String& message,
                     const juce::String& confirmText, std::function<void()> onConfirm, bool dangerous = false)
{
    auto* window = new juce::AlertWindow (title, message, dangerous ? juce::MessageBoxIconType::WarningIcon
                                                                    : juce::MessageBoxIconType::QuestionIcon, parent);
    window->addButton (confirmText, 1, dangerous ? juce::KeyPress() : juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (ko ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    if (dangerous)
        if (auto* button = window->getButton (0))
            setButtonKind (*button, ButtonKind::danger);

    window->enterModalState (true,
                             juce::ModalCallbackFunction::create ([onConfirm = std::move (onConfirm)] (int result)
                             {
                                 if (result == 1)
                                     onConfirm();
                             }),
                             true);
}

} // namespace ui
