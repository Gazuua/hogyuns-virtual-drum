#include "MainComponent.h"

#include "GamePage.h"
#include "KeysPage.h"
#include "KitPage.h"
#include "MixerPage.h"
#include "PlayPage.h"
#include "PracticePage.h"
#include "TrackPage.h"

namespace
{

const juce::Colour metronomeOnColour { 0xff7a5a1e };
constexpr int maxMessageLines = 5;

const char* const pageNames[MainComponent::numPages] = { "play", "kit", "keys", "mixer", "track", "game", "practice" };
const char* const pageTitles[MainComponent::numPages] = { "연주", "킷", "키 배열", "믹서", "반주 · 녹음", "리듬 게임", "연습 기록" };

const char* const pageHints[MainComponent::numPages] = {
    "이 창이 맨 앞에 있을 때 키를 누르면 연주됩니다. 패드를 클릭해도 소리가 납니다.",
    "패드를 골라 샘플을 바꾸고 소리를 다듬습니다. 고치면 바로 저장됩니다.",
    "키를 골라 그 키가 할 일을 정합니다. 고치면 바로 저장되고 연주에 반영됩니다.",
    "패드별 음량과 드럼 전체의 이펙트입니다. 킷 파일에 함께 저장됩니다.",
    "음악에 맞춰 치고, 친 것을 녹음하거나 기록해 다시 들어 봅니다.",
    "음악 폴더의 곡에서 드럼을 받아 적어, 내려오는 노트에 맞춰 칩니다.",
    "곡을 틀고 따라 친 것이 저절로 쌓입니다. 박자가 얼마나 맞았는지 봅니다.",
};

} // namespace

MainComponent::MainComponent (const Options& options)
    : session (options)
{
    titleLabel.setText ("Hogyun's Virtual Drum", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);

    ui::setupLabel (*this, hintLabel, {}, 14.0f, ui::quietText);

    pages[0] = std::make_unique<PlayPage> (session);
    pages[1] = std::make_unique<KitPage> (session);
    pages[2] = std::make_unique<KeysPage> (session);
    pages[3] = std::make_unique<MixerPage> (session);
    pages[4] = std::make_unique<TrackPage> (session);
    pages[5] = std::make_unique<GamePage> (session);
    pages[6] = std::make_unique<PracticePage> (session);

    for (int i = 0; i < numPages; ++i)
    {
        const auto index = static_cast<std::size_t> (i);
        addChildComponent (*pages[index]);

        ui::setupButton (*this, tabButtons[index], ko (pageTitles[index]), [this, i] { showPage (i); });
        tabButtons[index].setClickingTogglesState (true);
        tabButtons[index].setRadioGroupId (100);
        tabButtons[index].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a5d6e));
        tabButtons[index].setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0)
                                             | (i < numPages - 1 ? juce::Button::ConnectedOnRight : 0));
    }

    ui::setupButton (*this, audioButton, ko ("오디오 · MIDI 설정"), [this] { showAudioSettings(); });
    ui::setupButton (*this, tempoDownButton, "-", [this] { session.getEngine().setTempo (std::round (session.getEngine().getTempo()) - 1.0f); updateTempoControls(); });
    ui::setupButton (*this, tempoUpButton, "+", [this] { session.getEngine().setTempo (std::round (session.getEngine().getTempo()) + 1.0f); updateTempoControls(); });
    ui::setupButton (*this, tapButton, ko ("탭"), [this] { tapTempoFromButton(); });
    ui::setupButton (*this, metronomeButton, {}, [this]
    {
        session.getEngine().setMetronomeOn (! session.getEngine().isMetronomeOn());
        session.resetTiming();
        updateTempoControls();
    });

    tempoLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    tempoLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (tempoLabel);

    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setColour (juce::Label::textColourId, ui::quietText);
    statusLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (statusLabel);

    messageLabel.setFont (juce::FontOptions (13.0f));
    messageLabel.setJustificationType (juce::Justification::topLeft);
    messageLabel.setColour (juce::Label::textColourId, ui::warningText);
    addAndMakeVisible (messageLabel);

    session.addChangeListener (this);

    setWantsKeyboardFocus (true);
    setSize (1120, 760);

    showPage (0);
    refreshStatus();
    updateTempoControls();
    startTimerHz (60);
}

MainComponent::~MainComponent()
{
    stopTimer();
    session.removeChangeListener (this);
    settingsDialog.deleteAndZero();

    // 화면들이 세션을 쓰고 있으므로 세션보다 먼저 없앤다.
    for (auto& page : pages)
        page = nullptr;
}

int MainComponent::pageIndexFromName (const juce::String& name)
{
    for (int i = 0; i < numPages; ++i)
        if (name == pageNames[i])
            return i;

    return 0;
}

void MainComponent::showPage (int index)
{
    const auto previous = currentPage;
    currentPage = juce::jlimit (0, numPages - 1, index);

    if (previous != currentPage)
        pages[static_cast<std::size_t> (previous)]->pageHidden();

    for (int i = 0; i < numPages; ++i)
    {
        const auto shown = i == currentPage;
        pages[static_cast<std::size_t> (i)]->setVisible (shown);
        tabButtons[static_cast<std::size_t> (i)].setToggleState (shown, juce::dontSendNotification);
    }

    hintLabel.setText (ko (pageHints[currentPage]), juce::dontSendNotification);
    pages[static_cast<std::size_t> (currentPage)]->pageShown();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (18, 14);

    auto header = area.removeFromTop (32);
    titleLabel.setBounds (header.removeFromLeft (236));
    audioButton.setBounds (header.removeFromRight (150));

    // 탭은 창이 좁으면 조금씩 좁힌다.
    const auto tabWidth = juce::jlimit (76, 100, (header.getWidth() - 8) / numPages);

    for (auto& button : tabButtons)
        button.setBounds (header.removeFromLeft (tabWidth));

    area.removeFromTop (8);

    // 둘째 줄: 화면 설명은 왼쪽, 템포 조절은 오른쪽
    auto tempoRow = area.removeFromTop (28);
    metronomeButton.setBounds (tempoRow.removeFromRight (112));
    tempoRow.removeFromRight (8);
    tapButton.setBounds (tempoRow.removeFromRight (48));
    tempoRow.removeFromRight (8);
    tempoUpButton.setBounds (tempoRow.removeFromRight (30));
    tempoLabel.setBounds (tempoRow.removeFromRight (88));
    tempoDownButton.setBounds (tempoRow.removeFromRight (30));
    tempoRow.removeFromRight (12);
    hintLabel.setBounds (tempoRow);

    area.removeFromTop (10);

    // 아래: 문제 목록(있을 때만)과 상태 두 줄
    statusLabel.setBounds (area.removeFromBottom (38));
    area.removeFromBottom (4);

    if (messageLabel.getText().isNotEmpty())
    {
        const auto numLines = juce::StringArray::fromLines (messageLabel.getText()).size();
        messageLabel.setBounds (area.removeFromBottom (numLines * 18 + 6));
        area.removeFromBottom (4);
    }
    else
    {
        messageLabel.setBounds ({});
    }

    for (auto& page : pages)
        if (page != nullptr)
            page->setBounds (area);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // Esc는 보고 있는 화면에 먼저 넘긴다 (리듬 게임의 잠깐 멈춤).
    if (key == juce::KeyPress::escapeKey)
        pages[static_cast<std::size_t> (currentPage)]->escapePressed();

    // 연주 키는 입력 스레드가 직접 받는다. 여기서는 키가 다른 곳으로 흘러가
    // 포커스가 옮겨지거나 경고음이 나지 않도록 모두 삼킨다.
    return true;
}

void MainComponent::parentHierarchyChanged()
{
    // 이 창이 맨 앞에 있을 때만 연주되게 한다. 설정 창이 떠 있는 동안에는 치지 않는다.
    if (auto* peer = getPeer())
        session.getKeyboard().setActiveWindow (peer->getNativeHandle());
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshStatus();
    updateTempoControls();
}

void MainComponent::updateTempoControls()
{
    auto& engine = session.getEngine();
    tempoLabel.setText (ko ("템포 ") + juce::String (juce::roundToInt (engine.getTempo())), juce::dontSendNotification);

    const auto on = engine.isMetronomeOn();
    metronomeButton.setButtonText (on ? (beatFlashTicks > 0 ? ko ("메트로놈 ●") : ko ("메트로놈 ○")) : ko ("메트로놈"));
    metronomeButton.setColour (juce::TextButton::buttonColourId,
                               on ? metronomeOnColour
                                  : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
}

void MainComponent::tapTempoFromButton()
{
    // 버튼을 두 번 이상 누른 간격으로 템포를 정한다. (키로 누르는 탭 템포는 연주 로직이 따로 계산한다)
    const auto now = juce::Time::currentTimeMillis();
    const auto elapsed = now - lastButtonTapMs;
    lastButtonTapMs = now;

    if (elapsed >= 200 && elapsed <= 2000)
        session.getEngine().setTempo (60000.0f / static_cast<float> (elapsed));

    updateTempoControls();
}

void MainComponent::refreshStatus()
{
    auto& engine = session.getEngine();
    auto& audio = session.getAudio();

    juce::String text;
    text << ko ("출력: ") << audio.describe()
         << "  ·  CPU " << juce::String (audio.getCpuUsage() * 100.0, 1) << "%"
         << "  ·  " << ko ("늦은 콜백 ") << audio.getLateCallbackCount() << "\n";

    if (const auto* kit = session.getKit())
    {
        const auto mapping = session.getMapping();

        text << ko ("킷: ") << fromUtf8 (kit->name)
             << "  ·  " << ko ("패드 ") << static_cast<int> (kit->pads.size())
             << "  ·  " << ko ("키 배열: ") << (mapping != nullptr ? fromUtf8 (mapping->name) : ko ("없음"));
    }
    else
    {
        text << ko ("킷: 없음");
    }

    text << "  ·  " << ko ("울리는 소리 ") << engine.getActiveVoiceCount()
         << "  ·  " << ko ("이어 치기 ") << engine.getActiveRunnerCount()
         << "  ·  " << ko ("출력 크기 ")
         << (statusPeak > 0.0001f ? juce::String (juce::Decibels::gainToDecibels (statusPeak), 1) + " dB" : juce::String ("-"));

    if (audio.isRecording())
        text << "  ·  " << ko ("● 녹음 중");

    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        text << "  ·  " << ko ("글자 입력 중 (연주 멈춤, 엔터나 Esc로 끝내기)");

    statusPeak = 0.0f;
    statusLabel.setText (text, juce::dontSendNotification);

    // 문제 목록: 오디오와 키보드 오류를 먼저, 그다음 파일을 읽으며 생긴 경고.
    juce::StringArray lines;

    if (session.getAudioError().isNotEmpty())
        lines.add (ko ("오디오 장치: ") + session.getAudioError());

    if (! session.isKeyboardRegistered())
        lines.add (ko ("키보드 직접 입력을 시작하지 못했습니다."));

    lines.addArray (session.getMessages());

    if (lines.size() > maxMessageLines)
    {
        const auto hidden = lines.size() - (maxMessageLines - 1);
        lines.removeRange (maxMessageLines - 1, hidden);
        lines.add (ko ("그 밖에 ") + juce::String (hidden) + ko ("개"));
    }

    const auto messageText = lines.joinIntoString ("\n");

    if (messageText != messageLabel.getText())
    {
        messageLabel.setText (messageText, juce::dontSendNotification);
        resized();
    }
}

void MainComponent::showAudioSettings()
{
    if (settingsDialog != nullptr)
    {
        settingsDialog->toFront (true);
        return;
    }

    // 출력 장치와 함께 MIDI 입력 장치도 고른다. 켜 둔 MIDI 장치의 노트로 패드를 칠 수 있다.
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (session.getAudio().getDeviceManager(),
                                                                          0, 0,     // 입력은 쓰지 않는다
                                                                          2, 2,     // 출력은 스테레오
                                                                          true, false, true, false);
    selector->setSize (540, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = ko ("오디오 · MIDI 설정");
    options.dialogBackgroundColour = getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    settingsDialog = options.launchAsync();
}

void MainComponent::timerCallback()
{
    session.poll();
    statusPeak = juce::jmax (statusPeak, session.getOutputPeak());

    // 글자를 입력하는 칸에 커서가 있는 동안에는 키를 눌러도 연주하지 않는다.
    session.setPlayingSuspended (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr);

    pages[static_cast<std::size_t> (currentPage)]->tick();

    // 메트로놈 박에 맞춰 버튼의 점을 잠깐 채운다.
    if (const auto beats = session.getEngine().getBeatCount(); beats != seenBeatCount)
    {
        seenBeatCount = beats;
        beatFlashTicks = 6;
        updateTempoControls();
    }
    else if (beatFlashTicks > 0 && --beatFlashTicks == 0)
    {
        updateTempoControls();
    }

    ++tick;

    if (tick % 12 == 0)
    {
        refreshStatus();
        updateTempoControls();    // 키로 템포나 메트로놈을 바꿨을 때 화면에 반영한다
    }

    // 창이 다시 만들어져 핸들이 바뀌는 경우에 대비해 주기적으로 맞춰 둔다.
    if (tick % 30 == 0)
        parentHierarchyChanged();
}
