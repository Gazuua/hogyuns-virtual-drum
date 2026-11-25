#pragma once

#include <memory>
#include <vector>

#include "Session.h"
#include "Ui.h"

/** 소리 크기를 세로 막대로 보여 준다. */
class LevelMeter final : public juce::Component
{
public:
    /** 새로 잰 최대 크기(0 ~ 1)를 넣는다. 막대는 올라갈 때는 바로, 내려갈 때는 천천히 움직인다. */
    void push (float peak);

    void paint (juce::Graphics&) override;

private:
    float level = 0.0f;
};

/** 믹서 화면. 패드마다 음량, 팬, 리버브 양, 뮤트, 솔로를 조절하고, 드럼 전체에 EQ, 컴프레서, 리버브를 건다. */
class MixerPage final : public ui::Page,
                        private juce::ChangeListener
{
public:
    explicit MixerPage (Session& sessionToUse);
    ~MixerPage() override;

    void tick() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Strip;

    void changeListenerCallback (juce::ChangeBroadcaster*) override    { rebuild(); }

    void rebuild();
    void showFx();
    void fxChanged();

    Session& session;
    juce::StringArray stripIds;
    std::vector<std::unique_ptr<Strip>> strips;
    juce::Viewport viewport;
    juce::Component holder;
    bool dirty = false;
    bool updating = false;
    int tickCount = 0;

    juce::Label masterTitle, eqTitle, compTitle, reverbTitle, reductionLabel;
    std::array<juce::Label, 8> captions;
    juce::Slider masterSlider, eqLow, eqMid, eqHigh, comp, reverbLevel, reverbSize, reverbDamp;
    LevelMeter masterMeter;
    juce::TextButton resetFxButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPage)
};
