#pragma once

#include <array>
#include <vector>

#include "KitView.h"
#include "MouseRouter.h"
#include "PadGrid.h"
#include "Session.h"
#include "TrackControls.h"
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

    /** 배치 편집을 켜고 끈다. 켜면 툴바가 편집 막대로 바뀐다. 옮긴 것은 Session이 마우스를 놓은 뒤에 저장한다. */
    void setEditing (bool shouldEdit);
    void updateToolbar();
    void updateTimingLabel();
    void confirmResetLayout();
    void resetLayout();
    void publishMouseTargets();

    /** 클릭으로 친다. view가 setView이면 spot은 그림의 자리 번호, 아니면 타일(패드) 번호다. */
    static constexpr int tilesView = 0, setView = 1;
    void playClick (const MouseRouter::Click& click);

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

    // 그림 아래 줄: 왼쪽에 반주, 오른쪽에 박자(메트로놈을 켜면)
    TrackStrip trackStrip { session };
    juce::Label timingLabel;

    std::vector<std::uint32_t> seenHitCounts, seenChokeCounts;
    std::vector<int> actingPads;    // 그림의 자리마다 지금 치면 울리는 패드 (tick마다 다시 채운다)
    std::vector<int> shownAs;       // 패드마다 그림에서 대신 번쩍일 패드 (킷 파일의 "showAs"). 그대로면 자기 번호
    std::vector<std::vector<std::uint16_t>> chokeSets;    // 패드마다 오른쪽 단추로 잡으면 함께 멈출 패드들
    int tickCount = 0;

    // 클릭은 보통 입력 스레드가 받아 이미 쳤다. 화면 쪽은 그것과 맞춰 보기만 한다.
    MouseRouter mouse { session, [this] (const MouseRouter::Click& click) { playClick (click); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayPage)
};
