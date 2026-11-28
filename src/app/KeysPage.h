#pragma once

#include <functional>
#include <vector>

#include "KeyboardView.h"
#include "Session.h"
#include "Ui.h"

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
    juce::Rectangle<int> editorPanel;    // paint가 편집 칸 바탕을 그리는 자리
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
