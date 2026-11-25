#pragma once

#include <functional>
#include <memory>

#include "Session.h"
#include "Ui.h"

/** 반주의 재생 위치와 반복 구간을 보여 주는 막대. 클릭하거나 끌면 그 위치로 옮긴다. */
class TrackBar final : public juce::Component
{
public:
    TrackBar();

    void setState (double positionSeconds, double lengthSeconds, double loopStartSeconds, double loopEndSeconds, bool loopOn);

    std::function<void (double seconds)> onSeek;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void seekTo (const juce::MouseEvent& event);

    double position = 0.0, length = 0.0, loopStart = 0.0, loopEnd = 0.0;
    bool loopEnabled = false;
};

/** 타격이 메트로놈의 16분음표 자리에서 얼마나 벗어났는지 점으로 보여 준다. */
class TimingView final : public juce::Component
{
public:
    void setTiming (const Session::Timing& newTiming);
    void paint (juce::Graphics&) override;

private:
    Session::Timing timing;
    int shownCount = -1;
};

/** 반주, 녹음, 연주 기록, 박자 연습 화면. */
class TrackPage final : public ui::Page,
                        public juce::FileDragAndDropTarget,
                        private juce::ChangeListener
{
public:
    explicit TrackPage (Session& sessionToUse);
    ~TrackPage() override;

    void tick() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { refresh(); }

    void refresh();
    void openTrack();
    void loadTrack (const juce::File& file);
    void applyLoop();
    void toggleRecording();
    void exportMidi();
    void importMidi();

    Session& session;
    double loopStart = 0.0, loopEnd = 0.0;
    int tickCount = 0;
    std::array<juce::Rectangle<int>, 4> sections;

    // 반주
    juce::Label trackTitle, trackName, timeLabel, gainCaption, speedCaption, loopLabel;
    juce::TextButton openButton, playButton, rewindButton, unloadButton, loopStartButton, loopEndButton;
    juce::ToggleButton loopToggle;
    juce::Slider gainSlider, speedSlider;
    TrackBar bar;

    // 녹음
    juce::Label recordTitle, recordLabel;
    juce::TextButton recordButton, recordFolderButton;
    juce::ToggleButton recordTrackToggle;

    // 연주 기록
    juce::Label takeTitle, takeLabel;
    juce::TextButton takeRecordButton, takePlayButton, takeExportButton, takeImportButton;

    // 박자 연습
    juce::Label timingTitle, timingLabel;
    juce::TextButton timingResetButton;
    TimingView timingView;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackPage)
};
