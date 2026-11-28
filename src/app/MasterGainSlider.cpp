#include "MasterGainSlider.h"

namespace
{

constexpr int valueBoxWidth = 44;
constexpr int valueBoxHeight = 22;

} // namespace

MasterGainSlider::MasterGainSlider (Session& sessionToUse)
    : session (sessionToUse)
{
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::TextBoxRight, false, valueBoxWidth, valueBoxHeight);
    setRange (0.0, AudioHost::maxMasterGain, 0.01);
    setNumDecimalPlacesToDisplay (2);
    setDoubleClickReturnValue (true, AudioHost::defaultMasterGain);
    setScrollWheelEnabled (false);
    setTooltip (ko ("전체 음량: 드럼과 메트로놈. 두 번 누르면 ") + juce::String (AudioHost::defaultMasterGain, 1));
    ui::noFocus (*this);

    setValue (session.getMasterGain(), juce::dontSendNotification);

    onValueChange = [this] { session.setMasterGain (static_cast<float> (getValue())); };

    // 다른 슬라이더나 키로 바꾼 값을 따라간다.
    startTimerHz (10);
}

MasterGainSlider::~MasterGainSlider()
{
    stopTimer();
}

void MasterGainSlider::setValueBoxShown (bool shouldShow)
{
    if (shouldShow != (getTextBoxPosition() != juce::Slider::NoTextBox))
        setTextBoxStyle (shouldShow ? juce::Slider::TextBoxRight : juce::Slider::NoTextBox, false, valueBoxWidth, valueBoxHeight);
}

void MasterGainSlider::timerCallback()
{
    if (! isMouseButtonDown() && std::abs (getValue() - session.getMasterGain()) > 0.001)
        setValue (session.getMasterGain(), juce::dontSendNotification);
}
