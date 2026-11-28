#include "DocumentBar.h"

namespace
{

constexpr int comboWidth = 200;
constexpr int duplicateWidth = 64;
constexpr int restoreWidth = 84;
constexpr int moreWidth = 36;

} // namespace

//==============================================================================
MoreButton::MoreButton()
{
    setTooltip (ko ("더 보기"));
}

void MoreButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::TextButton::paintButton (g, highlighted, down);

    const auto& t = ui::theme();
    const auto centre = getLocalBounds().toFloat().getCentre();
    constexpr float radius = 1.8f, gap = 6.0f;

    g.setColour (t.text.withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f));

    for (const auto offset : { -gap, 0.0f, gap })
        g.fillEllipse (centre.x + offset - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
}

//==============================================================================
DocumentBar::DocumentBar (Source sourceToUse)
    : source (std::move (sourceToUse))
{
    ui::setupCombo (*this, combo, [this]
    {
        if (! updating && combo.getSelectedItemIndex() >= 0 && source.select != nullptr)
            source.select (combo.getText());
    });

    ui::setupButton (*this, duplicateButton, ko ("복제"), [this] { duplicate(); });
    duplicateButton.setTooltip (ko ("지금 것을 복사해 새로 만듭니다"));

    // 파일이 바뀔 때마다 남겨 둔 이전 내용 가운데 하나로 되돌린다.
    ui::setupButton (*this, restoreButton, ko ("되돌리기"), [this]
    {
        if (source.backups == nullptr || source.file == nullptr)
            return;

        showBackupMenu (restoreButton, *source.backups, source.file(), [this] (juce::File backup)
        {
            if (source.restore == nullptr)
                return;

            if (const auto error = source.restore (backup); error.isNotEmpty())
                ui::showMessage (ko ("되돌리지 못했습니다"), error);
        });
    });
    restoreButton.setTooltip (ko ("고치기 전 상태로 되돌립니다"));

    ui::setupButton (*this, moreButton, {}, [this] { showMenu(); });

    // 버튼 종류: 드문 일(되돌리기, 메뉴)은 조용하게
    ui::setButtonKind (restoreButton, ui::ButtonKind::quiet);
    ui::setButtonKind (moreButton, ui::ButtonKind::quiet);

    refresh();
}

void DocumentBar::addMenuItem (const juce::String& text, std::function<void()> action, bool separatorBefore)
{
    menuItems.push_back ({ text, std::move (action), separatorBefore });
}

void DocumentBar::setInfo (const juce::String& text)
{
    combo.setTooltip (text);
}

void DocumentBar::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto names = source.names != nullptr ? source.names() : juce::StringArray();
    combo.clear (juce::dontSendNotification);
    combo.addItemList (names, 1);
    combo.setSelectedItemIndex (names.indexOf (source.current != nullptr ? source.current() : juce::String()), juce::dontSendNotification);
}

int DocumentBar::getIdealWidth() const noexcept
{
    return comboWidth + ui::space::small + duplicateWidth + ui::space::tight + restoreWidth + ui::space::tight + moreWidth;
}

void DocumentBar::resized()
{
    auto area = getLocalBounds();

    // 좁으면 목록이 줄어든다. 버튼은 그대로 둔다.
    const auto buttons = duplicateWidth + ui::space::tight + restoreWidth + ui::space::tight + moreWidth;
    combo.setBounds (area.removeFromLeft (juce::jlimit (120, comboWidth, area.getWidth() - ui::space::small - buttons)));
    area.removeFromLeft (ui::space::small);
    duplicateButton.setBounds (area.removeFromLeft (duplicateWidth));
    area.removeFromLeft (ui::space::tight);
    restoreButton.setBounds (area.removeFromLeft (restoreWidth));
    area.removeFromLeft (ui::space::tight);
    moreButton.setBounds (area.removeFromLeft (moreWidth));
}

//==============================================================================
void DocumentBar::duplicate()
{
    if (source.duplicate == nullptr)
        return;

    ui::promptForText (getParentComponent(), source.noun + ko (" 복제"),
                       ko ("지금 ") + source.noun + ko ("을 복사해 새로 만듭니다. 이름을 적으세요."),
                       source.copyName != nullptr ? source.copyName() : juce::String(),
                       [this] (juce::String name)
                       {
                           if (const auto error = source.duplicate (name); error.isNotEmpty())
                               ui::showMessage (source.noun + ko ("을 복제하지 못했습니다"), error);
                       });
}

void DocumentBar::rename()
{
    if (source.rename == nullptr)
        return;

    ui::promptForText (getParentComponent(), source.noun + ko (" 이름 바꾸기"), source.renameNote,
                       source.displayName != nullptr ? source.displayName() : juce::String(),
                       [this] (juce::String name)
                       {
                           if (name.trim().isNotEmpty())
                               source.rename (name.trim());
                       });
}

void DocumentBar::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem (ko ("이름 바꾸기"), [this] { rename(); });

    for (const auto& item : menuItems)
    {
        if (item.separatorBefore)
            menu.addSeparator();

        menu.addItem (item.text, item.action);
    }

    menu.addSeparator();
    menu.addItem (ko ("폴더 열기"), source.file != nullptr, false, [this]
    {
        source.file().revealToUser();
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton));
}

