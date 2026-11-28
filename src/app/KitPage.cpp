#include "KitPage.h"

#include "PageTools.h"
#include "SfzImport.h"

namespace
{

constexpr int listWidth = 250;
constexpr int captionHeight = 20;       // 조절 항목의 이름 줄
constexpr int valueBoxWidth = 72;       // 슬라이더 값 상자 ("가운데", "+1.5 반음")

int textWidth (const juce::Label& label)
{
    return juce::GlyphArrangement::getStringWidthInt (label.getFont(), label.getText());
}

/** 팬 값(-1 ~ 1)을 "가운데", "L30", "R30" 꼴로. */
juce::String panText (double value)
{
    const auto percent = juce::roundToInt (value * 100.0);

    if (percent == 0)
        return ko ("가운데");

    return (percent < 0 ? "L" : "R") + juce::String (std::abs (percent));
}

double panValue (const juce::String& text)
{
    const auto trimmed = text.trim().toUpperCase();
    const auto number = trimmed.retainCharacters ("0123456789.-").getDoubleValue();

    if (trimmed.startsWith ("L"))
        return -std::abs (number) / 100.0;

    if (trimmed.startsWith ("R"))
        return std::abs (number) / 100.0;

    return std::abs (number) > 1.0 ? number / 100.0 : number;    // 숫자만 적으면 -1 ~ 1, 넘으면 퍼센트로 본다
}

/** 피치 값을 "0 반음", "+1.5 반음", "-2 반음" 꼴로. */
juce::String pitchText (double value)
{
    auto text = juce::String (value, 1);

    if (text.endsWith (".0"))
        text = text.dropLastCharacters (2);

    if (text == "-0")
        text = "0";

    return (value > 0.05 ? "+" : "") + text + ko (" 반음");
}

} // namespace

KitPage::KitPage (Session& sessionToUse)
    : session (sessionToUse),
      documents (makeDocumentSource())
{
    addAndMakeVisible (documents);
    documents.addMenuItem (ko ("ZIP 가져오기"), [this] { importKit(); }, true);
    documents.addMenuItem (ko ("SFZ 가져오기"), [this] { importSfzKit(); });
    documents.addMenuItem (ko ("내보내기"), [this] { exportKit(); });

    ui::setupLabel (*this, kitInfo, {}, ui::Text::caption);
    kitInfo.setJustificationType (juce::Justification::centredRight);

    //==============================================================================
    // 패드 목록
    padList.setRowHeight (28);
    ui::noFocus (padList);
    addAndMakeVisible (padList);

    ui::setupButton (*this, addPadButton, ko ("추가"), [this] { addPad(); });
    ui::setupButton (*this, removePadButton, ko ("삭제"), [this] { removePad(); });
    addPadButton.setTooltip (ko ("새 패드"));
    removePadButton.setTooltip (ko ("고른 패드를 킷에서 지웁니다"));

    //==============================================================================
    // 제목 줄: [킥] [이름 바꾸기]  ……  들어 보기 [약][중][강]
    ui::setupLabel (*this, padTitle, {}, ui::Text::section);
    padTitle.addMouseListener (this, false);    // 두 번 누르면 이름 바꾸기
    ui::setupButton (*this, renamePadButton, ko ("이름 바꾸기"), [this] { renamePad(); });

    ui::setupLabel (*this, previewCaption, ko ("들어 보기"), ui::Text::caption);
    previewCaption.setJustificationType (juce::Justification::centredRight);
    ui::setupButton (*this, softButton, ko ("약"), [this] { session.previewPad (selectedPad, 0.3f); });
    ui::setupButton (*this, mediumButton, ko ("중"), [this] { session.previewPad (selectedPad, 0.65f); });
    ui::setupButton (*this, hardButton, ko ("강"), [this] { session.previewPad (selectedPad, 1.0f); });
    softButton.setTooltip (ko ("약하게"));
    mediumButton.setTooltip (ko ("중간"));
    hardButton.setTooltip (ko ("세게"));
    softButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    mediumButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    hardButton.setConnectedEdges (juce::Button::ConnectedOnLeft);

    //==============================================================================
    // 모아 둔 소리 가운데 하나를 고른다. 화살표 버튼으로 차례로 넘기며 들어 볼 수 있다.
    ui::setupLabel (*this, soundCaption, ko ("소리"), ui::Text::caption);
    ui::setupCombo (*this, soundCombo, [this]
    {
        if (! updating && soundCombo.getSelectedItemIndex() >= 0)
            chooseSound (soundCombo.getSelectedItemIndex());
    });
    ui::setupButton (*this, previousSoundButton, "<", [this] { chooseSound (session.getKitDocument().getPadSoundIndex (selectedPadId()) - 1); });
    ui::setupButton (*this, nextSoundButton, ">", [this] { chooseSound (session.getKitDocument().getPadSoundIndex (selectedPadId()) + 1); });
    previousSoundButton.setTooltip (ko ("앞 소리"));
    nextSoundButton.setTooltip (ko ("다음 소리"));
    ui::setupLabel (*this, soundCount, {}, ui::Text::caption);
    soundCount.setJustificationType (juce::Justification::centred);
    soundCount.setTooltip (ko ("모아 둔 소리 가운데 몇 번째인지"));

    ui::setupToggle (*this, familyButton, ko ("같은 악기 함께"), {}, true);
    familyButton.setTooltip (ko ("같은 악기의 패드도 함께 바꾸기 (하이햇, 스네어 등)"));

    //==============================================================================
    // 조절 항목. 두 개씩 한 줄: 음량 · 팬 / 피치 · 시작 / 세기 곡선 · 리버브
    struct FieldSpec
    {
        const char* name;
        const char* caption;
        const char* tip;
        double minimum, maximum, step, defaultValue;
        const char* suffix;
        int decimals;
        double midPoint;    // 0이 아니면 슬라이더 가운데에 이 값이 오게 한다 (넓은 범위에서 자주 쓰는 쪽을 세밀하게)
    };

    // 음량은 0 ~ 10배. 1.0을 가운데에 두어, 왼쪽 절반이 0 ~ 1, 오른쪽 절반이 1 ~ 10이다.
    const FieldSpec specs[] = {
        { "volume", "음량", "0 ~ 10배", 0.0, static_cast<double> (hvd::maxPadVolume), 0.01, 1.0, "", 2, 1.0 },
        { "pan", "팬", "왼쪽 L, 오른쪽 R", -1.0, 1.0, 0.01, 0.0, "", 2, 0.0 },
        { "pitch", "피치", "반음 단위", -12.0, 12.0, 0.1, 0.0, "", 1, 0.0 },
        { "startMs", "시작", "앞부분을 건너뛴다", 0.0, 200.0, 0.5, 0.0, " ms", 1, 0.0 },
        { "velocityCurve", "세기 곡선", "클수록 약한 타가 더 작게", 0.2, 2.5, 0.05, 1.0, "", 2, 0.0 },
        { "reverbSend", "리버브", "리버브로 보내는 양", 0.0, 1.0, 0.01, 1.0, "", 2, 0.0 },
    };

    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        auto& field = fields[i];
        const auto& spec = specs[i];

        field.name = spec.name;
        ui::setupLabel (*this, field.caption, ko (spec.caption), ui::Text::caption);
        ui::setupSlider (*this, field.slider, spec.minimum, spec.maximum, spec.step, spec.defaultValue, ko (spec.suffix), spec.decimals);
        field.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, valueBoxWidth, 22);
        field.caption.setTooltip (ko (spec.tip));
        field.slider.setTooltip (ko (spec.tip));

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

    // 값 상자에 단위를 붙인다: 음량 ×1.00, 팬 가운데 / L30 / R30, 피치 +1.5 반음
    auto& volume = fields[0].slider;
    volume.textFromValueFunction = [] (double value) { return ko ("×") + juce::String (value, 2); };
    volume.valueFromTextFunction = [] (const juce::String& text) { return text.retainCharacters ("0123456789.").getDoubleValue(); };
    fields[1].slider.textFromValueFunction = panText;
    fields[1].slider.valueFromTextFunction = panValue;
    fields[2].slider.textFromValueFunction = pitchText;

    for (auto& field : fields)
        field.slider.updateText();

    //==============================================================================
    // 샘플: 한 줄 요약과 [파일 보기]. 펼치면 세기 구간마다 한 줄과 바꾸기·더하기 버튼
    ui::setupLabel (*this, samplesCaption, ko ("샘플"), ui::Text::caption);
    samplesCaption.setTooltip (ko ("세기 구간마다 한 줄, 같은 줄은 번갈아 냄"));
    ui::setupLabel (*this, samplesSummary, {}, ui::Text::body);
    ui::setupButton (*this, filesButton, ko ("파일 보기"), [this]
    {
        filesShown = ! filesShown;
        filesButton.setButtonText (filesShown ? ko ("접기") : ko ("파일 보기"));
        layoutEditor();
    });

    ui::setupLabel (*this, samplesText, {}, ui::Text::detail);
    samplesText.setJustificationType (juce::Justification::topLeft);

    ui::setupButton (*this, replaceButton, ko ("샘플 바꾸기"), [this] { chooseSamples (false); });
    ui::setupButton (*this, appendButton, ko ("샘플 더하기"), [this] { chooseSamples (true); });
    replaceButton.setTooltip (ko ("소리 파일을 끌어 놓아도 바뀝니다"));
    appendButton.setTooltip (ko ("Ctrl을 누른 채 끌어 놓아도 더해집니다"));

    // 버튼 종류: 드문 일은 조용하게, 지우는 일은 위험 표시
    ui::setButtonKind (renamePadButton, ui::ButtonKind::quiet);
    ui::setButtonKind (filesButton, ui::ButtonKind::quiet);
    ui::setButtonKind (removePadButton, ui::ButtonKind::danger);

    session.addChangeListener (this);
    rebuild();

}

DocumentBar::Source KitPage::makeDocumentSource()
{
    DocumentBar::Source source;
    source.noun = ko ("킷");
    source.names = [this] { return session.getKitFolderNames(); };
    source.current = [this] { return session.getKitFolderName(); };
    source.select = [this] (const juce::String& name) { session.selectKit (name); };
    source.copyName = [this] { return session.getKitDocument().getName() + ko (" 사본"); };
    source.duplicate = [this] (const juce::String& name) { return session.duplicateKit (name); };
    source.displayName = [this] { return session.getKitDocument().getName(); };
    source.renameNote = ko ("화면에 보여 줄 킷 이름을 적으세요. 폴더 이름은 바뀌지 않습니다.");
    source.rename = [this] (const juce::String& name)
    {
        session.getKitDocument().setName (name);
        session.saveKit (true);
    };
    source.file = [this] { return session.getKitFile(); };
    source.restore = [this] (const juce::File& backup) { return session.restoreKitBackup (backup); };
    source.backups = &session.getBackups();
    return source;
}

KitPage::~KitPage()
{
    session.removeChangeListener (this);
}

void KitPage::paint (juce::Graphics& g)
{
    // 패드 목록과 편집 칸
    ui::fillPanel (g, listPanel);
    ui::fillPanel (g, editorPanel);
}

void KitPage::paintOverChildren (juce::Graphics& g)
{
    // 소리 파일을 끌고 들어오면 편집 칸에 점선 테두리와 놓는 법을 띄운다.
    if (! fileDragOver || editorPanel.isEmpty())
        return;

    const auto& t = ui::theme();
    const auto area = editorPanel.toFloat();

    g.setColour (t.surface.withAlpha (0.9f));
    g.fillRoundedRectangle (area, t.panelRadius);

    juce::Path outline, dashed;
    outline.addRoundedRectangle (area.reduced (6.0f), t.panelRadius);
    const float dashes[] = { 8.0f, 6.0f };
    juce::PathStrokeType (2.0f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (t.accent);
    g.fillPath (dashed);

    g.setColour (t.text);
    g.setFont (juce::FontOptions (ui::type::section, juce::Font::bold));
    g.drawText (ko ("놓으면 바꾸기  ·  Ctrl 더하기"), editorPanel, juce::Justification::centred, true);
}

void KitPage::resized()
{
    auto area = getLocalBounds();

    // 윗줄: [킷 목록] [복제] [되돌리기] [⋯]  ……  샘플 수 · 크기 · 라이선스
    auto top = area.removeFromTop (ui::space::control);
    documents.setBounds (top.removeFromLeft (juce::jmin (documents.getIdealWidth(), top.getWidth())));
    top.removeFromLeft (ui::space::large);
    kitInfo.setBounds (top);
    kitInfo.setVisible (top.getWidth() >= 160);    // 모자라면 목록의 툴팁으로만 본다
    area.removeFromTop (ui::space::medium);

    auto left = area.removeFromLeft (listWidth);
    auto listButtons = left.removeFromBottom (ui::space::control);
    addPadButton.setBounds (listButtons.removeFromLeft (listWidth / 2 - ui::space::tight));
    removePadButton.setBounds (listButtons.removeFromRight (listWidth / 2 - ui::space::tight));
    left.removeFromBottom (ui::space::small);
    listPanel = left;
    padList.setBounds (left.reduced (ui::space::tight, ui::space::small));

    area.removeFromLeft (ui::space::medium);
    editorPanel = area;
    editorArea = area.reduced (ui::space::large);

    layoutEditor();
}

void KitPage::layoutEditor()
{
    auto editor = editorArea;

    //==============================================================================
    // 제목 줄
    auto title = editor.removeFromTop (ui::space::control);
    constexpr int previewWidth = 44;
    hardButton.setBounds (title.removeFromRight (previewWidth));
    mediumButton.setBounds (title.removeFromRight (previewWidth));
    softButton.setBounds (title.removeFromRight (previewWidth));
    title.removeFromRight (ui::space::small);
    previewCaption.setBounds (title.removeFromRight (textWidth (previewCaption) + 4));
    title.removeFromRight (ui::space::large);

    padTitle.setBounds (title.removeFromLeft (juce::jmin (title.getWidth() - 96, textWidth (padTitle) + ui::space::small)));
    renamePadButton.setBounds (title.removeFromLeft (92));

    editor.removeFromTop (ui::space::large);

    //==============================================================================
    // 소리: [<] [모아 둔 소리] [>] 1/15  ☑ 같은 악기 함께
    soundCaption.setBounds (editor.removeFromTop (captionHeight));
    auto sound = editor.removeFromTop (ui::space::control);
    constexpr int arrowWidth = 32, countWidth = 56, familyWidth = 150;
    const auto comboWidth = juce::jlimit (120, 420, sound.getWidth() - 2 * (arrowWidth + ui::space::tight)
                                                         - ui::space::small - countWidth - ui::space::large - familyWidth);
    previousSoundButton.setBounds (sound.removeFromLeft (arrowWidth));
    sound.removeFromLeft (ui::space::tight);
    soundCombo.setBounds (sound.removeFromLeft (comboWidth));
    sound.removeFromLeft (ui::space::tight);
    nextSoundButton.setBounds (sound.removeFromLeft (arrowWidth));
    sound.removeFromLeft (ui::space::small);
    soundCount.setBounds (sound.removeFromLeft (countWidth));
    sound.removeFromLeft (ui::space::large);
    familyButton.setBounds (sound.removeFromLeft (juce::jmin (familyWidth, sound.getWidth())));

    editor.removeFromTop (ui::space::large);

    //==============================================================================
    // 조절 항목: 두 개씩 한 줄. 음량 · 팬 다음에 묶음 사이를 띄운다.
    for (std::size_t i = 0; i < fields.size(); i += 2)
    {
        auto row = editor.removeFromTop (captionHeight + ui::space::control);
        auto leftHalf = row.removeFromLeft ((row.getWidth() - ui::space::xlarge) / 2);
        auto rightHalf = row.withTrimmedLeft (ui::space::xlarge);

        fields[i].caption.setBounds (leftHalf.removeFromTop (captionHeight));
        fields[i].slider.setBounds (leftHalf);
        fields[i + 1].caption.setBounds (rightHalf.removeFromTop (captionHeight));
        fields[i + 1].slider.setBounds (rightHalf);

        editor.removeFromTop (i == 0 || i + 2 >= fields.size() ? ui::space::large : ui::space::small);
    }

    //==============================================================================
    // 샘플: 요약 한 줄. 펼쳤거나 샘플이 없으면 아래에 목록과 버튼
    samplesCaption.setBounds (editor.removeFromTop (captionHeight));
    auto summary = editor.removeFromTop (ui::space::control);
    samplesSummary.setBounds (summary.removeFromLeft (juce::jmin (summary.getWidth() - 96, textWidth (samplesSummary) + ui::space::large)));
    filesButton.setBounds (summary.removeFromLeft (88));
    filesButton.setVisible (hasSamples);

    const auto showList = hasSamples && filesShown;
    const auto showButtons = showList || ! hasSamples;
    samplesText.setVisible (showList);
    replaceButton.setVisible (showButtons);
    appendButton.setVisible (showButtons);

    if (showList)
    {
        const auto lines = juce::StringArray::fromLines (samplesText.getText()).size();
        const auto lineHeight = juce::roundToInt (samplesText.getFont().getHeight());
        editor.removeFromTop (ui::space::small);
        samplesText.setBounds (editor.removeFromTop (juce::jmin (lines * lineHeight + 6,
                                                                 juce::jmax (lineHeight, editor.getHeight() - ui::space::control - ui::space::small))));
    }

    if (showButtons)
    {
        editor.removeFromTop (ui::space::small);
        auto buttons = editor.removeFromTop (ui::space::control);
        replaceButton.setBounds (buttons.removeFromLeft (112));
        buttons.removeFromLeft (ui::space::tight);
        appendButton.setBounds (buttons.removeFromLeft (112));
    }
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
    const auto empty = session.getKitDocument().countSamples (id) == 0;

    const auto& t = ui::theme();

    if (selected)
    {
        g.setColour (t.selection);
        g.fillRoundedRectangle (juce::Rectangle<int> (width, height).reduced (2, 1).toFloat(), juce::jmax (3.0f, t.controlRadius - 2.0f));
    }

    g.setColour (ui::padColour (id).withAlpha (pad.layers.empty() ? 0.35f : 1.0f));
    g.fillEllipse (12.0f, static_cast<float> (height) * 0.5f - 4.0f, 8.0f, 8.0f);

    // 샘플이 없는 패드에만 오른쪽에 "없음"을 붙인다.
    const auto nameWidth = width - 30 - (empty ? 52 : 12);
    g.setColour (t.text.withAlpha (pad.layers.empty() ? 0.5f : 1.0f));
    g.setFont (juce::FontOptions (ui::type::body));
    g.drawText (fromUtf8 (pad.name), 30, 0, nameWidth, height, juce::Justification::centredLeft, true);

    if (empty)
    {
        g.setColour (t.danger);
        g.setFont (juce::FontOptions (ui::type::caption));
        g.drawText (ko ("없음"), width - 52, 0, 40, height, juce::Justification::centredRight, true);
    }
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

void KitPage::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (event.eventComponent == &padTitle)
        renamePad();
}

juce::String KitPage::selectedPadId() const
{
    return session.getPadId (selectedPad);
}

void KitPage::rebuild()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    documents.refresh();

    const auto& stats = session.getKitStats();
    const auto& document = session.getKitDocument();
    // 킷의 이름은 창 아래의 상태 줄에 나오므로 여기에는 적지 않는다.
    juce::String info;
    info << ko ("샘플 ") << stats.numSamples << ko ("개")
         << "  ·  " << juce::roundToInt (static_cast<double> (stats.memoryBytes) / 1.0e6) << " MB";

    if (const auto license = document.getProperty ("license").toString(); license.isNotEmpty())
        info << "  ·  " << license;

    kitInfo.setText (info, juce::dontSendNotification);
    documents.setInfo (info);

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
    const auto& document = session.getKitDocument();

    // 제목은 이름만. 내부 id는 툴팁으로 본다.
    padTitle.setText (hasPad && kit != nullptr ? fromUtf8 (kit->pads[static_cast<std::size_t> (selectedPad)].name) : ko ("패드 없음"),
                      juce::dontSendNotification);
    padTitle.setTooltip (hasPad ? id : juce::String());

    for (auto& field : fields)
    {
        field.slider.setEnabled (hasPad);

        if (hasPad)
            field.slider.setValue (session.getPadValue (selectedPad, field.name), juce::dontSendNotification);
    }

    // 모아 둔 소리
    const auto sounds = document.getPadSounds (id);
    const auto soundIndex = document.getPadSoundIndex (id);

    soundCombo.clear (juce::dontSendNotification);

    for (int i = 0; i < sounds.size(); ++i)
        soundCombo.addItem (sounds.getReference (i).name.isNotEmpty() ? sounds.getReference (i).name : sounds.getReference (i).id, i + 1);

    soundCombo.setTextWhenNothingSelected (sounds.isEmpty() ? ko ("모아 둔 소리 없음") : ko ("직접 고른 샘플"));
    soundCombo.setSelectedItemIndex (soundIndex, juce::dontSendNotification);
    soundCombo.setEnabled (! sounds.isEmpty());
    previousSoundButton.setEnabled (sounds.size() > 1);
    nextSoundButton.setEnabled (sounds.size() > 1);
    familyButton.setEnabled (! sounds.isEmpty());

    soundCount.setText (sounds.isEmpty() ? juce::String()
                                         : (soundIndex >= 0 ? juce::String (soundIndex + 1) : juce::String ("-")) + "/" + juce::String (sounds.size()),
                        juce::dontSendNotification);

    // 샘플: 요약 한 줄 ("세기 6단 · 파일 18개"), 펼치면 세기 구간마다 한 줄. 파일 이름이 많으면 앞의 몇 개만 적는다.
    juce::StringArray lines;

    for (const auto& line : document.describeSamples (id))
        lines.add (line.length() > 150 ? line.substring (0, 150) + " …" : line);

    const auto files = document.countSamples (id);
    hasSamples = files > 0;

    juce::String summary;

    if (! hasSamples)
        summary = ko ("없음");
    else if (lines.size() > 1)
        summary << ko ("세기 ") << lines.size() << ko ("단  ·  파일 ") << files << ko ("개");
    else
        summary << ko ("파일 ") << files << ko ("개");

    samplesSummary.setText (summary, juce::dontSendNotification);
    samplesSummary.setColour (juce::Label::textColourId, hasSamples ? ui::theme().text : ui::theme().danger);
    samplesText.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);

    for (auto* button : { &renamePadButton, &softButton, &mediumButton, &hardButton, &replaceButton, &appendButton, &removePadButton })
        button->setEnabled (hasPad);

    layoutEditor();
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

void KitPage::fileDragEnter (const juce::StringArray&, int, int)
{
    fileDragOver = true;
    repaint();
}

void KitPage::fileDragExit (const juce::StringArray&)
{
    fileDragOver = false;
    repaint();
}

void KitPage::filesDropped (const juce::StringArray& files, int, int)
{
    fileDragOver = false;
    repaint();

    juce::Array<juce::File> audioFiles;

    for (const auto& path : files)
        if (const juce::File file (path); ui::isAudioFile (file))
            audioFiles.add (file);

    applySamples (audioFiles, juce::ModifierKeys::currentModifiers.isCtrlDown());
}

void KitPage::chooseSamples (bool append)
{
    const juce::File startFolder (session.getSetting ("sampleFolder", session.getKitFile().getParentDirectory().getFullPathName()).toString());

    ui::chooseFile (chooser, append ? ko ("더할 샘플 고르기") : ko ("바꿀 샘플 고르기"), startFolder, ui::audioFilePatterns(),
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                        | juce::FileBrowserComponent::canSelectMultipleItems,
                    [this, append] (const juce::Array<juce::File>& files)
                    {
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

    ui::confirmAction (this, ko ("패드 삭제"),
                       padTitle.getText() + ko (" 패드를 킷에서 지울까요? 샘플 파일은 지우지 않습니다."), ko ("삭제"),
                       [this, id]
                       {
                           session.getKitDocument().removePad (id);
                           session.saveKit (true);
                       });
}

void KitPage::importKit()
{
    ui::chooseFile (chooser, ko ("가져올 킷(ZIP) 고르기"), juce::File(), "*.zip",
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this] (const juce::Array<juce::File>& files)
                    {
                        if (const auto error = session.importKit (files.getFirst()); error.isNotEmpty())
                            ui::showMessage (ko ("킷을 가져오지 못했습니다"), error);
                    });
}

void KitPage::importSfzKit()
{
    const juce::File startFolder (session.getSetting ("sfzFolder", juce::String()).toString());
    ui::chooseFile (chooser, ko ("가져올 SFZ 파일 고르기"), startFolder, "*.sfz",
                    juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [this] (const juce::Array<juce::File>& files)
                    {
                        const auto file = files.getFirst();
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

    ui::chooseFile (chooser, ko ("킷을 ZIP으로 내보내기"), suggested, "*.zip",
                    juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                        | juce::FileBrowserComponent::warnAboutOverwriting,
                    [this] (const juce::Array<juce::File>& files)
                    {
                        const auto file = files.getFirst();
                        const auto error = session.exportKit (file.withFileExtension ("zip"));

                        if (error.isNotEmpty())
                            ui::showMessage (ko ("킷을 내보내지 못했습니다"), error);
                        else
                            ui::showMessage (ko ("킷을 내보냈습니다"), file.withFileExtension ("zip").getFullPathName());
                    });
}
