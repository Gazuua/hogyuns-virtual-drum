#pragma once

#include <memory>

#include "Session.h"
#include "Ui.h"

/** 킷 편집 화면. 킷을 고르고 복제하고, 패드마다 샘플을 바꾸고 음량, 팬, 피치 등을 조절한다. */
class KitPage final : public ui::Page,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::ListBoxModel
{
public:
    explicit KitPage (Session& sessionToUse);
    ~KitPage() override;

    void tick() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { rebuild(); }

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void rebuild();
    void showPad();
    juce::String selectedPadId() const;

    void chooseSamples (bool append);
    void applySamples (const juce::Array<juce::File>& files, bool append);
    void chooseSound (int index);
    void renamePad();
    void addPad();
    void removePad();
    void duplicateKit();
    void renameKit();
    void importKit();
    void importSfzKit();
    void exportKit();

    struct Field
    {
        juce::Label caption;
        juce::Slider slider;
        juce::Identifier name;
    };

    Session& session;
    int selectedPad = 0;
    bool valuesDirty = false;
    bool updating = false;

    juce::Label kitCaption, padTitle, samplesCaption, samplesText, hintLabel, kitInfo;
    juce::ComboBox kitCombo;
    juce::TextButton duplicateButton, renameKitButton, importButton, sfzButton, exportButton, folderButton, restoreButton;
    juce::ListBox padList { {}, this };
    juce::TextButton addPadButton, removePadButton;
    juce::TextButton renamePadButton, softButton, mediumButton, hardButton, replaceButton, appendButton;
    std::array<Field, 6> fields;

    // 모아 둔 소리 고르기
    juce::Label soundCaption;
    juce::ComboBox soundCombo;
    juce::TextButton previousSoundButton, nextSoundButton;
    juce::ToggleButton familyButton;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitPage)
};
