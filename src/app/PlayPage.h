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
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override    { rebuild(); }

    void rebuild();
    void setShowingSet (bool shouldShowSet);
    void updateLastKeyLabel();
    void updateTimingLabel();
    void resetLayout();

    Session& session;

    PadGrid padGrid;
    KitView kitView;
    bool showingSet = true;
    bool layoutDirty = false;

    juce::TextButton tilesButton, setButton, editButton, resetButton, labelsButton, silenceButton;
    juce::Label functionKeyLabel, lastKeyLabel, timingLabel;

    std::vector<std::uint32_t> seenHitCounts;
    std::uint32_t seenKeyPressCount = 0;
    int fixedSpotPad = -1;          // 방금 두 번째 자리(닫힘 고정)를 클릭한 패드. 그 타격은 그 자리를 번쩍인다
    std::vector<int> actingPads;    // 그림의 자리마다 지금 치면 울리는 패드 (tick마다 다시 채운다)
    int tickCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayPage)
};
