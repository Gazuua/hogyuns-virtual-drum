#pragma once

#include <functional>
#include <vector>

#include "Backups.h"
#include "Ui.h"

/** ⋯ 모양의 버튼: 점 셋을 직접 그린다 (글꼴에 없는 글자를 피한다). */
class MoreButton final : public juce::TextButton
{
public:
    MoreButton();
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

//==============================================================================
/** 킷·키 배열 화면 윗줄 왼쪽의 파일 부품: [목록] [복제] [되돌리기] [⋯].
    ⋯ 메뉴에는 이름 바꾸기, 화면이 더한 항목, 폴더 열기가 차례로 들어간다.
    Session을 모르고 Source의 함수로만 일한다. 화면은 이 부품 오른쪽에 제 것을 둔다. */
class DocumentBar final : public juce::Component
{
public:
    struct Source
    {
        juce::String noun;                                              // "킷", "키 배열": 창 제목과 알림에 쓴다
        std::function<juce::StringArray()> names;                       // 목록에 보일 이름들
        std::function<juce::String()> current;                          // 지금 고른 것 (목록의 이름)
        std::function<void (const juce::String&)> select;

        std::function<juce::String()> copyName;                         // 복제 창의 처음 이름
        std::function<juce::String (const juce::String&)> duplicate;    // 실패하면 이유

        std::function<juce::String()> displayName;                      // 이름 바꾸기 창의 처음 이름
        std::function<void (const juce::String&)> rename;               // 앞뒤 빈칸을 뗀, 비어 있지 않은 이름
        juce::String renameNote;                                        // 이름 바꾸기 창의 설명

        std::function<juce::File()> file;                               // 되돌리기와 폴더 열기의 대상
        std::function<juce::String (const juce::File&)> restore;        // 실패하면 이유
        const FileBackups* backups = nullptr;
    };

    explicit DocumentBar (Source);

    /** ⋯ 메뉴에 항목을 더한다. 이름 바꾸기와 폴더 열기 사이에 더한 차례대로 들어간다. */
    void addMenuItem (const juce::String& text, std::function<void()> action, bool separatorBefore = false);

    /** 목록에 마우스를 올렸을 때 보일 글 (킷의 샘플 수 등). */
    void setInfo (const juce::String& text);

    /** 목록을 다시 채운다. Session이 바뀌었다고 알릴 때 부른다. */
    void refresh();

    /** 부품을 모두 놓는 데 드는 폭. */
    int getIdealWidth() const noexcept;

    void resized() override;

private:
    void duplicate();
    void rename();
    void showMenu();

    struct MenuItem
    {
        juce::String text;
        std::function<void()> action;
        bool separatorBefore = false;
    };

    Source source;
    std::vector<MenuItem> menuItems;
    bool updating = false;

    juce::ComboBox combo;
    juce::TextButton duplicateButton, restoreButton;
    MoreButton moreButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DocumentBar)
};

//==============================================================================
namespace ui
{

/** 확인을 받는다. 버튼은 동작 이름(예: "삭제")과 "취소". 동작 버튼을 누르면 onConfirm이 불린다.
    (Ui.h의 confirm은 "예/아니오"라 따로 둔다. Ui.h를 고칠 때 합친다.) */
void confirmAction (juce::Component* parent, const juce::String& title, const juce::String& message,
                    const juce::String& actionText, std::function<void()> onConfirm);

} // namespace ui
