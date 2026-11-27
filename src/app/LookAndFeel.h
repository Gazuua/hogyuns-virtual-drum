#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace ui
{

/** 버튼의 종류. 모양과 색이 정해진다. 켜고 끄는 버튼은 켜진 동안 selection 바탕과 액센트 테두리로 그린다. */
enum class ButtonKind
{
    secondary,  // 보조(기본): 면으로 채운 버튼
    primary,    // 주요: 액센트로 채움. 화면마다 하나 (시작, 녹음, 재생)
    quiet,      // 조용한: 글자만, 올리면 덧칠 (폴더 열기, 새로 고침, 묶음 안의 작은 버튼)
    danger,     // 위험: 빨간 글자, 켜지면 빨간 바탕 (삭제, 뮤트, 녹음 중)
    tab,        // 화면을 고르는 탭 (테마의 tabs 모양)
    segment     // 보기 전환: 붙은 버튼 묶음(setConnectedEdges) 안에서 고른 칸을 채움
};

void setButtonKind (juce::Button&, ButtonKind);
ButtonKind getButtonKind (const juce::Button&);

/** 앱 전체의 그리기. 색은 지금 테마(ui::theme())에서 읽는다. */
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    /** 지금 테마의 색을 JUCE 부품들의 colour id에 옮긴다. */
    void applyTheme();

    //==============================================================================
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                      bool ticked, bool isEnabled, bool highlighted, bool down) override;

    //==============================================================================
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    //==============================================================================
    int getSliderThumbRadius (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip,
                     const juce::Rectangle<float>& body) override;

    //==============================================================================
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    //==============================================================================
    int getDefaultScrollbarWidth() override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    //==============================================================================
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    int getPopupMenuBorderSize() override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;

    //==============================================================================
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    int getAlertWindowButtonHeight() override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

    void drawTableHeaderBackground (juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn (juce::Graphics&, juce::TableHeaderComponent&, const juce::String& columnName, int columnId,
                                int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags) override;

private:
    void drawTab (juce::Graphics&, juce::Button&, bool highlighted, bool down);
    void drawSegment (juce::Graphics&, juce::Button&, bool highlighted, bool down);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LookAndFeel)
};

} // namespace ui
