#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

/** 드럼 세트를 드러머 자리에서 내려다본 모습으로 그린다. 북은 쉘과 테와 헤드가 있는 원통으로, 심벌은 황동 원판으로 그리고,
    치면 헤드가 번쩍이고 심벌이 흔들린다. 배치 편집을 켜면 끌어서 옮길 수 있다. */
class KitView final : public juce::Component
{
public:
    struct Item
    {
        juce::String id, name;
        juce::StringArray keys;    // 이 패드를 치는 키들
        float x = 0.5f;            // 가운데 위치. 0(왼쪽) ~ 1(오른쪽)
        float y = 0.5f;            // 0(멀리, 위) ~ 1(가까이, 아래)
        float size = 0.2f;         // 지름. 화면 높이에 대한 비율
        bool hasSound = true;
        int pad = -1;              // 치는 패드의 킷 안 순서
        bool fixed = false;        // 같은 패드의 두 번째 자리. 페달을 따르지 않고 작은 원으로 그린다
    };

    KitView();

    void setItems (std::vector<Item> newItems);
    const std::vector<Item>& getItems() const noexcept    { return items; }

    /** 이 패드의 자리를 번쩍인다. spot(자리 번호)을 주면 그 자리를, 아니면 지금 이 패드를 치고 있는 자리를. */
    void flash (int pad, float strength, int spot = -1);

    /** 이 패드의 자리들이 흔들리고 번쩍이던 것을 멈춘다 (손으로 심벌을 잡았다). */
    void damp (int pad);

    /** 마우스로 칠 수 있는 자리들의 테두리(이 부품 안의 좌표). 겹치면 앞의 것을 친다. 배치를 고치는 동안에는 비어 있다. */
    struct HitArea
    {
        int index = -1;                 // items의 번호
        juce::Rectangle<float> bounds;  // 이 안에 꽉 찬 타원
    };

    std::vector<HitArea> getHitAreas() const;

    /** 자리마다 지금 치면 실제로 울리는 패드 (items와 같은 순서). 페달로 바뀐 자리는 그 패드의 이름을 적고,
        하이햇은 바뀐 패드에 따라 위 심벌이 올라간다 (반열림, 열림). */
    void setActingPads (const std::vector<int>& pads);

    /** 번쩍임과 흔들림을 한 단계 진행한다. 다시 그려야 하면 true를 돌려준다. */
    bool fade();

    void setEditing (bool shouldEdit);
    bool isEditing() const noexcept                       { return editing; }

    /** 이름과 키를 그림 위에 적을지. */
    void setShowingLabels (bool shouldShow);
    bool isShowingLabels() const noexcept                 { return showLabels; }

    /** 패드 id로 고르는 기본 자리. index는 킷 안에서의 순서다. */
    static void defaultPlacement (const juce::String& padId, int index, float& x, float& y, float& size);

    std::function<void (int index)> onPadClicked;

    /** 오른쪽 단추로 눌렀을 때 (심벌 잡기). */
    std::function<void (int index)> onPadChoked;

    /** 배치 편집에서 패드를 옮기거나 크기를 바꾼 뒤에 불린다. */
    std::function<void (int index)> onItemChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** 패드가 어떤 악기 모양으로 그려지는지. */
    enum class Kind { kick, snare, tom, floorTom, hihat, crash, china, ride, bell, pedal, zone };

    static Kind kindOf (const juce::String& padId);

private:
    /** 치고 난 뒤의 움직임. */
    struct Motion
    {
        float level = 0.0f;       // 번쩍임 0 ~ 1
        float strength = 0.0f;    // 친 세기
        float age = 100.0f;       // 친 뒤로 흐른 시간(초)
        float openness = 0.0f;    // 하이햇이 열린 정도 0(닫힘) ~ 1(열림). 목표를 향해 빠르게 따라간다
        float targetOpenness = 0.0f;
    };

    /** 이 패드의 원래 자리(두 번째 자리가 아닌 것). 없으면 -1. */
    int itemOfPad (int pad) const;
    const juce::String& shownName (int index) const;

    juce::Point<float> centreOf (const Item& item) const;
    float radiusOf (const Item& item) const;
    int itemAt (juce::Point<float> position) const;
    void drawFloor (juce::Graphics&) const;
    void drawItem (juce::Graphics&, int index, int layer) const;
    void drawLabel (juce::Graphics&, int index) const;

    std::vector<Item> items;
    std::vector<Kind> kinds;
    std::vector<Motion> motions;
    std::vector<int> acting;       // 페달로 바뀌어 대신 치는 패드. 바뀌지 않았으면 -1
    std::vector<int> drawOrder;    // 멀리 있는 것부터
    juce::Image floorImage;        // 바닥은 크기가 바뀔 때만 다시 그린다
    bool editing = false;
    bool showLabels = true;
    int dragged = -1;
    juce::Point<float> dragOffset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitView)
};
