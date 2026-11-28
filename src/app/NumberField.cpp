#include "NumberField.h"

#include "Text.h"
#include "Theme.h"

namespace ui
{

NumberField::NumberField (int minimumValue, int maximumValue, juce::String suffixToShow, float pixels, bool drawBox)
    : minimum (minimumValue),
      maximum (juce::jmax (minimumValue, maximumValue)),
      value (minimumValue),
      suffix (std::move (suffixToShow)),
      pixelsPerStep (juce::jmax (1.0f, pixels)),
      boxed (drawBox)
{
    setJustificationType (juce::Justification::centred);
    setEditable (false, true, false);    // 두 번 누르면 숫자를 적는다
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setTooltip (ko ("끌어서 바꾸기, 두 번 눌러 적기"));
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    showValue();
}

void NumberField::setValue (int newValue)
{
    if (dragging || isBeingEdited())
        return;

    value = juce::jlimit (minimum, maximum, newValue);
    showValue();
}

void NumberField::setRange (int newMinimum, int newMaximum)
{
    minimum = newMinimum;
    maximum = juce::jmax (newMinimum, newMaximum);
    value = juce::jlimit (minimum, maximum, value);
    showValue();
}

void NumberField::showValue()
{
    setText (juce::String (value) + suffix, juce::dontSendNotification);
}

void NumberField::change (int newValue)
{
    newValue = juce::jlimit (minimum, maximum, newValue);

    if (newValue == value)
        return;

    value = newValue;
    showValue();

    if (onValueChange)
        onValueChange (value);
}

void NumberField::paint (juce::Graphics& g)
{
    if (boxed)
    {
        const auto& t = theme();
        g.setColour (t.surfaceSunken);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), t.controlRadius);
    }

    juce::Label::paint (g);
}

void NumberField::mouseDown (const juce::MouseEvent& event)
{
    dragging = false;
    dragStartValue = value;
    juce::Label::mouseDown (event);
}

void NumberField::mouseDrag (const juce::MouseEvent& event)
{
    // 위나 오른쪽으로 pixelsPerStep마다 1씩
    const auto offset = event.getDistanceFromDragStartX() - event.getDistanceFromDragStartY();

    if (! dragging && std::abs (offset) < 3)
        return;

    dragging = true;
    change (dragStartValue + static_cast<int> (static_cast<float> (offset) / pixelsPerStep));
}

void NumberField::mouseUp (const juce::MouseEvent& event)
{
    dragging = false;
    juce::Label::mouseUp (event);
}

juce::TextEditor* NumberField::createEditorComponent()
{
    auto* textEditor = juce::Label::createEditorComponent();
    textEditor->setInputRestrictions (juce::String (maximum).length(), "0123456789");
    textEditor->setJustification (juce::Justification::centred);
    return textEditor;
}

void NumberField::editorShown (juce::TextEditor* textEditor)
{
    textEditor->setText (juce::String (value), false);
    textEditor->selectAll();
}

void NumberField::textWasEdited()
{
    // 숫자가 아니면 그대로 둔다.
    if (const auto text = getText().trim(); text.containsOnly ("0123456789") && text.isNotEmpty())
        change (text.getIntValue());

    showValue();
}

} // namespace ui
