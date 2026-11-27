#pragma once

#include <functional>
#include <vector>

#include "Session.h"
#include "Ui.h"

/** 화면에 그린 키보드. 키마다 지정된 동작을 보여 주고, 눌려 있는 키와 고른 키를 표시한다. */
class KeyboardView final : public juce::Component
{
public:
    struct Cap
    {
        juce::String text;       // 지정된 동작. 비어 있으면 지정되지 않은 키
        juce::Colour colour;
        int count = 0;           // 이 키에 지정된 동작 수
    };

    KeyboardView();

    void setCaps (std::vector<Cap> newCaps);
    void setSelected (hvd::KeyCode key);
    hvd::KeyCode getSelected() const noexcept    { return selected; }

    /** 그림에 있는 키인지. */
    static bool hasKey (hvd::KeyCode key);

    /** 눌린 키가 바뀌었으면 다시 그린다. */
    void refreshHeld();

    std::function<void (hvd::KeyCode)> onKeyClicked;
    std::function<bool (hvd::KeyCode)> isKeyHeld;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Shape
    {
        hvd::KeyCode code;
        float x, y, w, h;
    };

    static const std::vector<Shape>& shapes();
    juce::Rectangle<float> boundsOf (const Shape& shape) const;

    std::vector<Cap> caps;
    std::vector<bool> heldShown;
    hvd::KeyCode selected = hvd::invalidKey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardView)
};

//==============================================================================
/** 페달이 바꿀 패드 한 쌍: [원래 패드] → [바꿔 칠 패드] */
class PadSwapPicker final : public juce::Component
{
public:
    juce::ComboBox from, to;
    juce::Label arrow;

    void resized() override
    {
        auto area = getLocalBounds();
        const auto half = (area.getWidth() - 24) / 2;
        from.setBounds (area.removeFromLeft (half));
        to.setBounds (area.removeFromRight (half));
        arrow.setBounds (area);
    }
};

//==============================================================================
/** 키 배열 편집 화면. 키를 고르고 그 키가 할 일을 정한다. 고치면 바로 파일에 저장되고 연주에 반영된다. */
class KeysPage final : public ui::Page,
                       private juce::ChangeListener
{
public:
    explicit KeysPage (Session& sessionToUse);
    ~KeysPage() override;

    void tick() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { rebuild(); }

    void pedalButtonClicked();
    void startPedalLearning();
    void updatePedalStatus();

    void rebuild();
    void selectKey (hvd::KeyCode key);
    void showBinding();
    void layoutEditor();
    void commit();

    int currentBindingIndex() const;
    juce::DynamicObject* currentBinding() const;
    juce::StringArray padsOf (const juce::var& binding) const;
    void setPads (juce::DynamicObject& binding, const juce::StringArray& pads) const;
    juce::String padName (const juce::String& padId) const;
    juce::String describe (const juce::var& binding) const;

    void addBinding();
    void removeBinding();
    void startQuickAssign();
    void showPadsMenu();
    void actionChanged();
    void strokeChanged();
    void fillPadCombo (juce::ComboBox& combo, bool withNone) const;
    juce::String comboPadId (const juce::ComboBox& combo) const;

    struct Row
    {
        juce::Label caption;
        juce::Component* control = nullptr;
        bool shown = false;
    };

    Session& session;
    KeyboardView keyboard;
    std::vector<std::vector<int>> bindingsByKey;    // 키 코드 → 키 배열 문서의 줄 번호들
    juce::StringArray padIds;
    hvd::KeyCode selectedKey = hvd::invalidKey;
    int selectedSlot = 0;
    bool updating = false;
    bool valuesDirty = false;
    std::uint32_t seenKeyPressCount = 0;
    int tickCount = 0;

    bool pedalConnected = false;

    juce::Label keymapCaption, quickCaption, heldLabel, pedalLabel, keyTitle, emptyHint;
    juce::ComboBox keymapCombo, quickPadCombo, slotCombo;
    juce::TextButton duplicateButton, renameButton, folderButton, restoreButton, quickButton, pedalButton, resetHeldButton, addButton, removeButton;
    juce::ToggleButton followButton;

    juce::ComboBox actionCombo, padCombo, strokeCombo, rateCombo, divisionCombo, pressPadCombo, releasePadCombo, splashPadCombo;
    PadSwapPicker heldSwap, releasedSwap;
    juce::TextButton padsButton;
    juce::Slider velocitySlider, accentSlider, softSlider, gapSlider;
    juce::TextEditor stepsEditor;
    std::array<Row, 16> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeysPage)
};
