#include "TrackPage.h"

namespace
{

constexpr float timingRangeMs = 60.0f;
const juce::Colour recordingColour { 0xff8e2f2f };

} // namespace

//==============================================================================
TrackBar::TrackBar()
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void TrackBar::setState (double positionSeconds, double lengthSeconds, double loopStartSeconds, double loopEndSeconds, bool loopOn)
{
    if (positionSeconds == position && lengthSeconds == length && loopStartSeconds == loopStart
        && loopEndSeconds == loopEnd && loopOn == loopEnabled)
        return;

    position = positionSeconds;
    length = lengthSeconds;
    loopStart = loopStartSeconds;
    loopEnd = loopEndSeconds;
    loopEnabled = loopOn;
    repaint();
}

void TrackBar::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff161b20));
    g.fillRoundedRectangle (bounds, 5.0f);

    if (length <= 0.0)
        return;

    const auto toX = [&bounds, this] (double seconds)
    {
        return bounds.getX() + bounds.getWidth() * static_cast<float> (juce::jlimit (0.0, 1.0, seconds / length));
    };

    // 반복 구간
    if (loopEnd > loopStart)
    {
        g.setColour (ui::accent.withAlpha (loopEnabled ? 0.35f : 0.15f));
        g.fillRect (juce::Rectangle<float> (toX (loopStart), bounds.getY(), toX (loopEnd) - toX (loopStart), bounds.getHeight()));
    }

    // 지나온 부분과 지금 위치
    const auto x = toX (position);
    g.setColour (juce::Colour (0xff4dd0e1).withAlpha (0.30f));
    g.fillRoundedRectangle (bounds.withRight (x), 5.0f);
    g.setColour (juce::Colours::white);
    g.fillRect (x - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
}

void TrackBar::seekTo (const juce::MouseEvent& event)
{
    if (length > 0.0 && getWidth() > 0 && onSeek)
        onSeek (length * juce::jlimit (0.0, 1.0, static_cast<double> (event.position.x) / getWidth()));
}

void TrackBar::mouseDown (const juce::MouseEvent& event)    { seekTo (event); }
void TrackBar::mouseDrag (const juce::MouseEvent& event)    { seekTo (event); }

//==============================================================================
void TimingView::setTiming (const Session::Timing& newTiming)
{
    if (newTiming.count == shownCount)
        return;

    timing = newTiming;
    shownCount = newTiming.count;
    repaint();
}

void TimingView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff161b20));
    g.fillRoundedRectangle (bounds, 5.0f);

    const auto centreX = bounds.getCentreX();
    const auto toX = [&bounds, centreX] (float ms)
    {
        return centreX + juce::jlimit (-1.0f, 1.0f, ms / timingRangeMs) * (bounds.getWidth() * 0.5f - 10.0f);
    };

    // "정확"으로 보는 범위(±10ms)와 가운데 선
    g.setColour (ui::good.withAlpha (0.12f));
    g.fillRect (juce::Rectangle<float> (toX (-10.0f), bounds.getY(), toX (10.0f) - toX (-10.0f), bounds.getHeight()));
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.fillRect (centreX - 0.5f, bounds.getY(), 1.0f, bounds.getHeight());

    g.setColour (ui::quietText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (ko ("빠름"), getLocalBounds().reduced (8, 2), juce::Justification::topLeft);
    g.drawText (ko ("늦음"), getLocalBounds().reduced (8, 2), juce::Justification::topRight);

    // 최근 타격일수록 아래쪽에, 진하게 그린다.
    const auto used = juce::jmin (timing.count, Session::Timing::historySize);

    for (int age = used - 1; age >= 0; --age)
    {
        const auto index = static_cast<std::size_t> (((timing.count - 1 - age) % Session::Timing::historySize + Session::Timing::historySize)
                                                     % Session::Timing::historySize);
        const auto ms = timing.offsetsMs[index];
        const auto distance = std::abs (ms);
        const auto freshness = 1.0f - static_cast<float> (age) / static_cast<float> (Session::Timing::historySize);
        const auto y = bounds.getY() + 18.0f + (bounds.getHeight() - 30.0f) * freshness;

        g.setColour ((distance <= 10.0f ? ui::good : (distance <= 25.0f ? ui::accent : ui::danger)).withAlpha (0.25f + 0.75f * freshness));
        g.fillEllipse (toX (ms) - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }
}

//==============================================================================
TrackPage::TrackPage (Session& sessionToUse)
    : session (sessionToUse)
{
    // 반주
    ui::setupLabel (*this, trackTitle, ko ("반주"), 17.0f, juce::Colours::white, true);
    ui::setupLabel (*this, trackName, {}, 14.0f);
    ui::setupLabel (*this, timeLabel, {}, 14.0f, ui::quietText);
    timeLabel.setJustificationType (juce::Justification::centredRight);

    ui::setupButton (*this, openButton, ko ("음악 파일 열기…"), [this] { openTrack(); });
    ui::setupButton (*this, playButton, {}, [this] { session.getTrack().setPlaying (! session.getTrack().isPlaying()); refresh(); });
    ui::setupButton (*this, rewindButton, ko ("처음으로"), [this] { session.getTrack().seek (loopToggle.getToggleState() ? loopStart : 0.0); });
    ui::setupButton (*this, unloadButton, ko ("닫기"), [this] { session.unloadTrack(); });

    bar.onSeek = [this] (double seconds) { session.getTrack().seek (seconds); };
    addAndMakeVisible (bar);

    ui::setupLabel (*this, gainCaption, ko ("반주 음량"), 13.0f, ui::quietText);
    ui::setupSlider (*this, gainSlider, 0.0, 1.5, 0.01, 0.7);
    gainSlider.onValueChange = [this] { session.setTrackGain (static_cast<float> (gainSlider.getValue())); };

    ui::setupLabel (*this, speedCaption, ko ("속도 (음높이는 그대로)"), 13.0f, ui::quietText);
    ui::setupSlider (*this, speedSlider, 50.0, 150.0, 5.0, 100.0, " %", 0);
    speedSlider.onValueChange = [this] { session.getTrack().setSpeed (static_cast<float> (speedSlider.getValue() / 100.0)); };

    ui::setupButton (*this, loopStartButton, ko ("여기부터 (A)"), [this]
    {
        loopStart = session.getTrack().getPosition();

        if (loopEnd <= loopStart)
            loopEnd = session.getTrack().getLength();

        applyLoop();
    });

    ui::setupButton (*this, loopEndButton, ko ("여기까지 (B)"), [this]
    {
        loopEnd = session.getTrack().getPosition();

        if (loopEnd <= loopStart)
            loopStart = 0.0;

        loopToggle.setToggleState (true, juce::dontSendNotification);
        applyLoop();
    });

    loopToggle.setButtonText (ko ("구간 반복"));
    loopToggle.onClick = [this] { applyLoop(); };
    ui::noFocus (loopToggle);
    addAndMakeVisible (loopToggle);
    ui::setupLabel (*this, loopLabel, {}, 13.0f, ui::quietText);

    // 녹음
    ui::setupLabel (*this, recordTitle, ko ("녹음 (WAV 파일)"), 17.0f, juce::Colours::white, true);
    ui::setupButton (*this, recordButton, {}, [this] { toggleRecording(); });
    ui::setupButton (*this, recordFolderButton, ko ("녹음 폴더 열기"), [this]
    {
        session.getRecordingsFolder().createDirectory();
        session.getRecordingsFolder().revealToUser();
    });

    recordTrackToggle.setButtonText (ko ("반주도 함께 녹음"));
    recordTrackToggle.setToggleState (session.getSetting ("recordTrack", true), juce::dontSendNotification);
    recordTrackToggle.onClick = [this] { session.setSetting ("recordTrack", recordTrackToggle.getToggleState()); };
    ui::noFocus (recordTrackToggle);
    addAndMakeVisible (recordTrackToggle);
    ui::setupLabel (*this, recordLabel, {}, 13.0f, ui::quietText);

    // 연주 기록
    ui::setupLabel (*this, takeTitle, ko ("연주 기록 (다시 듣기, MIDI)"), 17.0f, juce::Colours::white, true);
    ui::setupButton (*this, takeRecordButton, {}, [this]
    {
        if (session.isTakeRecording())
            session.stopTake();
        else
            session.startTake();
    });

    ui::setupButton (*this, takePlayButton, {}, [this]
    {
        if (session.isTakePlaying())
            session.stopTakePlayback();
        else
            session.playTake();
    });

    ui::setupButton (*this, takeExportButton, ko ("MIDI로 내보내기…"), [this] { exportMidi(); });
    ui::setupButton (*this, takeImportButton, ko ("MIDI 불러오기…"), [this] { importMidi(); });
    ui::setupLabel (*this, takeLabel, {}, 13.0f, ui::quietText);

    // 박자 연습
    ui::setupLabel (*this, timingTitle, ko ("박자 연습"), 17.0f, juce::Colours::white, true);
    ui::setupLabel (*this, timingLabel, {}, 13.0f, ui::quietText);
    ui::setupButton (*this, timingResetButton, ko ("지우기"), [this] { session.resetTiming(); });
    addAndMakeVisible (timingView);

    session.addChangeListener (this);
    refresh();
}

TrackPage::~TrackPage()
{
    session.removeChangeListener (this);
}

void TrackPage::paint (juce::Graphics& g)
{
    g.setColour (ui::panel);

    for (const auto& section : sections)
        g.fillRoundedRectangle (section.toFloat(), 8.0f);
}

void TrackPage::resized()
{
    auto area = getLocalBounds();

    //==============================================================================
    // 반주 (위쪽 전체 폭)
    sections[0] = area.removeFromTop (196);
    auto track = sections[0].reduced (16, 12);

    auto row = track.removeFromTop (28);
    trackTitle.setBounds (row.removeFromLeft (56));
    openButton.setBounds (row.removeFromLeft (130));
    row.removeFromLeft (6);
    unloadButton.setBounds (row.removeFromLeft (56));
    row.removeFromLeft (12);
    timeLabel.setBounds (row.removeFromRight (150));
    trackName.setBounds (row);

    track.removeFromTop (10);
    row = track.removeFromTop (30);
    playButton.setBounds (row.removeFromLeft (92));
    row.removeFromLeft (6);
    rewindButton.setBounds (row.removeFromLeft (84));
    row.removeFromLeft (12);
    bar.setBounds (row.reduced (0, 3));

    track.removeFromTop (12);
    row = track.removeFromTop (46);
    auto half = row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (12);
    gainCaption.setBounds (half.removeFromTop (18));
    gainSlider.setBounds (half.removeFromTop (26));
    half = row.withTrimmedLeft (12);
    speedCaption.setBounds (half.removeFromTop (18));
    speedSlider.setBounds (half.removeFromTop (26));

    track.removeFromTop (10);
    row = track.removeFromTop (28);
    loopStartButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (6);
    loopEndButton.setBounds (row.removeFromLeft (110));
    row.removeFromLeft (12);
    loopToggle.setBounds (row.removeFromLeft (110));
    loopLabel.setBounds (row);

    //==============================================================================
    area.removeFromTop (12);
    auto left = area.removeFromLeft (area.getWidth() / 2 - 6);
    area.removeFromLeft (12);

    // 녹음 (왼쪽 위)
    sections[1] = left.removeFromTop (120);
    auto record = sections[1].reduced (16, 12);
    recordTitle.setBounds (record.removeFromTop (26));
    record.removeFromTop (6);
    row = record.removeFromTop (28);
    recordButton.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (10);
    recordTrackToggle.setBounds (row.removeFromLeft (160));
    recordFolderButton.setBounds (row.removeFromRight (120));
    record.removeFromTop (6);
    recordLabel.setBounds (record.removeFromTop (22));

    // 연주 기록 (왼쪽 아래)
    left.removeFromTop (12);
    sections[2] = left;
    auto take = sections[2].reduced (16, 12);
    takeTitle.setBounds (take.removeFromTop (26));
    take.removeFromTop (6);
    row = take.removeFromTop (28);
    takeRecordButton.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (6);
    takePlayButton.setBounds (row.removeFromLeft (120));
    take.removeFromTop (6);
    row = take.removeFromTop (28);
    takeExportButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (6);
    takeImportButton.setBounds (row.removeFromLeft (150));
    take.removeFromTop (6);
    takeLabel.setBounds (take.removeFromTop (40));

    // 박자 연습 (오른쪽)
    sections[3] = area;
    auto timing = sections[3].reduced (16, 12);
    row = timing.removeFromTop (26);
    timingResetButton.setBounds (row.removeFromRight (64));
    timingTitle.setBounds (row);
    timing.removeFromTop (4);
    timingLabel.setBounds (timing.removeFromTop (40));
    timing.removeFromTop (6);
    timingView.setBounds (timing);
}

//==============================================================================
void TrackPage::refresh()
{
    auto& track = session.getTrack();
    const auto hasTrack = session.getTrackFile() != juce::File() && track.getLength() > 0.0;

    trackName.setText (session.getTrackFile() != juce::File()
                           ? session.getTrackFile().getFileName()
                           : ko ("아직 연 음악이 없습니다. 파일을 이 화면에 끌어다 놓아도 됩니다."),
                       juce::dontSendNotification);

    playButton.setButtonText (track.isPlaying() ? ko ("일시 정지") : ko ("재생"));

    for (auto* component : std::initializer_list<juce::Component*> { &playButton, &rewindButton, &unloadButton, &loopStartButton,
                                                                      &loopEndButton, &loopToggle, &speedSlider })
        component->setEnabled (hasTrack);

    gainSlider.setValue (track.getGain(), juce::dontSendNotification);
    speedSlider.setValue (juce::roundToInt (track.getSpeed() * 100.0f), juce::dontSendNotification);

    const auto recording = session.getAudio().isRecording();
    recordButton.setButtonText (recording ? ko ("녹음 멈추기") : ko ("녹음 시작"));
    recordButton.setColour (juce::TextButton::buttonColourId,
                            recording ? recordingColour : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
    recordTrackToggle.setEnabled (! recording);

    const auto takeRecording = session.isTakeRecording();
    const auto takePlaying = session.isTakePlaying();
    takeRecordButton.setButtonText (takeRecording ? ko ("기록 멈추기") : ko ("기록 시작"));
    takeRecordButton.setColour (juce::TextButton::buttonColourId,
                                takeRecording ? recordingColour : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
    takePlayButton.setButtonText (takePlaying ? ko ("재생 멈추기") : ko ("다시 재생"));
    takePlayButton.setEnabled (session.hasTake() || takeRecording);
    takeExportButton.setEnabled (session.hasTake());
}

void TrackPage::applyLoop()
{
    session.getTrack().setLoop (loopStart, loopEnd, loopToggle.getToggleState() && loopEnd > loopStart);
}

void TrackPage::openTrack()
{
    const juce::File startFolder (session.getSetting ("trackFolder",
                                                      juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName())
                                      .toString());

    chooser = std::make_unique<juce::FileChooser> (ko ("반주로 쓸 음악 파일 고르기"), startFolder, ui::audioFilePatterns());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fileChooser)
                          {
                              if (const auto file = fileChooser.getResult(); file != juce::File())
                                  loadTrack (file);
                          });
}

void TrackPage::loadTrack (const juce::File& file)
{
    session.setSetting ("trackFolder", file.getParentDirectory().getFullPathName());

    // 곡을 통째로 읽으므로 잠깐 걸린다.
    juce::MouseCursor::showWaitCursor();
    const auto error = session.loadTrack (file);
    juce::MouseCursor::hideWaitCursor();

    if (error.isNotEmpty())
    {
        ui::showMessage (ko ("음악을 열지 못했습니다"), error);
        return;
    }

    loopStart = 0.0;
    loopEnd = 0.0;
    loopToggle.setToggleState (false, juce::dontSendNotification);
    refresh();
}

bool TrackPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && ui::isAudioFile (juce::File (files[0]));
}

void TrackPage::filesDropped (const juce::StringArray& files, int, int)
{
    if (isInterestedInFileDrag (files))
        loadTrack (juce::File (files[0]));
}

void TrackPage::toggleRecording()
{
    if (session.getAudio().isRecording())
    {
        session.stopRecording();
        return;
    }

    if (const auto error = session.startRecording (recordTrackToggle.getToggleState()); error.isNotEmpty())
        ui::showMessage (ko ("녹음을 시작하지 못했습니다"), error);
}

void TrackPage::exportMidi()
{
    const auto suggested = session.getRecordingsFolder().getChildFile ("take-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".mid");
    session.getRecordingsFolder().createDirectory();

    chooser = std::make_unique<juce::FileChooser> (ko ("연주 기록을 MIDI 파일로 내보내기"), suggested, "*.mid");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fileChooser)
                          {
                              const auto file = fileChooser.getResult();

                              if (file == juce::File())
                                  return;

                              if (const auto error = session.exportTakeAsMidi (file.withFileExtension ("mid")); error.isNotEmpty())
                                  ui::showMessage (ko ("내보내지 못했습니다"), error);
                          });
}

void TrackPage::importMidi()
{
    chooser = std::make_unique<juce::FileChooser> (ko ("불러올 MIDI 파일 고르기"), session.getRecordingsFolder(), "*.mid;*.midi");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fileChooser)
                          {
                              const auto file = fileChooser.getResult();

                              if (file == juce::File())
                                  return;

                              if (const auto message = session.importTakeFromMidi (file); message.isNotEmpty())
                                  ui::showMessage (ko ("MIDI 불러오기"), message);
                          });
}

void TrackPage::tick()
{
    auto& track = session.getTrack();
    bar.setState (track.getPosition(), track.getLength(), loopStart, loopEnd, loopToggle.getToggleState());

    if (++tickCount % 6 != 0)
        return;

    timeLabel.setText (track.getLength() > 0.0 ? ui::formatTime (track.getPosition()) + " / " + ui::formatTime (track.getLength())
                                               : juce::String(),
                       juce::dontSendNotification);

    // 곡이 끝나 저절로 멈췄을 때 버튼 글자를 맞춘다.
    const auto playText = track.isPlaying() ? ko ("일시 정지") : ko ("재생");

    if (playButton.getButtonText() != playText)
        playButton.setButtonText (playText);

    loopLabel.setText (loopEnd > loopStart ? ui::formatTime (loopStart) + "  ~  " + ui::formatTime (loopEnd)
                                           : ko ("재생하다가 반복할 구간의 처음과 끝에서 A, B를 누르세요"),
                       juce::dontSendNotification);

    auto& audio = session.getAudio();

    if (audio.isRecording())
        recordLabel.setText (ko ("녹음 중  ") + ui::formatTime (audio.getRecordedSeconds()) + "   " + audio.getRecordingFile().getFileName(),
                             juce::dontSendNotification);
    else if (audio.getRecordingFile() != juce::File())
        recordLabel.setText (ko ("저장했습니다: ") + audio.getRecordingFile().getFullPathName(), juce::dontSendNotification);
    else
        recordLabel.setText (ko ("지금 나오는 드럼 소리를 파일로 저장합니다. 메트로놈 소리는 들어가지 않습니다."), juce::dontSendNotification);

    juce::String takeText;

    if (session.isTakeRecording())
        takeText << ko ("기록 중: 타격 ") << session.getTakeHitCount() << ko ("개");
    else if (session.isTakePlaying())
        takeText << ko ("다시 재생 중  ") << ui::formatTime (session.getTakePlaybackSeconds()) << " / " << ui::formatTime (session.getTakeLength());
    else if (session.hasTake())
        takeText << ko ("기록: 타격 ") << session.getTakeHitCount() << ko ("개, 길이 ") << ui::formatTime (session.getTakeLength());
    else
        takeText << ko ("친 것을 그대로 기록해 두었다가 다시 듣거나, MIDI 파일로 내보내 다른 음악 프로그램에서 쓸 수 있습니다.");

    takeLabel.setText (takeText, juce::dontSendNotification);

    const auto& timing = session.getTiming();
    timingView.setTiming (timing);

    juce::String timingText;

    if (! session.getEngine().isMetronomeOn())
        timingText << ko ("메트로놈을 켜고 치면, 타격이 16분음표 자리에서 얼마나 벗어났는지 보여 줍니다.");
    else if (timing.count == 0)
        timingText << ko ("메트로놈에 맞춰 쳐 보세요.");
    else
        timingText << ko ("마지막 ") << (timing.lastMs > 0.0f ? "+" : "") << juce::roundToInt (timing.lastMs) << " ms"
                   << "  ·  " << ko ("평균 오차 ") << juce::roundToInt (timing.averageAbsMs) << " ms"
                   << "  ·  " << ko ("잰 타격 ") << timing.count << ko ("개");

    timingLabel.setText (timingText, juce::dontSendNotification);
}
