#include "TrackControls.h"

namespace transport
{

bool hasTrack (Session& session)
{
    return session.getTrackFile() != juce::File() && session.getTrack().getLength() > 0.0;
}

void togglePlay (Session& session)
{
    auto& track = session.getTrack();
    track.setPlaying (! track.isPlaying());
}

void rewind (Session& session)
{
    auto& track = session.getTrack();
    track.seek (track.isLoopEnabled() ? track.getLoopStart() : 0.0);
}

juce::String playText (Session& session)
{
    return session.getTrack().isPlaying() ? ko ("정지") : ko ("재생");
}

bool load (Session& session, const juce::File& file)
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
        return false;
    }

    return true;
}

void chooseAndLoad (Session& session, std::unique_ptr<juce::FileChooser>& holder, std::function<void()> onLoaded)
{
    const juce::File startFolder (session.getSetting ("trackFolder",
                                                      juce::File::getSpecialLocation (juce::File::userMusicDirectory).getFullPathName())
                                      .toString());

    ui::chooseFile (holder, ko ("반주로 쓸 음악 파일 고르기"), startFolder, ui::audioFilePatterns(),
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [&session, onLoaded = std::move (onLoaded)] (const juce::File& file)
                    {
                        if (load (session, file) && onLoaded != nullptr)
                            onLoaded();
                    });
}

} // namespace transport

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

void TrackBar::update (const hvd::TrackPlayer& track)
{
    setState (track.getPosition(), track.getLength(), track.getLoopStart(), track.getLoopEnd(), track.isLoopEnabled());
}

void TrackBar::paint (juce::Graphics& g)
{
    const auto& t = ui::theme();
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = juce::jmin (6.0f, t.controlRadius, bounds.getHeight() * 0.5f);
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
TrackStrip::TrackStrip (Session& sessionToUse)
    : session (sessionToUse)
{
    ui::setupButton (*this, openButton, ko ("반주 열기"), [this] { transport::chooseAndLoad (session, chooser, [this] { update(); }); });
    openButton.setTooltip (ko ("곡에 맞춰 칠 음악 파일 (끌어 놓아도 됨)"));
    ui::setupButton (*this, playButton, ko ("재생"), [this]
    {
        transport::togglePlay (session);
        playButton.setButtonText (transport::playText (session));
    });
    ui::setupButton (*this, rewindButton, ko ("처음으로"), [this] { transport::rewind (session); });
    ui::setButtonKind (openButton, ui::ButtonKind::quiet);
    ui::setButtonKind (rewindButton, ui::ButtonKind::quiet);

    ui::setupLabel (*this, nameLabel, {}, ui::Text::detail);
    nameLabel.setMinimumHorizontalScale (1.0f);
    ui::setupLabel (*this, timeLabel, {}, ui::Text::caption);
    timeLabel.setJustificationType (juce::Justification::centredRight);

    bar.onSeek = [this] (double seconds) { session.getTrack().seek (seconds); };
    addChildComponent (bar);

    update();
}

void TrackStrip::update()
{
    auto& track = session.getTrack();
    const auto nowHasTrack = transport::hasTrack (session);

    if (nowHasTrack != hasTrack || session.getTrackFile() != shownFile)
    {
        hasTrack = nowHasTrack;
        shownFile = session.getTrackFile();
        openButton.setVisible (! hasTrack);

        for (auto* component : std::initializer_list<juce::Component*> { &playButton, &rewindButton, &nameLabel, &timeLabel, &bar })
            component->setVisible (hasTrack);

        nameLabel.setText (shownFile.getFileNameWithoutExtension(), juce::dontSendNotification);
        nameLabel.setTooltip (shownFile.getFileName());
        tickCount = 0;
        resized();
    }

    if (! hasTrack)
        return;

    bar.update (track);

    // 글자는 열 번에 한 번 (곡이 끝나 저절로 멈추면 버튼 글자도 맞춘다).
    if (tickCount++ % 10 != 0)
        return;

    auto time = ui::formatMinutes (track.getPosition()) + " / " + ui::formatMinutes (track.getLength());

    // 속도가 100%가 아니면 붙인다. 속도 올리기 중이면 화살표도.
    const auto training = session.getLoopTrainer().isOn();

    if (const auto speed = juce::roundToInt (track.getSpeed() * 100.0f); speed != 100 || training)
        time << ui::separator() << ui::formatPercent (speed) << (training ? ko (" ↑") : juce::String());

    timeLabel.setText (time, juce::dontSendNotification);

    if (const auto text = transport::playText (session); playButton.getButtonText() != text)
        playButton.setButtonText (text);
}

void TrackStrip::resized()
{
    auto row = getLocalBounds();

    if (! hasTrack)
    {
        openButton.setBounds (row.removeFromLeft (96));
        return;
    }

    playButton.setBounds (row.removeFromLeft (64));
    row.removeFromLeft (ui::space::tight);
    rewindButton.setBounds (row.removeFromLeft (76));
    row.removeFromLeft (ui::space::small);

    nameLabel.setBounds (row.removeFromLeft (juce::jlimit (60, 220, ui::textWidth (nameLabel) + ui::space::small)));
    row.removeFromLeft (ui::space::small);
    timeLabel.setBounds (row.removeFromRight (140));
    row.removeFromRight (ui::space::small);
    bar.setBounds (row.withSizeKeepingCentre (row.getWidth(), 12));
}

void TrackStrip::paint (juce::Graphics& g)
{
    // 음악 파일을 끌고 들어오면 줄 전체를 액센트로 두른다.
    if (dragOver)
    {
        const auto& t = ui::theme();
        g.setColour (t.accent.withAlpha (0.10f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), t.controlRadius);
        g.setColour (t.accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.75f), t.controlRadius, 1.5f);
    }
}

bool TrackStrip::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && ui::isAudioFile (juce::File (files[0]));
}

void TrackStrip::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void TrackStrip::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void TrackStrip::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();

    if (isInterestedInFileDrag (files) && transport::load (session, juce::File (files[0])))
        update();
}
