#include "MixerPage.h"

#include <algorithm>

#include "core/PadKinds.h"

namespace
{

constexpr int stripWidth = 64;
constexpr int groupGap = 12;             // 악기 무리 사이
constexpr int groupCaptionHeight = 20;   // 무리 이름 줄
constexpr int masterWidth = 320;
constexpr int captionWidth = 76;         // 오른쪽 칸의 항목 이름 폭

/** 패드 줄을 묶는 악기 무리. 색과 같은 규칙(hvd::instrumentOfPad)으로 고른다. */
enum Group
{
    kickGroup,
    snareGroup,
    hihatGroup,
    tomGroup,
    cymbalGroup,
    otherGroup
};

const char* const groupNames[] = { "킥", "스네어", "하이햇", "탐", "심벌", "기타" };

int groupOf (const juce::String& padId)
{
    switch (const auto instrument = hvd::instrumentOfPad (padId.toStdString()))
    {
        case hvd::Instrument::kick:     return kickGroup;
        case hvd::Instrument::snare:    return snareGroup;
        case hvd::Instrument::hihat:    return hihatGroup;
        case hvd::Instrument::tom:      return tomGroup;
        default:                        return hvd::isCymbal (instrument) ? cymbalGroup : otherGroup;
    }
}

/** 패드 이름 버튼: 누르면 그 패드를 들어 본다. 오른쪽 위의 작은 ▶가 그것을 알린다. */
class PreviewButton final : public juce::TextButton
{
public:
    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        juce::TextButton::paintButton (g, highlighted, down);

        const auto fill = findColour (juce::TextButton::buttonColourId);
        const auto tip = juce::Rectangle<float> (4.0f, 6.0f).withPosition (static_cast<float> (getWidth()) - 9.0f, 5.0f);
        juce::Path triangle;
        triangle.addTriangle (tip.getTopLeft(), tip.getBottomLeft(), { tip.getRight(), tip.getCentreY() });
        g.setColour (ui::theme().textOn (fill).withAlpha (highlighted ? 0.9f : 0.5f));
        g.fillPath (triangle);
    }
};

} // namespace

//==============================================================================
void LevelMeter::push (float peak)
{
    const auto next = peak > level ? peak : level * 0.88f;

    if (std::abs (next - level) > 1.0e-4f)
    {
        level = next < 1.0e-4f ? 0.0f : next;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto& t = ui::theme();
    const auto bounds = getLocalBounds().toFloat();
    const auto vertical = getHeight() >= getWidth();
    const auto radius = (vertical ? bounds.getWidth() : bounds.getHeight()) * 0.5f;

    // 소리가 없을 때는 슬라이더의 홈보다 옅게 두어 둘째 조절 막대로 보이지 않게 한다.
    g.setColour (t.track.withMultipliedAlpha (0.5f));
    g.fillRoundedRectangle (bounds, radius);

    if (level <= 0.0f)
        return;

    // -48dB ~ 0dB를 막대 길이로 바꾼다.
    const auto decibels = juce::Decibels::gainToDecibels (level, -48.0f);
    const auto fraction = juce::jlimit (0.0f, 1.0f, (decibels + 48.0f) / 48.0f);

    g.setColour (level > 0.95f ? t.danger : (level > 0.6f ? t.warning : t.good));

    if (vertical)
        g.fillRoundedRectangle (bounds.withTop (bounds.getBottom() - bounds.getHeight() * fraction), radius);
    else
        g.fillRoundedRectangle (bounds.withWidth (bounds.getWidth() * fraction), radius);
}

//==============================================================================
/** 패드 줄들을 담는 판. 악기 무리마다 위에 이름을 적는다. */
class MixerPage::StripHolder final : public juce::Component
{
public:
    struct Span
    {
        int x = 0, width = 0;
        juce::String name;
    };

    std::vector<Span> spans;

    void paint (juce::Graphics& g) override
    {
        const auto& t = ui::theme();
        g.setFont (juce::FontOptions (ui::type::caption));

        for (const auto& span : spans)
        {
            const auto area = juce::Rectangle<int> (span.x, 0, span.width, groupCaptionHeight);
            g.setColour (t.textMuted);
            g.drawText (span.name, area.reduced (ui::space::tight, 0).withTrimmedBottom (6), juce::Justification::centredLeft, true);
            g.setColour (t.border);
            g.fillRect (area.reduced (ui::space::tight, 0).removeFromBottom (1).translated (0, -2));
        }
    }
};

//==============================================================================
/** 패드 하나의 조절 줄. */
class MixerPage::Strip final : public juce::Component
{
public:
    Strip (MixerPage& ownerToUse, int padToUse, const juce::String& idToUse, const juce::String& name)
        : pad (padToUse), group (groupOf (idToUse)), owner (ownerToUse), id (idToUse)
    {
        ui::setupButton (*this, nameButton, twoLines (name), [this] { owner.session.previewPad (pad, 0.9f); });
        nameButton.setColour (juce::TextButton::buttonColourId, ui::theme().padTint (ui::padColour (id)));
        nameButton.setTooltip (name + ko ("  ·  들어 보기"));

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, stripWidth - 8, 18);
        // 패드 음량은 0 ~ 10배. 1.0이 가운데에 오게 해서 자주 쓰는 0 ~ 2를 세밀하게 움직인다.
        fader.setRange (0.0, static_cast<double> (hvd::maxPadVolume), 0.01);
        fader.setSkewFactorFromMidPoint (1.0);
        fader.setDoubleClickReturnValue (true, 1.0);
        fader.setScrollWheelEnabled (false);
        fader.setTooltip (ko ("음량"));
        ui::noFocus (fader);
        addAndMakeVisible (fader);
        addAndMakeVisible (meter);
        meter.setInterceptsMouseClicks (false, false);

        ui::setupKnob (*this, pan, -1.0, 1.0, 0.01, 0.0);
        ui::setupKnob (*this, send, 0.0, 1.0, 0.01, 1.0);
        pan.setPopupDisplayEnabled (false, false, nullptr);     // 값은 손잡이 아래에 적는다
        send.setPopupDisplayEnabled (false, false, nullptr);
        pan.setTooltip (ko ("팬"));
        send.setTooltip (ko ("리버브로 보내는 양"));
        pan.addMouseListener (this, false);
        send.addMouseListener (this, false);

        ui::setupLabel (*this, panCaption, {}, ui::Text::caption);
        ui::setupLabel (*this, sendCaption, {}, ui::Text::caption);
        panCaption.setJustificationType (juce::Justification::centred);
        sendCaption.setJustificationType (juce::Justification::centred);
        panCaption.setInterceptsMouseClicks (false, false);
        sendCaption.setInterceptsMouseClicks (false, false);

        ui::setupButton (*this, muteButton, "M", [this] { owner.session.setPadMuted (pad, muteButton.getToggleState()); });
        ui::setupButton (*this, soloButton, "S", [this] { owner.session.setPadSolo (pad, soloButton.getToggleState()); });
        muteButton.setClickingTogglesState (true);
        soloButton.setClickingTogglesState (true);
        muteButton.setColour (juce::TextButton::buttonOnColourId, ui::theme().danger);    // 켜지면 빨간 바탕
        muteButton.setTooltip (ko ("뮤트: 이 패드의 소리를 끈다"));
        soloButton.setTooltip (ko ("솔로: 이 패드만 듣는다"));

        const auto bind = [this] (juce::Slider& slider, const char* field)
        {
            slider.onValueChange = [this, &slider, field]
            {
                if (owner.updating)
                    return;

                owner.session.setPadValue (pad, field, slider.getValue());
                owner.session.markKitEdited();
                updateCaptions();
            };
        };

        bind (fader, "volume");
        bind (pan, "pan");
        bind (send, "reverbSend");
        refresh();
    }

    void refresh()
    {
        auto& source = owner.session;

        // 끌고 있는 손잡이는 건드리지 않는다.
        if (! fader.isMouseButtonDown())
            fader.setValue (source.getPadValue (pad, "volume"), juce::dontSendNotification);

        if (! pan.isMouseButtonDown())
            pan.setValue (source.getPadValue (pad, "pan"), juce::dontSendNotification);

        if (! send.isMouseButtonDown())
            send.setValue (source.getPadValue (pad, "reverbSend"), juce::dontSendNotification);

        muteButton.setToggleState (source.isPadMuted (pad), juce::dontSendNotification);
        soloButton.setToggleState (source.isPadSolo (pad), juce::dontSendNotification);
        updateCaptions();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (2, 0);
        nameButton.setBounds (area.removeFromTop (40));
        area.removeFromTop (ui::space::small);

        auto buttons = area.removeFromBottom (24);
        muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
        soloButton.setBounds (buttons.reduced (1, 0));
        area.removeFromBottom (ui::space::small);

        auto knobs = area.removeFromBottom (48);
        auto left = knobs.removeFromLeft (knobs.getWidth() / 2);
        panCaption.setBounds (left.removeFromBottom (16).expanded (2, 0));
        pan.setBounds (left);
        sendCaption.setBounds (knobs.removeFromBottom (16).expanded (2, 0));
        send.setBounds (knobs);
        area.removeFromBottom (ui::space::tight);

        fader.setBounds (area);

        // 미터: 페이더 홈 바로 오른쪽의 가는 막대 (값 상자와 손잡이 반지름만큼은 비운다)
        const auto thumb = 7;
        const auto track = area.withTrimmedBottom (18 + thumb).withTrimmedTop (thumb);
        meter.setBounds (track.getCentreX() + 4, track.getY(), 3, track.getHeight());
    }

    /** 한 줄에 들어가지 않는 이름은 띄어쓰기에서 두 줄로 나눈다 (글자 가운데서 끊기지 않게). */
    static juce::String twoLines (const juce::String& name)
    {
        const auto width = juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (ui::type::body)), name);

        if (width <= static_cast<float> (stripWidth - 20) || ! name.containsChar (' '))
            return name;

        // 가운데에 가장 가까운 띄어쓰기
        auto best = -1;

        for (int i = 0; i < name.length(); ++i)
            if (name[i] == ' ' && (best < 0 || std::abs (i - name.length() / 2) < std::abs (best - name.length() / 2)))
                best = i;

        return name.substring (0, best) + "\n" + name.substring (best + 1);
    }

    void mouseEnter (const juce::MouseEvent& event) override    { hovered = event.eventComponent; updateCaptions(); }
    void mouseExit (const juce::MouseEvent&) override            { hovered = nullptr; updateCaptions(); }

    LevelMeter meter;
    const int pad;
    const int group;

private:
    /** 손잡이 아래에는 값을 적고, 마우스를 올린 동안에만 무엇인지 적는다. */
    void updateCaptions()
    {
        panCaption.setText (hovered == &pan ? ko ("팬") : ui::formatPan (pan.getValue()), juce::dontSendNotification);
        sendCaption.setText (hovered == &send ? ko ("리버브") : juce::String (send.getValue(), 2), juce::dontSendNotification);
    }

    MixerPage& owner;
    juce::String id;
    PreviewButton nameButton;
    juce::TextButton muteButton, soloButton;
    juce::Slider fader, pan, send;
    juce::Label panCaption, sendCaption;
    juce::Component* hovered = nullptr;
};

//==============================================================================
MixerPage::MixerPage (Session& sessionToUse)
    : session (sessionToUse),
      holder (std::make_unique<StripHolder>())
{
    viewport.setViewedComponent (holder.get(), false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    ui::setupLabel (*this, masterCaption, ko ("전체 음량"), ui::Text::caption);
    addAndMakeVisible (masterSlider);
    addAndMakeVisible (masterMeter);

    ui::setupLabel (*this, kitTitle, {}, ui::Text::section);
    kitTitle.setTooltip (ko ("패드 줄과 이펙트 값은 이 킷에 저장된다"));
    ui::setupLabel (*this, eqTitle, "EQ", ui::Text::heading);
    ui::setupLabel (*this, compTitle, ko ("컴프레서"), ui::Text::heading);
    ui::setupLabel (*this, reverbTitle, ko ("리버브"), ui::Text::heading);
    ui::setupLabel (*this, reductionLabel, {}, ui::Text::caption);
    eqTitle.setTooltip (ko ("드럼 전체의 음색"));
    compTitle.setTooltip (ko ("큰 소리를 눌러 단단하게"));
    reverbTitle.setTooltip (ko ("공간의 울림"));

    const char* const captionTexts[] = { "저음", "중음", "고음", "세기", "양", "공간", "고음 감쇠" };

    for (std::size_t i = 0; i < captions.size(); ++i)
        ui::setupLabel (*this, captions[i], ko (captionTexts[i]), ui::Text::caption);

    ui::setupSlider (*this, eqLow, -12.0, 12.0, 0.1, 0.0, "dB", 1);
    ui::setupSlider (*this, eqMid, -12.0, 12.0, 0.1, 0.0, "dB", 1);
    ui::setupSlider (*this, eqHigh, -12.0, 12.0, 0.1, 0.0, "dB", 1);
    ui::setupSlider (*this, comp, 0.0, 1.0, 0.01, 0.0);
    ui::setupSlider (*this, reverbLevel, 0.0, 1.0, 0.01, 0.0);
    ui::setupSlider (*this, reverbSize, 0.0, 1.0, 0.01, 0.5);
    ui::setupSlider (*this, reverbDamp, 0.0, 1.0, 0.01, 0.5);

    for (auto* slider : { &eqLow, &eqMid, &eqHigh, &comp, &reverbLevel, &reverbSize, &reverbDamp })
        slider->onValueChange = [this] { fxChanged(); };

    // 이펙트 켜기/끄기: 값은 그대로 두고 잠깐 꺼서 견줘 듣는다 (파일에는 적지 않는다).
    ui::setupToggle (*this, fxToggle, ko ("이펙트"), [this] { setFxBypassed (! fxToggle.getToggleState()); }, true);
    fxToggle.setTooltip (ko ("끄면 값은 두고 이펙트만 잠깐 뺀다"));

    ui::setupButton (*this, resetFxButton, ko ("초기화"), [this] { resetFx(); });
    ui::setButtonKind (resetFxButton, ui::ButtonKind::danger);
    resetFxButton.setTooltip (ko ("EQ, 컴프레서, 리버브를 처음 값으로"));

    session.addChangeListener (this);
    rebuild();
}

MixerPage::~MixerPage()
{
    session.removeChangeListener (this);

    // 잠깐 꺼 둔 이펙트를 되살린다 (화면이 사라진 뒤에도 꺼진 채로 남지 않게).
    if (fxBypassed)
        session.getEngine().setFxSettings (session.getKitDocument().getFx());
}

void MixerPage::paint (juce::Graphics& g)
{
    // 채널 줄들과 오른쪽 칸
    auto area = getLocalBounds();
    ui::fillPanel (g, area.removeFromRight (masterWidth));
    area.removeFromRight (ui::space::medium);
    ui::fillPanel (g, area);

    g.setColour (ui::theme().border);
    g.fillRect (masterDivider);
}

void MixerPage::resized()
{
    auto area = getLocalBounds();
    auto master = area.removeFromRight (masterWidth).reduced (ui::space::large);
    area.removeFromRight (ui::space::medium);

    viewport.setBounds (area.reduced (ui::space::small, ui::space::medium));
    layoutStrips();

    // 오른쪽 칸: 전체 음량, 그 아래 킷의 이펙트
    ui::placeLabelled (master, masterCaption, masterSlider, captionWidth);
    masterMeter.setBounds (master.removeFromTop (4).withTrimmedLeft (captionWidth).withTrimmedRight (52));
    master.removeFromTop (ui::space::large);
    masterDivider = master.removeFromTop (1);
    master.removeFromTop (ui::space::large);

    auto titleRow = master.removeFromTop (ui::space::control);
    fxToggle.setBounds (titleRow.removeFromRight (84));
    titleRow.removeFromRight (ui::space::small);
    kitTitle.setBounds (titleRow);
    master.removeFromTop (ui::space::small);

    eqTitle.setBounds (master.removeFromTop (24));
    ui::placeLabelled (master, captions[0], eqLow, captionWidth);
    ui::placeLabelled (master, captions[1], eqMid, captionWidth);
    ui::placeLabelled (master, captions[2], eqHigh, captionWidth);
    master.removeFromTop (ui::space::small);

    compTitle.setBounds (master.removeFromTop (24));
    ui::placeLabelled (master, captions[3], comp, captionWidth);
    reductionLabel.setBounds (master.removeFromTop (20).withTrimmedLeft (captionWidth));
    master.removeFromTop (ui::space::tight);

    reverbTitle.setBounds (master.removeFromTop (24));
    ui::placeLabelled (master, captions[4], reverbLevel, captionWidth);
    ui::placeLabelled (master, captions[5], reverbSize, captionWidth);
    ui::placeLabelled (master, captions[6], reverbDamp, captionWidth);
    master.removeFromTop (ui::space::medium);

    resetFxButton.setBounds (master.removeFromTop (ui::space::control).removeFromLeft (96));
}

void MixerPage::layoutStrips()
{
    const auto area = viewport.getLocalBounds();
    const auto height = juce::jmax (200, area.getHeight() - 14);    // 가로 스크롤 막대 자리
    holder->spans.clear();

    auto x = 0;
    auto lastGroup = -1;

    for (auto& strip : strips)
    {
        if (strip->group != lastGroup)
        {
            if (lastGroup >= 0)
                x += groupGap;

            holder->spans.push_back ({ x, 0, ko (groupNames[strip->group]) });
            lastGroup = strip->group;
        }

        strip->setBounds (x, groupCaptionHeight, stripWidth, height - groupCaptionHeight);
        holder->spans.back().width = x + stripWidth - holder->spans.back().x;
        x += stripWidth;
    }

    holder->setSize (juce::jmax (area.getWidth(), x), height);
    holder->repaint();
}

void MixerPage::rebuild()
{
    const auto* kit = session.getKit();
    juce::StringArray ids;

    if (kit != nullptr)
        for (const auto& pad : kit->pads)
            ids.add (juce::String (pad.id) + "|" + fromUtf8 (pad.name));

    kitTitle.setText (kit != nullptr ? ko ("킷  ·  ") + fromUtf8 (kit->name) : ko ("킷 없음"), juce::dontSendNotification);

    // 패드 구성(id와 이름)이 그대로면 값만 다시 읽는다. 끌고 있던 손잡이가 사라지지 않게 하기 위해서다.
    if (ids != stripIds || kit == nullptr)
    {
        strips.clear();
        stripIds = ids;

        if (kit != nullptr)
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);

            for (int i = 0; i < static_cast<int> (kit->pads.size()); ++i)
            {
                const auto& pad = kit->pads[static_cast<std::size_t> (i)];
                strips.push_back (std::make_unique<Strip> (*this, i, juce::String (pad.id), fromUtf8 (pad.name)));
            }

            // 악기별로 묶는다. 무리 안에서는 킷의 순서를 따른다.
            std::stable_sort (strips.begin(), strips.end(), [] (const auto& a, const auto& b) { return a->group < b->group; });

            for (auto& strip : strips)
                holder->addAndMakeVisible (*strip);
        }

        layoutStrips();
    }
    else
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);

        for (auto& strip : strips)
            strip->refresh();
    }

    // 이펙트를 꺼 둔 동안 킷을 다시 읽었으면(엔진이 킷의 값을 받았다) 다시 끈다.
    if (fxBypassed && ! (session.getFx() == hvd::FxSettings {}))
        session.getEngine().setFxSettings ({});

    showFx();
}

void MixerPage::showFx()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    // 이펙트를 꺼 둔 동안에도 슬라이더는 킷 파일에 적힌 값을 보인다.
    const auto fx = fxBypassed ? session.getKitDocument().getFx() : session.getFx();

    const auto set = [] (juce::Slider& slider, double value)
    {
        if (! slider.isMouseButtonDown())
            slider.setValue (value, juce::dontSendNotification);
    };

    set (eqLow, fx.eqLowDb);
    set (eqMid, fx.eqMidDb);
    set (eqHigh, fx.eqHighDb);
    set (comp, fx.compAmount);
    set (reverbLevel, fx.reverbLevel);
    set (reverbSize, fx.reverbSize);
    set (reverbDamp, fx.reverbDamp);
}

void MixerPage::fxChanged()
{
    if (updating || fxBypassed)
        return;

    hvd::FxSettings fx;
    fx.eqLowDb = static_cast<float> (eqLow.getValue());
    fx.eqMidDb = static_cast<float> (eqMid.getValue());
    fx.eqHighDb = static_cast<float> (eqHigh.getValue());
    fx.compAmount = static_cast<float> (comp.getValue());
    fx.reverbLevel = static_cast<float> (reverbLevel.getValue());
    fx.reverbSize = static_cast<float> (reverbSize.getValue());
    fx.reverbDamp = static_cast<float> (reverbDamp.getValue());

    session.setFx (fx);
    session.markKitEdited();
}

void MixerPage::setFxBypassed (bool shouldBypass)
{
    fxBypassed = shouldBypass;
    fxToggle.setToggleState (! fxBypassed, juce::dontSendNotification);
    session.getEngine().setFxSettings (fxBypassed ? hvd::FxSettings {} : session.getKitDocument().getFx());

    // 꺼 둔 동안에는 값을 고칠 수 없게 흐리게 둔다.
    for (auto* component : std::initializer_list<juce::Component*> { &eqTitle, &compTitle, &reverbTitle, &eqLow, &eqMid, &eqHigh, &comp,
                                                                      &reverbLevel, &reverbSize, &reverbDamp, &reductionLabel })
        component->setEnabled (! fxBypassed);

    for (auto& caption : captions)
        caption.setEnabled (! fxBypassed);

    showFx();
}

void MixerPage::resetFx()
{
    ui::confirm (this, ko ("이펙트 초기화"), ko ("EQ, 컴프레서, 리버브를 처음 값으로 되돌립니다."), ko ("초기화"), [this]
    {
        session.setFx ({});
        session.markKitEdited();

        if (fxBypassed)
            setFxBypassed (false);

        showFx();
    }, true);
}

void MixerPage::tick()
{
    for (auto& strip : strips)
        strip->meter.push (session.getEngine().takePadPeak (strip->pad));

    masterMeter.push (session.getOutputPeak());

    if (++tickCount % 6 == 0)
    {
        const auto reduction = session.getEngine().getGainReductionDb();
        reductionLabel.setText (comp.getValue() > 0.0 && ! fxBypassed ? ko ("줄이는 양 ") + ui::formatDb (reduction) : juce::String(),
                                juce::dontSendNotification);
    }
}
