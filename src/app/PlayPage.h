#pragma once

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

    void tick() override;
    void pageShown() override;
    void pageHidden() override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { rebuild(); }

    void rebuild();
    void setShowingSet (bool shouldShowSet);
    void updateLastKeyLabel();
    void updateTimingLabel();
    void resetLayout();
    void publishMouseTargets();

    /** 화면 스레드로 온 클릭. inSet이면 spot은 그림의 자리 번호, 아니면 타일(패드) 번호다. */
    struct UiClick
    {
        bool inSet = true;
        int spot = -1;
        bool right = false;
        double timeMs = 0.0;
    };

    void handleClick (bool inSet, int spot, bool right);
    void playClick (const UiClick& click);
    void reconcileClicks();

    Session& session;

    PadGrid padGrid;
    KitView kitView;
    bool showingSet = true;
    bool layoutDirty = false;

    juce::TextButton tilesButton, setButton, editButton, resetButton, labelsButton, silenceButton;
    juce::Label functionKeyLabel, lastKeyLabel, timingLabel;

    std::vector<std::uint32_t> seenHitCounts, seenChokeCounts;
    std::uint32_t seenKeyPressCount = 0;
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
