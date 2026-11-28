#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

#include "GameRunner.h"

/** 리듬 게임의 연주 화면. 노트가 위에서 내려오고, 판정선에 닿을 때 그 레인의 악기를 치면 된다.
    오른쪽에는 점수와 콤보, 판정 수를 보여 주고, 한 판이 끝나면 결과를 덮어 그린다. */
class GameHighway final : public juce::Component
{
public:
    struct LaneInfo
    {
        hvd::Lane lane = hvd::Lane::snare;
        juce::String name;
        juce::StringArray keys;    // 이 레인의 악기를 치는 키들
        juce::Colour colour;
    };

    GameHighway();

    /** columns는 화면에 왼쪽부터 놓을 레인들(킥 제외), kick은 가로 막대로 그릴 킥이다. */
    void setLanes (std::vector<LaneInfo> columns, LaneInfo kick);
    void setRunner (const GameRunner* runnerToShow)       { runner = runnerToShow; }
    void setScrollSpeed (float pixelsPerSecond)           { scrollSpeed = pixelsPerSecond; }
    void setInfo (const juce::String& title, const juce::String& detail);

    /** 한 판을 새로 시작할 때 화면의 효과를 지운다. */
    void reset();

    void addFeedback (const GameRunner::Feedback& item);

    /** 화면 타이머가 부른다. 효과를 한 단계 진행한다. */
    void advance();

    /** 결과 화면에서 버튼을 놓을 자리. */
    juce::Rectangle<int> getResultButtonArea() const;

    /** 마우스로 치는 자리: 레인마다 판정선 둘레부터 이름 칸까지, 킥은 아래 막대. */
    struct ClickArea
    {
        juce::Rectangle<float> bounds;
        hvd::Lane lane = hvd::Lane::snare;
    };

    std::vector<ClickArea> getClickAreas() const;

    /** 한 판이 진행 중일 때 자리를 누르면 그 번호(getClickAreas의 순서)와 오른쪽 단추(멈춤)인지를 알린다. */
    std::function<void (int index, bool right)> onLaneClicked;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct Popup
    {
        hvd::Judgement judgement = hvd::Judgement::pending;
        float offsetMs = 0.0f;
        float age = 1.0f;    // 0에서 시작해 1이 되면 사라진다
    };

    struct Layout
    {
        juce::Rectangle<float> field, lanes, hud;
        float laneWidth = 0.0f, hitY = 0.0f, top = 0.0f;
    };

    Layout getLayout() const;
    int columnOf (hvd::Lane lane) const noexcept;
    void paintField (juce::Graphics&, const Layout&);
    void paintHud (juce::Graphics&, const Layout&);
    void paintResult (juce::Graphics&, const Layout&);

    const GameRunner* runner = nullptr;
    std::vector<LaneInfo> lanes;
    LaneInfo kickLane;
    float scrollSpeed = 420.0f;
    juce::String infoTitle, infoDetail;

    std::array<float, hvd::numLanes> flashes {};
    std::array<Popup, hvd::numLanes> popups {};
    Popup missPopup;
    float comboPulse = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GameHighway)
};
