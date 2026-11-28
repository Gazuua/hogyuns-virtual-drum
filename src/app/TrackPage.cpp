#include "TrackPage.h"

namespace
{

constexpr float timingRangeMs = 60.0f;

} // namespace

//==============================================================================
void TimingView::setTiming (const hvd::MetronomeTiming& newTiming)
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
    for (int age = timing.stored() - 1; age >= 0; --age)
    {
        const auto ms = timing.recent (age);
        const auto freshness = 1.0f - static_cast<float> (age) / static_cast<float> (hvd::MetronomeTiming::historySize);
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

    ui::setupButton (*this, openButton, ko ("열기"), [this] { transport::chooseAndLoad (session, chooser, [this] { refresh(); }); });
    openButton.setTooltip (ko ("반주로 쓸 음악 파일 열기"));
    ui::setupButton (*this, playButton, {}, [this] { transport::togglePlay (session); refresh(); });
    ui::setupButton (*this, rewindButton, ko ("처음으로"), [this] { transport::rewind (session); });
    ui::setupButton (*this, unloadButton, ko ("닫기"), [this] { session.unloadTrack(); });

    bar.onSeek = [this] (double seconds) { session.getTrack().seek (seconds); };
    addAndMakeVisible (bar);

    // 반주 음량: 소리에는 바로, 설정 파일에는 마우스를 놓은 뒤에 적는다.
    ui::setupLabel (*this, gainCaption, ko ("반주 음량"), ui::Text::caption);
    ui::setupSlider (*this, gainSlider, 0.0, 1.5, 0.01, 0.7);
    gainSlider.onValueChange = [this] { session.setTrackGain (static_cast<float> (gainSlider.getValue())); };

    ui::setupLabel (*this, speedCaption, ko ("속도"), ui::Text::caption);
    ui::setupSlider (*this, speedSlider, 50.0, 150.0, 5.0, 100.0, "%", 0);
    speedSlider.setTooltip (ko ("음높이는 그대로"));
    speedSlider.onValueChange = [this] { session.getTrack().setSpeed (static_cast<float> (speedSlider.getValue() / 100.0)); };

    // 반복 구간은 재생기가 가진 값 하나만 쓴다 (연주 화면의 반주 줄, 게임 시작이 함께 본다).
    ui::setupButton (*this, loopStartButton, ko ("구간 시작"), [this]
    {
        const auto& track = session.getTrack();
        const auto start = track.getPosition();
        setLoop (start, track.getLoopEnd() > start ? track.getLoopEnd() : track.getLength(), track.isLoopEnabled());
    });

    ui::setupButton (*this, loopEndButton, ko ("구간 끝"), [this]
    {
        const auto& track = session.getTrack();
        const auto end = track.getPosition();
        setLoop (end > track.getLoopStart() ? track.getLoopStart() : 0.0, end, true);
    });

    loopStartButton.setTooltip (ko ("지금 위치를 반복 구간의 시작으로"));
    loopEndButton.setTooltip (ko ("지금 위치를 반복 구간의 끝으로"));
    ui::setupToggle (*this, loopToggle, ko ("구간 반복"), [this]
    {
        const auto& track = session.getTrack();
        setLoop (track.getLoopStart(), track.getLoopEnd(), loopToggle.getToggleState());
    });
    ui::setupLabel (*this, loopLabel, {}, ui::Text::caption);

    // 속도 올리기: 구간을 돌 때마다 박자를 재어, 박에 맞음이 기준을 넘으면 5%씩 올린다.
    ui::setupToggle (*this, trainToggle, ko ("속도 올리기"), [this]
    {
        auto& trainer = session.getLoopTrainer();

        if (trainToggle.getToggleState())
            trainToggle.setToggleState (trainer.start (session.getTrackFile()), juce::dontSendNotification);
        else
            trainer.stop();

        updateLoop();
    });
    trainToggle.setTooltip (ko ("구간을 한 바퀴 돌 때마다 박자를 재어 속도를 올린다"));
    ui::setupLabel (*this, trainLabel, {}, ui::Text::caption);

    // 속도 올리기 설정. 끌어서 바꾸고 두 번 눌러 적는다. 켜 둔 동안 바꿔도 되고, 시작은 다음에 켤 때부터.
    const char* const captions[] = { "시작", "끝", "올림", "기준" };
    const char* const tips[] = { "켤 때의 속도", "여기까지 올린다", "한 번에 올리는 폭", "한 바퀴의 박에 맞음이 이 이상이면 올린다" };
    ui::NumberField* const fields[] = { &trainStart, &trainTarget, &trainStep, &trainPass };
    const auto settings = session.getLoopTrainer().getSettings();
    const int values[] = { settings.startPercent, settings.targetPercent, settings.stepPercent, settings.passPercent };

    for (std::size_t i = 0; i < trainCaptions.size(); ++i)
    {
        ui::setupLabel (*this, trainCaptions[i], ko (captions[i]), ui::Text::caption);
        trainCaptions[i].setTooltip (ko (tips[i]));
        ui::setTextStyle (*fields[i], ui::Text::body);
        fields[i]->setValue (values[i]);
        fields[i]->setTooltip (ko (tips[i]) + ko (" (끌거나 두 번 눌러 적기)"));
        fields[i]->onValueChange = [this, field = fields[i]] (int) { trainerSettingChanged (*field); };
        addAndMakeVisible (*fields[i]);
    }

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
    sections[0] = area.removeFromTop (236);
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

        // 속도 올리기: [켜기] 시작 [70%] 끝 [100%] 올림 [5%] 기준 [80%]  지난 바퀴 결과
        track.removeFromTop (ui::space::medium);
        row = track.removeFromTop (ui::space::control);
        trainToggle.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (ui::space::small);

        ui::NumberField* const fields[] = { &trainStart, &trainTarget, &trainStep, &trainPass };

        for (std::size_t i = 0; i < trainCaptions.size(); ++i)
        {
            trainCaptions[i].setBounds (row.removeFromLeft (ui::textWidth (trainCaptions[i]) + trainCaptions[i].getBorderSize().getLeftAndRight()));
            fields[i]->setBounds (row.removeFromLeft (60));
            row.removeFromLeft (ui::space::large);
        }

        trainLabel.setBounds (row);
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
    const auto nowHasTrack = transport::hasTrack (session);

    // 곡이 없으면 반주 칸에는 끌어 놓는 자리와 [열기]만 둔다.
    for (auto* component : std::initializer_list<juce::Component*> { &trackName, &timeLabel, &playButton, &rewindButton, &unloadButton, &bar,
                                                                      &gainCaption, &gainSlider, &speedCaption, &speedSlider,
                                                                      &loopStartButton, &loopEndButton, &loopToggle, &loopLabel, &trainToggle, &trainLabel,
                                                                      &trainCaptions[0], &trainCaptions[1], &trainCaptions[2], &trainCaptions[3],
                                                                      &trainStart, &trainTarget, &trainStep, &trainPass })
        component->setVisible (nowHasTrack);

    dropLabel.setVisible (! nowHasTrack);

    if (nowHasTrack != hasTrack)
    {
        hasTrack = nowHasTrack;
        resized();
    }

    trackName.setText (session.getTrackFile().getFileName(), juce::dontSendNotification);
    playButton.setButtonText (transport::playText (session));
    updateLoop();

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

void TrackPage::setLoop (double start, double end, bool enabled)
{
    session.getTrack().setLoop (start, end, enabled);
    updateLoop();
}

void TrackPage::trainerSettingChanged (ui::NumberField& changed)
{
    // 끝은 시작보다 작지 않게: 바꾼 쪽에 맞춰 다른 쪽을 민다.
    if (trainTarget.getValue() < trainStart.getValue())
        (&changed == &trainStart ? trainTarget : trainStart).setValue (changed.getValue());

    LoopTrainer::Settings settings;
    settings.startPercent = trainStart.getValue();
    settings.targetPercent = trainTarget.getValue();
    settings.stepPercent = trainStep.getValue();
    settings.passPercent = trainPass.getValue();

    session.getLoopTrainer().setSettings (settings);
    session.markSettingEdited ("speedTrainer", settings.toVar());    // 마우스를 놓은 뒤에 저장한다
    updateLoop();
}

void TrackPage::updateLoop()
{
    const auto& track = session.getTrack();
    const auto hasRange = track.getLoopEnd() > track.getLoopStart();
    loopToggle.setEnabled (hasRange);
    loopToggle.setToggleState (track.isLoopEnabled(), juce::dontSendNotification);
    loopLabel.setText (hasRange ? ui::formatTime (track.getLoopStart()) + "  ~  " + ui::formatTime (track.getLoopEnd()) : ko ("구간 없음"),
                       juce::dontSendNotification);

    // 속도 올리기는 구간이 있어야 켠다. 구간을 끄거나 곡을 바꾸면 저절로 꺼진다.
    const auto& trainer = session.getLoopTrainer();
    trainToggle.setEnabled (hasRange);
    trainToggle.setToggleState (trainer.isOn(), juce::dontSendNotification);
    trainLabel.setText (trainer.getStatusText(), juce::dontSendNotification);

    if (! speedSlider.isMouseButtonDown())
        speedSlider.setValue (juce::roundToInt (track.getSpeed() * 100.0f), juce::dontSendNotification);
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

    if (isInterestedInFileDrag (files) && transport::load (session, juce::File (files[0])))
        refresh();
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
    // 곡의 길이는 오디오 스레드가 곡을 받은 뒤에 정해진다. 열자마자 부른 refresh가 놓쳤으면 여기서 맞춘다.
    if (transport::hasTrack (session) != hasTrack)
        refresh();

    auto& track = session.getTrack();
    bar.update (track);

    if (++tickCount % 6 != 0)
        return;

    timeLabel.setText (track.getLength() > 0.0 ? ui::formatTime (track.getPosition()) + " / " + ui::formatTime (track.getLength())
                                               : juce::String(),
                       juce::dontSendNotification);

    // 곡이 끝나 저절로 멈췄을 때 버튼 글자를 맞춘다. 게임이 구간을 끌 수도 있다.
    if (const auto playText = transport::playText (session); playButton.getButtonText() != playText)
        playButton.setButtonText (playText);

    updateLoop();

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
        const auto used = timing.stored();
        auto within = 0;

        for (int age = 0; age < used; ++age)
            within += std::abs (timing.recent (age)) <= ui::timing::onBeatMs ? 1 : 0;

        timingText << ko ("마지막 ") << ui::formatSignedMs (timing.lastMs)
                   << ui::separator() << ko ("평균 오차 ") << ui::formatMs (timing.averageAbsMs)
                   << ui::separator() << ko ("박에 맞음 ") << ui::formatPercent (100.0 * within / used);
    }

    timingLabel.setText (timingText, juce::dontSendNotification);
}
