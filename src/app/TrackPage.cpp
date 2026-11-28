#include "TrackPage.h"

namespace
{

constexpr float timingRangeMs = 60.0f;

/** Session::Timing의 고리 버퍼에서 age번째로 최근 값 (0이 가장 최근). */
float recentOffset (const Session::Timing& timing, int age)
{
    constexpr auto size = Session::Timing::historySize;
    return timing.offsetsMs[static_cast<std::size_t> (((timing.count - 1 - age) % size + size) % size)];
}

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
    const auto& t = ui::theme();
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = juce::jmin (6.0f, t.controlRadius);
    g.setColour (t.surfaceSunken);
    g.fillRoundedRectangle (bounds, radius);

    if (length <= 0.0)
        return;

    const auto toX = [&bounds, this] (double seconds)
    {
        return bounds.getX() + bounds.getWidth() * static_cast<float> (juce::jlimit (0.0, 1.0, seconds / length));
    };

    // 반복 구간
    if (loopEnd > loopStart)
    {
        g.setColour (t.accent.withAlpha (loopEnabled ? 0.35f : 0.15f));
        g.fillRect (juce::Rectangle<float> (toX (loopStart), bounds.getY(), toX (loopEnd) - toX (loopStart), bounds.getHeight()));
    }

    // 지나온 부분과 지금 위치
    const auto x = toX (position);
    g.setColour (t.info.withAlpha (0.35f));
    g.fillRoundedRectangle (bounds.withRight (x), radius);
    g.setColour (t.text);
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
    const auto& t = ui::theme();
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (t.surfaceSunken);
    g.fillRoundedRectangle (bounds, juce::jmin (6.0f, t.controlRadius));

    const auto centreX = bounds.getCentreX();
    const auto toX = [&bounds, centreX] (float ms)
    {
        return centreX + juce::jlimit (-1.0f, 1.0f, ms / timingRangeMs) * (bounds.getWidth() * 0.5f - 10.0f);
    };

    // "박에 맞음" 범위와 가운데 선
    constexpr auto onBeat = ui::timing::onBeatMs;
    g.setColour (t.good.withAlpha (0.14f));
    g.fillRect (juce::Rectangle<float> (toX (-onBeat), bounds.getY(), toX (onBeat) - toX (-onBeat), bounds.getHeight()));
    g.setColour (t.text.withAlpha (0.5f));
    g.fillRect (centreX - 0.5f, bounds.getY(), 1.0f, bounds.getHeight());

    g.setColour (t.textMuted);
    g.setFont (juce::FontOptions (ui::type::caption));
    g.drawText (ko ("빠름"), getLocalBounds().reduced (8, 2), juce::Justification::topLeft);
    g.drawText (ko ("늦음"), getLocalBounds().reduced (8, 2), juce::Justification::topRight);
    g.drawText (ko ("박에 맞음"), juce::Rectangle<float> (toX (-onBeat) + 6.0f, bounds.getY() + 2.0f, centreX - toX (-onBeat) - 8.0f, 14.0f),
                juce::Justification::centredLeft, true);

    // 최근 타격일수록 아래쪽에, 진하게 그린다.
    const auto used = juce::jmin (timing.count, Session::Timing::historySize);

    for (int age = used - 1; age >= 0; --age)
    {
        const auto ms = recentOffset (timing, age);
        const auto freshness = 1.0f - static_cast<float> (age) / static_cast<float> (Session::Timing::historySize);
        const auto y = bounds.getY() + 22.0f + (bounds.getHeight() - 34.0f) * freshness;

        g.setColour ((std::abs (ms) <= onBeat ? t.good : t.warning).withAlpha (0.25f + 0.75f * freshness));
        g.fillEllipse (toX (ms) - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }
}

//==============================================================================
TrackPage::TrackPage (Session& sessionToUse)
    : session (sessionToUse)
{
    // 반주
    ui::setupLabel (*this, trackTitle, ko ("반주"), ui::Text::section);
    ui::setupLabel (*this, trackName, {});
    ui::setupLabel (*this, timeLabel, {}, ui::Text::hint);
    timeLabel.setJustificationType (juce::Justification::centredRight);

    // 곡이 없을 때: 반주 칸 전체가 끌어 놓는 자리
    ui::setupLabel (*this, dropLabel, ko ("음악 파일을 끌어 놓기"), ui::Text::hint);
    dropLabel.setJustificationType (juce::Justification::centred);

    ui::setupButton (*this, openButton, ko ("열기"), [this] { openTrack(); });
    openButton.setTooltip (ko ("반주로 쓸 음악 파일 열기"));
    ui::setupButton (*this, playButton, {}, [this] { session.getTrack().setPlaying (! session.getTrack().isPlaying()); refresh(); });
    ui::setupButton (*this, rewindButton, ko ("처음으로"), [this] { session.getTrack().seek (loopToggle.getToggleState() ? loopStart : 0.0); });
    ui::setupButton (*this, unloadButton, ko ("닫기"), [this] { session.unloadTrack(); });

    bar.onSeek = [this] (double seconds) { session.getTrack().seek (seconds); };
    addAndMakeVisible (bar);

    // 반주 음량: 소리에는 바로, 설정 파일에는 마우스를 놓은 뒤에 적는다.
    ui::setupLabel (*this, gainCaption, ko ("반주 음량"), ui::Text::caption);
    ui::setupSlider (*this, gainSlider, 0.0, 1.5, 0.01, 0.7);
    gainSaver.save = [this] { session.setTrackGain (static_cast<float> (gainSlider.getValue())); };
    gainSlider.onValueChange = [this]
    {
        session.getTrack().setGain (static_cast<float> (gainSlider.getValue()));
        gainSaver.mark();
    };

    ui::setupLabel (*this, speedCaption, ko ("속도"), ui::Text::caption);
    ui::setupSlider (*this, speedSlider, 50.0, 150.0, 5.0, 100.0, "%", 0);
    speedSlider.setTooltip (ko ("음높이는 그대로"));
    speedSlider.onValueChange = [this] { session.getTrack().setSpeed (static_cast<float> (speedSlider.getValue() / 100.0)); };

    ui::setupButton (*this, loopStartButton, ko ("구간 시작"), [this]
    {
        loopStart = session.getTrack().getPosition();

        if (loopEnd <= loopStart)
            loopEnd = session.getTrack().getLength();

        applyLoop();
    });

    ui::setupButton (*this, loopEndButton, ko ("구간 끝"), [this]
    {
        loopEnd = session.getTrack().getPosition();

        if (loopEnd <= loopStart)
            loopStart = 0.0;

        loopToggle.setToggleState (true, juce::dontSendNotification);
        applyLoop();
    });

    loopStartButton.setTooltip (ko ("지금 위치를 반복 구간의 시작으로"));
    loopEndButton.setTooltip (ko ("지금 위치를 반복 구간의 끝으로"));
    ui::setupToggle (*this, loopToggle, ko ("구간 반복"), [this] { applyLoop(); });
    ui::setupLabel (*this, loopLabel, {}, ui::Text::caption);

    // 녹음
    ui::setupLabel (*this, recordTitle, ko ("녹음"), ui::Text::section);
    ui::setupButton (*this, recordButton, {}, [this] { toggleRecording(); });
    recordButton.setTooltip (ko ("드럼 소리를 WAV로 저장. 메트로놈은 빠진다"));
    ui::setupButton (*this, recordFolderButton, ko ("폴더 열기"), [this]
    {
        session.getRecordingsFolder().createDirectory();
        session.getRecordingsFolder().revealToUser();
    });

    ui::setupToggle (*this, recordTrackToggle, ko ("반주 포함"), [this] { session.setSetting ("recordTrack", recordTrackToggle.getToggleState()); },
                     static_cast<bool> (session.getSetting ("recordTrack", true)));
    ui::setupLabel (*this, recordLabel, {}, ui::Text::caption);

    // MIDI: 친 것을 기록해 다시 듣거나 MIDI 파일로 내보낸다.
    ui::setupLabel (*this, takeTitle, "MIDI", ui::Text::section);
    takeTitle.setTooltip (ko ("친 것을 기록해 다시 듣거나 MIDI 파일로 내보낸다"));
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

    ui::setupButton (*this, takeExportButton, ko ("내보내기"), [this] { exportMidi(); });
    ui::setupButton (*this, takeImportButton, ko ("불러오기"), [this] { importMidi(); });
    takeExportButton.setTooltip (ko ("기록을 MIDI 파일로 내보내기"));
    takeImportButton.setTooltip (ko ("MIDI 파일을 기록으로 불러오기"));
    ui::setupLabel (*this, takeLabel, {}, ui::Text::caption);

    // 박자 연습
    ui::setupLabel (*this, timingTitle, ko ("박자 연습"), ui::Text::section);
    ui::setupLabel (*this, timingLabel, {}, ui::Text::caption);
    timingLabel.setTooltip (ko ("메트로놈의 16분음표 자리에서 벗어난 정도. 박에 맞음: ") + ui::plusMinus()
                            + ui::formatMs (ui::timing::onBeatMs) + ko (" 안"));
    ui::setupButton (*this, timingResetButton, ko ("초기화"), [this] { session.resetTiming(); });
    addAndMakeVisible (timingView);

    // 버튼 종류: 재생이 주요, 녹음은 켜지면 빨간 바탕, 폴더 열기·초기화는 조용한 버튼
    ui::setButtonKind (playButton, ui::ButtonKind::primary);
    ui::setButtonKind (recordButton, ui::ButtonKind::danger);
    ui::setButtonKind (takeRecordButton, ui::ButtonKind::danger);
    ui::setButtonKind (recordFolderButton, ui::ButtonKind::quiet);
    ui::setButtonKind (timingResetButton, ui::ButtonKind::quiet);

    session.addChangeListener (this);
    refresh();
}

TrackPage::~TrackPage()
{
    session.removeChangeListener (this);
    gainSaver.flush();
}

void TrackPage::paint (juce::Graphics& g)
{
    for (const auto& section : sections)
        ui::fillPanel (g, section);

    // 곡이 없을 때의 끌어 놓는 자리 (끌고 들어오면 액센트로)
    if (! hasTrack && ! dropZone.isEmpty())
    {
        const auto& t = ui::theme();
        const auto zone = dropZone.toFloat().reduced (0.5f);
        juce::Path outline;
        outline.addRoundedRectangle (zone, t.controlRadius + 2.0f);

        const float dashes[] = { 6.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);

        if (dragOver)
        {
            g.setColour (t.accent.withAlpha (0.10f));
            g.fillPath (outline);
        }

        g.setColour (dragOver ? t.accent : t.textFaint.withMultipliedAlpha (0.7f));
        g.fillPath (dashed);
    }
}

void TrackPage::resized()
{
    auto area = getLocalBounds();

    //==============================================================================
    // 반주 (위쪽 전체 폭)
    sections[0] = area.removeFromTop (196);
    auto track = sections[0].reduced (ui::space::large, ui::space::medium);

    auto row = track.removeFromTop (ui::space::control);
    trackTitle.setBounds (row.removeFromLeft (56));

    if (! hasTrack)
    {
        // 곡이 없으면 끌어 놓는 칸 하나: 가운데에 안내와 [열기]
        track.removeFromTop (ui::space::small);
        dropZone = track;
        auto centre = dropZone.withSizeKeepingCentre (dropZone.getWidth(), 24 + ui::space::small + ui::space::control);
        dropLabel.setBounds (centre.removeFromTop (24));
        centre.removeFromTop (ui::space::small);
        openButton.setBounds (centre.withSizeKeepingCentre (96, ui::space::control));
    }
    else
    {
        dropZone = {};
        openButton.setBounds (row.removeFromLeft (72));
        row.removeFromLeft (ui::space::small);
        unloadButton.setBounds (row.removeFromLeft (56));
        row.removeFromLeft (ui::space::medium);
        timeLabel.setBounds (row.removeFromRight (150));
        trackName.setBounds (row);

        track.removeFromTop (ui::space::medium);
        row = track.removeFromTop (30);
        playButton.setBounds (row.removeFromLeft (92));
        row.removeFromLeft (ui::space::small);
        rewindButton.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (ui::space::medium);
        bar.setBounds (row.reduced (0, 3));

        track.removeFromTop (ui::space::medium);
        row = track.removeFromTop (46);
        auto half = row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (ui::space::medium);
        gainCaption.setBounds (half.removeFromTop (18));
        gainSlider.setBounds (half.removeFromTop (26));
        half = row.withTrimmedLeft (ui::space::medium);
        speedCaption.setBounds (half.removeFromTop (18));
        speedSlider.setBounds (half.removeFromTop (26));

        track.removeFromTop (ui::space::medium);
        row = track.removeFromTop (ui::space::control);
        loopStartButton.setBounds (row.removeFromLeft (96));
        row.removeFromLeft (ui::space::small);
        loopEndButton.setBounds (row.removeFromLeft (96));
        row.removeFromLeft (ui::space::medium);
        loopToggle.setBounds (row.removeFromLeft (110));
        loopLabel.setBounds (row);
    }

    //==============================================================================
    area.removeFromTop (ui::space::medium);
    auto left = area.removeFromLeft (area.getWidth() / 2 - ui::space::medium / 2);
    area.removeFromLeft (ui::space::medium);

    // 녹음 (왼쪽 위)
    sections[1] = left.removeFromTop (120);
    auto record = sections[1].reduced (ui::space::large, ui::space::medium);
    recordTitle.setBounds (record.removeFromTop (26));
    record.removeFromTop (ui::space::small);
    row = record.removeFromTop (ui::space::control);
    recordButton.setBounds (row.removeFromLeft (96));
    row.removeFromLeft (ui::space::medium);
    recordTrackToggle.setBounds (row.removeFromLeft (120));
    recordFolderButton.setBounds (row.removeFromRight (84));
    record.removeFromTop (ui::space::small);
    recordLabel.setBounds (record.removeFromTop (22));

    // MIDI (왼쪽 아래)
    left.removeFromTop (ui::space::medium);
    sections[2] = left;
    auto take = sections[2].reduced (ui::space::large, ui::space::medium);
    takeTitle.setBounds (take.removeFromTop (26));
    take.removeFromTop (ui::space::small);
    row = take.removeFromTop (ui::space::control);
    takeRecordButton.setBounds (row.removeFromLeft (96));
    row.removeFromLeft (ui::space::small);
    takePlayButton.setBounds (row.removeFromLeft (96));
    row.removeFromLeft (ui::space::large);
    takeExportButton.setBounds (row.removeFromLeft (96));
    row.removeFromLeft (ui::space::small);
    takeImportButton.setBounds (row.removeFromLeft (96));
    take.removeFromTop (ui::space::small);
    takeLabel.setBounds (take.removeFromTop (22));

    // 박자 연습 (오른쪽)
    sections[3] = area;
    auto timing = sections[3].reduced (ui::space::large, ui::space::medium);
    row = timing.removeFromTop (26);
    timingResetButton.setBounds (row.removeFromRight (72));
    timingTitle.setBounds (row);
    timing.removeFromTop (ui::space::tight);
    timingLabel.setBounds (timing.removeFromTop (22));
    timing.removeFromTop (ui::space::small);
    timingView.setBounds (timing);

    repaint();
}

//==============================================================================
void TrackPage::refresh()
{
    auto& track = session.getTrack();
    const auto nowHasTrack = session.getTrackFile() != juce::File() && track.getLength() > 0.0;

    // 곡이 없으면 반주 칸에는 끌어 놓는 자리와 [열기]만 둔다.
    for (auto* component : std::initializer_list<juce::Component*> { &trackName, &timeLabel, &playButton, &rewindButton, &unloadButton, &bar,
                                                                      &gainCaption, &gainSlider, &speedCaption, &speedSlider,
                                                                      &loopStartButton, &loopEndButton, &loopToggle, &loopLabel })
        component->setVisible (nowHasTrack);

    dropLabel.setVisible (! nowHasTrack);

    if (nowHasTrack != hasTrack)
    {
        hasTrack = nowHasTrack;
        resized();
    }

    trackName.setText (session.getTrackFile().getFileName(), juce::dontSendNotification);
    playButton.setButtonText (track.isPlaying() ? ko ("정지") : ko ("재생"));

    if (! gainSlider.isMouseButtonDown())
        gainSlider.setValue (track.getGain(), juce::dontSendNotification);

    speedSlider.setValue (juce::roundToInt (track.getSpeed() * 100.0f), juce::dontSendNotification);

    const auto recording = session.getAudio().isRecording();
    recordButton.setButtonText (recording ? ko ("정지") : ko ("녹음"));
    recordButton.setToggleState (recording, juce::dontSendNotification);    // 녹음 중이면 빨간 바탕
    recordTrackToggle.setEnabled (! recording);

    const auto takeRecording = session.isTakeRecording();
    const auto takePlaying = session.isTakePlaying();
    takeRecordButton.setButtonText (takeRecording ? ko ("정지") : ko ("기록"));
    takeRecordButton.setToggleState (takeRecording, juce::dontSendNotification);
    takePlayButton.setButtonText (takePlaying ? ko ("정지") : ko ("다시 듣기"));
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

    ui::chooseFile (chooser, ko ("반주로 쓸 음악 파일 고르기"), startFolder, ui::audioFilePatterns(),
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this] (const juce::File& file) { loadTrack (file); });
}

void TrackPage::loadTrack (const juce::File& file)
{
    session.setSetting ("trackFolder", file.getParentDirectory().getFullPathName());

    // 곡을 통째로 읽으므로 잠깐 걸린다.
    juce::String error;

    {
        const ui::WaitCursor wait;
        error = session.loadTrack (file);
    }

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

void TrackPage::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void TrackPage::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void TrackPage::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();

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

    ui::chooseFile (chooser, ko ("기록을 MIDI 파일로 내보내기"), suggested, "*.mid",
                    juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                        | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this] (const juce::File& file)
                    {
                        if (const auto error = session.exportTakeAsMidi (file.withFileExtension ("mid")); error.isNotEmpty())
                            ui::showMessage (ko ("내보내지 못했습니다"), error);
                    });
}

void TrackPage::importMidi()
{
    ui::chooseFile (chooser, ko ("불러올 MIDI 파일 고르기"), session.getRecordingsFolder(), "*.mid;*.midi",
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this] (const juce::File& file)
                    {
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
    const auto playText = track.isPlaying() ? ko ("정지") : ko ("재생");

    if (playButton.getButtonText() != playText)
        playButton.setButtonText (playText);

    loopLabel.setText (loopEnd > loopStart ? ui::formatTime (loopStart) + "  ~  " + ui::formatTime (loopEnd) : ko ("구간 없음"),
                       juce::dontSendNotification);

    // 녹음: 파일 이름만 적고, 전체 경로는 툴팁으로
    auto& audio = session.getAudio();

    if (audio.isRecording())
    {
        recordLabel.setText (ko ("녹음 중  ") + ui::formatTime (audio.getRecordedSeconds()) + ui::separator() + audio.getRecordingFile().getFileName(),
                             juce::dontSendNotification);
        recordLabel.setTooltip (audio.getRecordingFile().getFullPathName());
    }
    else if (audio.getRecordingFile() != juce::File())
    {
        recordLabel.setText (ko ("저장함: ") + audio.getRecordingFile().getFileName(), juce::dontSendNotification);
        recordLabel.setTooltip (audio.getRecordingFile().getFullPathName());
    }
    else
    {
        recordLabel.setText ({}, juce::dontSendNotification);
    }

    juce::String takeText;

    if (session.isTakeRecording())
        takeText << ko ("기록 중  ·  타격 ") << session.getTakeHitCount() << ko ("개");
    else if (session.isTakePlaying())
        takeText << ko ("다시 듣는 중  ") << ui::formatTime (session.getTakePlaybackSeconds()) << " / " << ui::formatTime (session.getTakeLength());
    else if (session.hasTake())
        takeText << ko ("타격 ") << session.getTakeHitCount() << ko ("개") << ui::separator() << ui::formatTime (session.getTakeLength());

    takeLabel.setText (takeText, juce::dontSendNotification);

    const auto& timing = session.getTiming();
    timingView.setTiming (timing);

    juce::String timingText;

    if (! session.getEngine().isMetronomeOn())
    {
        timingText << ko ("메트로놈을 켜고 치기");
    }
    else if (timing.count == 0)
    {
        timingText << ko ("메트로놈에 맞춰 치기");
    }
    else
    {
        // 최근 타격 가운데 박에 맞은 비율 (연습 기록의 "박에 맞음"과 같은 기준)
        const auto used = juce::jmin (timing.count, Session::Timing::historySize);
        auto within = 0;

        for (int age = 0; age < used; ++age)
            within += std::abs (recentOffset (timing, age)) <= ui::timing::onBeatMs ? 1 : 0;

        timingText << ko ("마지막 ") << ui::formatSignedMs (timing.lastMs)
                   << ui::separator() << ko ("평균 오차 ") << ui::formatMs (timing.averageAbsMs)
                   << ui::separator() << ko ("박에 맞음 ") << ui::formatPercent (100.0 * within / used);
    }

    timingLabel.setText (timingText, juce::dontSendNotification);
}
