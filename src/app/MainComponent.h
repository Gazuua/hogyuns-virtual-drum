#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <functional>
#include <memory>

#include "MasterGainSlider.h"
#include "Session.h"
#include "Ui.h"

/** 문제 목록 띠. 화면 내용 위에 겹쳐 뜨고, 누르면 펼치고 접는다. 화면 높이를 바꾸지 않는다. */
class NoticeBand final : public juce::Component
{
public:
    NoticeBand();

    /** 보일 줄들. 비어 있으면 숨긴다. 바뀌었으면 true. */
    bool setLines (const juce::StringArray& newLines);

    /** 지금(접힘/펼침) 필요한 높이. */
    int getWantedHeight() const;

    /** 펼치거나 접었을 때 불린다 (놓을 자리를 다시 정하도록). */
    std::function<void()> onToggle;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::StringArray lines;
    bool expanded = false;

    static constexpr int headerHeight = 28;
    static constexpr int lineHeight = 18;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoticeBand)
};

/** 윗줄의 템포 값 칸 ("120 BPM"). 위아래(또는 좌우)로 끌면 바뀌고, 두 번 누르면 숫자를 적는다. */
class TempoField final : public juce::Label
{
public:
    TempoField();

    /** 보여 줄 템포. 끄는 중이거나 적는 중에는 바꾸지 않는다. */
    void setTempo (float beatsPerMinute);

    /** 끌거나 적어서 템포를 바꿨을 때 불린다. */
    std::function<void (float)> onTempoChange;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

protected:
    juce::TextEditor* createEditorComponent() override;
    void editorShown (juce::TextEditor*) override;
    void textWasEdited() override;

private:
    float tempo = 120.0f;
    float dragStartTempo = 120.0f;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TempoField)
};

/** 메인 화면. 위 한 줄에 탭과 트랜스포트(전체 음량, 템포, 메트로놈)와 설정, 가운데에 고른 화면
    (연주, 킷, 키 배열, 믹서, 반주·녹음, 리듬 게임, 연습 기록), 아래에 한 줄짜리 상태 표시줄을 둔다. */
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

    /** 창의 크기와 자리를 settings.json에서 되살린다 / 적는다. 평소에 켠 창만 부른다 (검사와 화면 찍기는 창이 없다). */
    void restoreWindowState (juce::ResizableWindow& window);
    void saveWindowState (juce::ResizableWindow& window);

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
    void updateTransportVisibility();
    void tapTempoFromButton();
    void showAudioSettings();
    void layoutNotices();

    Session session;
    juce::TooltipWindow tooltips { this, 600 };

    std::array<std::unique_ptr<ui::Page>, numPages> pages;
    std::array<juce::TextButton, numPages> tabButtons;
    int currentPage = 0;

    std::uint32_t seenBeatCount = 0;
    int beatFlashTicks = 0;
    juce::int64 lastButtonTapMs = 0;
    int tick = 0;

    // 끊김(늦은 콜백) 칩: 켜고 2초 동안 생긴 것은 장치가 자리 잡는 동안의 것이라 세지 않는다.
    int lateCallbackBaseline = -1;
    int dropouts = 0;

    juce::Label statusLabel, latencyLabel;
    TempoField tempoField;
    juce::TextButton settingsButton, tempoDownButton, tempoUpButton, tapButton, metronomeButton;
    NoticeBand notices;
    bool transportHidden = false;    // 리듬 게임을 하는 동안 템포, 탭, 메트로놈을 숨긴다

    // 전체 음량. 어느 화면에서든 보이게 윗줄에 둔다 (믹서 화면의 "전체 음량"과 같은 부품, 같은 값).
    MasterGainSlider masterSlider { session };
    juce::Component::SafePointer<juce::DialogWindow> settingsDialog;

    // paint가 그리는 것들: 탭 묶음, 머리줄 아래 선, 트랜스포트 칸과 그 안의 나눔 선, 음량 그림, 상태 표시줄 위 선, 끊김 칩
    juce::Rectangle<int> tabArea, headerLine, transportArea, speakerArea, statusLine, dropoutChip;
    std::array<int, 2> transportDividers {};    // 0이면 그리지 않는다
    juce::Rectangle<int> contentArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
