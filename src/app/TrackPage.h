#pragma once

#include <memory>

#include "Session.h"
#include "TrackControls.h"
#include "Ui.h"

/** 타격이 메트로놈의 16분음표 자리에서 얼마나 벗어났는지 점으로 보여 준다.
    가운데 띠가 "박에 맞음"(ui::timing::onBeatMs) 범위다. */
class TimingView final : public juce::Component
{
public:
    void setTiming (const hvd::MetronomeTiming& newTiming);
    void paint (juce::Graphics&) override;

private:
    hvd::MetronomeTiming timing;
    int shownCount = -1;
};

/** 반주, 녹음, MIDI(친 것을 기록해 다시 듣기·내보내기), 박자 연습 화면. */
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
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { refresh(); }

    void refresh();
    void setLoop (double start, double end, bool enabled);
    void updateLoop();
    void toggleRecording();
    void exportMidi();
    void importMidi();

    Session& session;
    int tickCount = 0;
    bool hasTrack = false;
    bool dragOver = false;    // 소리 파일을 끌고 들어와 있는 동안
    std::array<juce::Rectangle<int>, 4> sections;
    juce::Rectangle<int> dropZone;

    // 반주
    juce::Label trackTitle, trackName, timeLabel, gainCaption, speedCaption, loopLabel, dropLabel;
    juce::TextButton openButton, playButton, rewindButton, unloadButton, loopStartButton, loopEndButton;
    juce::ToggleButton loopToggle;
    juce::Slider gainSlider, speedSlider;
    TrackBar bar;

    // 녹음
    juce::Label recordTitle, recordLabel;
    juce::TextButton recordButton, recordFolderButton;
    juce::ToggleButton recordTrackToggle;

    // MIDI
    juce::Label takeTitle, takeLabel;
    juce::TextButton takeRecordButton, takePlayButton, takeExportButton, takeImportButton;

    // 박자 연습
    juce::Label timingTitle, timingLabel;
    juce::TextButton timingResetButton;
    TimingView timingView;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackPage)
};
