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

constexpr int maxMessageLines = 5;
constexpr int headerHeight = 40;
constexpr int statusHeight = 24;

const char* const pageNames[MainComponent::numPages] = { "play", "kit", "keys", "mixer", "track", "game", "practice" };
const char* const pageTitles[MainComponent::numPages] = { "연주", "킷", "키 배열", "믹서", "반주 · 녹음", "리듬 게임", "연습 기록" };

/** 오디오·MIDI 장치 고르기 아래에 화면 테마 고르기를 붙인 설정 창 내용. */
class SettingsPanel final : public juce::Component
{
public:
    SettingsPanel (Session& sessionToUse)
        : session (sessionToUse),
          selector (session.getAudio().getDeviceManager(),
                    0, 0,     // 입력은 쓰지 않는다
                    2, 2,     // 출력은 스테레오
                    true, false, true, false)
    {
        // 출력 장치와 함께 MIDI 입력 장치도 고른다. 켜 둔 MIDI 장치의 노트로 패드를 칠 수 있다.
        // 장치 고르기는 내용에 맞춰 스스로 높이를 바꾸므로, 그 아래에 테마 줄을 붙인다(childBoundsChanged).
        selector.setSize (540, 420);
        addAndMakeVisible (selector);

        ui::setupLabel (*this, themeCaption, ko ("화면 테마"));

        const auto ids = ui::themeIds();

        for (int i = 0; i < ids.size(); ++i)
            themeCombo.addItem (ui::themeDisplayName (ids[i]), i + 1);

        themeCombo.setSelectedItemIndex (juce::jmax (0, ids.indexOf (ui::theme().id)), juce::dontSendNotification);
        ui::setupCombo (*this, themeCombo, [this, ids]
        {
            const auto id = ids[themeCombo.getSelectedItemIndex()];
            session.setSetting ("theme", id);
            themeNote.setVisible (id != ui::theme().id);
        });

        ui::setupLabel (*this, themeNote, ko ("다시 켜면 바뀜"), ui::Text::caption);
        themeNote.setVisible (false);

        setSize (540, selector.getHeight() + themeRowHeight);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (ui::theme().border);
        g.fillRect (getLocalBounds().withHeight (1).withY (selector.getBottom()));
    }

    void childBoundsChanged (juce::Component* child) override
    {
        if (child == &selector)
            setSize (getWidth(), selector.getHeight() + themeRowHeight);
    }

    void resized() override
    {
        selector.setBounds (0, 0, getWidth(), selector.getHeight());
        auto bottom = juce::Rectangle<int> (0, selector.getBottom(), getWidth(), themeRowHeight).reduced (ui::space::large, 10);

        // 장치 고르기의 항목 이름 칸과 줄을 맞춘다 (이름은 폭의 35% 앞에서 끝나고, 고르는 칸은 거기서 시작한다).
        const auto controlX = proportionOfWidth (0.35f);
        themeCaption.setJustificationType (juce::Justification::centredRight);
        themeCaption.setBounds (bottom.withRight (controlX - ui::space::tight));
        bottom.setLeft (controlX);
        themeCombo.setBounds (bottom.removeFromLeft (160));
        bottom.removeFromLeft (ui::space::medium);
        themeNote.setBounds (bottom);
    }

private:
    static constexpr int themeRowHeight = 48;

    Session& session;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label themeCaption, themeNote;
    juce::ComboBox themeCombo;
};

} // namespace

//==============================================================================
NoticeBand::NoticeBand()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

bool NoticeBand::setLines (const juce::StringArray& newLines)
{
    if (newLines == lines)
        return false;

    lines = newLines;

    if (lines.isEmpty())
        expanded = false;

    repaint();
    return true;
}

int NoticeBand::getWantedHeight() const
{
    if (lines.isEmpty())
        return 0;

    return expanded ? headerHeight + lines.size() * lineHeight + ui::space::small : headerHeight;
}

void NoticeBand::paint (juce::Graphics& g)
{
    const auto& t = ui::theme();
    const auto bounds = getLocalBounds().toFloat();

    // 내용 위에 떠 있으므로 불투명한 면에 경고 색을 살짝 섞는다.
    g.setColour (t.shadow);
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), t.controlRadius);
    g.setColour (t.surfaceRaised.interpolatedWith (t.warning, t.dark ? 0.16f : 0.10f));
    g.fillRoundedRectangle (bounds, t.controlRadius);
    g.setColour (t.warning.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), t.controlRadius, 1.0f);

    auto area = getLocalBounds().reduced (ui::space::medium, 0);
    auto header = area.removeFromTop (headerHeight);

    // 동그라미 안의 느낌표
    const auto mark = header.removeFromLeft (16).toFloat().withSizeKeepingCentre (16.0f, 16.0f);
    g.setColour (t.warning);
    g.fillEllipse (mark);
    g.setColour (t.textOn (t.warning));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("!", mark, juce::Justification::centred, false);
    header.removeFromLeft (ui::space::small);

    g.setColour (t.textMuted);
    g.setFont (juce::FontOptions (ui::type::caption));
    g.drawText (expanded ? ko ("접기") : ko ("펼치기"), header.removeFromRight (48), juce::Justification::centredRight, false);

    const auto title = ko ("문제 ") + juce::String (lines.size()) + ko ("개");
    g.setColour (t.warning);
    g.setFont (juce::FontOptions (ui::type::caption, juce::Font::bold));
    const auto titleWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + ui::space::medium;
    g.drawText (title, header.removeFromLeft (titleWidth), juce::Justification::centredLeft, false);

    g.setColour (t.text);
    g.setFont (juce::FontOptions (ui::type::caption));

    if (! expanded)
    {
        g.drawText (lines[0], header, juce::Justification::centredLeft, true);
        return;
    }

    area.removeFromLeft (16 + ui::space::small);

    for (const auto& line : lines)
        g.drawText (line, area.removeFromTop (lineHeight), juce::Justification::centredLeft, true);
}

void NoticeBand::mouseUp (const juce::MouseEvent& event)
{
    if (! event.mouseWasClicked() || lines.isEmpty())
        return;

    expanded = ! expanded;

    if (onToggle)
        onToggle();

    repaint();
}

//==============================================================================
TempoField::TempoField()
{
    setJustificationType (juce::Justification::centred);
    setEditable (false, true, false);    // 두 번 누르면 숫자를 적는다
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setTooltip (ko ("끌어서 바꾸기, 두 번 눌러 적기"));
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setTempo (tempo);
}

void TempoField::setTempo (float beatsPerMinute)
{
    if (dragging || isBeingEdited())
        return;

    tempo = beatsPerMinute;
    setText (juce::String (juce::roundToInt (tempo)) + " BPM", juce::dontSendNotification);
}

void TempoField::mouseDown (const juce::MouseEvent& event)
{
    dragging = false;
    dragStartTempo = static_cast<float> (juce::roundToInt (tempo));
    juce::Label::mouseDown (event);
}

void TempoField::mouseDrag (const juce::MouseEvent& event)
{
    // 위나 오른쪽으로 4px마다 1 BPM
    const auto offset = event.getDistanceFromDragStartX() - event.getDistanceFromDragStartY();

    if (! dragging && std::abs (offset) < 3)
        return;

    dragging = true;
    const auto next = juce::jlimit (30.0f, 300.0f, dragStartTempo + static_cast<float> (offset / 4));

    if (juce::roundToInt (next) != juce::roundToInt (tempo))
    {
        tempo = next;
        setText (juce::String (juce::roundToInt (tempo)) + " BPM", juce::dontSendNotification);

        if (onTempoChange)
            onTempoChange (tempo);
    }
}

void TempoField::mouseUp (const juce::MouseEvent& event)
{
    dragging = false;
    juce::Label::mouseUp (event);
}

juce::TextEditor* TempoField::createEditorComponent()
{
    auto* textEditor = juce::Label::createEditorComponent();
    textEditor->setInputRestrictions (3, "0123456789");
    textEditor->setJustification (juce::Justification::centred);
    return textEditor;
}

void TempoField::editorShown (juce::TextEditor* textEditor)
{
    textEditor->setText (juce::String (juce::roundToInt (tempo)), false);
    textEditor->selectAll();
}

void TempoField::textWasEdited()
{
    if (const auto value = getText().getIntValue(); value > 0)
    {
        tempo = juce::jlimit (30.0f, 300.0f, static_cast<float> (value));

        if (onTempoChange)
            onTempoChange (tempo);
    }

    setText (juce::String (juce::roundToInt (tempo)) + " BPM", juce::dontSendNotification);
}

//==============================================================================
MainComponent::MainComponent (const Options& options)
    : session (options)
{
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
        ui::setButtonKind (tabButtons[index], ui::ButtonKind::tab);
    }

    // 설정은 탭과 떨어진 조용한 버튼이다.
    ui::setupButton (*this, settingsButton, ko ("설정"), [this] { showAudioSettings(); });
    ui::setButtonKind (settingsButton, ui::ButtonKind::quiet);
    settingsButton.setTooltip (ko ("오디오 · MIDI 설정"));

    ui::setupButton (*this, tempoDownButton, ko ("−"), [this] { session.getEngine().setTempo (std::round (session.getEngine().getTempo()) - 1.0f); updateTempoControls(); });
    ui::setupButton (*this, tempoUpButton, "+", [this] { session.getEngine().setTempo (std::round (session.getEngine().getTempo()) + 1.0f); updateTempoControls(); });
    ui::setupButton (*this, tapButton, ko ("탭"), [this] { tapTempoFromButton(); });
    ui::setupButton (*this, metronomeButton, {}, [this]
    {
        session.getEngine().setMetronomeOn (! session.getEngine().isMetronomeOn());
        session.resetTiming();
        updateTempoControls();
    });

    tempoDownButton.setTooltip (ko ("템포 −1"));
    tempoUpButton.setTooltip (ko ("템포 +1"));
    tapButton.setTooltip (ko ("두 번 넘게 눌러 템포 정하기 (F1)"));

    // 트랜스포트(전체 음량, 템포, 메트로놈) 안의 버튼은 한 덩어리로 보이게 바탕 없이 그린다.
    for (auto* button : { &tempoDownButton, &tempoUpButton, &tapButton, &metronomeButton })
        ui::setButtonKind (*button, ui::ButtonKind::quiet);

    ui::setTextStyle (tempoField, ui::Text::heading);
    tempoField.onTempoChange = [this] (float tempo) { session.getEngine().setTempo (tempo); };
    addAndMakeVisible (tempoField);

    // 전체 음량: 소리에는 바로, 설정 파일에는 마우스를 놓은 뒤에 적는다. 두 번 누르면 기본값.
    addAndMakeVisible (masterSlider);

    // 맨 아래 상태 표시줄 한 줄: 왼쪽은 킷과 키 배열, 오른쪽은 지연과 CPU (자세한 장치 정보는 툴팁)
    ui::setupLabel (*this, statusLabel, {}, ui::Text::caption);
    statusLabel.setColour (juce::Label::textColourId, ui::theme().textFaint);
    statusLabel.setMinimumHorizontalScale (0.7f);

    ui::setupLabel (*this, latencyLabel, {}, ui::Text::caption);
    latencyLabel.setColour (juce::Label::textColourId, ui::theme().textFaint);
    latencyLabel.setJustificationType (juce::Justification::centredRight);

    // 문제 목록은 화면 내용 위에 겹쳐 띄운다 (생기거나 없어져도 화면이 밀리지 않게).
    notices.onToggle = [this] { layoutNotices(); };
    addChildComponent (notices);

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

    pages[static_cast<std::size_t> (currentPage)]->pageShown();
    notices.toFront (false);
}

void MainComponent::paint (juce::Graphics& g)
{
    const auto& t = ui::theme();
    g.fillAll (t.background);

    // 탭: 알약 모양이면 묶음 바탕을, 밑줄 모양이면 머리줄 아래에 옅은 선을 그린다 (고른 탭의 줄이 그 위에 놓인다).
    if (t.tabs == ui::TabStyle::segment)
    {
        g.setColour (t.tabTrack);
        g.fillRoundedRectangle (tabArea.toFloat(), t.controlRadius + 1.0f);
    }
    else
    {
        g.setColour (t.border);
        g.fillRect (headerLine);
    }

    // 트랜스포트: 전체 음량, 템포, 메트로놈을 한 덩어리로
    g.setColour (t.surface);
    g.fillRoundedRectangle (transportArea.toFloat(), t.controlRadius + 1.0f);

    if (! t.dark)
    {
        g.setColour (t.border);
        g.drawRoundedRectangle (transportArea.toFloat().reduced (0.5f), t.controlRadius + 1.0f, 1.0f);
    }

    g.setColour (t.border);

    for (const auto x : transportDividers)
        if (x > 0)
            g.fillRect (juce::Rectangle<int> (x, transportArea.getY() + 8, 1, transportArea.getHeight() - 16));

    // 음량 그림 (스피커)
    {
        const auto box = speakerArea.toFloat().withSizeKeepingCentre (16.0f, 14.0f);
        juce::Path speaker;
        speaker.startNewSubPath (box.getX(), box.getY() + 4.5f);
        speaker.lineTo (box.getX() + 3.5f, box.getY() + 4.5f);
        speaker.lineTo (box.getX() + 7.5f, box.getY() + 1.0f);
        speaker.lineTo (box.getX() + 7.5f, box.getBottom() - 1.0f);
        speaker.lineTo (box.getX() + 3.5f, box.getBottom() - 4.5f);
        speaker.lineTo (box.getX(), box.getBottom() - 4.5f);
        speaker.closeSubPath();
        g.setColour (t.textMuted);
        g.fillPath (speaker);

        juce::Path waves;
        waves.addCentredArc (box.getX() + 8.0f, box.getCentreY(), 3.5f, 3.5f, 0.0f, 0.75f, 2.4f, true);
        waves.addCentredArc (box.getX() + 8.0f, box.getCentreY(), 6.5f, 6.5f, 0.0f, 0.75f, 2.4f, true);
        g.strokePath (waves, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 상태 표시줄 위의 선
    g.setColour (t.border);
    g.fillRect (statusLine);

    // 끊김 칩: 늦은 콜백이 생기면 주황으로
    if (dropouts > 0 && ! dropoutChip.isEmpty())
    {
        const auto chip = dropoutChip.toFloat();
        g.setColour (t.warning.withAlpha (t.dark ? 0.20f : 0.14f));
        g.fillRoundedRectangle (chip, chip.getHeight() * 0.5f);
        g.setColour (t.warning);
        g.setFont (juce::FontOptions (ui::type::caption, juce::Font::bold));
        g.drawText (ko ("끊김 ") + juce::String (dropouts), dropoutChip, juce::Justification::centred, false);
    }
}

void MainComponent::resized()
{
    const auto& t = ui::theme();
    auto area = getLocalBounds().reduced (ui::space::large, ui::space::medium);

    //==============================================================================
    // 머리줄 한 줄: [탭] ── [음량] [− 120 BPM +] [탭] [메트로놈]  [설정]
    auto header = area.removeFromTop (headerHeight);
    headerLine = { 0, header.getBottom() + (t.tabs == ui::TabStyle::segment ? 0 : ui::space::tight), getWidth(), 1 };

    settingsButton.setBounds (header.removeFromRight (48).withSizeKeepingCentre (48, ui::space::control));
    header.removeFromRight (ui::space::large);

    // 트랜스포트 안의 고정 폭. 음량 슬라이더는 남는 자리를 쓰고, 좁으면 값 상자를 뺀다.
    // 탭의 폭은 리듬 게임 중에 템포 무리를 숨겨도 그대로 둔다 (탭이 옆으로 움직이지 않게).
    constexpr int pad = ui::space::tight;
    constexpr int tempoWidth = 28 + 64 + 28;
    constexpr int speakerWidth = pad + ui::space::small + 16 + ui::space::tight;       // 여백, 스피커 그림
    constexpr int tempoGroupWidth = ui::space::small + 1 + ui::space::small              // 나눔 선
                                  + tempoWidth + ui::space::tight + 36                   // − 120 BPM + 탭
                                  + ui::space::small + 1 + ui::space::small              // 나눔 선
                                  + 84;                                                  // 메트로놈
    constexpr int fullFixedWidth = speakerWidth + tempoGroupWidth + pad;
    const int fixedWidth = transportHidden ? speakerWidth + ui::space::small + pad : fullFixedWidth;
    constexpr int minSlider = 96, sliderWithValue = 150, fullSlider = 200;

    // 탭은 음량 값 상자가 들어갈 자리를 남기고 넓힌다. 창이 좁으면 탭을 줄이고, 그래도 모자라면 값 상자를 뺀다.
    const auto tabWidth = juce::jlimit (68, 84, (header.getWidth() - ui::space::large - fullFixedWidth - sliderWithValue) / numPages);
    tabArea = header.removeFromLeft (tabWidth * numPages).withSizeKeepingCentre (tabWidth * numPages, 36);

    auto tabs = tabArea;

    for (auto& button : tabButtons)
        button.setBounds (tabs.removeFromLeft (tabWidth));

    header.removeFromLeft (ui::space::large);
    const auto sliderWidth = juce::jlimit (minSlider, fullSlider, header.getWidth() - fixedWidth);
    masterSlider.setValueBoxShown (sliderWidth >= sliderWithValue);

    transportArea = header.removeFromRight (fixedWidth + sliderWidth).withSizeKeepingCentre (fixedWidth + sliderWidth, 36);

    auto transport = transportArea.reduced (pad);
    transport.removeFromLeft (ui::space::small);
    speakerArea = transport.removeFromLeft (16);
    transport.removeFromLeft (ui::space::tight);
    masterSlider.setBounds (transport.removeFromLeft (sliderWidth));
    transportDividers = {};

    if (! transportHidden)
    {
        transport.removeFromLeft (ui::space::small);
        transportDividers[0] = transport.getX();
        transport.removeFromLeft (ui::space::small + 1);
        tempoDownButton.setBounds (transport.removeFromLeft (28));
        tempoField.setBounds (transport.removeFromLeft (64));
        tempoUpButton.setBounds (transport.removeFromLeft (28));
        transport.removeFromLeft (ui::space::tight);
        tapButton.setBounds (transport.removeFromLeft (36));
        transport.removeFromLeft (ui::space::small);
        transportDividers[1] = transport.getX();
        transport.removeFromLeft (ui::space::small + 1);
        metronomeButton.setBounds (transport);
    }

    area.removeFromTop (ui::space::medium);

    //==============================================================================
    // 아래: 상태 표시줄 한 줄
    auto status = area.removeFromBottom (statusHeight);
    statusLine = { 0, status.getY() - 1, getWidth(), 1 };
    status = status.withTrimmedTop (ui::space::tight);

    auto right = status.removeFromRight (200);
    latencyLabel.setBounds (right);

    if (dropouts > 0)
    {
        const auto text = ko ("끊김 ") + juce::String (dropouts);
        const auto width = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (ui::type::caption, juce::Font::bold)), text) + 16;
        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (ui::type::caption)), latencyLabel.getText()) + 8;
        dropoutChip = juce::Rectangle<int> (right.getRight() - textWidth - ui::space::small - width, right.getY() + 1, width, right.getHeight() - 2);
    }
    else
    {
        dropoutChip = {};
    }

    statusLabel.setBounds (status);
    area.removeFromBottom (ui::space::small);

    contentArea = area;

    for (auto& page : pages)
        if (page != nullptr)
            page->setBounds (area);

    layoutNotices();
}

void MainComponent::layoutNotices()
{
    // 화면 내용의 아래쪽에 겹쳐, 펼치면 위로 자란다.
    const auto height = notices.getWantedHeight();
    notices.setVisible (height > 0);

    if (height > 0)
        notices.setBounds (contentArea.withTop (contentArea.getBottom() - height));
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
    tempoField.setTempo (engine.getTempo());

    const auto on = engine.isMetronomeOn();
    metronomeButton.setButtonText (on ? (beatFlashTicks > 0 ? ko ("메트로놈 ●") : ko ("메트로놈 ○")) : ko ("메트로놈"));
    metronomeButton.setToggleState (on, juce::dontSendNotification);    // 켜져 있으면 켜진 모양으로 그린다
}

void MainComponent::updateTransportVisibility()
{
    // 리듬 게임을 하는 동안에는 템포, 탭, 메트로놈을 숨긴다 (채보의 BPM과 헷갈리지 않게).
    const auto hide = pages[static_cast<std::size_t> (currentPage)]->hidesTransport();

    if (hide == transportHidden)
        return;

    transportHidden = hide;

    for (auto* component : std::initializer_list<juce::Component*> { &tempoDownButton, &tempoField, &tempoUpButton, &tapButton, &metronomeButton })
        component->setVisible (! hide);

    resized();
    repaint();
}

void MainComponent::restoreWindowState (juce::ResizableWindow& window)
{
    if (const auto saved = session.getSetting ("window", {}).toString(); saved.isNotEmpty())
        window.restoreWindowStateFromString (saved);
}

void MainComponent::saveWindowState (juce::ResizableWindow& window)
{
    session.setSetting ("window", window.getWindowStateAsString());
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
    auto& audio = session.getAudio();

    // 왼쪽: 킷 이름 · 키 배열 이름
    juce::String text;

    if (const auto* kit = session.getKit())
    {
        const auto mapping = session.getMapping();
        text << fromUtf8 (kit->name) << "  ·  " << (mapping != nullptr ? fromUtf8 (mapping->name) : ko ("키 배열 없음"));
    }
    else
    {
        text << ko ("킷 없음");
    }

    if (audio.isRecording())
        text << "  ·  " << ko ("● 녹음 중");

    // 글자 입력 칸에 커서가 있으면 연주가 멈춘다. 넓은 칸은 칸 안에 알리고, 좁은 칸(값 상자 등)은 여기에 적는다.
    if (auto* editor = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent());
        editor != nullptr && editor->getWidth() < ui::typingHintMinWidth)
        text << "  ·  " << ui::typingHint();

    statusLabel.setText (text, juce::dontSendNotification);

    // 오른쪽: 버퍼 지연과 CPU. 장치 이름, 샘플레이트, 버퍼 크기는 툴팁으로.
    const auto status = audio.getStatus();
    juce::String latency;

    if (status.open && status.sampleRate > 0.0)
        latency << juce::String (1000.0 * status.bufferSize / status.sampleRate, 1) << " ms";
    else
        latency << ko ("장치 없음");

    latency << "  ·  CPU " << juce::roundToInt (audio.getCpuUsage() * 100.0) << "%";
    latencyLabel.setText (latency, juce::dontSendNotification);
    latencyLabel.setTooltip (ko ("출력: ") + audio.describe() + "  ·  " + ko ("늦은 콜백 ") + juce::String (audio.getLateCallbackCount()));

    // 끊김: 켜고 2초 뒤부터 생긴 늦은 콜백 수
    if (lateCallbackBaseline < 0 && tick >= 120)
        lateCallbackBaseline = audio.getLateCallbackCount();

    const auto nowDropouts = lateCallbackBaseline >= 0 ? juce::jmax (0, audio.getLateCallbackCount() - lateCallbackBaseline) : 0;

    if (nowDropouts != dropouts)
    {
        dropouts = nowDropouts;
        resized();
        repaint();
    }

    // 문제 목록: 오디오와 키보드 오류를 먼저, 그다음 파일을 읽으며 생긴 경고.
    juce::StringArray lines;

    if (session.getAudioError().isNotEmpty())
        lines.add (ko ("오디오 장치: ") + session.getAudioError());

    if (! session.isKeyboardRegistered())
        lines.add (ko ("키보드 직접 입력을 시작하지 못했습니다."));

    if (session.hasGivenUpMouseTakeover())
        lines.add (ko ("마우스 클릭 자리가 맞지 않아 예전 방식으로 받습니다 (조금 늦을 수 있음)."));

    lines.addArray (session.getMessages());

    if (lines.size() > maxMessageLines)
    {
        const auto hidden = lines.size() - (maxMessageLines - 1);
        lines.removeRange (maxMessageLines - 1, hidden);
        lines.add (ko ("그 밖에 ") + juce::String (hidden) + ko ("개"));
    }

    if (notices.setLines (lines))
        layoutNotices();
}

void MainComponent::showAudioSettings()
{
    if (settingsDialog != nullptr)
    {
        settingsDialog->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new SettingsPanel (session));
    options.dialogTitle = ko ("오디오 · MIDI 설정");
    options.dialogBackgroundColour = ui::theme().background;
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    settingsDialog = options.launchAsync();
}

void MainComponent::timerCallback()
{
    session.poll();

    // 글자를 입력하는 칸에 커서가 있는 동안에는 키를 눌러도 연주하지 않는다.
    session.setPlayingSuspended (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr);

    pages[static_cast<std::size_t> (currentPage)]->tick();
    updateTransportVisibility();

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
