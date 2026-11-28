#pragma once

#include "Session.h"
#include "Ui.h"

/** 전체 음량 슬라이더. 윗줄과 믹서 화면이 같은 부품을 쓴다.
    소리에는 바로 반영하고, settings.json에는 마우스를 놓은 뒤에 적는다. 다른 곳(다른 슬라이더, 키)에서 바꾼 값은 저절로 따라간다. */
class MasterGainSlider final : public juce::Slider,
                               private juce::Timer
{
public:
    explicit MasterGainSlider (Session& sessionToUse);
    ~MasterGainSlider() override;

    /** 값 상자를 보일지. 윗줄이 좁으면 뺀다. */
    void setValueBoxShown (bool shouldShow);

private:
    void timerCallback() override;

    Session& session;
    ui::DeferredSave saver;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterGainSlider)
};
