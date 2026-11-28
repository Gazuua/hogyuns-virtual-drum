#pragma once

#include <functional>
#include <memory>

#include "Session.h"
#include "Ui.h"

/** 반주를 다루는 공통 동작. 반주 화면과 연주 화면의 반주 줄이 함께 쓴다.
    반복 구간은 재생기(hvd::TrackPlayer)가 가진 값 하나만 본다. */
namespace transport
{

/** 곡이 열려 있는지. */
bool hasTrack (Session&);

/** 재생과 정지를 바꾼다. */
void togglePlay (Session&);

/** 처음으로. 반복 구간이 켜져 있으면 그 시작으로. */
void rewind (Session&);

/** 재생 버튼 글자: "재생" / "정지". */
juce::String playText (Session&);

/** 파일을 반주로 연다. 못 열면 알리고 false. 고른 폴더는 다음에 열 때의 시작 폴더가 된다. */
bool load (Session&, const juce::File&);

/** 음악 파일을 고르는 창을 띄우고, 고르면 연다. 열었으면 onLoaded를 부른다. */
void chooseAndLoad (Session&, std::unique_ptr<juce::FileChooser>& holder, std::function<void()> onLoaded = {});

} // namespace transport

//==============================================================================
/** 반주의 재생 위치와 반복 구간을 보여 주는 막대. 클릭하거나 끌면 그 위치로 옮긴다. */
class TrackBar final : public juce::Component
{
public:
    TrackBar();

    void setState (double positionSeconds, double lengthSeconds, double loopStartSeconds, double loopEndSeconds, bool loopOn);

    /** 재생기의 지금 상태로 맞춘다. */
    void update (const hvd::TrackPlayer&);

    std::function<void (double seconds)> onSeek;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void seekTo (const juce::MouseEvent& event);

    double position = 0.0, length = 0.0, loopStart = 0.0, loopEnd = 0.0;
    bool loopEnabled = false;
};

//==============================================================================
/** 한 줄짜리 반주 막대: [재생] [처음으로] 곡 이름 [진행 막대] 0:42 / 3:36.
    곡이 없으면 [반주 열기]만 보인다. 음악 파일을 끌어 놓아도 열린다. 연주 화면 아래에 둔다. */
class TrackStrip final : public juce::Component,
                         public juce::FileDragAndDropTarget
{
public:
    explicit TrackStrip (Session& sessionToUse);

    /** 화면의 tick에서 부른다. */
    void update();

    void resized() override;
    void paint (juce::Graphics&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    Session& session;
    bool hasTrack = false;
    bool dragOver = false;
    int tickCount = 0;
    juce::File shownFile;

    juce::TextButton openButton, playButton, rewindButton;
    juce::Label nameLabel, timeLabel;
    TrackBar bar;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackStrip)
};
