#include "MixerPage.h"

namespace
{

constexpr int stripWidth = 86;
constexpr int masterWidth = 320;

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
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff161b20));
    g.fillRoundedRectangle (bounds, 2.0f);

    if (level <= 0.0f)
        return;

    // -48dB ~ 0dB를 막대 길이로 바꾼다.
    const auto decibels = juce::Decibels::gainToDecibels (level, -48.0f);
    const auto fraction = juce::jlimit (0.0f, 1.0f, (decibels + 48.0f) / 48.0f);
    const auto vertical = getHeight() >= getWidth();

    g.setColour (level > 0.95f ? ui::danger : (level > 0.6f ? ui::accent : ui::good));

    if (vertical)
        g.fillRoundedRectangle (bounds.withTop (bounds.getBottom() - bounds.getHeight() * fraction), 2.0f);
    else
        g.fillRoundedRectangle (bounds.withWidth (bounds.getWidth() * fraction), 2.0f);
}

//==============================================================================
/** 패드 하나의 조절 줄. */
class MixerPage::Strip final : public juce::Component
{
public:
    Strip (MixerPage& ownerToUse, int padToUse, const juce::String& idToUse, const juce::String& name)
        : owner (ownerToUse), pad (padToUse), id (idToUse)
    {
        ui::setupButton (*this, nameButton, name, [this] { owner.session.previewPad (pad, 0.9f); });
        nameButton.setColour (juce::TextButton::buttonColourId, ui::padColour (id).withMultipliedBrightness (0.45f));
        nameButton.setTooltip (name);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, stripWidth - 10, 18);
        // 패드 음량은 0 ~ 10배. 1.0이 가운데에 오게 해서 자주 쓰는 0 ~ 2를 세밀하게 움직인다.
        fader.setRange (0.0, static_cast<double> (hvd::maxPadVolume), 0.01);
        fader.setSkewFactorFromMidPoint (1.0);
        fader.setDoubleClickReturnValue (true, 1.0);
        fader.setScrollWheelEnabled (false);
        ui::noFocus (fader);
        addAndMakeVisible (fader);
        addAndMakeVisible (meter);

        ui::setupKnob (*this, pan, -1.0, 1.0, 0.01, 0.0);
        ui::setupKnob (*this, send, 0.0, 1.0, 0.01, 1.0);
        ui::setupLabel (*this, panCaption, ko ("팬"), 11.0f, ui::quietText);
        ui::setupLabel (*this, sendCaption, ko ("리버브"), 11.0f, ui::quietText);
        panCaption.setJustificationType (juce::Justification::centred);
        sendCaption.setJustificationType (juce::Justification::centred);

        ui::setupButton (*this, muteButton, "M", [this] { owner.session.setPadMuted (pad, muteButton.getToggleState()); });
        ui::setupButton (*this, soloButton, "S", [this] { owner.session.setPadSolo (pad, soloButton.getToggleState()); });
        muteButton.setClickingTogglesState (true);
        soloButton.setClickingTogglesState (true);
        muteButton.setColour (juce::TextButton::buttonOnColourId, ui::danger.darker (0.3f));
        soloButton.setColour (juce::TextButton::buttonOnColourId, ui::accent.darker (0.4f));
        muteButton.setTooltip (ko ("뮤트: 이 패드의 소리를 끈다"));
        soloButton.setTooltip (ko ("솔로: 이 패드만 듣는다"));

        const auto bind = [this] (juce::Slider& slider, const char* field)
        {
            slider.onValueChange = [this, &slider, field]
            {
                if (owner.updating)
                    return;

                owner.session.setPadValue (pad, field, slider.getValue());
                owner.dirty = true;
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
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (3, 0);
        nameButton.setBounds (area.removeFromTop (30));
        area.removeFromTop (4);

        auto buttons = area.removeFromBottom (24);
        muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
        soloButton.setBounds (buttons.reduced (1, 0));
        area.removeFromBottom (4);

        auto knobs = area.removeFromBottom (64);
        auto left = knobs.removeFromLeft (knobs.getWidth() / 2);
        panCaption.setBounds (left.removeFromBottom (14));
        pan.setBounds (left);
        sendCaption.setBounds (knobs.removeFromBottom (14));
        send.setBounds (knobs);
        area.removeFromBottom (4);

        meter.setBounds (area.removeFromRight (8).withTrimmedBottom (20));
        fader.setBounds (area);
    }

    LevelMeter meter;

private:
    MixerPage& owner;
    int pad;
    juce::String id;
    juce::TextButton nameButton, muteButton, soloButton;
    juce::Slider fader, pan, send;
    juce::Label panCaption, sendCaption;
};

//==============================================================================
MixerPage::MixerPage (Session& sessionToUse)
    : session (sessionToUse)
{
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    ui::setupLabel (*this, masterTitle, ko ("전체"), 17.0f, juce::Colours::white, true);
    ui::setupLabel (*this, eqTitle, ko ("EQ (드럼 전체의 음색)"), 14.0f, juce::Colours::white, true);
    ui::setupLabel (*this, compTitle, ko ("컴프레서 (큰 소리를 눌러 단단하게)"), 14.0f, juce::Colours::white, true);
    ui::setupLabel (*this, reverbTitle, ko ("리버브 (공간의 울림)"), 14.0f, juce::Colours::white, true);
    ui::setupLabel (*this, reductionLabel, {}, 13.0f, ui::quietText);

    const char* const captionTexts[] = { "전체 음량", "저음", "중음", "고음", "세기", "크기", "공간 크기", "고음 줄이기" };

    for (std::size_t i = 0; i < captions.size(); ++i)
        ui::setupLabel (*this, captions[i], ko (captionTexts[i]), 13.0f, ui::quietText);

    ui::setupSlider (*this, masterSlider, 0.0, AudioHost::maxMasterGain, 0.01, AudioHost::defaultMasterGain);
    ui::setupSlider (*this, eqLow, -12.0, 12.0, 0.1, 0.0, " dB", 1);
    ui::setupSlider (*this, eqMid, -12.0, 12.0, 0.1, 0.0, " dB", 1);
    ui::setupSlider (*this, eqHigh, -12.0, 12.0, 0.1, 0.0, " dB", 1);
    ui::setupSlider (*this, comp, 0.0, 1.0, 0.01, 0.0);
    ui::setupSlider (*this, reverbLevel, 0.0, 1.0, 0.01, 0.0);
    ui::setupSlider (*this, reverbSize, 0.0, 1.0, 0.01, 0.5);
    ui::setupSlider (*this, reverbDamp, 0.0, 1.0, 0.01, 0.5);
    addAndMakeVisible (masterMeter);

    // 전체 음량은 윗줄의 슬라이더와 같은 값이다. 소리에는 바로, 설정 파일에는 마우스를 놓은 뒤에 적는다.
    masterSlider.onValueChange = [this]
    {
        if (updating)
            return;

        session.setMasterGain (static_cast<float> (masterSlider.getValue()), false);
        masterDirty = true;
    };

    for (auto* slider : { &eqLow, &eqMid, &eqHigh, &comp, &reverbLevel, &reverbSize, &reverbDamp })
        slider->onValueChange = [this] { fxChanged(); };

    ui::setupButton (*this, resetFxButton, ko ("이펙트 모두 끄기"), [this]
    {
        session.setFx ({});
        dirty = true;
        showFx();
    });

    session.addChangeListener (this);
    rebuild();
}

MixerPage::~MixerPage()
{
    session.removeChangeListener (this);
}

void MixerPage::paint (juce::Graphics& g)
{
    g.setColour (ui::panel);
    g.fillRoundedRectangle (getLocalBounds().removeFromRight (masterWidth).toFloat(), 8.0f);
}

void MixerPage::resized()
{
    auto area = getLocalBounds();
    auto master = area.removeFromRight (masterWidth).reduced (16, 12);
    area.removeFromRight (12);

    viewport.setBounds (area);
    holder.setSize (juce::jmax (area.getWidth(), static_cast<int> (strips.size()) * stripWidth), area.getHeight() - 14);

    for (std::size_t i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (static_cast<int> (i) * stripWidth, 0, stripWidth, holder.getHeight());

    const auto row = [&master] (juce::Label& caption, juce::Slider& slider)
    {
        auto line = master.removeFromTop (28);
        caption.setBounds (line.removeFromLeft (84));
        slider.setBounds (line);
        master.removeFromTop (2);
    };

    masterTitle.setBounds (master.removeFromTop (26));
    row (captions[0], masterSlider);
    masterMeter.setBounds (master.removeFromTop (8).withTrimmedLeft (84).withTrimmedRight (70));
    master.removeFromTop (14);

    eqTitle.setBounds (master.removeFromTop (24));
    row (captions[1], eqLow);
    row (captions[2], eqMid);
    row (captions[3], eqHigh);
    master.removeFromTop (10);

    compTitle.setBounds (master.removeFromTop (24));
    row (captions[4], comp);
    reductionLabel.setBounds (master.removeFromTop (20).withTrimmedLeft (84));
    master.removeFromTop (10);

    reverbTitle.setBounds (master.removeFromTop (24));
    row (captions[5], reverbLevel);
    row (captions[6], reverbSize);
    row (captions[7], reverbDamp);
    master.removeFromTop (12);

    resetFxButton.setBounds (master.removeFromTop (28).removeFromLeft (150));
}

void MixerPage::rebuild()
{
    const auto* kit = session.getKit();
    juce::StringArray ids;

    if (kit != nullptr)
        for (const auto& pad : kit->pads)
            ids.add (juce::String (pad.id) + "|" + fromUtf8 (pad.name));

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
                auto strip = std::make_unique<Strip> (*this, i, juce::String (pad.id), fromUtf8 (pad.name));
                holder.addAndMakeVisible (*strip);
                strips.push_back (std::move (strip));
            }
        }

        resized();
    }
    else
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);

        for (auto& strip : strips)
            strip->refresh();
    }

    showFx();
}

void MixerPage::showFx()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto fx = session.getFx();

    const auto set = [] (juce::Slider& slider, double value)
    {
        if (! slider.isMouseButtonDown())
            slider.setValue (value, juce::dontSendNotification);
    };

    set (masterSlider, session.getMasterGain());
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
    if (updating)
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
    dirty = true;
}

void MixerPage::tick()
{
    for (std::size_t i = 0; i < strips.size(); ++i)
        strips[i]->meter.push (session.getEngine().takePadPeak (static_cast<int> (i)));

    masterMeter.push (session.getOutputPeak());

    if (++tickCount % 6 == 0)
    {
        const auto reduction = session.getEngine().getGainReductionDb();
        reductionLabel.setText (comp.getValue() > 0.0 ? ko ("지금 줄이는 양: ") + juce::String (reduction, 1) + " dB" : juce::String(),
                                juce::dontSendNotification);

        // 윗줄의 전체 음량 슬라이더로 바꾼 값을 따라간다.
        if (! masterSlider.isMouseButtonDown() && std::abs (masterSlider.getValue() - session.getMasterGain()) > 0.001)
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            masterSlider.setValue (session.getMasterGain(), juce::dontSendNotification);
        }
    }

    // 고친 값은 마우스를 놓은 뒤에 킷 파일에 저장한다.
    if (dirty && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        dirty = false;
        session.saveKit (false);
    }

    if (masterDirty && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        masterDirty = false;
        session.rememberMasterGain();
    }
}
