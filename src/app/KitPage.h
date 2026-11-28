#pragma once

#include <memory>

#include "DocumentBar.h"
#include "Session.h"
#include "Ui.h"

/** 킷 편집 화면. 킷을 고르고 복제하고, 패드마다 소리와 샘플을 바꾸고 음량, 피치 등을 조절한다. */
class KitPage final : public ui::Page,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::ListBoxModel
{
public:
    explicit KitPage (Session& sessionToUse);
    ~KitPage() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { rebuild(); }

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    DocumentBar::Source makeDocumentSource();

    void rebuild();
    void showPad();
    void layoutEditor();
    juce::String selectedPadId() const;

    void chooseSamples (bool append);
    void applySamples (const juce::Array<juce::File>& files, bool append);
    void chooseSound (int index);
    void renamePad();
    void addPad();
    void removePad();
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
    DocumentBar documents;
    int selectedPad = 0;
    bool updating = false;
    bool filesShown = false;        // 샘플 파일 목록을 펼쳤는지
    bool hasSamples = false;        // 고른 패드에 샘플이 있는지
    bool fileDragOver = false;      // 소리 파일을 끌고 들어와 있는지

    juce::Label kitInfo, padTitle, previewCaption;
    juce::Rectangle<int> listPanel, editorPanel, editorArea;    // paint가 칸 바탕을 그리는 자리, 편집 칸 안쪽
    juce::ListBox padList { {}, this };
    juce::TextButton addPadButton, removePadButton;
    juce::TextButton renamePadButton, softButton, mediumButton, hardButton;
    std::array<Field, 4> fields;    // 음량, 피치, 시작, 세기 곡선 (팬·리버브는 믹서)

    // 모아 둔 소리 고르기
    juce::Label soundCaption, soundCount;
    juce::ComboBox soundCombo;
    juce::TextButton previousSoundButton, nextSoundButton;
    juce::ToggleButton familyButton;

    // 샘플 파일
    juce::Label samplesCaption, samplesSummary, samplesText;
    juce::TextButton filesButton, replaceButton, appendButton;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitPage)
};
