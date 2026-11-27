#include "LookAndFeel.h"

namespace ui
{

namespace
{

const juce::Identifier kindProperty { "uiButtonKind" };

juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

/** 붙어 있는 쪽은 모서리를 깎지 않는 둥근 네모. 붙은 버튼 사이에는 1px 틈을 둔다. */
juce::Path controlShape (juce::Rectangle<float> bounds, float radius, bool connectedLeft, bool connectedRight)
{
    if (connectedRight)
        bounds.removeFromRight (1.0f);

    radius = juce::jmin (radius, bounds.getHeight() * 0.5f, bounds.getWidth() * 0.5f);

    juce::Path path;
    path.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius, radius,
                              ! connectedLeft, ! connectedRight, ! connectedLeft, ! connectedRight);
    return path;
}

void strokeInside (juce::Graphics& g, const juce::Path& shape, juce::Colour colour, float thickness = 1.0f)
{
    if (colour.isTransparent())
        return;

    g.saveState();
    g.reduceClipRegion (shape);
    g.setColour (colour);
    g.strokePath (shape, juce::PathStrokeType (thickness * 2.0f));
    g.restoreState();
}

/** 버튼의 바탕색과 글자색. */
struct ButtonColours
{
    juce::Colour fill, text, outline;
};

ButtonColours coloursFor (const juce::Button& button)
{
    const auto& t = theme();
    const auto on = button.getToggleState();
    const auto kind = getButtonKind (button);

    // 화면이 직접 정해 둔 색(믹서의 악기 색 등)이 있으면 따른다.
    if (on && button.isColourSpecified (juce::TextButton::buttonOnColourId))
    {
        const auto fill = button.findColour (juce::TextButton::buttonOnColourId);
        return { fill, t.textOn (fill), juce::Colours::transparentBlack };
    }

    if (! on && button.isColourSpecified (juce::TextButton::buttonColourId))
    {
        const auto fill = button.findColour (juce::TextButton::buttonColourId);
        return { fill, t.textOn (fill), juce::Colours::transparentBlack };
    }

    // 켜진 토글: 액센트가 섞인 바탕에 액센트 테두리
    const ButtonColours selected { t.selection, t.text, t.accent.withAlpha (t.dark ? 0.75f : 0.6f) };

    switch (kind)
    {
        case ButtonKind::primary:
            // 쓸 수 없는 동안에는 보조 버튼처럼 그려 눈길을 끌지 않게 한다.
            if (! button.isEnabled())
                return { t.surfaceRaised, t.text, t.controlOutline };

            return { t.accent, t.accentText, juce::Colours::transparentBlack };

        case ButtonKind::danger:
            if (on)
                return { t.danger, t.textOn (t.danger), juce::Colours::transparentBlack };

            return { t.surfaceRaised, t.danger, t.controlOutline };

        case ButtonKind::quiet:
            return on ? selected : ButtonColours { juce::Colours::transparentBlack, t.text, juce::Colours::transparentBlack };

        case ButtonKind::secondary:
        case ButtonKind::tab:
        case ButtonKind::segment:
            break;
    }

    if (on)
        return selected;

    return { t.surfaceRaised, t.text, t.controlOutline };
}

} // namespace

//==============================================================================
void setButtonKind (juce::Button& button, ButtonKind kind)
{
    button.getProperties().set (kindProperty, static_cast<int> (kind));
    button.repaint();
}

ButtonKind getButtonKind (const juce::Button& button)
{
    return static_cast<ButtonKind> (static_cast<int> (button.getProperties().getWithDefault (kindProperty, 0)));
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    // 한글이 깨지지 않도록 기본 글꼴을 맑은 고딕으로 둔다.
    setDefaultSansSerifTypefaceName ("Malgun Gothic");
    applyTheme();
}

void LookAndFeel::applyTheme()
{
    const auto& t = theme();

    setColourScheme ({ t.background, t.surface, t.menu,
                       t.border, t.text, t.accent,
                       t.text, t.selection, t.text });

    const auto set = [this] (int id, juce::Colour colour) { setColour (id, colour); };
    const auto none = juce::Colours::transparentBlack;

    set (juce::ResizableWindow::backgroundColourId, t.background);
    set (juce::DocumentWindow::textColourId, t.text);

    set (juce::Label::textColourId, t.text);
    set (juce::Label::backgroundColourId, none);
    set (juce::Label::outlineColourId, none);
    set (juce::Label::textWhenEditingColourId, t.text);
    set (juce::Label::backgroundWhenEditingColourId, t.surfaceRaised);
    set (juce::Label::outlineWhenEditingColourId, t.accent);

    set (juce::TextButton::buttonColourId, t.surfaceRaised);
    set (juce::TextButton::buttonOnColourId, t.selection);
    set (juce::TextButton::textColourOffId, t.text);
    set (juce::TextButton::textColourOnId, t.text);

    set (juce::ToggleButton::textColourId, t.text);
    set (juce::ToggleButton::tickColourId, t.accentText);
    set (juce::ToggleButton::tickDisabledColourId, t.textFaint);

    set (juce::ComboBox::backgroundColourId, t.surfaceRaised);
    set (juce::ComboBox::buttonColourId, t.surfaceRaised);
    set (juce::ComboBox::textColourId, t.text);
    set (juce::ComboBox::outlineColourId, t.controlOutline);
    set (juce::ComboBox::arrowColourId, t.textMuted);
    set (juce::ComboBox::focusedOutlineColourId, t.accent);

    // 메뉴는 모서리를 둥글게 그리도록 아주 조금 투명하게 둔다 (불투명하면 창이 네모로 잡힌다).
    set (juce::PopupMenu::backgroundColourId, t.menu.withAlpha (0.995f));
    set (juce::PopupMenu::textColourId, t.text);
    set (juce::PopupMenu::headerTextColourId, t.textMuted);
    set (juce::PopupMenu::highlightedBackgroundColourId, t.selection);
    set (juce::PopupMenu::highlightedTextColourId, t.text);

    set (juce::Slider::backgroundColourId, t.track);
    set (juce::Slider::trackColourId, t.accent);
    set (juce::Slider::thumbColourId, t.dark ? t.text : juce::Colours::white);
    set (juce::Slider::rotarySliderFillColourId, t.accent);
    set (juce::Slider::rotarySliderOutlineColourId, t.track);
    set (juce::Slider::textBoxTextColourId, t.text);
    set (juce::Slider::textBoxBackgroundColourId, t.surfaceRaised);
    set (juce::Slider::textBoxHighlightColourId, t.accent.withAlpha (0.35f));
    set (juce::Slider::textBoxOutlineColourId, none);

    set (juce::TextEditor::backgroundColourId, t.surfaceRaised);
    set (juce::TextEditor::textColourId, t.text);
    set (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.35f));
    set (juce::TextEditor::highlightedTextColourId, t.text);
    set (juce::TextEditor::outlineColourId, t.controlOutline);
    set (juce::TextEditor::focusedOutlineColourId, t.accent);
    set (juce::TextEditor::shadowColourId, none);
    set (juce::CaretComponent::caretColourId, t.accent);

    set (juce::ScrollBar::backgroundColourId, none);
    set (juce::ScrollBar::trackColourId, none);
    set (juce::ScrollBar::thumbColourId, t.textFaint);

    set (juce::ListBox::backgroundColourId, t.surface);
    set (juce::ListBox::outlineColourId, none);
    set (juce::ListBox::textColourId, t.text);

    set (juce::TableHeaderComponent::backgroundColourId, t.surface);
    set (juce::TableHeaderComponent::textColourId, t.textMuted);
    set (juce::TableHeaderComponent::outlineColourId, t.border);
    set (juce::TableHeaderComponent::highlightColourId, t.hover);

    set (juce::AlertWindow::backgroundColourId, t.surface);
    set (juce::AlertWindow::textColourId, t.text);
    set (juce::AlertWindow::outlineColourId, t.border);

    set (juce::TooltipWindow::backgroundColourId, t.menu);
    set (juce::TooltipWindow::textColourId, t.text);
    set (juce::TooltipWindow::outlineColourId, t.menu);

    set (juce::BubbleComponent::backgroundColourId, t.menu);
    set (juce::BubbleComponent::outlineColourId, t.border);

    set (juce::GroupComponent::outlineColourId, t.border);
    set (juce::GroupComponent::textColourId, t.textMuted);

    set (juce::ProgressBar::backgroundColourId, t.surfaceSunken);
    set (juce::ProgressBar::foregroundColourId, t.accent);
}

//==============================================================================
juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const auto kind = getButtonKind (button);
    const auto selected = (kind == ButtonKind::tab || kind == ButtonKind::segment) && button.getToggleState();

    // −, + 같은 기호 한 글자는 조금 크게
    if (button.getButtonText().length() == 1 && ! juce::CharacterFunctions::isLetterOrDigit (button.getButtonText()[0]))
        return font (juce::jmin (18.0f, static_cast<float> (buttonHeight) * 0.68f));

    return font (juce::jmin (type::body, static_cast<float> (buttonHeight) * 0.56f), selected);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                        bool highlighted, bool down)
{
    if (getButtonKind (button) == ButtonKind::tab)
    {
        drawTab (g, button, highlighted, down);
        return;
    }

    if (getButtonKind (button) == ButtonKind::segment)
    {
        drawSegment (g, button, highlighted, down);
        return;
    }

    const auto& t = theme();
    const auto look = coloursFor (button);
    const auto shape = controlShape (button.getLocalBounds().toFloat(), t.controlRadius,
                                     button.isConnectedOnLeft(), button.isConnectedOnRight());
    const auto alpha = button.isEnabled() ? 1.0f : 0.45f;

    g.setColour (look.fill.withMultipliedAlpha (alpha));
    g.fillPath (shape);

    if (button.isEnabled() && (highlighted || down))
    {
        g.setColour (down ? t.pressed : t.hover);
        g.fillPath (shape);
    }

    strokeInside (g, shape, look.outline.withMultipliedAlpha (alpha));
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool)
{
    const auto& t = theme();
    auto colour = coloursFor (button).text;

    if (const auto kind = getButtonKind (button); kind == ButtonKind::tab || kind == ButtonKind::segment)
    {
        const auto on = button.getToggleState();

        if (kind == ButtonKind::segment || t.tabs == TabStyle::segment)
            colour = on ? t.tabSelectedText : (highlighted ? t.text : t.textMuted);
        else
            colour = on || highlighted ? t.text : t.textMuted;
    }

    // 화면이 글자색을 따로 정해 두었으면 따른다.
    const auto textId = button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId;

    if (button.isColourSpecified (textId))
        colour = button.findColour (textId);

    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (colour.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));

    const auto tabLike = getButtonKind (button) == ButtonKind::tab || getButtonKind (button) == ButtonKind::segment;
    const auto area = button.getLocalBounds().reduced (tabLike ? 4 : juce::jmin (8, button.getWidth() / 8), 2);
    g.drawFittedText (button.getButtonText(), area, juce::Justification::centred, 2, 0.85f);
}

void LookAndFeel::drawTab (juce::Graphics& g, juce::Button& button, bool highlighted, bool down)
{
    const auto& t = theme();
    const auto bounds = button.getLocalBounds().toFloat();
    const auto on = button.getToggleState();

    if (t.tabs == TabStyle::segment)
    {
        // 묶음의 바탕(tabTrack)은 탭들을 담은 쪽이 그린다. 여기서는 고른 칸만 채운다.
        const auto pill = bounds.reduced (3.0f);
        const auto radius = juce::jmax (2.0f, t.controlRadius - 2.0f);

        if (on)
        {
            if (! t.dark)
            {
                g.setColour (t.shadow);
                g.fillRoundedRectangle (pill.translated (0.0f, 1.0f), radius);
            }

            g.setColour (t.tabSelectedFill);
            g.fillRoundedRectangle (pill, radius);
        }
        else if (highlighted || down)
        {
            g.setColour (down ? t.pressed : t.hover);
            g.fillRoundedRectangle (pill, radius);
        }

        return;
    }

    // 밑줄 탭: 고른 탭 아래에 액센트 줄
    if (! on && (highlighted || down))
    {
        g.setColour (down ? t.pressed : t.hover);
        g.fillRoundedRectangle (bounds.reduced (2.0f, 4.0f), t.controlRadius);
    }

    if (on)
    {
        const auto textWidth = juce::GlyphArrangement::getStringWidth (getTextButtonFont (dynamic_cast<juce::TextButton&> (button), button.getHeight()),
                                                                       button.getButtonText());
        const auto lineWidth = juce::jmin (bounds.getWidth() - 8.0f, textWidth + 12.0f);
        g.setColour (t.accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (lineWidth, 2.5f).withCentre ({ bounds.getCentreX(), bounds.getBottom() - 1.5f }), 1.25f);
    }
}

void LookAndFeel::drawSegment (juce::Graphics& g, juce::Button& button, bool highlighted, bool down)
{
    const auto& t = theme();
    const auto bounds = button.getLocalBounds().toFloat();
    const auto left = button.isConnectedOnLeft(), right = button.isConnectedOnRight();
    const auto radius = t.controlRadius + 1.0f;

    // 묶음의 바탕: 붙은 쪽은 모서리를 깎지 않고 틈 없이 잇는다.
    juce::Path track;
    track.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius, radius,
                               ! left, ! right, ! left, ! right);
    g.setColour (t.tabTrack);
    g.fillPath (track);

    const auto pill = bounds.reduced (3.0f);
    const auto pillRadius = juce::jmax (2.0f, radius - 3.0f);

    if (button.getToggleState())
    {
        if (! t.dark)
        {
            g.setColour (t.shadow);
            g.fillRoundedRectangle (pill.translated (0.0f, 1.0f), pillRadius);
        }

        g.setColour (t.tabSelectedFill.withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
        g.fillRoundedRectangle (pill, pillRadius);
    }
    else if (button.isEnabled() && (highlighted || down))
    {
        g.setColour (down ? t.pressed : t.hover);
        g.fillRoundedRectangle (pill, pillRadius);
    }
}

//==============================================================================
void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    const auto fontSize = juce::jmin (type::body, static_cast<float> (button.getHeight()) * 0.62f);
    const auto box = juce::jmin (16.0f, static_cast<float> (button.getHeight()) - 4.0f);

    drawTickBox (g, button, 2.0f, (static_cast<float> (button.getHeight()) - box) * 0.5f, box, box,
                 button.getToggleState(), button.isEnabled(), highlighted, down);

    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f));
    g.setFont (font (fontSize));
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().withTrimmedLeft (juce::roundToInt (box) + 10).withTrimmedRight (2),
                      juce::Justification::centredLeft, 2, 0.85f);
}

void LookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component, float x, float y, float w, float h,
                               bool ticked, bool isEnabled, bool highlighted, bool)
{
    const auto& t = theme();
    const auto bounds = juce::Rectangle<float> (x, y, w, h);
    const auto radius = juce::jmin (5.0f, t.controlRadius * 0.6f);
    const auto alpha = isEnabled ? 1.0f : 0.45f;

    if (ticked)
    {
        g.setColour (t.accent.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bounds, radius);

        juce::Path tick;
        tick.startNewSubPath (bounds.getX() + w * 0.26f, bounds.getY() + h * 0.52f);
        tick.lineTo (bounds.getX() + w * 0.43f, bounds.getY() + h * 0.69f);
        tick.lineTo (bounds.getX() + w * 0.76f, bounds.getY() + h * 0.33f);
        g.setColour (component.findColour (juce::ToggleButton::tickColourId).withMultipliedAlpha (alpha));
        g.strokePath (tick, juce::PathStrokeType (juce::jmax (1.6f, w * 0.12f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour (t.surfaceSunken.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour ((highlighted && isEnabled ? t.textMuted : (t.dark ? t.textFaint.withAlpha (0.6f) : t.controlOutline)).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (bounds.reduced (0.75f), radius, 1.5f);
    }
}

//==============================================================================
void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                int, int, int, int, juce::ComboBox& box)
{
    const auto& t = theme();
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
    const auto shape = controlShape (bounds, t.controlRadius, false, false);
    const auto alpha = box.isEnabled() ? 1.0f : 0.45f;

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withMultipliedAlpha (alpha));
    g.fillPath (shape);

    if (box.isEnabled() && (box.isMouseOver (true) || isButtonDown || box.isPopupActive()))
    {
        g.setColour (isButtonDown || box.isPopupActive() ? t.pressed : t.hover);
        g.fillPath (shape);
    }

    strokeInside (g, shape, box.findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (alpha));

    // 아래로 꺾인 화살표
    const auto centre = juce::Point<float> (static_cast<float> (width) - 16.0f, static_cast<float> (height) * 0.5f);
    juce::Path arrow;
    arrow.startNewSubPath (centre.x - 4.5f, centre.y - 2.0f);
    arrow.lineTo (centre.x, centre.y + 2.5f);
    arrow.lineTo (centre.x + 4.5f, centre.y - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (alpha));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (juce::jmin (type::body, static_cast<float> (box.getHeight()) * 0.56f));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

//==============================================================================
int LookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    return slider.isRotary() ? LookAndFeel_V4::getSliderThumbRadius (slider) : 7;
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                    float sliderPos, float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    const auto& t = theme();
    const auto vertical = style == juce::Slider::LinearVertical;
    const auto alpha = slider.isEnabled() ? 1.0f : 0.45f;
    constexpr float track = 4.0f;

    const auto area = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                              static_cast<float> (width), static_cast<float> (height));
    const auto groove = vertical ? juce::Rectangle<float> (track, area.getHeight()).withCentre (area.getCentre())
                                 : juce::Rectangle<float> (area.getWidth(), track).withCentre (area.getCentre());

    g.setColour (slider.findColour (juce::Slider::backgroundColourId).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (groove, track * 0.5f);

    // 채운 부분: 0을 사이에 둔 범위(팬, EQ)는 0부터, 아니면 맨 앞부터
    const auto minimum = slider.getMinimum(), maximum = slider.getMaximum();
    auto origin = vertical ? groove.getBottom() : groove.getX();

    if (minimum < 0.0 && maximum > 0.0)
        origin = static_cast<float> (slider.getPositionOfValue (0.0));

    const auto fill = vertical ? juce::Rectangle<float>::leftTopRightBottom (groove.getX(), juce::jmin (origin, sliderPos), groove.getRight(), juce::jmax (origin, sliderPos))
                               : juce::Rectangle<float>::leftTopRightBottom (juce::jmin (origin, sliderPos), groove.getY(), juce::jmax (origin, sliderPos), groove.getBottom());

    // 세로 페이더가 여럿 늘어선 믹서에서 너무 시끄럽지 않게 세로는 조금 옅게 채운다.
    g.setColour (slider.findColour (juce::Slider::trackColourId).withMultipliedAlpha (alpha * (vertical ? 0.7f : 1.0f)));
    g.fillRoundedRectangle (fill, track * 0.5f);

    // 손잡이
    const auto centre = vertical ? juce::Point<float> (groove.getCentreX(), sliderPos) : juce::Point<float> (sliderPos, groove.getCentreY());
    const auto knob = juce::Rectangle<float> (14.0f, 14.0f).withCentre (centre);
    const auto hot = slider.isMouseOverOrDragging() && slider.isEnabled();

    g.setColour (juce::Colours::black.withAlpha (t.dark ? 0.35f : 0.18f));
    g.fillEllipse (knob.translated (0.0f, 1.0f).expanded (0.5f));
    g.setColour (slider.findColour (juce::Slider::thumbColourId).withMultipliedAlpha (alpha));
    g.fillEllipse (knob);

    if (! t.dark)
    {
        g.setColour (t.controlOutline);
        g.drawEllipse (knob.reduced (0.5f), 1.0f);
    }

    if (hot)
    {
        g.setColour (t.accent.withAlpha (0.25f));
        g.drawEllipse (knob.expanded (2.5f), 3.0f);
    }
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                    float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto& t = theme();
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto alpha = slider.isEnabled() ? 1.0f : 0.45f;
    const auto lineWidth = juce::jmax (2.5f, radius * 0.16f);
    const auto arcRadius = radius - lineWidth * 0.5f;
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path back;
    back.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId).withMultipliedAlpha (alpha));
    g.strokePath (back, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 0을 사이에 둔 범위(팬)는 가운데부터 채운다.
    auto originAngle = rotaryStartAngle;

    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        originAngle = rotaryStartAngle + static_cast<float> (slider.valueToProportionOfLength (0.0)) * (rotaryEndAngle - rotaryStartAngle);

    if (std::abs (angle - originAngle) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (originAngle, angle), juce::jmax (originAngle, angle), true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (alpha));
        g.strokePath (value, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 가운데 손잡이와 가리키는 줄
    const auto knobRadius = radius - lineWidth - 3.0f;

    if (knobRadius > 2.0f)
    {
        g.setColour (t.surfaceRaised.withMultipliedAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre));

        if (! t.controlOutline.isTransparent())
        {
            g.setColour (t.controlOutline);
            g.drawEllipse (juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre).reduced (0.5f), 1.0f);
        }

        const auto tip = centre.getPointOnCircumference (knobRadius - 1.0f, angle);
        const auto inner = centre.getPointOnCircumference (knobRadius * 0.35f, angle);
        g.setColour (t.text.withMultipliedAlpha (alpha));
        g.drawLine ({ inner, tip }, 2.0f);
    }
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setJustificationType (juce::Justification::centred);
    label->setFont (font (type::caption + 0.5f));
    return label;
}

void LookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // 슬라이더의 값 상자: 테두리 없는 둥근 면
    if (auto* slider = dynamic_cast<juce::Slider*> (label.getParentComponent()); slider != nullptr && ! label.isBeingEdited())
    {
        const auto& t = theme();
        const auto bounds = label.getLocalBounds().toFloat();
        const auto shape = controlShape (bounds, juce::jmin (t.controlRadius, 6.0f), false, false);
        const auto alpha = label.isEnabled() ? 1.0f : 0.45f;

        g.setColour (slider->findColour (juce::Slider::textBoxBackgroundColourId).withMultipliedAlpha (alpha));
        g.fillPath (shape);
        strokeInside (g, shape, t.controlOutline.withMultipliedAlpha (alpha));

        g.setColour (slider->findColour (juce::Slider::textBoxTextColourId).withMultipliedAlpha (alpha));
        g.setFont (getLabelFont (label));
        g.drawFittedText (label.getText(), label.getLocalBounds().reduced (4, 0), label.getJustificationType(), 1, 0.8f);
        return;
    }

    LookAndFeel_V4::drawLabel (g, label);
}

void LookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent& bubble, const juce::Point<float>&,
                              const juce::Rectangle<float>& body)
{
    g.setColour (bubble.findColour (juce::BubbleComponent::backgroundColourId));
    g.fillRoundedRectangle (body.reduced (0.5f), 5.0f);
    g.setColour (bubble.findColour (juce::BubbleComponent::outlineColourId));
    g.drawRoundedRectangle (body.reduced (0.5f), 5.0f, 1.0f);
}

//==============================================================================
void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto& t = theme();

    // 대화 상자 안의 입력 칸도 같은 모양으로 그린다.
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), juce::jmin (t.controlRadius, 6.0f));
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const auto& t = theme();
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    const auto radius = juce::jmin (t.controlRadius, 6.0f);

    if (editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
    {
        g.setColour (editor.findColour (juce::TextEditor::focusedOutlineColourId));
        g.drawRoundedRectangle (bounds.reduced (0.75f), radius, 1.5f);
    }
    else if (const auto outline = editor.findColour (juce::TextEditor::outlineColourId); ! outline.isTransparent())
    {
        g.setColour (outline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }
}

//==============================================================================
int LookAndFeel::getDefaultScrollbarWidth()
{
    return 10;
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar& bar, int x, int y, int width, int height,
                                 bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    juce::Rectangle<float> thumb;

    if (isScrollbarVertical)
        thumb = { static_cast<float> (x), static_cast<float> (thumbStartPosition), static_cast<float> (width), static_cast<float> (thumbSize) };
    else
        thumb = { static_cast<float> (thumbStartPosition), static_cast<float> (y), static_cast<float> (thumbSize), static_cast<float> (height) };

    thumb = thumb.reduced (2.5f);
    const auto colour = bar.findColour (juce::ScrollBar::thumbColourId);
    g.setColour (colour.withAlpha (isMouseDown ? 0.8f : (isMouseOver ? 0.65f : 0.4f)));
    g.fillRoundedRectangle (thumb, juce::jmin (thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

//==============================================================================
void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto& t = theme();
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    const auto radius = t.controlRadius + 2.0f;

    g.fillAll (juce::Colours::transparentBlack);
    g.setColour (findColour (juce::PopupMenu::backgroundColourId).withAlpha (1.0f));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (t.dark ? t.border.brighter (0.15f) : t.controlOutline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

int LookAndFeel::getPopupMenuBorderSize()
{
    return 6;
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return font (type::body);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                     bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                     const juce::String& text, const juce::String& shortcutKeyText,
                                     const juce::Drawable* icon, const juce::Colour* textColour)
{
    const auto& t = theme();

    if (isSeparator)
    {
        g.setColour (t.border);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (2, 1);

    if (isHighlighted && isActive)
    {
        g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRoundedRectangle (r.toFloat(), juce::jmax (3.0f, t.controlRadius - 2.0f));
    }

    const auto colour = isHighlighted && isActive ? findColour (juce::PopupMenu::highlightedTextColourId)
                                                  : (textColour != nullptr ? *textColour : findColour (juce::PopupMenu::textColourId));
    g.setColour (colour.withMultipliedAlpha (isActive ? 1.0f : 0.45f));

    r.reduce (8, 0);
    auto itemFont = getPopupMenuFont();
    const auto maxFontHeight = static_cast<float> (r.getHeight()) / 1.4f;

    if (itemFont.getHeight() > maxFontHeight)
        itemFont = itemFont.withHeight (maxFontHeight);

    g.setFont (itemFont);

    // 체크 자리: 고른 항목에 액센트 체크
    const auto iconArea = r.removeFromLeft (18).toFloat();

    if (icon != nullptr)
    {
        icon->drawWithin (g, iconArea, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
    }
    else if (isTicked)
    {
        const auto box = iconArea.withSizeKeepingCentre (12.0f, 12.0f);
        juce::Path tick;
        tick.startNewSubPath (box.getX() + 1.0f, box.getCentreY());
        tick.lineTo (box.getX() + 4.5f, box.getBottom() - 2.5f);
        tick.lineTo (box.getRight() - 1.0f, box.getY() + 2.5f);
        g.setColour (t.accent.withMultipliedAlpha (isActive ? 1.0f : 0.45f));
        g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour.withMultipliedAlpha (isActive ? 1.0f : 0.45f));
    }

    r.removeFromLeft (4);

    if (hasSubMenu)
    {
        const auto arrowArea = r.removeFromRight (12).toFloat();
        juce::Path arrow;
        arrow.startNewSubPath (arrowArea.getCentreX() - 2.0f, arrowArea.getCentreY() - 4.0f);
        arrow.lineTo (arrowArea.getCentreX() + 2.0f, arrowArea.getCentreY());
        arrow.lineTo (arrowArea.getCentreX() - 2.0f, arrowArea.getCentreY() + 4.0f);
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.drawFittedText (text, r, juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (t.textMuted);
        g.setFont (font (type::caption));
        g.drawText (shortcutKeyText, r, juce::Justification::centredRight, true);
    }
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                             int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 9;
        return;
    }

    idealHeight = juce::jmax (28, standardMenuItemHeight);
    idealWidth = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 48;
}

//==============================================================================
void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    // 툴팁 창은 불투명하므로 네모로 채우고 옅은 테두리를 두른다.
    g.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
    LookAndFeel_V4::drawTooltip (g, text, width, height);
    g.setColour (theme().dark ? theme().border.brighter (0.15f) : theme().controlOutline);
    g.drawRect (juce::Rectangle<int> (width, height), 1);
}

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout)
{
    const auto& t = theme();
    const auto bounds = alert.getLocalBounds().toFloat();

    g.setColour (alert.findColour (juce::AlertWindow::backgroundColourId));
    g.fillRect (bounds);
    g.setColour (alert.findColour (juce::AlertWindow::outlineColourId));
    g.drawRect (bounds, 1.0f);

    auto iconSpaceUsed = 0;

    if (alert.getAlertType() != juce::MessageBoxIconType::NoIcon)
    {
        // 작은 원 안에 기호 하나
        const auto warning = alert.getAlertType() == juce::MessageBoxIconType::WarningIcon;
        const auto colour = warning ? t.warning : t.accent;
        const auto circle = juce::Rectangle<float> (24.0f, 30.0f, 32.0f, 32.0f);

        g.setColour (colour.withAlpha (t.dark ? 0.22f : 0.14f));
        g.fillEllipse (circle);
        g.setColour (colour);
        g.setFont (font (18.0f, true));
        g.drawText (warning ? "!" : (alert.getAlertType() == juce::MessageBoxIconType::InfoIcon ? "i" : "?"),
                    circle, juce::Justification::centred, false);

        iconSpaceUsed = 80;
    }

    juce::ignoreUnused (textArea);
    g.setColour (alert.findColour (juce::AlertWindow::textColourId));
    const auto text = juce::Rectangle<int> (1 + iconSpaceUsed, 30, alert.getWidth() - 2, alert.getHeight() - getAlertWindowButtonHeight() - 20);
    textLayout.draw (g, text.toFloat());
}

int LookAndFeel::getAlertWindowButtonHeight()    { return 30; }
juce::Font LookAndFeel::getAlertWindowTitleFont()     { return font (16.0f, true); }
juce::Font LookAndFeel::getAlertWindowMessageFont()   { return font (type::body); }
juce::Font LookAndFeel::getAlertWindowFont()          { return font (type::body); }

//==============================================================================
void LookAndFeel::drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.fillAll (header.findColour (juce::TableHeaderComponent::backgroundColourId));
    g.setColour (header.findColour (juce::TableHeaderComponent::outlineColourId));
    g.fillRect (header.getLocalBounds().removeFromBottom (1));
}

void LookAndFeel::drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent& header, const juce::String& columnName, int,
                                         int width, int height, bool isMouseOver, bool isMouseDown, int columnFlags)
{
    if (isMouseDown || isMouseOver)
    {
        g.setColour (isMouseDown ? theme().pressed : theme().hover);
        g.fillRect (0, 0, width, height - 1);
    }

    auto area = juce::Rectangle<int> (width, height).reduced (6, 0);
    const auto sorted = (columnFlags & (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0;

    if (sorted)
    {
        const auto forwards = (columnFlags & juce::TableHeaderComponent::sortedForwards) != 0;
        const auto arrowArea = area.removeFromRight (12).toFloat();
        const auto c = arrowArea.getCentre();
        juce::Path arrow;
        arrow.startNewSubPath (c.x - 3.5f, c.y + (forwards ? 1.5f : -1.5f));
        arrow.lineTo (c.x, c.y + (forwards ? -2.0f : 2.0f));
        arrow.lineTo (c.x + 3.5f, c.y + (forwards ? 1.5f : -1.5f));
        g.setColour (theme().accent);
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (sorted ? theme().text : header.findColour (juce::TableHeaderComponent::textColourId));
    g.setFont (font (type::caption, sorted));
    g.drawFittedText (columnName, area, juce::Justification::centredLeft, 1);
}

} // namespace ui
