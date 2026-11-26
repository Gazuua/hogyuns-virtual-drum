#include "KitPage.h"

#include "SfzImport.h"

namespace
{

constexpr int listWidth = 250;

} // namespace

KitPage::KitPage (Session& sessionToUse)
    : session (sessionToUse)
{
    ui::setupLabel (*this, kitCaption, ko ("킷"), 14.0f, ui::quietText);
    ui::setupCombo (*this, kitCombo, [this]
    {
        if (! updating && kitCombo.getSelectedItemIndex() >= 0)
            session.selectKit (kitCombo.getText());
    });

    ui::setupButton (*this, duplicateButton, ko ("복제"), [this] { duplicateKit(); });
    ui::setupButton (*this, renameKitButton, ko ("이름 바꾸기"), [this] { renameKit(); });
    ui::setupButton (*this, importButton, ko ("가져오기"), [this] { importKit(); });
    ui::setupButton (*this, sfzButton, ko ("SFZ 가져오기"), [this] { importSfzKit(); });
    importButton.setTooltip (ko ("내보내기로 만든 ZIP 파일에서 킷을 가져옵니다"));
    sfzButton.setTooltip (ko ("SFZ 형식의 드럼 킷을 읽어 새 킷을 만듭니다"));
    ui::setupButton (*this, exportButton, ko ("내보내기"), [this] { exportKit(); });
    ui::setupButton (*this, folderButton, ko ("폴더 열기"), [this] { session.getKitFile().revealToUser(); });
    ui::setupButton (*this, restoreButton, ko ("되돌리기…"), [this]
    {
        // 킷 파일이 바뀔 때마다 남겨 둔 이전 내용 가운데 하나로 되돌린다 (패드 값, 고른 소리, 드럼 그림의 배치).
        showBackupMenu (restoreButton, session.getBackups(), session.getKitFile(), [this] (juce::File backup)
        {
            if (const auto error = session.restoreKitBackup (backup); error.isNotEmpty())
                ui::showMessage (ko ("되돌리지 못했습니다"), error);
        });
    });
    restoreButton.setTooltip (ko ("고치기 전 상태로 되돌립니다."));
    ui::setupLabel (*this, kitInfo, {}, 13.0f, ui::quietText);

    padList.setRowHeight (26);
    padList.setColour (juce::ListBox::backgroundColourId, ui::panel);
    ui::noFocus (padList);
    addAndMakeVisible (padList);

    ui::setupButton (*this, addPadButton, ko ("패드 추가"), [this] { addPad(); });
    ui::setupButton (*this, removePadButton, ko ("패드 삭제"), [this] { removePad(); });

    ui::setupLabel (*this, padTitle, {}, 18.0f, juce::Colours::white, true);
    ui::setupButton (*this, renamePadButton, ko ("이름 바꾸기"), [this] { renamePad(); });
    ui::setupButton (*this, softButton, ko ("약하게"), [this] { session.previewPad (selectedPad, 0.3f); });
    ui::setupButton (*this, mediumButton, ko ("중간"), [this] { session.previewPad (selectedPad, 0.65f); });
    ui::setupButton (*this, hardButton, ko ("세게"), [this] { session.previewPad (selectedPad, 1.0f); });

    struct FieldSpec
    {
        const char* name;
        const char* caption;
        double minimum, maximum, step, defaultValue;
        const char* suffix;
        int decimals;
        double midPoint;    // 0이 아니면 슬라이더 가운데에 이 값이 오게 한다 (넓은 범위에서 자주 쓰는 쪽을 세밀하게)
    };

    // 음량은 0 ~ 10배. 1.0을 가운데에 두어, 왼쪽 절반이 0 ~ 1, 오른쪽 절반이 1 ~ 10이다.
    const FieldSpec specs[] = {
        { "volume", "음량 (0 ~ 10배)", 0.0, static_cast<double> (hvd::maxPadVolume), 0.01, 1.0, "", 2, 1.0 },
        { "pan", "팬 (− 왼쪽, + 오른쪽)", -1.0, 1.0, 0.01, 0.0, "", 2, 0.0 },
        { "pitch", "피치 (반음)", -12.0, 12.0, 0.1, 0.0, "", 1, 0.0 },
        { "reverbSend", "리버브로 보내는 양", 0.0, 1.0, 0.01, 1.0, "", 2, 0.0 },
        { "startMs", "시작 지점 (앞부분 건너뛰기)", 0.0, 200.0, 0.5, 0.0, " ms", 1, 0.0 },
        { "velocityCurve", "세기 곡선 (클수록 약한 타가 더 작게)", 0.2, 2.5, 0.05, 1.0, "", 2, 0.0 },
    };

    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        auto& field = fields[i];
        const auto& spec = specs[i];

        field.name = spec.name;
        ui::setupLabel (*this, field.caption, ko (spec.caption), 13.0f, ui::quietText);
        ui::setupSlider (*this, field.slider, spec.minimum, spec.maximum, spec.step, spec.defaultValue, ko (spec.suffix), spec.decimals);

        if (spec.midPoint != 0.0)
            field.slider.setSkewFactorFromMidPoint (spec.midPoint);

        field.slider.onValueChange = [this, &field]
        {
            if (updating)
                return;

            // 소리에는 바로 반영하고, 파일에는 마우스를 놓은 뒤에 저장한다.
            session.setPadValue (selectedPad, field.name, field.slider.getValue());
            valuesDirty = true;
        };
    }

    // 모아 둔 소리 가운데 하나를 고른다. 화살표 버튼으로 차례로 넘기며 들어 볼 수 있다.
    ui::setupLabel (*this, soundCaption, {}, 13.0f, ui::quietText);
    ui::setupCombo (*this, soundCombo, [this]
    {
        if (! updating && soundCombo.getSelectedItemIndex() >= 0)
            chooseSound (soundCombo.getSelectedItemIndex());
    });
    ui::setupButton (*this, previousSoundButton, "<", [this] { chooseSound (session.getKitDocument().getPadSoundIndex (selectedPadId()) - 1); });
    ui::setupButton (*this, nextSoundButton, ">", [this] { chooseSound (session.getKitDocument().getPadSoundIndex (selectedPadId()) + 1); });
    previousSoundButton.setTooltip (ko ("앞 소리"));
    nextSoundButton.setTooltip (ko ("다음 소리"));

    familyButton.setButtonText (ko ("같은 악기의 패드도 함께 바꾸기 (하이햇, 스네어 등)"));
    familyButton.setToggleState (true, juce::dontSendNotification);
    ui::noFocus (familyButton);
    addAndMakeVisible (familyButton);

    ui::setupLabel (*this, samplesCaption, ko ("샘플 (세기 구간마다 한 줄, 같은 줄은 번갈아 냄)"), 13.0f, ui::quietText);
    ui::setupLabel (*this, samplesText, {}, 13.0f);
    samplesText.setJustificationType (juce::Justification::topLeft);

    ui::setupButton (*this, replaceButton, ko ("샘플 바꾸기…"), [this] { chooseSamples (false); });
    ui::setupButton (*this, appendButton, ko ("샘플 더하기…"), [this] { chooseSamples (true); });
    ui::setupLabel (*this, hintLabel,
                    ko ("소리 파일을 끌어다 놓으면 바꿉니다. Ctrl을 누른 채 놓으면 더합니다."),
                    13.0f, ui::quietText);

    session.addChangeListener (this);
    rebuild();
}

KitPage::~KitPage()
{
    session.removeChangeListener (this);
}

void KitPage::paint (juce::Graphics& g)
{
    g.setColour (ui::panel);
    g.fillRoundedRectangle (getLocalBounds().withTrimmedTop (40).withTrimmedLeft (listWidth + 12).toFloat(), 8.0f);
}

void KitPage::resized()
{
    auto area = getLocalBounds();

    auto top = area.removeFromTop (30);
    kitCaption.setBounds (top.removeFromLeft (30));
    kitCombo.setBounds (top.removeFromLeft (200));
    top.removeFromLeft (8);

    for (auto* button : { &duplicateButton, &renameKitButton, &importButton, &sfzButton, &exportButton, &folderButton, &restoreButton })
    {
        button->setBounds (top.removeFromLeft ((button == &renameKitButton || button == &sfzButton) ? 104 : (button == &restoreButton ? 92 : 80)));
        top.removeFromLeft (6);
    }

    top.removeFromLeft (6);

    kitInfo.setBounds (top);
    area.removeFromTop (10);

    auto left = area.removeFromLeft (listWidth);
    auto listButtons = left.removeFromBottom (28);
    addPadButton.setBounds (listButtons.removeFromLeft (listWidth / 2 - 3));
    removePadButton.setBounds (listButtons.removeFromRight (listWidth / 2 - 3));
    left.removeFromBottom (6);
    padList.setBounds (left);

    area.removeFromLeft (12);
    auto editor = area.reduced (16, 12);

    auto title = editor.removeFromTop (30);
    hardButton.setBounds (title.removeFromRight (64));
    title.removeFromRight (4);
    mediumButton.setBounds (title.removeFromRight (64));
    title.removeFromRight (4);
    softButton.setBounds (title.removeFromRight (64));
    title.removeFromRight (16);
    renamePadButton.setBounds (title.removeFromRight (100));
    padTitle.setBounds (title);

    editor.removeFromTop (10);

    // 조절 항목은 두 줄씩 나란히 놓는다.
    for (std::size_t i = 0; i < fields.size(); i += 2)
    {
        auto row = editor.removeFromTop (50);
        auto leftHalf = row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (12);
        auto rightHalf = row.withTrimmedLeft (12);

        fields[i].caption.setBounds (leftHalf.removeFromTop (20));
        fields[i].slider.setBounds (leftHalf.removeFromTop (26));
        fields[i + 1].caption.setBounds (rightHalf.removeFromTop (20));
        fields[i + 1].slider.setBounds (rightHalf.removeFromTop (26));
    }

    auto soundRow = editor.removeFromTop (50);
    soundCaption.setBounds (soundRow.removeFromTop (20));
    auto soundControls = soundRow.removeFromTop (26);
    soundCombo.setBounds (soundControls.removeFromLeft (juce::jmin (340, soundControls.getWidth() / 2)));
    soundControls.removeFromLeft (6);
    previousSoundButton.setBounds (soundControls.removeFromLeft (34));
    soundControls.removeFromLeft (4);
    nextSoundButton.setBounds (soundControls.removeFromLeft (34));
    soundControls.removeFromLeft (16);
    familyButton.setBounds (soundControls);

    editor.removeFromTop (8);
    samplesCaption.setBounds (editor.removeFromTop (20));

    hintLabel.setBounds (editor.removeFromBottom (22));
    editor.removeFromBottom (6);
    auto buttons = editor.removeFromBottom (28);
    replaceButton.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (6);
    appendButton.setBounds (buttons.removeFromLeft (120));
    editor.removeFromBottom (6);

    samplesText.setBounds (editor);
}

//==============================================================================
int KitPage::getNumRows()
{
    const auto* kit = session.getKit();
    return kit != nullptr ? static_cast<int> (kit->pads.size()) : 0;
}

void KitPage::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto* kit = session.getKit();

    if (kit == nullptr || ! juce::isPositiveAndBelow (row, static_cast<int> (kit->pads.size())))
        return;

    const auto& pad = kit->pads[static_cast<std::size_t> (row)];
    const auto id = juce::String (pad.id);

    if (selected)
    {
        g.setColour (ui::accent.withAlpha (0.22f));
        g.fillRect (0, 0, width, height);
    }

    g.setColour (ui::padColour (id).withAlpha (pad.layers.empty() ? 0.35f : 1.0f));
    g.fillEllipse (8.0f, static_cast<float> (height) * 0.5f - 5.0f, 10.0f, 10.0f);

    g.setColour (juce::Colours::white.withAlpha (pad.layers.empty() ? 0.5f : 1.0f));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (fromUtf8 (pad.name), 26, 0, width - 80, height, juce::Justification::centredLeft, true);

    const auto count = session.getKitDocument().countSamples (id);
    g.setColour (ui::quietText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (count > 0 ? juce::String (count) + ko ("개") : ko ("샘플 없음"), width - 78, 0, 70, height,
                juce::Justification::centredRight, true);
}

void KitPage::selectedRowsChanged (int lastRowSelected)
{
    if (updating || lastRowSelected < 0)
        return;

    selectedPad = lastRowSelected;
    showPad();
}

void KitPage::listBoxItemClicked (int row, const juce::MouseEvent& event)
{
    // 목록에서 패드를 더블 클릭하지 않아도, 고르면서 소리를 들어 볼 수 있게 한다.
    if (event.mods.isLeftButtonDown())
        session.previewPad (row, 0.8f);
}

juce::String KitPage::selectedPadId() const
{
    return session.getPadId (selectedPad);
}

void KitPage::rebuild()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto names = session.getKitFolderNames();
    kitCombo.clear (juce::dontSendNotification);
    kitCombo.addItemList (names, 1);
    kitCombo.setSelectedItemIndex (names.indexOf (session.getKitFolderName()), juce::dontSendNotification);

    const auto& stats = session.getKitStats();
    const auto& document = session.getKitDocument();
    // 킷의 이름은 창 아래의 상태 줄에 나오므로 여기에는 적지 않는다 (윗줄에 버튼이 많아 자리가 좁다).
    juce::String info;
    info << ko ("샘플 ") << stats.numSamples << ko ("개")
         << "  ·  " << juce::roundToInt (static_cast<double> (stats.memoryBytes) / 1.0e6) << " MB";

    if (const auto license = document.getProperty ("license").toString(); license.isNotEmpty())
        info << "  ·  " << license;

    kitInfo.setText (info, juce::dontSendNotification);

    padList.updateContent();
    selectedPad = juce::jlimit (0, juce::jmax (0, getNumRows() - 1), selectedPad);
    padList.selectRow (selectedPad, true, true);
    padList.repaint();
    showPad();
}

void KitPage::showPad()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto id = selectedPadId();
    const auto hasPad = id.isNotEmpty();
    const auto* kit = session.getKit();

    padTitle.setText (hasPad && kit != nullptr ? fromUtf8 (kit->pads[static_cast<std::size_t> (selectedPad)].name) + "   (" + id + ")"
                                               : ko ("패드가 없습니다"),
                      juce::dontSendNotification);

    for (auto& field : fields)
    {
        field.slider.setEnabled (hasPad);

        if (hasPad)
            field.slider.setValue (session.getPadValue (selectedPad, field.name), juce::dontSendNotification);
    }

    // 모아 둔 소리
    const auto sounds = session.getKitDocument().getPadSounds (id);
    const auto soundIndex = session.getKitDocument().getPadSoundIndex (id);

    soundCombo.clear (juce::dontSendNotification);

    for (int i = 0; i < sounds.size(); ++i)
        soundCombo.addItem (sounds.getReference (i).name.isNotEmpty() ? sounds.getReference (i).name : sounds.getReference (i).id, i + 1);

    soundCombo.setTextWhenNothingSelected (sounds.isEmpty() ? ko ("모아 둔 소리가 없습니다") : ko ("직접 고른 샘플"));
    soundCombo.setSelectedItemIndex (soundIndex, juce::dontSendNotification);
    soundCombo.setEnabled (! sounds.isEmpty());
    previousSoundButton.setEnabled (sounds.size() > 1);
    nextSoundButton.setEnabled (sounds.size() > 1);
    familyButton.setEnabled (! sounds.isEmpty());

    auto caption = ko ("소리");

    if (! sounds.isEmpty())
        caption << "  (" << ko ("모아 둔 것 ") << sounds.size() << ko ("개 중 ")
                << (soundIndex >= 0 ? juce::String (soundIndex + 1) + ko ("번째") : ko ("직접 고른 샘플")) << ")";

    soundCaption.setText (caption, juce::dontSendNotification);

    // 레이어마다 한 줄. 파일 이름이 많으면 앞의 몇 개만 적는다.
    juce::StringArray lines;

    for (const auto& line : session.getKitDocument().describeSamples (id))
        lines.add (line.length() > 150 ? line.substring (0, 150) + " …" : line);

    samplesText.setText (lines.isEmpty() ? ko ("샘플이 없습니다. 아래 버튼으로 고르거나 파일을 끌어다 놓으세요.")
                                        : lines.joinIntoString ("\n"),
                         juce::dontSendNotification);

    for (auto* button : { &renamePadButton, &softButton, &mediumButton, &hardButton, &replaceButton, &appendButton, &removePadButton })
        button->setEnabled (hasPad);
}

void KitPage::tick()
{
    // 조절 값을 바꿨으면 마우스를 놓은 뒤에 한 번만 저장한다.
    if (valuesDirty && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        valuesDirty = false;
        session.saveKit (false);
    }
}

//==============================================================================
bool KitPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (ui::isAudioFile (juce::File (path)))
            return true;

    return false;
}

void KitPage::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> audioFiles;

    for (const auto& path : files)
        if (const juce::File file (path); ui::isAudioFile (file))
            audioFiles.add (file);

    applySamples (audioFiles, juce::ModifierKeys::currentModifiers.isCtrlDown());
}

void KitPage::chooseSamples (bool append)
{
    const juce::File startFolder (session.getSetting ("sampleFolder", session.getKitFile().getParentDirectory().getFullPathName()).toString());

    chooser = std::make_unique<juce::FileChooser> (append ? ko ("더할 샘플 고르기") : ko ("바꿀 샘플 고르기"),
                                                   startFolder, ui::audioFilePatterns());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, append] (const juce::FileChooser& fileChooser)
                          {
                              const auto files = fileChooser.getResults();

                              if (files.isEmpty())
                                  return;

                              session.setSetting ("sampleFolder", files.getFirst().getParentDirectory().getFullPathName());
                              applySamples (files, append);
                          });
}

void KitPage::applySamples (const juce::Array<juce::File>& files, bool append)
{
    const auto id = selectedPadId();

    if (id.isEmpty() || files.isEmpty())
        return;

    if (const auto error = session.getKitDocument().setPadSamples (id, files, append); error.isNotEmpty())
    {
        ui::showMessage (ko ("샘플을 바꾸지 못했습니다"), error);
        return;
    }

    session.saveKit (true);
    session.previewPad (selectedPad, 0.9f);
}

void KitPage::chooseSound (int index)
{
    const auto id = selectedPadId();
    const auto count = session.getKitDocument().getPadSounds (id).size();

    if (id.isEmpty() || count == 0)
        return;

    // 끝을 넘으면 처음으로 돌아간다.
    index = ((index % count) + count) % count;

    if (session.getKitDocument().selectPadSound (id, index, familyButton.getToggleState()) > 0)
    {
        session.saveKit (true);
        session.previewPad (selectedPad, 0.9f);
    }
}

void KitPage::renamePad()
{
    const auto id = selectedPadId();
    const auto* kit = session.getKit();

    if (id.isEmpty() || kit == nullptr)
        return;

    ui::promptForText (this, ko ("패드 이름 바꾸기"), ko ("화면에 보여 줄 이름을 적으세요."),
                       fromUtf8 (kit->pads[static_cast<std::size_t> (selectedPad)].name),
                       [this, id] (juce::String name)
                       {
                           if (name.trim().isEmpty())
                               return;

                           session.getKitDocument().setPadName (id, name.trim());
                           session.saveKit (true);
                       });
}

void KitPage::addPad()
{
    ui::promptForText (this, ko ("패드 추가"), ko ("새 패드의 이름을 적으세요. 샘플과 키는 만든 뒤에 정합니다."),
                       ko ("새 패드"),
                       [this] (juce::String name)
                       {
                           if (session.getKitDocument().addPad (name.trim().isEmpty() ? ko ("새 패드") : name.trim()).isEmpty())
                               return;

                           selectedPad = getNumRows();    // 새 패드는 맨 뒤에 생긴다
                           session.saveKit (true);
                       });
}

void KitPage::removePad()
{
    const auto id = selectedPadId();

    if (id.isEmpty())
        return;

    ui::confirm (this, ko ("패드 삭제"),
                 ko ("이 패드를 킷에서 지울까요? 샘플 파일은 지우지 않습니다.\n\n") + padTitle.getText(),
                 [this, id]
                 {
                     session.getKitDocument().removePad (id);
                     session.saveKit (true);
                 });
}

void KitPage::duplicateKit()
{
    ui::promptForText (this, ko ("킷 복제"),
                       ko ("지금 킷을 복사해 새로 만듭니다. 이름을 적으세요."),
                       session.getKitDocument().getName() + ko (" 사본"),
                       [this] (juce::String name)
                       {
                           if (const auto error = session.duplicateKit (name); error.isNotEmpty())
                               ui::showMessage (ko ("킷을 복제하지 못했습니다"), error);
                       });
}

void KitPage::renameKit()
{
    ui::promptForText (this, ko ("킷 이름 바꾸기"), ko ("화면에 보여 줄 킷 이름을 적으세요. 폴더 이름은 바뀌지 않습니다."),
                       session.getKitDocument().getName(),
                       [this] (juce::String name)
                       {
                           if (name.trim().isEmpty())
                               return;

                           session.getKitDocument().setName (name.trim());
                           session.saveKit (true);
                       });
}

void KitPage::importKit()
{
    chooser = std::make_unique<juce::FileChooser> (ko ("가져올 킷(ZIP) 고르기"), juce::File(), "*.zip");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fileChooser)
                          {
                              const auto file = fileChooser.getResult();

                              if (file == juce::File())
                                  return;

                              if (const auto error = session.importKit (file); error.isNotEmpty())
                                  ui::showMessage (ko ("킷을 가져오지 못했습니다"), error);
                          });
}

void KitPage::importSfzKit()
{
    const juce::File startFolder (session.getSetting ("sfzFolder", juce::String()).toString());
    chooser = std::make_unique<juce::FileChooser> (ko ("가져올 SFZ 파일 고르기"), startFolder, "*.sfz");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fileChooser)
                          {
                              const auto file = fileChooser.getResult();

                              if (file == juce::File())
                                  return;

                              session.setSetting ("sfzFolder", file.getParentDirectory().getFullPathName());
                              const auto result = importSfz (file, session.getKitsFolder());

                              if (result.error.isNotEmpty())
                              {
                                  ui::showMessage (ko ("SFZ를 가져오지 못했습니다"), result.error);
                                  return;
                              }

                              session.selectKit (result.kitFolderName);

                              juce::String summary;
                              summary << ko ("패드 ") << result.numPads << ko ("개, 샘플 ") << result.numSamples << ko ("개를 가져왔습니다.");

                              if (result.numMissing > 0)
                                  summary << "\n" << ko ("찾지 못한 샘플 파일: ") << result.numMissing << ko ("개");

                              summary << "\n\n" << ko ("샘플은 원래 자리의 파일을 씁니다. 그 폴더를 옮기면 소리가 나지 않습니다.")
                                      << "\n" << ko ("표준 드럼 번호가 아닌 패드는 키 배열 화면에서 키를 정하세요.");
                              ui::showMessage (ko ("SFZ를 가져왔습니다"), summary);
                          });
}

void KitPage::exportKit()
{
    const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile (session.getKitFolderName() + ".zip");

    chooser = std::make_unique<juce::FileChooser> (ko ("킷을 ZIP으로 내보내기"), suggested, "*.zip");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fileChooser)
                          {
                              const auto file = fileChooser.getResult();

                              if (file == juce::File())
                                  return;

                              const auto error = session.exportKit (file.withFileExtension ("zip"));

                              if (error.isNotEmpty())
                                  ui::showMessage (ko ("킷을 내보내지 못했습니다"), error);
                              else
                                  ui::showMessage (ko ("킷을 내보냈습니다"), file.withFileExtension ("zip").getFullPathName());
                          });
}
