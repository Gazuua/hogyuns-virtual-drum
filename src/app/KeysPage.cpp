#include "KeysPage.h"

#include "Json.h"

namespace
{

constexpr int rowCaptionHeight = 20;    // 편집 항목의 이름 줄

struct Choice
{
    const char* value;
    const char* label;
};

constexpr Choice actions[] = {
    { "hit", "패드 치기" },
    { "choke", "울리는 소리 멈추기" },
    { "accent", "악센트 (누르는 동안 세게)" },
    { "ghost", "고스트 (누르는 동안 약하게)" },
    { "pedal", "페달 (패드 바꾸기)" },
    { "tapTempo", "탭 템포" },
    { "metronome", "메트로놈 켜고 끄기" },
};

constexpr Choice strokes[] = {
    { "single", "단타" },
    { "downUp", "누를 때와 뗄 때 한 번씩" },
    { "double", "더블 (두 번)" },
    { "flam", "플램 (꾸밈음 + 본음)" },
    { "drag", "드래그 (꾸밈음 둘 + 본음)" },
    { "repeat", "반복 (누르는 동안 연타)" },
    { "buzz", "버즈 롤 (누르는 동안)" },
    { "pattern", "패턴 (누르면 시작, 다시 누르면 멈춤)" },
};

constexpr Choice rates[] = {
    { "1/4", "4분음표" },
    { "1/8", "8분음표" },
    { "1/8t", "8분 셋잇단" },
    { "1/16", "16분음표" },
    { "1/16t", "16분 셋잇단" },
    { "1/32", "32분음표" },
    { "follow", "직전에 친 간격 따라가기" },
};

constexpr int divisions[] = { 2, 3, 4, 6, 8 };

template <std::size_t N>
void fillChoices (juce::ComboBox& combo, const Choice (&choices)[N])
{
    for (std::size_t i = 0; i < N; ++i)
        combo.addItem (ko (choices[i].label), static_cast<int> (i) + 1);
}

template <std::size_t N>
int indexOfChoice (const Choice (&choices)[N], const juce::String& value)
{
    for (std::size_t i = 0; i < N; ++i)
        if (value == choices[i].value)
            return static_cast<int> (i);

    return 0;
}

/** 키 하나로 패드 하나를 세기 1로 치는 새 줄. */
juce::var makeHitBinding (hvd::KeyCode key, const juce::String& pad)
{
    auto* binding = new juce::DynamicObject();
    binding->setProperty ("key", fromUtf8 (hvd::keyName (key)));
    binding->setProperty ("pad", pad);
    binding->setProperty ("velocity", 1.0);
    return juce::var (binding);
}

/** 장치의 보고 간격을 화면에 보여 줄 글로. */
juce::String intervalText (const hvd::ReportInterval& interval)
{
    if (! interval.enough)
        return ko ("재는 중 ") + juce::String (interval.numSamples) + "/" + juce::String (hvd::ReportInterval::samplesNeeded);

    // 어느 간격에도 맞지 않으면 잴 수 있는 것보다 촘촘하거나, 컴퓨터가 바빠서 시각이 흔들린 것이다.
    if (interval.intervalMs <= 0.0)
        return ko ("1ms 이하 (또는 불규칙)");

    return ko ("약 ") + juce::String (juce::roundToInt (interval.intervalMs)) + "ms";
}

} // namespace

//==============================================================================
KeysPage::KeysPage (Session& sessionToUse)
    : session (sessionToUse),
      documents (makeDocumentSource())
{
    addAndMakeVisible (documents);

    ui::setupToggle (*this, followButton, ko ("누른 키 따라가기"), {}, true);

    ui::setupLabel (*this, pedalLabel, {}, ui::Text::caption);
    pedalLabel.setMinimumHorizontalScale (0.75f);
    ui::setupButton (*this, pedalButton, ko ("페달 등록"), [this] { pedalButtonClicked(); });

    keyboard.onKeyClicked = [this] (hvd::KeyCode key) { selectKey (key); };
    keyboard.isKeyHeld = [this] (hvd::KeyCode key) { return session.isKeyHeld (key); };
    addAndMakeVisible (keyboard);

    // 페달을 등록하는 동안 키보드 그림 윗줄에 겹쳐 띄우는 띠 (그림보다 나중에 넣어야 위에 보인다)
    ui::setupLabel (banner, banner.text, ko ("왼쪽 스위치부터 밟으세요"), ui::Text::section);
    ui::setupLabel (banner, banner.keys, {}, ui::Text::body);
    ui::setupButton (banner, banner.doneButton, ko ("완료"), [this] { pedalButtonClicked(); });
    ui::setupButton (banner, banner.cancelButton, ko ("취소"), [this]
    {
        session.endPedalLearning (false);
        updatePedalStatus();
    });
    ui::setButtonKind (banner.doneButton, ui::ButtonKind::primary);
    banner.cancelButton.setTooltip (ko ("Esc"));
    addChildComponent (banner);

    // 동시 입력 검사: 키보드 그림의 빈자리에 겹쳐 둔다 (그림보다 나중에 넣어야 위에 보인다).
    ui::setupLabel (*this, heldLabel, {}, ui::Text::caption);
    heldLabel.setJustificationType (juce::Justification::centred);
    heldLabel.setTooltip (ko ("동시에 눌린 키와 그 최대값. 키보드가 한꺼번에 몇 개까지 알아듣는지 봅니다."));
    ui::setupButton (*this, resetHeldButton, ko ("초기화"), [this] { session.resetMaxHeldKeyCount(); });
    resetHeldButton.setTooltip (ko ("최대값을 지웁니다"));
    ui::setButtonKind (resetHeldButton, ui::ButtonKind::quiet);

    ui::setupLabel (*this, keyTitle, {}, ui::Text::section);
    ui::setupButton (*this, addButton, ko ("동작 추가"), [this] { addBinding(); });
    ui::setupButton (*this, removeButton, ko ("삭제"), [this] { removeBinding(); });
    removeButton.setTooltip (ko ("고른 동작을 지웁니다"));

    ui::setupLabel (*this, quickCaption, ko ("빠른 지정"), ui::Text::caption);
    quickCaption.setJustificationType (juce::Justification::centredRight);
    ui::setupCombo (*this, quickPadCombo, [] {});
    quickPadCombo.setTooltip (ko ("고른 패드를 칠 키를 정합니다"));
    ui::setupButton (*this, quickButton, ko ("키 지정"), [this] { startQuickAssign(); });
    quickButton.setTooltip (ko ("누르고 나서 칠 키를 누르세요"));

    // 키를 고르지 않았거나 동작이 없는 키: 패널 가운데에 한 줄과 버튼
    ui::setupLabel (*this, emptyHint, {}, ui::Text::hint);
    emptyHint.setJustificationType (juce::Justification::centred);
    ui::setupButton (*this, emptyAddButton, ko ("동작 추가"), [this] { addBinding(); });
    ui::setButtonKind (emptyAddButton, ui::ButtonKind::primary);

    //==============================================================================
    // 편집 항목들
    fillChoices (actionCombo, actions);
    fillChoices (strokeCombo, strokes);
    fillChoices (rateCombo, rates);

    for (const auto division : divisions)
        divisionCombo.addItem (juce::String (division), division);

    ui::setupCombo (*this, actionCombo, [this] { if (! updating) actionChanged(); });
    ui::setupCombo (*this, strokeCombo, [this] { if (! updating) strokeChanged(); });

    ui::setupCombo (*this, padCombo, [this]
    {
        const auto index = currentBindingIndex();
        const auto chosen = comboPadId (padCombo);

        if (updating || index < 0 || chosen.isEmpty())
            return;

        // 첫 패드를 고른 것으로 바꾼다. 함께 칠 패드에 있던 것이면 그 자리에서 뺀다.
        auto& keymap = session.getKeymapDocument();
        auto pads = keymap.getBindingPads (index);
        pads.removeString (chosen);
        pads.set (0, chosen);
        keymap.setBindingPads (index, pads);
        commit();
    });

    ui::setupCombo (*this, rateCombo, [this]
    {
        if (const auto index = currentBindingIndex(); ! updating && index >= 0)
        {
            auto& keymap = session.getKeymapDocument();
            keymap.setBindingProperty (index, "rate", rates[juce::jmax (0, rateCombo.getSelectedItemIndex())].value);
            keymap.setBindingProperty (index, "intervalMs", {});
            commit();
        }
    });

    ui::setupCombo (*this, divisionCombo, [this]
    {
        if (const auto index = currentBindingIndex(); ! updating && index >= 0)
        {
            session.getKeymapDocument().setBindingProperty (index, "division", divisionCombo.getSelectedId());
            commit();
        }
    });

    const auto pedalChanged = [this]
    {
        const auto index = currentBindingIndex();

        if (updating || index < 0)
            return;

        // 화면에서는 누르는 동안, 떼고 있는 동안 각각 한 쌍만 다룬다.
        const auto pairOf = [this] (const PadSwapPicker& picker)
        {
            juce::DynamicObject::Ptr swap (new juce::DynamicObject());

            if (const auto from = comboPadId (picker.from), to = comboPadId (picker.to); from.isNotEmpty() && to.isNotEmpty())
                swap->setProperty (from, to);

            return swap;
        };

        // 고르지 않은 칸은 그 항목을 지운다 (빈 값).
        const auto padOrNone = [this] (const juce::ComboBox& combo)
        {
            const auto id = comboPadId (combo);
            return id.isNotEmpty() ? juce::var (id) : juce::var();
        };

        auto& keymap = session.getKeymapDocument();
        const auto released = pairOf (releasedSwap);
        const auto press = comboPadId (pressPadCombo);

        keymap.setBindingProperty (index, "swap", juce::var (pairOf (heldSwap).get()));
        keymap.setBindingProperty (index, "releasedSwap", released->getProperties().size() > 0 ? juce::var (released.get()) : juce::var());
        keymap.setBindingPads (index, press.isNotEmpty() ? juce::StringArray (press) : juce::StringArray());
        keymap.setBindingProperty (index, "releasePad", padOrNone (releasePadCombo));
        keymap.setBindingProperty (index, "splash", padOrNone (splashPadCombo));
        commit();
    };

    ui::setupCombo (*this, pressPadCombo, pedalChanged);
    ui::setupCombo (*this, releasePadCombo, pedalChanged);
    ui::setupCombo (*this, splashPadCombo, pedalChanged);

    for (auto* picker : { &heldSwap, &releasedSwap })
    {
        ui::setupCombo (*picker, picker->from, pedalChanged);
        ui::setupCombo (*picker, picker->to, pedalChanged);
        ui::setupLabel (*picker, picker->arrow, ko ("→"), ui::Text::hint);
        picker->arrow.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*picker);
    }

    ui::setupButton (*this, padsButton, {}, [this] { showPadsMenu(); });

    ui::setupSlider (*this, velocitySlider, 0.05, 1.0, 0.01, 1.0);
    ui::setupSlider (*this, accentSlider, 0.0, 8.0, 1.0, 0.0, {}, 0);
    ui::setupSlider (*this, softSlider, 0.1, 1.0, 0.01, 0.8);
    ui::setupSlider (*this, gapSlider, 10.0, 250.0, 1.0, 85.0, " ms", 0);

    const auto bindSlider = [this] (juce::Slider& slider, const char* name, bool integer)
    {
        slider.onValueChange = [this, &slider, name, integer]
        {
            if (const auto index = currentBindingIndex(); ! updating && index >= 0)
            {
                session.getKeymapDocument().setBindingProperty (index, name, integer ? juce::var (juce::roundToInt (slider.getValue()))
                                                                                     : juce::var (slider.getValue()));
                session.markKeymapEdited();    // 마우스를 놓은 뒤에 저장한다
            }
        };
    };

    bindSlider (velocitySlider, "velocity", false);
    bindSlider (accentSlider, "accentEvery", true);
    bindSlider (softSlider, "softVelocity", false);
    bindSlider (gapSlider, "gapMs", false);

    const auto stepsChanged = [this]
    {
        const auto index = currentBindingIndex();

        if (updating || index < 0)
            return;

        auto& keymap = session.getKeymapDocument();
        const auto steps = stepsEditor.getText().retainCharacters ("Xxo.");

        if (steps.isNotEmpty() && steps != keymap.getBinding (index)["steps"].toString())
        {
            keymap.setBindingProperty (index, "steps", steps);
            commit();
        }
    };

    // 다 적고 엔터나 Esc를 누르면 커서를 칸 밖으로 내보낸다. 커서가 입력 칸에 있는 동안에는 연주가 멈추기 때문이다.
    stepsEditor.onReturnKey = [this, stepsChanged] { stepsChanged(); stepsEditor.giveAwayKeyboardFocus(); };
    stepsEditor.onEscapeKey = [this] { stepsEditor.giveAwayKeyboardFocus(); };
    stepsEditor.onFocusLost = stepsChanged;
    stepsEditor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::plain));
    addAndMakeVisible (stepsEditor);

    struct RowSpec
    {
        const char* caption;
        const char* tip;    // 마우스를 올렸을 때 보일 설명. 없으면 nullptr
        juce::Component* control;
    };

    const RowSpec rowSpecs[] = {
        { "동작", nullptr, &actionCombo },
        { "패드", nullptr, &padCombo },
        { "함께 칠 패드", "같은 키로 겹쳐 칠 패드", &padsButton },
        { "세기", nullptr, &velocitySlider },
        { "타격 방식", nullptr, &strokeCombo },
        { "반복 간격", nullptr, &rateCombo },
        { "악센트 주기", "몇 번째마다 세게 칠지. 0은 첫 타만", &accentSlider },
        { "나머지 세기", "악센트가 아닌 타의 세기 (악센트 대비)", &softSlider },
        { "두 타 사이 간격", nullptr, &gapSlider },
        { "패턴", "X 세게, x 보통, o 약하게, . 쉼", &stepsEditor },
        { "한 박을 나누는 수", nullptr, &divisionCombo },
        { "누를 때 칠 패드", nullptr, &pressPadCombo },
        { "뗄 때 칠 패드", nullptr, &releasePadCombo },
        { "누르는 동안 바꿔 칠 패드", nullptr, &heldSwap },
        { "뗀 동안 바꿔 칠 패드", nullptr, &releasedSwap },
        { "빨리 떼면 칠 패드", "페달을 밟았다가 금방 떼면 칠 패드 (풋 스플래시)", &splashPadCombo },
    };

    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        ui::setupLabel (*this, rows[i].caption, ko (rowSpecs[i].caption), ui::Text::caption);
        rows[i].control = rowSpecs[i].control;

        if (rowSpecs[i].tip != nullptr)
        {
            rows[i].caption.setTooltip (ko (rowSpecs[i].tip));

            if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (rows[i].control))
                client->setTooltip (ko (rowSpecs[i].tip));
        }
    }

    // 버튼 종류: 지우는 일은 위험 표시
    ui::setButtonKind (removeButton, ui::ButtonKind::danger);

    session.addChangeListener (this);
    rebuild();

    pedalConnected = session.isPedalConnected();
    updatePedalStatus();
}

KeysPage::~KeysPage()
{
    session.cancelKeyCapture();
    session.endPedalLearning (false);
    session.removeChangeListener (this);
}

void PedalBanner::paint (juce::Graphics& g)
{
    // 밑의 키가 비치지 않게 바탕부터 칠하고, 고른 것의 색과 액센트 테두리로 모드를 보인다.
    const auto& t = ui::theme();
    const auto area = getLocalBounds().toFloat();
    const auto radius = juce::jmin (t.panelRadius, area.getHeight() * 0.5f);

    g.setColour (t.background);
    g.fillRoundedRectangle (area, radius);
    g.setColour (t.selection);
    g.fillRoundedRectangle (area, radius);
    g.setColour (t.accent.withAlpha (t.dark ? 0.75f : 0.6f));
    g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
}

void PedalBanner::resized()
{
    auto area = getLocalBounds().reduced (ui::space::large, 0);
    const auto buttons = area.withSizeKeepingCentre (area.getWidth(), ui::space::control);

    doneButton.setBounds (buttons.withLeft (buttons.getRight() - 72));
    cancelButton.setBounds (doneButton.getBounds().translated (-72 - ui::space::tight, 0));
    area.setRight (cancelButton.getX() - ui::space::large);

    text.setBounds (area.removeFromLeft (juce::jmin (area.getWidth() / 2,
                                                     juce::GlyphArrangement::getStringWidthInt (text.getFont(), text.getText()) + ui::space::xlarge)));
    keys.setBounds (area);
}

//==============================================================================
DocumentBar::Source KeysPage::makeDocumentSource()
{
    DocumentBar::Source source;
    source.noun = ko ("키 배열");
    source.names = [this] { return session.getKeymapNames(); };
    source.current = [this] { return session.getKeymapName(); };
    source.select = [this] (const juce::String& name) { session.selectKeymap (name); };
    source.copyName = [this] { return session.getKeymapName() + "-2"; };
    source.duplicate = [this] (const juce::String& name) { return session.duplicateKeymap (name); };
    source.displayName = [this] { return session.getKeymapDocument().getName(); };
    source.renameNote = ko ("화면에 보여 줄 이름을 적으세요. 파일 이름은 바뀌지 않습니다.");
    source.rename = [this] (const juce::String& name)
    {
        session.getKeymapDocument().setName (name);
        commit();
    };
    source.file = [this] { return session.getKeymapFile(); };
    source.restore = [this] (const juce::File& backup) { return session.restoreKeymapBackup (backup); };
    source.backups = &session.getBackups();
    return source;
}

void KeysPage::visibilityChanged()
{
    // 다른 화면으로 넘어가면 페달 등록을 그만둔다. 그대로 두면 누르는 키가 계속 연주되지 않는다.
    if (! isVisible())
        session.endPedalLearning (false);
}

void KeysPage::paint (juce::Graphics& g)
{
    const auto& t = ui::theme();

    // 페달 상태 칩: 둥근 면에 연결 상태 점
    if (pedalLabel.isVisible() && ! pedalChip.isEmpty())
    {
        const auto chip = pedalChip.toFloat();
        g.setColour (t.surface);
        g.fillRoundedRectangle (chip, chip.getHeight() * 0.5f);

        g.setColour (pedalConnected ? t.good : t.warning);
        g.fillEllipse (chip.getX() + 10.0f, chip.getCentreY() - 3.5f, 7.0f, 7.0f);
    }

    ui::fillPanel (g, editorPanel);
}

void KeysPage::resized()
{
    auto area = getLocalBounds();

    //==============================================================================
    // 윗줄: [키 배열 목록] [복제] [되돌리기] [⋯]  [누른 키 따라가기]  ……  [● 페달 상태] [페달]
    auto top = area.removeFromTop (ui::space::control);
    documents.setBounds (top.removeFromLeft (juce::jmin (documents.getIdealWidth(), top.getWidth())));
    top.removeFromLeft (ui::space::large);

    pedalButton.setBounds (top.removeFromRight (84));
    top.removeFromRight (ui::space::small);

    pedalChip = {};

    if (pedalLabel.isVisible())
    {
        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (pedalLabel.getFont(), pedalLabel.getText());
        pedalChip = top.removeFromRight (juce::jmin (textWidth + 36, juce::jmax (0, top.getWidth() - 150)));
        pedalLabel.setBounds (pedalChip.withTrimmedLeft (22).withTrimmedRight (8));
        top.removeFromRight (ui::space::small);
    }

    followButton.setBounds (top.removeFromLeft (juce::jmin (160, top.getWidth())));

    area.removeFromTop (ui::space::medium);

    //==============================================================================
    // 키보드는 폭에 맞추되, 편집 칸이 들어갈 높이는 남긴다.
    constexpr int editorMinHeight = ui::space::large + ui::space::control + ui::space::large
                                    + 3 * (rowCaptionHeight + ui::space::control) + 2 * ui::space::small + ui::space::large;
    const auto keyboardHeight = juce::jmin (KeyboardView::heightForWidth (area.getWidth()),
                                            juce::jmax (150, area.getHeight() - editorMinHeight - ui::space::medium));
    keyboard.setBounds (area.removeFromTop (keyboardHeight));

    // 페달 등록 띠는 기능 키 줄에, 동시 입력 검사는 그림의 빈자리에
    banner.setBounds (keyboard.getFunctionRowArea().translated (keyboard.getX(), keyboard.getY()));
    auto spare = keyboard.getSpareArea().translated (keyboard.getX(), keyboard.getY());
    resetHeldButton.setBounds (spare.removeFromBottom (22).withSizeKeepingCentre (64, 22));
    heldLabel.setBounds (spare);

    area.removeFromTop (ui::space::medium);
    editorPanel = area;

    auto editor = area.reduced (ui::space::large);
    titleArea = editor.removeFromTop (ui::space::control);
    editor.removeFromTop (ui::space::large);
    fieldsArea = editor;

    layoutEditor();
}

void KeysPage::layoutEditor()
{
    //==============================================================================
    // 제목 줄: [D] [동작 1|동작 2] [동작 추가] [삭제]  ……  빠른 지정 [패드] [키 지정]
    auto title = titleArea;

    const auto quickWidth = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (ui::type::body)), quickButton.getButtonText()) + 32;
    quickButton.setBounds (title.removeFromRight (juce::jmax (80, quickWidth)));
    title.removeFromRight (ui::space::tight);
    quickPadCombo.setBounds (title.removeFromRight (150));
    title.removeFromRight (ui::space::small);
    quickCaption.setBounds (title.removeFromRight (juce::GlyphArrangement::getStringWidthInt (quickCaption.getFont(), quickCaption.getText()) + 4));
    title.removeFromRight (ui::space::large);

    keyTitle.setBounds (title.removeFromLeft (juce::jmin (title.getWidth() / 3,
                                                          juce::GlyphArrangement::getStringWidthInt (keyTitle.getFont(), keyTitle.getText()) + ui::space::large)));

    for (auto* slot : slotButtons)
        if (slot->isVisible())
            slot->setBounds (title.removeFromLeft (64));

    if (! slotButtons.isEmpty() && slotButtons.getFirst()->isVisible())
        title.removeFromLeft (ui::space::large);

    addButton.setBounds (title.removeFromLeft (88));
    title.removeFromLeft (ui::space::tight);
    removeButton.setBounds (title.removeFromLeft (64));

    //==============================================================================
    // 보이는 항목을 세 칸짜리 격자에 차례로 놓는다.
    const auto area = fieldsArea;
    constexpr int columns = 3;
    constexpr int rowPitch = rowCaptionHeight + ui::space::control + ui::space::small;
    const auto cellWidth = (area.getWidth() + ui::space::large) / columns;
    auto index = 0;

    for (auto& row : rows)
    {
        row.caption.setVisible (row.shown);
        row.control->setVisible (row.shown);

        if (! row.shown)
            continue;

        auto cell = juce::Rectangle<int> (area.getX() + (index % columns) * cellWidth,
                                          area.getY() + (index / columns) * rowPitch,
                                          cellWidth - ui::space::large, rowPitch);
        row.caption.setBounds (cell.removeFromTop (rowCaptionHeight));
        row.control->setBounds (cell.removeFromTop (ui::space::control));
        ++index;
    }

    // 동작이 없으면 가운데에 한 줄과 버튼
    const auto empty = index == 0;
    emptyHint.setVisible (empty);
    emptyAddButton.setVisible (empty && selectedKey != hvd::invalidKey);

    auto centre = area.withSizeKeepingCentre (area.getWidth(), 22 + ui::space::medium + ui::space::control);

    if (! emptyAddButton.isVisible())
        centre = area.withSizeKeepingCentre (area.getWidth(), 22);

    emptyHint.setBounds (centre.removeFromTop (22));
    centre.removeFromTop (ui::space::medium);
    emptyAddButton.setBounds (centre.withSizeKeepingCentre (104, ui::space::control));
}

//==============================================================================
juce::String KeysPage::padName (const juce::String& padId) const
{
    if (const auto* kit = session.getKit())
        if (const auto index = kit->findPad (padId.toStdString()); index >= 0)
            return fromUtf8 (kit->pads[static_cast<std::size_t> (index)].name);

    return padId;
}

juce::String KeysPage::describe (int index) const
{
    const auto& keymap = session.getKeymapDocument();
    const auto binding = keymap.getBinding (index);
    const auto action = binding["action"].toString();

    if (action.isEmpty() || action == "hit")
    {
        juce::StringArray names;

        for (const auto& pad : keymap.getBindingPads (index))
            names.add (padName (pad));

        auto text = names.joinIntoString ("+");
        const auto stroke = binding["stroke"].toString();

        if (stroke == "repeat")        text << ko (" 반복");
        else if (stroke == "buzz")     text << ko (" 롤");
        else if (stroke == "double")   text << ko (" 더블");
        else if (stroke == "flam")     text << ko (" 플램");
        else if (stroke == "drag")     text << ko (" 드래그");
        else if (stroke == "pattern")  text << ko (" 패턴");
        else if (stroke == "downUp")   text << ko (" 뗌");
        else if (json::numberOr (binding["velocity"], 1.0) < 0.6)    text << ko (" 약");    // 약하게 치는 키

        return text.isEmpty() ? ko ("(패드 없음)") : text;
    }

    if (action == "choke")      return ko ("멈춤");
    if (action == "accent")     return ko ("악센트");
    if (action == "ghost")      return ko ("고스트");
    if (action == "pedal")
    {
        // 누를 때 소리를 내는 페달은 그 패드 이름으로 보인다 (하이햇 페달 등).
        const auto pads = keymap.getBindingPads (index);
        return pads.isEmpty() ? ko ("페달") : padName (pads[0]);
    }

    if (action == "tapTempo")   return ko ("탭 템포");
    if (action == "metronome")  return ko ("메트로놈");

    return action;
}

void KeysPage::fillPadCombo (juce::ComboBox& combo, bool withNone) const
{
    combo.clear (juce::dontSendNotification);

    if (withNone)
        combo.addItem (ko ("없음"), 1);

    for (int i = 0; i < padIds.size(); ++i)
        combo.addItem (padName (padIds[i]), i + 2);
}

juce::String KeysPage::comboPadId (const juce::ComboBox& combo) const
{
    return padIds[combo.getSelectedId() - 2];    // 범위를 벗어나면 빈 문자열
}

int KeysPage::currentBindingIndex() const
{
    if (selectedKey == hvd::invalidKey || selectedKey >= bindingsByKey.size())
        return -1;

    const auto& list = bindingsByKey[selectedKey];
    return juce::isPositiveAndBelow (selectedSlot, static_cast<int> (list.size())) ? list[static_cast<std::size_t> (selectedSlot)] : -1;
}

//==============================================================================
void KeysPage::rebuild()
{
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);

        documents.refresh();

        padIds = session.getKitDocument().getPadIds();

        const auto quickPad = comboPadId (quickPadCombo);
        fillPadCombo (quickPadCombo, false);
        quickPadCombo.setSelectedId (juce::jmax (0, padIds.indexOf (quickPad)) + 2, juce::dontSendNotification);

        fillPadCombo (padCombo, false);
        for (auto* combo : { &pressPadCombo, &releasePadCombo, &splashPadCombo, &heldSwap.from, &heldSwap.to, &releasedSwap.from, &releasedSwap.to })
            fillPadCombo (*combo, true);

        // 키마다 지정된 줄을 모은다.
        const auto& document = session.getKeymapDocument();
        bindingsByKey.assign (hvd::numKeyCodes, {});

        for (int i = 0; i < document.getNumBindings(); ++i)
            if (const auto key = hvd::keyFromName (document.getBinding (i)["key"].toString().toStdString()))
                bindingsByKey[*key].push_back (i);

        std::vector<KeyboardView::Cap> caps (hvd::numKeyCodes);

        for (std::size_t code = 0; code < hvd::numKeyCodes; ++code)
        {
            const auto& list = bindingsByKey[code];

            if (list.empty())
                continue;

            const auto action = document.getBinding (list.front())["action"].toString();
            const auto pads = document.getBindingPads (list.front());

            caps[code].text = describe (list.front());
            caps[code].count = static_cast<int> (list.size());

            juce::StringArray all;

            for (const auto index : list)
                all.add (describe (index));

            caps[code].tip = all.joinIntoString (", ");
            caps[code].colour = ((action.isEmpty() || action == "hit") && ! pads.isEmpty()) ? ui::padColour (pads[0]) : ui::theme().otherPad;
        }

        keyboard.setCaps (std::move (caps));
    }

    showBinding();
}

void KeysPage::selectKey (hvd::KeyCode key)
{
    if (! KeyboardView::hasKey (key))
        return;

    selectedKey = key;
    selectedSlot = 0;
    keyboard.setSelected (key);
    showBinding();
}

void KeysPage::showBinding()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto hasKey = selectedKey != hvd::invalidKey;
    const auto count = hasKey ? static_cast<int> (bindingsByKey[selectedKey].size()) : 0;
    selectedSlot = juce::jlimit (0, juce::jmax (0, count - 1), selectedSlot);

    keyTitle.setText (hasKey ? fromUtf8 (hvd::keyLabel (selectedKey)) : juce::String(), juce::dontSendNotification);
    keyTitle.setTooltip (hasKey ? fromUtf8 (hvd::keyName (selectedKey)) : juce::String());

    // 한 키에 동작이 여럿이면 동작마다 탭을 둔다.
    while (slotButtons.size() < count)
    {
        const auto index = slotButtons.size();
        auto* slot = slotButtons.add (new juce::TextButton());
        ui::setupButton (*this, *slot, ko ("동작 ") + juce::String (index + 1), [this, index]
        {
            selectedSlot = index;
            showBinding();
        });
        ui::setButtonKind (*slot, ui::ButtonKind::segment);
    }

    while (slotButtons.size() > count)
        slotButtons.removeLast();

    for (int i = 0; i < slotButtons.size(); ++i)
    {
        auto* slot = slotButtons[i];
        slot->setToggleState (i == selectedSlot, juce::dontSendNotification);
        slot->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < count - 1 ? juce::Button::ConnectedOnRight : 0));
        slot->setVisible (count > 1);
    }

    addButton.setEnabled (hasKey && ! padIds.isEmpty());
    emptyAddButton.setEnabled (addButton.isEnabled());
    addButton.setVisible (count > 0);
    removeButton.setVisible (count > 0);

    emptyHint.setText (hasKey ? ko ("동작 없음") : ko ("키를 누르거나 클릭하세요"), juce::dontSendNotification);

    for (auto& row : rows)
        row.shown = false;

    const auto& keymap = session.getKeymapDocument();

    if (const auto line = currentBindingIndex(); keymap.getBinding (line).isObject())
    {
        const auto binding = keymap.getBinding (line);
        const auto action = binding["action"].toString().isEmpty() ? juce::String ("hit") : binding["action"].toString();
        const auto pads = keymap.getBindingPads (line);

        actionCombo.setSelectedItemIndex (indexOfChoice (actions, action), juce::dontSendNotification);
        rows[0].shown = true;

        if (action == "hit")
        {
            const auto stroke = binding["stroke"].toString().isEmpty() ? juce::String ("single") : binding["stroke"].toString();

            padCombo.setSelectedId (padIds.indexOf (pads[0]) + 2, juce::dontSendNotification);

            juce::StringArray extra;

            for (int i = 1; i < pads.size(); ++i)
                extra.add (padName (pads[i]));

            rows[2].caption.setText (ko ("함께 칠 패드"), juce::dontSendNotification);
            padsButton.setButtonText (extra.isEmpty() ? ko ("없음") : extra.joinIntoString (", "));

            velocitySlider.setValue (json::numberOr (binding["velocity"], 1.0), juce::dontSendNotification);
            strokeCombo.setSelectedItemIndex (indexOfChoice (strokes, stroke), juce::dontSendNotification);

            for (const auto index : { 1, 2, 3, 4 })
                rows[static_cast<std::size_t> (index)].shown = true;

            if (stroke == "repeat")
            {
                const auto rate = binding["rate"].toString();
                rateCombo.setSelectedItemIndex (indexOfChoice (rates, rate.isEmpty() ? juce::String ("1/16") : rate), juce::dontSendNotification);
                accentSlider.setValue (juce::jmax (0.0, json::numberOr (binding["accentEvery"], 0.0)), juce::dontSendNotification);
                softSlider.setValue (json::numberOr (binding["softVelocity"], 0.8), juce::dontSendNotification);
                rows[5].shown = rows[6].shown = rows[7].shown = true;
            }
            else if (stroke == "double" || stroke == "flam" || stroke == "drag")
            {
                gapSlider.setValue (json::numberOr (binding["gapMs"], hvd::defaultGapMs (stroke.toStdString())), juce::dontSendNotification);
                rows[8].shown = true;
            }
            else if (stroke == "pattern")
            {
                stepsEditor.setText (binding["steps"].toString(), false);
                divisionCombo.setSelectedId (juce::roundToInt (json::numberOr (binding["division"], 4.0)), juce::dontSendNotification);
                rows[9].shown = rows[10].shown = true;
            }
        }
        else if (action == "choke")
        {
            juce::StringArray names;

            for (const auto& pad : pads)
                names.add (padName (pad));

            rows[2].caption.setText (ko ("멈출 패드"), juce::dontSendNotification);
            padsButton.setButtonText (names.isEmpty() ? ko ("없음") : names.joinIntoString (", "));
            rows[2].shown = true;
        }
        else if (action == "pedal")
        {
            // 콤보의 번호: 1은 없음, 패드는 순서 + 2
            const auto selectPad = [this] (juce::ComboBox& combo, const juce::String& padId)
            {
                combo.setSelectedId (padIds.indexOf (padId) + 2, juce::dontSendNotification);
            };

            const auto showPair = [&selectPad] (PadSwapPicker& picker, const juce::var& swaps)
            {
                juce::String from, to;

                if (const auto* swap = swaps.getDynamicObject(); swap != nullptr && swap->getProperties().size() > 0)
                {
                    from = swap->getProperties().getName (0).toString();
                    to = swap->getProperties().getValueAt (0).toString();
                }

                selectPad (picker.from, from);
                selectPad (picker.to, to);
            };

            selectPad (pressPadCombo, pads[0]);
            selectPad (releasePadCombo, binding["releasePad"].toString());
            selectPad (splashPadCombo, binding["splash"].toString());
            showPair (heldSwap, binding["swap"]);
            showPair (releasedSwap, binding["releasedSwap"]);

            for (const auto index : { 11, 12, 13, 14, 15 })
                rows[static_cast<std::size_t> (index)].shown = true;
        }
    }

    layoutEditor();
    repaint();
}

void KeysPage::commit()
{
    // 화면은 고친 문서로 바로 다시 그리고, 파일은 Session이 마우스를 놓은 뒤에 저장해 적용한다 (그때 한 번 더 그린다).
    session.markKeymapEdited();
    rebuild();
}

//==============================================================================
void KeysPage::addBinding()
{
    if (selectedKey == hvd::invalidKey || padIds.isEmpty())
        return;

    const auto quickPad = comboPadId (quickPadCombo);
    session.getKeymapDocument().addBinding (makeHitBinding (selectedKey, quickPad.isNotEmpty() ? quickPad : padIds[0]));

    selectedSlot = static_cast<int> (bindingsByKey[selectedKey].size());    // 새 동작은 이 키의 마지막에 온다
    commit();
}

void KeysPage::removeBinding()
{
    if (const auto index = currentBindingIndex(); index >= 0)
    {
        session.getKeymapDocument().removeBinding (index);
        selectedSlot = 0;
        commit();
    }
}

//==============================================================================
void KeysPage::pedalButtonClicked()
{
    // 등록하는 중이면 마친다.
    if (session.isPedalLearning())
    {
        if (session.endPedalLearning (true))
        {
            pedalConnected = true;
            selectKey (hvd::pedalKey (0));
        }

        updatePedalStatus();
        return;
    }

    if (session.getPedal().device.isEmpty())
    {
        startPedalLearning();
        return;
    }

    juce::PopupMenu menu;
    menu.addItem (1, ko ("다시 등록"));
    menu.addItem (2, ko ("등록 지우기"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&pedalButton),
                        [this] (int result)
                        {
                            if (result == 1)
                            {
                                startPedalLearning();
                            }
                            else if (result == 2)
                            {
                                session.setPedal ({});
                                pedalConnected = false;
                                updatePedalStatus();
                            }
                        });
}

void KeysPage::startPedalLearning()
{
    session.cancelKeyCapture();
    session.beginPedalLearning();
    updatePedalStatus();
}

void KeysPage::updatePedalStatus()
{
    auto learning = session.isPedalLearning();

    if (learning)
    {
        auto cancelled = false;
        const auto learned = session.getPedalLearning (cancelled);

        if (cancelled)
        {
            session.endPedalLearning (false);
            learning = false;
        }
        else
        {
            // 밟은 스위치를 차례로 보인다: 1 F13   2 F14
            juce::String keys;

            for (std::size_t i = 0; i < learned.keys.size(); ++i)
                keys << (i > 0 ? "     " : "") << juce::String (static_cast<int> (i) + 1) << "  " << fromUtf8 (hvd::keyLabel (learned.keys[i]));

            banner.keys.setText (keys, juce::dontSendNotification);
        }
    }

    const auto& pedal = session.getPedal();
    const auto registered = pedal.device.isNotEmpty();
    juce::String chip, tip;

    if (registered)
    {
        chip = pedalConnected ? ko ("연결됨") : ko ("연결 안 됨");
        tip = ko ("스위치 ") + juce::String (static_cast<int> (pedal.keys.size())) + ko ("개");

        if (pedalConnected)
        {
            const auto interval = intervalText (session.getReportInterval (true));
            chip << ko ("  ·  ") << interval;
            tip << ko ("  ·  보고 간격 ") << interval;
        }
    }

    // 견주어 볼 수 있게 키보드의 것도 함께 적는다.
    if (const auto ofKeyboard = session.getReportInterval (false); ofKeyboard.enough)
        tip << (tip.isNotEmpty() ? ko ("  ·  ") : juce::String()) << ko ("키보드 보고 간격 ") << intervalText (ofKeyboard);

    pedalButton.setButtonText (registered ? ko ("페달") : ko ("페달 등록"));
    pedalButton.setTooltip (registered ? tip : ko ("풋스위치를 등록하면 키보드와 따로 정할 수 있습니다"));
    pedalLabel.setText (chip, juce::dontSendNotification);
    pedalLabel.setTooltip (tip);

    // 등록하는 동안에는 키보드 위의 띠가 대신한다.
    const auto chipShown = registered && ! learning;
    const auto layoutChanged = chipShown != pedalLabel.isVisible() || chip != shownChip;

    pedalButton.setVisible (! learning);
    pedalLabel.setVisible (chipShown);
    banner.setVisible (learning);
    shownChip = chip;

    if (layoutChanged)
    {
        resized();
        repaint();
    }
}

//==============================================================================
void KeysPage::startQuickAssign()
{
    if (session.isKeyCaptureArmed())
    {
        session.cancelKeyCapture();
        return;
    }

    session.endPedalLearning (false);

    const auto pad = comboPadId (quickPadCombo);

    if (pad.isEmpty())
        return;

    session.armKeyCapture ([this, pad] (hvd::KeyCode key)
    {
        if (key == 0x01)    // Esc: 그만둔다
            return;

        session.getKeymapDocument().addBinding (makeHitBinding (key, pad));

        selectedKey = key;
        keyboard.setSelected (key);
        selectedSlot = key < bindingsByKey.size() ? static_cast<int> (bindingsByKey[key].size()) : 0;
        commit();
    });
}

void KeysPage::showPadsMenu()
{
    const auto index = currentBindingIndex();

    if (index < 0)
        return;

    const auto& keymap = session.getKeymapDocument();
    const auto pads = keymap.getBindingPads (index);
    const auto action = keymap.getBinding (index)["action"].toString();
    const auto hit = action.isEmpty() || action == "hit";

    juce::PopupMenu menu;

    for (int i = 0; i < padIds.size(); ++i)
    {
        // 치기에서는 첫 패드를 "패드" 칸에서 고르므로 여기서는 끌 수 없다.
        const auto main = hit && pads.indexOf (padIds[i]) == 0;
        menu.addItem (i + 1, padName (padIds[i]), ! main, pads.contains (padIds[i]));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&padsButton),
                        [this] (int result)
                        {
                            const auto target = currentBindingIndex();

                            if (result <= 0 || target < 0)
                                return;

                            auto& document = session.getKeymapDocument();
                            auto chosen = document.getBindingPads (target);
                            const auto id = padIds[result - 1];

                            if (chosen.contains (id))
                                chosen.removeString (id);
                            else
                                chosen.add (id);

                            document.setBindingPads (target, chosen);
                            commit();
                        });
}

void KeysPage::actionChanged()
{
    // 지울 항목과 처음 채울 값은 문서가 정한다 (KeymapDocument::setBindingAction).
    if (const auto index = currentBindingIndex(); index >= 0)
    {
        session.getKeymapDocument().setBindingAction (index, actions[juce::jmax (0, actionCombo.getSelectedItemIndex())].value, padIds);
        commit();
    }
}

void KeysPage::strokeChanged()
{
    if (const auto index = currentBindingIndex(); index >= 0)
    {
        session.getKeymapDocument().setBindingStroke (index, strokes[juce::jmax (0, strokeCombo.getSelectedItemIndex())].value);
        commit();
    }
}

//==============================================================================
void KeysPage::tick()
{
    keyboard.refreshHeld();

    if (++tickCount % 6 == 0)
    {
        heldLabel.setText (ko ("동시 ") + juce::String (session.getHeldKeyCount())
                               + ko ("  ·  최대 ") + juce::String (session.getMaxHeldKeyCount()),
                           juce::dontSendNotification);

        // 키를 기다리는 동안 켜진 모양. 글이 길어지므로 제목 줄을 다시 놓는다.
        if (const auto armed = session.isKeyCaptureArmed(); armed != quickButton.getToggleState())
        {
            quickButton.setButtonText (armed ? ko ("키를 누르세요  ·  Esc") : ko ("키 지정"));
            quickButton.setToggleState (armed, juce::dontSendNotification);
            layoutEditor();
        }

        // 페달이 꽂혀 있는지는 1초에 한 번쯤만 알아본다.
        if (tickCount % 60 == 0)
            pedalConnected = session.isPedalConnected();

        updatePedalStatus();
    }

    // 키보드의 키를 누르면 그 키를 고른다.
    if (const auto count = session.getKeyPressCount(); count != seenKeyPressCount)
    {
        seenKeyPressCount = count;

        if (followButton.getToggleState() && ! session.isKeyCaptureArmed() && session.getLastKey() != selectedKey)
            selectKey (session.getLastKey());
    }
}
