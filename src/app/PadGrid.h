#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

/** 패드를 타일로 늘어놓고, 방금 친 패드를 밝게 표시한다. */
class PadGrid final : public juce::Component
{
public:
    struct Tile
    {
        juce::String name;         // 패드 이름
        juce::StringArray keys;    // 이 패드를 치는 키들. 예: "J", "5(반복)"
        bool hasSound = true;      // 샘플을 하나도 못 읽은 패드는 흐리게 그린다
    };

    PadGrid();

    void setTiles (std::vector<Tile> newTiles);

    /** index번 패드를 strength(0 ~ 1) 밝기로 켠다. */
    void flash (int index, float strength);

    /** 켜진 패드를 조금씩 어둡게 한다. 화면을 다시 그려야 하면 true를 돌려준다. */
    bool fade();

    /** 패드를 마우스로 누르면 불린다. */
    std::function<void (int index)> onPadClicked;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> getTileBounds (int index) const;

    std::vector<Tile> tiles;
    std::vector<float> brightness;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadGrid)
};
