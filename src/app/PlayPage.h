#pragma once

#include <array>
#include <vector>

#include "KitView.h"
#include "PadGrid.h"
#include "Session.h"
#include "Ui.h"

/** 연주 화면. 패드를 타일이나 드럼 세트 그림으로 보여 주고, 친 패드를 밝힌다. */
class PlayPage final : public ui::Page,
                       private juce::ChangeListener
{
public:
    explicit PlayPage (Session& sessionToUse);
    ~PlayPage() override;

    /** 다른 창(팝업 메뉴, 대화 상자 등)이 화면 좌표의 area를 가리거나 입력을 붙잡고 있는지. owner는 area를 그린 부품이다.
        그동안은 입력 스레드가 클릭을 받지 않는다. 툴팁처럼 클릭을 받지 않는 창은 셈하지 않는다. */
    static bool isCoveredByOtherWindow (const juce::Component& owner, juce::Rectangle<int> area);

    /** 부품에서 화면에 보이는 부분(화면 좌표). 부모와 창의 클라이언트 영역 밖으로 나간 부분은 뺀다. */
    static juce::Rectangle<int> visibleScreenArea (const juce::Component& component);

    void tick() override;
    void pageShown() override;
    void pageHidden() override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { rebuild(); }

    void rebuild();
    void setShowingSet (bool shouldShowSet);

    /** 배치 편집을 켜고 끈다. 켜면 툴바가 편집 막대로 바뀐다. 옮긴 것은 Session이 마우스를 놓은 뒤에 저장한다. */
    void setEditing (bool shouldEdit);
    void updateToolbar();
    void updateTimingLabel();
    void confirmResetLayout();
    void resetLayout();
    void publishMouseTargets();

    /** 화면 스레드로 온 클릭. inSet이면 spot은 그림의 자리 번호, 아니면 타일(패드) 번호다. */
    struct UiClick
    {
        bool inSet = true;
        int spot = -1;
        bool right = false;
        double timeMs = 0.0;       // 화면 스레드가 받은 때 (juce::Time::getMillisecondCounterHiRes)
        double pressedMs = 0.0;    // 누른 때의 어림. 화면 스레드가 바빴으면 timeMs보다 한참 이르다
    };

    void handleClick (bool inSet, int spot, bool right);
    void playClick (const UiClick& click);
    void reconcileClicks();

    Session& session;

    PadGrid padGrid;
    KitView kitView;
    bool showingSet = true;

    // 툴바: [타일 | 드럼 세트] [이름 · 키] ... [배치 편집] [소리 끄기]
    std::array<juce::TextButton, 2> viewButtons;    // 타일, 드럼 세트
    juce::TextButton editButton, silenceButton;
    juce::ToggleButton labelsToggle;

    // 배치 편집 중의 툴바: 배치 편집 · 끌기 이동 · 휠 크기 ... [기본 배치로] [완료]
    juce::Label editTitle, editHint;
    juce::TextButton resetButton, doneButton;

    juce::Label timingLabel;    // 그림 아래 줄 오른쪽. 메트로놈을 켜면 박자를 보인다

    std::vector<std::uint32_t> seenHitCounts, seenChokeCounts;
    std::vector<int> actingPads;    // 그림의 자리마다 지금 치면 울리는 패드 (tick마다 다시 채운다)
    std::vector<int> shownAs;       // 패드마다 그림에서 대신 번쩍일 패드 (킷 파일의 "showAs"). 그대로면 자기 번호
    std::vector<std::vector<std::uint16_t>> chokeSets;    // 패드마다 오른쪽 단추로 잡으면 함께 멈출 패드들
    std::shared_ptr<const hvd::MouseTargets> publishedTargets;
    std::vector<UiClick> uiClicks;                  // 입력 스레드가 친 것과 아직 맞춰 보지 않은 클릭
    std::vector<Session::RawClick> rawClicks;       // 입력 스레드가 친 클릭 가운데 아직 짝을 찾지 못한 것
    int failedClicks = 0;                           // 입력 스레드가 놓쳤거나 다른 자리를 친 클릭이 이어진 수
    int tickCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayPage)
};
