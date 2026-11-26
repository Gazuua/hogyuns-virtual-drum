#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

#include "Session.h"
#include "Ui.h"

/** 메인 화면. 위에 탭과 템포 조절, 가운데에 고른 화면(연주, 킷, 키 배열, 믹서, 반주·녹음, 리듬 게임, 연습 기록), 아래에 상태를 보여 준다. */
class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    using Options = Session::Options;

    static constexpr int numPages = 7;

    explicit MainComponent (const Options& options);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override;

    Session& getSession() noexcept    { return session; }

    /** 0 연주, 1 킷, 2 키 배열, 3 믹서, 4 반주·녹음, 5 리듬 게임, 6 연습 기록 */
    void showPage (int index);

    /** "play", "kit", "keys", "mixer", "track", "game", "practice"를 화면 번호로 바꾼다. 모르는 이름이면 0. */
    static int pageIndexFromName (const juce::String& name);

    void refreshStatus();

    ui::Page& getPage (int index)     { return *pages[static_cast<std::size_t> (index)]; }

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void updateTempoControls();
    void updateMasterControl();
    void tapTempoFromButton();
    void showAudioSettings();

    Session session;
    juce::TooltipWindow tooltips { this, 600 };

    std::array<std::unique_ptr<ui::Page>, numPages> pages;
    std::array<juce::TextButton, numPages> tabButtons;
    int currentPage = 0;

    std::uint32_t seenBeatCount = 0;
    int beatFlashTicks = 0;
    juce::int64 lastButtonTapMs = 0;
    float statusPeak = 0.0f;
    int tick = 0;

    juce::Label titleLabel, hintLabel, statusLabel, messageLabel, tempoLabel, masterCaption;
    juce::TextButton audioButton, tempoDownButton, tempoUpButton, tapButton, metronomeButton;

    // 전체 음량. 어느 화면에서든 보이게 윗줄에 둔다 (믹서 화면의 "전체 음량"과 같은 값).
    juce::Slider masterSlider;
    bool masterDirty = false;
    juce::Component::SafePointer<juce::DialogWindow> settingsDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
