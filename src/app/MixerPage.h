#pragma once

#include <memory>
#include <vector>

#include "MasterGainSlider.h"
#include "Session.h"
#include "Ui.h"

/** 소리 크기를 가는 막대로 보여 준다. 소리가 없으면 슬라이더의 홈과 같은 색이다. */
class LevelMeter final : public juce::Component
{
public:
    /** 새로 잰 최대 크기(0 ~ 1)를 넣는다. 막대는 올라갈 때는 바로, 내려갈 때는 천천히 움직인다. */
    void push (float peak);

    void paint (juce::Graphics&) override;

private:
    float level = 0.0f;
};

/** 믹서 화면. 패드마다 음량, 팬, 리버브 양, 뮤트, 솔로를 조절하고, 드럼 전체에 EQ, 컴프레서, 리버브를 건다.
    패드 줄은 악기별(킥, 스네어, 하이햇, 탐, 심벌)로 묶어 놓는다. */
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
    class StripHolder;

    void changeListenerCallback (juce::ChangeBroadcaster*) override    { rebuild(); }

    void rebuild();
    void layoutStrips();
    void showFx();
    void fxChanged();
    void setFxBypassed (bool shouldBypass);
    void resetFx();

    /** 킷 파일에 적힌 이펙트 값. 이펙트를 꺼 둔 동안에도 슬라이더는 이 값을 보인다. */
    hvd::FxSettings fxFromDocument() const;

    Session& session;
    juce::StringArray stripIds;
    std::vector<std::unique_ptr<Strip>> strips;    // 악기별로 묶은 순서
    juce::Viewport viewport;
    std::unique_ptr<StripHolder> holder;
    ui::DeferredSave kitSaver;
    bool updating = false;
    bool fxBypassed = false;    // 이펙트를 잠깐 끈 동안. 값은 그대로 두고 엔진에만 끈 값을 넘긴다 (저장하지 않는다)
    int tickCount = 0;

    juce::Label masterCaption, kitTitle, eqTitle, compTitle, reverbTitle, reductionLabel;
    std::array<juce::Label, 7> captions;
    MasterGainSlider masterSlider { session };
    juce::Slider eqLow, eqMid, eqHigh, comp, reverbLevel, reverbSize, reverbDamp;
    LevelMeter masterMeter;
    juce::ToggleButton fxToggle;
    juce::TextButton resetFxButton;
    juce::Rectangle<int> masterDivider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPage)
};
