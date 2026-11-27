#include "KeysPage.h"

namespace
{

constexpr float unitsWide = 22.5f;
constexpr float unitsHigh = 6.25f;

const juce::Colour functionColour { 0xff78909c };
const juce::Colour emptyKeyColour { 0xff28323b };

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

double numberOr (const juce::var& value, double fallback)
{
    return (value.isDouble() || value.isInt() || value.isInt64()) ? static_cast<double> (value) : fallback;
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
KeyboardView::KeyboardView()
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    caps.resize (hvd::numKeyCodes);
    heldShown.assign (hvd::numKeyCodes, false);
}

const std::vector<KeyboardView::Shape>& KeyboardView::shapes()
{
    // 표준 104키 배열. 좌표의 단위는 보통 키 하나의 폭이다.
    static const std::vector<Shape> all = []
    {
        std::vector<Shape> list;

        const auto add = [&list] (unsigned scan, bool extended, float x, float y, float w = 1.0f, float h = 1.0f)
        {
            list.push_back ({ hvd::makeKeyCode (scan, extended), x, y, w, h });
        };

        const auto run = [&add] (unsigned first, unsigned last, float x, float y)
        {
            for (auto scan = first; scan <= last; ++scan)
                add (scan, false, x + static_cast<float> (scan - first), y);
        };

        // 기능 키 줄
        add (0x01, false, 0.0f, 0.0f);
        run (0x3b, 0x3e, 2.0f, 0.0f);
        run (0x3f, 0x42, 6.5f, 0.0f);
        run (0x43, 0x44, 11.0f, 0.0f);
        add (0x57, false, 13.0f, 0.0f);
        add (0x58, false, 14.0f, 0.0f);
        add (0x46, false, 16.25f, 0.0f);

        // 숫자 줄
        add (0x29, false, 0.0f, 1.25f);
        run (0x02, 0x0d, 1.0f, 1.25f);
        add (0x0e, false, 13.0f, 1.25f, 2.0f);

        // 윗줄
        add (0x0f, false, 0.0f, 2.25f, 1.5f);
        run (0x10, 0x1b, 1.5f, 2.25f);
        add (0x2b, false, 13.5f, 2.25f, 1.5f);

        // 가운데 줄
        add (0x3a, false, 0.0f, 3.25f, 1.75f);
        run (0x1e, 0x28, 1.75f, 3.25f);
        add (0x1c, false, 12.75f, 3.25f, 2.25f);

        // 아랫줄
        add (0x2a, false, 0.0f, 4.25f, 2.25f);
        run (0x2c, 0x35, 2.25f, 4.25f);
        add (0x36, false, 12.25f, 4.25f, 2.75f);

        // 맨 아랫줄
        add (0x1d, false, 0.0f, 5.25f, 1.25f);
        add (0x5b, true, 1.25f, 5.25f, 1.25f);
        add (0x38, false, 2.5f, 5.25f, 1.25f);
        add (0x39, false, 3.75f, 5.25f, 6.25f);
        add (0x38, true, 10.0f, 5.25f, 1.25f);
        add (0x5c, true, 11.25f, 5.25f, 1.25f);
        add (0x5d, true, 12.5f, 5.25f, 1.25f);
        add (0x1d, true, 13.75f, 5.25f, 1.25f);

        // 이동 키
        add (0x52, true, 15.25f, 1.25f);
        add (0x47, true, 16.25f, 1.25f);
        add (0x49, true, 17.25f, 1.25f);
        add (0x53, true, 15.25f, 2.25f);
        add (0x4f, true, 16.25f, 2.25f);
        add (0x51, true, 17.25f, 2.25f);
        add (0x48, true, 16.25f, 4.25f);
        add (0x4b, true, 15.25f, 5.25f);
        add (0x50, true, 16.25f, 5.25f);
        add (0x4d, true, 17.25f, 5.25f);

        // 숫자 패드
        add (0x35, true, 19.5f, 1.25f);
        add (0x37, false, 20.5f, 1.25f);
        add (0x4a, false, 21.5f, 1.25f);
        run (0x47, 0x49, 18.5f, 2.25f);
        add (0x4e, false, 21.5f, 2.25f, 1.0f, 2.0f);
        run (0x4b, 0x4d, 18.5f, 3.25f);
        run (0x4f, 0x51, 18.5f, 4.25f);
        add (0x1c, true, 21.5f, 4.25f, 1.0f, 2.0f);
        add (0x52, false, 18.5f, 5.25f, 2.0f);
        add (0x53, false, 20.5f, 5.25f);

        // 페달(풋스위치)의 스위치. 숫자 패드 위의 빈자리에 둔다.
        for (int i = 0; i < hvd::maxPedalKeys; ++i)
            list.push_back ({ hvd::pedalKey (i), 18.5f + static_cast<float> (i), 0.0f, 1.0f, 1.0f });

        return list;
    }();

    return all;
}

bool KeyboardView::hasKey (hvd::KeyCode key)
{
    for (const auto& shape : shapes())
        if (shape.code == key)
            return true;

    return false;
}

juce::Rectangle<float> KeyboardView::boundsOf (const Shape& shape) const
{
    const auto unit = juce::jmin (static_cast<float> (getWidth()) / unitsWide, static_cast<float> (getHeight()) / unitsHigh);
    const auto left = (static_cast<float> (getWidth()) - unitsWide * unit) * 0.5f;

    return { left + shape.x * unit, shape.y * unit, shape.w * unit - 3.0f, shape.h * unit - 3.0f };
}

void KeyboardView::setCaps (std::vector<Cap> newCaps)
{
    caps = std::move (newCaps);
    caps.resize (hvd::numKeyCodes);
    repaint();
}

void KeyboardView::setSelected (hvd::KeyCode key)
{
    if (selected != key)
    {
        selected = key;
        repaint();
    }
}

void KeyboardView::refreshHeld()
{
    if (isKeyHeld == nullptr)
        return;

    auto changed = false;

    for (const auto& shape : shapes())
    {
        const auto held = isKeyHeld (shape.code);

        if (heldShown[shape.code] != held)
        {
            heldShown[shape.code] = held;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void KeyboardView::paint (juce::Graphics& g)
{
    const auto unit = juce::jmin (static_cast<float> (getWidth()) / unitsWide, static_cast<float> (getHeight()) / unitsHigh);
    const auto labelSize = juce::jlimit (9.0f, 12.0f, unit * 0.25f);
    const auto textSize = juce::jlimit (9.5f, 13.0f, unit * 0.27f);

    for (const auto& shape : shapes())
    {
        const auto& cap = caps[shape.code];
        const auto bounds = boundsOf (shape);
        const auto held = heldShown[shape.code];
        const auto bound = cap.text.isNotEmpty();

        auto fill = bound ? cap.colour.withMultipliedBrightness (0.42f) : emptyKeyColour;

        if (held)
            fill = bound ? cap.colour : ui::accent;

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 5.0f);

        if (shape.code == selected)
        {
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (bounds.reduced (1.0f), 5.0f, 2.0f);
        }
        else
        {
            g.setColour (bound ? cap.colour.withAlpha (0.8f) : ui::panelEdge);
            g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);
        }

        const auto dark = held;
        auto inner = bounds.reduced (4.0f, 2.0f).toNearestInt();

        g.setColour (dark ? juce::Colour (0xff14181c) : (bound ? juce::Colours::white.withAlpha (0.75f) : ui::quietText));
        g.setFont (juce::FontOptions (labelSize));
        g.drawText (fromUtf8 (hvd::keyLabel (shape.code)), inner.removeFromTop (juce::roundToInt (labelSize + 3.0f)),
                    juce::Justification::topLeft, true);

        if (bound)
        {
            g.setColour (dark ? juce::Colour (0xff14181c) : juce::Colours::white);
            g.setFont (juce::FontOptions (textSize, juce::Font::bold));

            auto text = cap.text;

            if (cap.count > 1)
                text << " +" << (cap.count - 1);

            g.drawFittedText (text, inner, juce::Justification::centred, 2, 0.6f);
        }
    }
}

void KeyboardView::mouseDown (const juce::MouseEvent& event)
{
    for (const auto& shape : shapes())
    {
        if (boundsOf (shape).contains (event.position))
        {
            if (onKeyClicked)
                onKeyClicked (shape.code);

            return;
        }
    }
}

//==============================================================================
KeysPage::KeysPage (Session& sessionToUse)
    : session (sessionToUse)
{
    ui::setupLabel (*this, keymapCaption, ko ("키 배열"), 14.0f, ui::quietText);
    ui::setupCombo (*this, keymapCombo, [this]
    {
        if (! updating && keymapCombo.getSelectedItemIndex() >= 0)
            session.selectKeymap (keymapCombo.getText());
    });

    ui::setupButton (*this, duplicateButton, ko ("복제"), [this]
    {
        ui::promptForText (this, ko ("키 배열 복제"), ko ("지금 키 배열을 복사해 새로 만듭니다. 이름을 적으세요."),
                           session.getKeymapName() + "-2",
                           [this] (juce::String name)
                           {
                               if (const auto error = session.duplicateKeymap (name); error.isNotEmpty())
                                   ui::showMessage (ko ("키 배열을 복제하지 못했습니다"), error);
                           });
    });

    ui::setupButton (*this, renameButton, ko ("이름 바꾸기"), [this]
    {
        ui::promptForText (this, ko ("키 배열 이름 바꾸기"), ko ("화면에 보여 줄 이름을 적으세요. 파일 이름은 바뀌지 않습니다."),
                           session.getKeymapDocument().getName(),
                           [this] (juce::String name)
                           {
                               if (name.trim().isNotEmpty())
                               {
                                   session.getKeymapDocument().setName (name.trim());
                                   commit();
                               }
                           });
    });

    ui::setupButton (*this, folderButton, ko ("폴더 열기"), [this] { session.getKeymapFile().revealToUser(); });
    ui::setupButton (*this, restoreButton, ko ("되돌리기…"), [this]
    {
        // 키 배열 파일이 바뀔 때마다 남겨 둔 이전 내용 가운데 하나로 되돌린다.
        showBackupMenu (restoreButton, session.getBackups(), session.getKeymapFile(), [this] (juce::File backup)
        {
            if (const auto error = session.restoreKeymapBackup (backup); error.isNotEmpty())
                ui::showMessage (ko ("되돌리지 못했습니다"), error);
        });
    });
    restoreButton.setTooltip (ko ("고치기 전 상태로 되돌립니다."));

    ui::setupLabel (*this, quickCaption, ko ("빠른 지정"), 14.0f, ui::quietText);
    ui::setupCombo (*this, quickPadCombo, [] {});
    ui::setupButton (*this, quickButton, ko ("이 패드를 칠 키 누르기"), [this] { startQuickAssign(); });

    followButton.setButtonText (ko ("누른 키 따라가기"));
    followButton.setToggleState (true, juce::dontSendNotification);
    ui::noFocus (followButton);
    addAndMakeVisible (followButton);

    ui::setupButton (*this, pedalButton, ko ("페달 등록"), [this] { pedalButtonClicked(); });
    ui::setupLabel (*this, pedalLabel, {}, 13.0f, ui::quietText);
    pedalLabel.setMinimumHorizontalScale (0.75f);

    ui::setupLabel (*this, heldLabel, {}, 13.0f, ui::quietText);
    heldLabel.setJustificationType (juce::Justification::centredRight);
    ui::setupButton (*this, resetHeldButton, ko ("최대값 지우기"), [this] { session.resetMaxHeldKeyCount(); });

    keyboard.onKeyClicked = [this] (hvd::KeyCode key) { selectKey (key); };
    keyboard.isKeyHeld = [this] (hvd::KeyCode key) { return session.isKeyHeld (key); };
    addAndMakeVisible (keyboard);

    ui::setupLabel (*this, keyTitle, {}, 17.0f, juce::Colours::white, true);
    ui::setupCombo (*this, slotCombo, [this]
    {
        if (! updating)
        {
            selectedSlot = juce::jmax (0, slotCombo.getSelectedItemIndex());
            showBinding();
        }
    });
    ui::setupButton (*this, addButton, ko ("이 키에 동작 추가"), [this] { addBinding(); });
    ui::setupButton (*this, removeButton, ko ("이 동작 지우기"), [this] { removeBinding(); });
    ui::setupLabel (*this, emptyHint, {}, 14.0f, ui::quietText);
    emptyHint.setJustificationType (juce::Justification::topLeft);

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
        auto* binding = currentBinding();

        if (updating || binding == nullptr)
            return;

        auto pads = padsOf (juce::var (binding));
        const auto chosen = comboPadId (padCombo);

        if (chosen.isEmpty())
            return;

        pads.removeString (chosen);
        pads.set (0, chosen);
        setPads (*binding, pads);
        commit();
    });

    ui::setupCombo (*this, rateCombo, [this]
    {
        if (auto* binding = currentBinding(); ! updating && binding != nullptr)
        {
            binding->setProperty ("rate", rates[juce::jmax (0, rateCombo.getSelectedItemIndex())].value);
            binding->removeProperty ("intervalMs");
            commit();
        }
    });

    ui::setupCombo (*this, divisionCombo, [this]
    {
        if (auto* binding = currentBinding(); ! updating && binding != nullptr)
        {
            binding->setProperty ("division", divisionCombo.getSelectedId());
            commit();
        }
    });

    const auto pedalChanged = [this]
    {
        auto* binding = currentBinding();

        if (updating || binding == nullptr)
            return;

        // 화면에서는 누르는 동안, 떼고 있는 동안 각각 한 쌍만 다룬다.
        const auto pairOf = [this] (const PadSwapPicker& picker)
        {
            juce::DynamicObject::Ptr swap (new juce::DynamicObject());

            if (const auto from = comboPadId (picker.from), to = comboPadId (picker.to); from.isNotEmpty() && to.isNotEmpty())
                swap->setProperty (from, to);

            return swap;
        };

        binding->setProperty ("swap", juce::var (pairOf (heldSwap).get()));

        if (const auto released = pairOf (releasedSwap); released->getProperties().size() > 0)
            binding->setProperty ("releasedSwap", juce::var (released.get()));
        else
            binding->removeProperty ("releasedSwap");

        const auto press = comboPadId (pressPadCombo);
        setPads (*binding, press.isNotEmpty() ? juce::StringArray (press) : juce::StringArray());

        if (const auto release = comboPadId (releasePadCombo); release.isNotEmpty())
            binding->setProperty ("releasePad", release);
        else
            binding->removeProperty ("releasePad");

        if (const auto splash = comboPadId (splashPadCombo); splash.isNotEmpty())
            binding->setProperty ("splash", splash);
        else
            binding->removeProperty ("splash");

        commit();
    };

    ui::setupCombo (*this, pressPadCombo, pedalChanged);
    ui::setupCombo (*this, releasePadCombo, pedalChanged);
    ui::setupCombo (*this, splashPadCombo, pedalChanged);

    for (auto* picker : { &heldSwap, &releasedSwap })
    {
        ui::setupCombo (*picker, picker->from, pedalChanged);
        ui::setupCombo (*picker, picker->to, pedalChanged);
        ui::setupLabel (*picker, picker->arrow, ko ("→"), 15.0f, ui::quietText);
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
            if (auto* binding = currentBinding(); ! updating && binding != nullptr)
            {
                if (integer)
                    binding->setProperty (name, juce::roundToInt (slider.getValue()));
                else
                    binding->setProperty (name, slider.getValue());

                valuesDirty = true;    // 마우스를 놓은 뒤에 저장한다
            }
        };
    };

    bindSlider (velocitySlider, "velocity", false);
    bindSlider (accentSlider, "accentEvery", true);
    bindSlider (softSlider, "softVelocity", false);
    bindSlider (gapSlider, "gapMs", false);

    const auto stepsChanged = [this]
    {
        auto* binding = currentBinding();

        if (updating || binding == nullptr)
            return;

        const auto steps = stepsEditor.getText().retainCharacters ("Xxo.");

        if (steps.isNotEmpty() && steps != binding->getProperty ("steps").toString())
        {
            binding->setProperty ("steps", steps);
            commit();
        }
    };

    // 다 적고 엔터나 Esc를 누르면 커서를 칸 밖으로 내보낸다. 커서가 입력 칸에 있는 동안에는 연주가 멈추기 때문이다.
    stepsEditor.onReturnKey = [this, stepsChanged] { stepsChanged(); stepsEditor.giveAwayKeyboardFocus(); };
    stepsEditor.onEscapeKey = [this] { stepsEditor.giveAwayKeyboardFocus(); };
    stepsEditor.onFocusLost = stepsChanged;
    stepsEditor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::plain));
    addAndMakeVisible (stepsEditor);

    const std::pair<const char*, juce::Component*> rowSpecs[] = {
        { "동작", &actionCombo },
        { "패드", &padCombo },
        { "함께 칠 패드", &padsButton },
        { "세기", &velocitySlider },
        { "타격 방식", &strokeCombo },
        { "반복 간격", &rateCombo },
        { "악센트 주기 (몇 번째마다, 0은 첫 타만)", &accentSlider },
        { "악센트가 아닌 타의 세기 (악센트 대비)", &softSlider },
        { "두 타 사이 간격", &gapSlider },
        { "패턴 (X 세게, x 보통, o 약하게, . 쉼)", &stepsEditor },
        { "한 박을 나누는 수", &divisionCombo },
        { "누를 때 칠 패드", &pressPadCombo },
        { "뗄 때 칠 패드", &releasePadCombo },
        { "누르는 동안 바꿔 칠 패드", &heldSwap },
        { "뗀 동안 바꿔 칠 패드", &releasedSwap },
        { "빨리 떼면 칠 패드", &splashPadCombo },
    };

    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        ui::setupLabel (*this, rows[i].caption, ko (rowSpecs[i].first), 13.0f, ui::quietText);
        rows[i].control = rowSpecs[i].second;
    }

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

void KeysPage::visibilityChanged()
{
    // 다른 화면으로 넘어가면 페달 등록을 그만둔다. 그대로 두면 누르는 키가 계속 연주되지 않는다.
    if (! isVisible())
        session.endPedalLearning (false);
}

void KeysPage::paint (juce::Graphics& g)
{
    g.setColour (ui::panel);
    g.fillRoundedRectangle (keyTitle.getBounds().getUnion (emptyHint.getBounds()).expanded (14, 10).toFloat(), 8.0f);
}

void KeysPage::resized()
{
    auto area = getLocalBounds();

    auto top = area.removeFromTop (30);
    keymapCaption.setBounds (top.removeFromLeft (56));
    keymapCombo.setBounds (top.removeFromLeft (180));
    top.removeFromLeft (8);
    duplicateButton.setBounds (top.removeFromLeft (60));
    top.removeFromLeft (6);
    renameButton.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (6);
    folderButton.setBounds (top.removeFromLeft (84));
    top.removeFromLeft (6);
    restoreButton.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (24);
    quickCaption.setBounds (top.removeFromLeft (64));
    quickPadCombo.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (6);
    quickButton.setBounds (top.removeFromLeft (170));

    area.removeFromTop (6);
    auto second = area.removeFromTop (24);
    followButton.setBounds (second.removeFromLeft (150));
    second.removeFromLeft (8);
    pedalButton.setBounds (second.removeFromLeft (96));
    second.removeFromLeft (8);
    resetHeldButton.setBounds (second.removeFromRight (104));
    second.removeFromRight (8);
    heldLabel.setBounds (second.removeFromRight (190));
    second.removeFromRight (8);
    pedalLabel.setBounds (second);

    area.removeFromTop (6);

    // 키보드는 폭에 맞추되, 편집 칸이 들어갈 높이는 남긴다.
    const auto keyboardHeight = juce::jmin (juce::roundToInt (static_cast<float> (area.getWidth()) / unitsWide * unitsHigh),
                                            juce::jmax (150, area.getHeight() - 236));
    keyboard.setBounds (area.removeFromTop (keyboardHeight));

    area.removeFromTop (12);
    auto editor = area.reduced (14, 10);

    auto title = editor.removeFromTop (28);
    removeButton.setBounds (title.removeFromRight (120));
    title.removeFromRight (6);
    addButton.setBounds (title.removeFromRight (140));
    title.removeFromRight (12);
    slotCombo.setBounds (title.removeFromRight (100));
    keyTitle.setBounds (title);

    editor.removeFromTop (6);
    emptyHint.setBounds (editor);
    layoutEditor();
}

void KeysPage::layoutEditor()
{
    // 보이는 항목을 세 칸짜리 격자에 차례로 놓는다.
    auto area = emptyHint.getBounds();
    constexpr int columns = 3;
    constexpr int rowHeight = 50;
    const auto cellWidth = area.getWidth() / columns;
    auto index = 0;

    for (auto& row : rows)
    {
        row.caption.setVisible (row.shown);
        row.control->setVisible (row.shown);

        if (! row.shown)
            continue;

        auto cell = juce::Rectangle<int> (area.getX() + (index % columns) * cellWidth,
                                          area.getY() + (index / columns) * rowHeight,
                                          cellWidth - 18, rowHeight - 4);
        row.caption.setBounds (cell.removeFromTop (19));
        row.control->setBounds (cell.removeFromTop (26));
        ++index;
    }

    emptyHint.setVisible (index == 0);
}

//==============================================================================
juce::String KeysPage::padName (const juce::String& padId) const
{
    if (const auto* kit = session.getKit())
        if (const auto index = kit->findPad (padId.toStdString()); index >= 0)
            return fromUtf8 (kit->pads[static_cast<std::size_t> (index)].name);

    return padId;
}

juce::StringArray KeysPage::padsOf (const juce::var& binding) const
{
    juce::StringArray pads;

    if (const auto pad = binding["pad"].toString(); pad.isNotEmpty())
        pads.add (pad);

    if (const auto* list = binding["pads"].getArray())
        for (const auto& pad : *list)
            if (pad.toString().isNotEmpty())
                pads.addIfNotAlreadyThere (pad.toString());

    return pads;
}

void KeysPage::setPads (juce::DynamicObject& binding, const juce::StringArray& pads) const
{
    binding.removeProperty ("pad");
    binding.removeProperty ("pads");

    if (pads.size() == 1)
    {
        binding.setProperty ("pad", pads[0]);
    }
    else if (pads.size() > 1)
    {
        juce::Array<juce::var> list;

        for (const auto& pad : pads)
            list.add (pad);

        binding.setProperty ("pads", list);
    }
}

juce::String KeysPage::describe (const juce::var& binding) const
{
    const auto action = binding["action"].toString();

    if (action.isEmpty() || action == "hit")
    {
        juce::StringArray names;

        for (const auto& pad : padsOf (binding))
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
        else if (numberOr (binding["velocity"], 1.0) < 0.6)    text << ko (" 약");    // 약하게 치는 키

        return text.isEmpty() ? ko ("(패드 없음)") : text;
    }

    if (action == "choke")      return ko ("멈춤");
    if (action == "accent")     return ko ("악센트");
    if (action == "ghost")      return ko ("고스트");
    if (action == "pedal")
    {
        // 누를 때 소리를 내는 페달은 그 패드 이름으로 보인다 (하이햇 페달 등).
        const auto pads = padsOf (binding);
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
        combo.addItem (ko ("(없음)"), 1);

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

juce::DynamicObject* KeysPage::currentBinding() const
{
    return session.getKeymapDocument().getBinding (currentBindingIndex()).getDynamicObject();
}

//==============================================================================
void KeysPage::rebuild()
{
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);

        const auto names = session.getKeymapNames();
        keymapCombo.clear (juce::dontSendNotification);
        keymapCombo.addItemList (names, 1);
        keymapCombo.setSelectedItemIndex (names.indexOf (session.getKeymapName()), juce::dontSendNotification);

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

            const auto first = document.getBinding (list.front());
            const auto action = first["action"].toString();
            const auto pads = padsOf (first);

            caps[code].text = describe (first);
            caps[code].count = static_cast<int> (list.size());
            caps[code].colour = ((action.isEmpty() || action == "hit") && ! pads.isEmpty()) ? ui::padColour (pads[0]) : functionColour;
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

    keyTitle.setText (hasKey ? ko ("고른 키: ") + fromUtf8 (hvd::keyLabel (selectedKey)) + "   (" + fromUtf8 (hvd::keyName (selectedKey)) + ")"
                             : ko ("키를 고르세요"),
                      juce::dontSendNotification);

    slotCombo.clear (juce::dontSendNotification);

    for (int i = 0; i < count; ++i)
        slotCombo.addItem (ko ("동작 ") + juce::String (i + 1), i + 1);

    slotCombo.setSelectedItemIndex (selectedSlot, juce::dontSendNotification);
    slotCombo.setVisible (count > 1);
    addButton.setEnabled (hasKey && ! padIds.isEmpty());
    removeButton.setEnabled (count > 0);

    emptyHint.setText (hasKey ? ko ("이 키에는 아직 지정된 동작이 없습니다. \"이 키에 동작 추가\"를 누르세요.")
                              : ko ("위 그림의 키를 클릭하거나 키보드에서 누르세요."),
                       juce::dontSendNotification);

    for (auto& row : rows)
        row.shown = false;

    if (const auto* object = currentBinding())
    {
        const juce::var binding (const_cast<juce::DynamicObject*> (object));
        const auto action = binding["action"].toString().isEmpty() ? juce::String ("hit") : binding["action"].toString();
        const auto pads = padsOf (binding);

        actionCombo.setSelectedItemIndex (indexOfChoice (actions, action), juce::dontSendNotification);
        rows[0].shown = true;

        if (action == "hit")
        {
            const auto stroke = binding["stroke"].toString().isEmpty() ? juce::String ("single") : binding["stroke"].toString();

            padCombo.setSelectedId (padIds.indexOf (pads[0]) + 2, juce::dontSendNotification);

            juce::StringArray extra;

            for (int i = 1; i < pads.size(); ++i)
                extra.add (padName (pads[i]));

            rows[2].caption.setText (ko ("함께 칠 패드 (겹쳐 치기)"), juce::dontSendNotification);
            padsButton.setButtonText (extra.isEmpty() ? ko ("없음 (고르기…)") : extra.joinIntoString (", "));

            velocitySlider.setValue (numberOr (binding["velocity"], 1.0), juce::dontSendNotification);
            strokeCombo.setSelectedItemIndex (indexOfChoice (strokes, stroke), juce::dontSendNotification);

            for (const auto index : { 1, 2, 3, 4 })
                rows[static_cast<std::size_t> (index)].shown = true;

            if (stroke == "repeat")
            {
                const auto rate = binding["rate"].toString();
                rateCombo.setSelectedItemIndex (indexOfChoice (rates, rate.isEmpty() ? juce::String ("1/16") : rate), juce::dontSendNotification);
                accentSlider.setValue (juce::jmax (0.0, numberOr (binding["accentEvery"], 0.0)), juce::dontSendNotification);
                softSlider.setValue (numberOr (binding["softVelocity"], 0.8), juce::dontSendNotification);
                rows[5].shown = rows[6].shown = rows[7].shown = true;
            }
            else if (stroke == "double" || stroke == "flam" || stroke == "drag")
            {
                gapSlider.setValue (numberOr (binding["gapMs"], stroke == "double" ? 85.0 : 28.0), juce::dontSendNotification);
                rows[8].shown = true;
            }
            else if (stroke == "pattern")
            {
                stepsEditor.setText (binding["steps"].toString(), false);
                divisionCombo.setSelectedId (juce::roundToInt (numberOr (binding["division"], 4.0)), juce::dontSendNotification);
                rows[9].shown = rows[10].shown = true;
            }
        }
        else if (action == "choke")
        {
            juce::StringArray names;

            for (const auto& pad : pads)
                names.add (padName (pad));

            rows[2].caption.setText (ko ("멈출 패드"), juce::dontSendNotification);
            padsButton.setButtonText (names.isEmpty() ? ko ("없음 (고르기…)") : names.joinIntoString (", "));
            rows[2].shown = true;
        }
        else if (action == "pedal")
        {
            // 콤보의 번호: 1은 (없음), 패드는 순서 + 2
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
    valuesDirty = false;
    session.saveKeymap();
    rebuild();
}

//==============================================================================
void KeysPage::addBinding()
{
    if (selectedKey == hvd::invalidKey || padIds.isEmpty())
        return;

    auto* binding = new juce::DynamicObject();
    binding->setProperty ("key", fromUtf8 (hvd::keyName (selectedKey)));
    binding->setProperty ("pad", comboPadId (quickPadCombo).isNotEmpty() ? comboPadId (quickPadCombo) : padIds[0]);
    binding->setProperty ("velocity", 1.0);
    session.getKeymapDocument().addBinding (juce::var (binding));

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
    juce::String text;

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
            text = ko ("페달의 스위치를 왼쪽부터 하나씩 밟고 \"등록 마치기\"를 누르세요 (Esc 취소)");

            for (std::size_t i = 0; i < learned.keys.size(); ++i)
                text << "   " << juce::String (static_cast<int> (i) + 1) << ": " << fromUtf8 (hvd::keyLabel (learned.keys[i]));
        }
    }

    if (! learning)
    {
        if (const auto& pedal = session.getPedal(); pedal.device.isNotEmpty())
        {
            text = ko ("페달: 스위치 ") + juce::String (static_cast<int> (pedal.keys.size())) + ko ("개")
                     + "  ·  " + (pedalConnected ? ko ("연결됨") : ko ("연결 안 됨"));

            if (pedalConnected)
                text << "  ·  " << ko ("보고 간격 ") << intervalText (session.getReportInterval (true));
        }
        else
        {
            text = ko ("풋스위치를 등록하면 키보드와 따로 정할 수 있습니다");
        }

        // 견주어 볼 수 있게 키보드의 것도 보여 준다.
        if (const auto ofKeyboard = session.getReportInterval (false); ofKeyboard.enough)
            text << "  ·  " << ko ("키보드 ") << intervalText (ofKeyboard);
    }

    const auto registered = session.getPedal().device.isNotEmpty();

    pedalButton.setButtonText (learning ? ko ("등록 마치기") : (registered ? ko ("페달…") : ko ("페달 등록")));
    pedalButton.setColour (juce::TextButton::buttonColourId,
                           learning ? juce::Colour (0xff7a5a1e) : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
    pedalLabel.setText (text, juce::dontSendNotification);
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

        auto* binding = new juce::DynamicObject();
        binding->setProperty ("key", fromUtf8 (hvd::keyName (key)));
        binding->setProperty ("pad", pad);
        binding->setProperty ("velocity", 1.0);
        session.getKeymapDocument().addBinding (juce::var (binding));

        selectedKey = key;
        keyboard.setSelected (key);
        selectedSlot = key < bindingsByKey.size() ? static_cast<int> (bindingsByKey[key].size()) : 0;
        commit();
    });
}

void KeysPage::showPadsMenu()
{
    auto* binding = currentBinding();

    if (binding == nullptr)
        return;

    const auto pads = padsOf (juce::var (binding));
    const auto hit = binding->getProperty ("action").toString().isEmpty() || binding->getProperty ("action").toString() == "hit";

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
                            auto* target = currentBinding();

                            if (result <= 0 || target == nullptr)
                                return;

                            auto chosen = padsOf (juce::var (target));
                            const auto id = padIds[result - 1];

                            if (chosen.contains (id))
                                chosen.removeString (id);
                            else
                                chosen.add (id);

                            setPads (*target, chosen);
                            commit();
                        });
}

void KeysPage::actionChanged()
{
    auto* binding = currentBinding();

    if (binding == nullptr)
        return;

    const juce::String action (actions[juce::jmax (0, actionCombo.getSelectedItemIndex())].value);
    const juce::var asVar (binding);

    if (action == "hit")
        binding->removeProperty ("action");
    else
        binding->setProperty ("action", action);

    // 새 동작에서 쓰지 않는 항목은 지운다.
    if (action != "hit")
        for (const auto* name : { "velocity", "stroke", "rate", "intervalMs", "gap", "gapMs", "secondVelocity", "graceVelocity",
                                  "softVelocity", "ramp", "swellMs", "swellStart", "jitter", "velocityJitter", "accentEvery", "followDivisor",
                                  "steps", "division" })
            binding->removeProperty (name);

    if (action != "pedal")
        for (const auto* name : { "swap", "releasedSwap", "releasePad", "releaseVelocity", "splash", "splashMs" })
            binding->removeProperty (name);

    if (action != "hit" && action != "choke")
        setPads (*binding, juce::StringArray());

    if (action == "hit")
    {
        if (padsOf (asVar).isEmpty() && ! padIds.isEmpty())
            setPads (*binding, juce::StringArray (padIds[0]));

        if (! binding->hasProperty ("velocity"))
            binding->setProperty ("velocity", 1.0);
    }
    else if (action == "choke" && padsOf (asVar).size() <= 1)
    {
        // 처음에는 심벌을 모두 고른다. 손으로 심벌을 잡아 멈추는 동작이기 때문이다.
        juce::StringArray cymbals;

        for (const auto& id : padIds)
            if (ui::isCymbalPad (id) && ! id.startsWith ("hihat"))
                cymbals.add (id);

        if (! cymbals.isEmpty())
            setPads (*binding, cymbals);
    }
    else if (action == "pedal" && ! binding->hasProperty ("swap"))
    {
        // 처음에는 하이햇 페달로 둔다: 누르는 동안 닫힌 하이햇을 열린 하이햇으로 바꾼다.
        auto* swap = new juce::DynamicObject();

        if (padIds.contains ("hihat_closed") && padIds.contains ("hihat_open"))
            swap->setProperty ("hihat_closed", "hihat_open");
        else if (padIds.size() >= 2)
            swap->setProperty (padIds[0], padIds[1]);

        binding->setProperty ("swap", juce::var (swap));

        if (padIds.contains ("hihat_pedal"))
            binding->setProperty ("releasePad", "hihat_pedal");
    }

    commit();
}

void KeysPage::strokeChanged()
{
    auto* binding = currentBinding();

    if (binding == nullptr)
        return;

    const juce::String stroke (strokes[juce::jmax (0, strokeCombo.getSelectedItemIndex())].value);

    if (stroke == "single")
        binding->removeProperty ("stroke");
    else
        binding->setProperty ("stroke", stroke);

    // 방식마다 쓰는 항목이 다르므로 다른 방식의 값은 지운다.
    if (stroke != "repeat")
        for (const auto* name : { "rate", "intervalMs", "accentEvery", "softVelocity", "ramp", "followDivisor" })
            binding->removeProperty (name);

    if (stroke != "double" && stroke != "flam" && stroke != "drag")
        for (const auto* name : { "gap", "gapMs", "secondVelocity", "graceVelocity" })
            binding->removeProperty (name);

    if (stroke != "pattern")
        for (const auto* name : { "steps", "division" })
            binding->removeProperty (name);

    if (stroke == "repeat" && ! binding->hasProperty ("rate"))
        binding->setProperty ("rate", "1/16");

    if (stroke == "pattern" && ! binding->hasProperty ("steps"))
    {
        binding->setProperty ("steps", "X.x.");
        binding->setProperty ("division", 4);
    }

    commit();
}

//==============================================================================
void KeysPage::tick()
{
    keyboard.refreshHeld();

    if (++tickCount % 6 == 0)
    {
        heldLabel.setText (ko ("동시에 눌린 키 ") + juce::String (session.getHeldKeyCount())
                               + "  ·  " + ko ("최대 ") + juce::String (session.getMaxHeldKeyCount()),
                           juce::dontSendNotification);

        const auto armed = session.isKeyCaptureArmed();
        quickButton.setButtonText (armed ? ko ("키를 누르세요… (Esc 취소)") : ko ("이 패드를 칠 키 누르기"));
        quickButton.setColour (juce::TextButton::buttonColourId,
                               armed ? juce::Colour (0xff7a5a1e) : getLookAndFeel().findColour (juce::TextButton::buttonColourId));

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

    // 슬라이더로 바꾼 값은 마우스를 놓은 뒤에 저장한다.
    if (valuesDirty && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        commit();
}
