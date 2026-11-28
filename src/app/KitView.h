#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "KitArt.h"

/** 드럼 세트를 드러머 자리에서 내려다본 모습으로 그린다. 북은 쉘과 테와 헤드가 있는 원통으로, 심벌은 황동 원판으로 그리고,
    치면 헤드가 번쩍이고 심벌이 흔들린다. 배치 편집을 켜면 끌어서 옮길 수 있다. 악기 그림 자체는 KitArt가 그린다. */
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

    /** 번쩍임과 흔들림을 한 단계(1/60초) 진행하고, 움직인 자리만 다시 그린다. 아직 움직이는 것이 있으면 true. */
    bool fade();

    void setEditing (bool shouldEdit);
    bool isEditing() const noexcept                       { return editing; }

    /** 이름과 키를 그림 위에 적을지. */
    void setShowingLabels (bool shouldShow);
    bool isShowingLabels() const noexcept                 { return showLabels; }

    /** 패드 id로 고르는 기본 자리. index는 킷 안에서의 순서다. */
    static void defaultPlacement (const juce::String& padId, int index, float& x, float& y, float& size);

    /** 지름(화면 높이에 대한 비율)의 한도. */
    static constexpr float minSize = 0.06f, maxSize = 0.60f;
    static float clampSize (float size) noexcept          { return juce::jlimit (minSize, maxSize, size); }

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
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** 패드가 어떤 악기 모양으로 그려지는지. */
    using Kind = kitart::Kind;

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
        bool wasMoving = false;          // 지난 단계에 움직였다 (멈춘 뒤 한 번 더 그린다)
        bool stickWasVisible = false;    // 지난 단계에 스틱이 보였다
    };

    /** 자리 하나의 이름과 키 캡을 놓을 곳. */
    struct Label
    {
        juce::String name;                    // 페달로 바뀐 자리는 바뀐 패드의 이름
        juce::Rectangle<float> nameArea;      // drawName에 넘기는 칸
        juce::Rectangle<float> nameBounds;    // 글자가 실제로 차지하는 곳
        float nameSize = 0.0f;
        juce::Colour nameColour, nameHalo;
        juce::Point<float> capsCentre;
        juce::Rectangle<float> capsBounds;
        bool nameOutside = false;             // 이름을 악기 밖(위)에 적는다 (작은 심벌, 벨, 작은 패드)
        bool showName = true;                 // 다른 이름이나 키, 다른 악기와 겹치면 이름을 빼고 키만 보인다

        bool looksSame (const Label& other) const
        {
            return name == other.name && showName == other.showName && nameBounds == other.nameBounds && capsBounds == other.capsBounds;
        }
    };

    /** 치는 순간의 스틱: tip은 닿는 곳, hand는 손이 있는 쪽(화면 아래). */
    struct Stick
    {
        juce::Point<float> tip, hand;
        float length = 0.0f, thickness = 0.0f;
    };

    /** 이 패드의 원래 자리(두 번째 자리가 아닌 것). 없으면 -1. */
    int itemOfPad (int pad) const;
    const juce::String& shownName (int index) const;

    Label layoutLabel (int index) const;
    void layoutLabels();

    /** 이름이 바뀐 뒤 (페달) 이름표를 다시 놓고, 달라진 이름표 자리만 다시 그린다. */
    void refreshLabels();
    void setHovered (int index);

    juce::Point<float> centreOf (const Item& item) const;
    float radiusOf (const Item& item) const;

    /** 클릭을 받는 타원의 테두리: 가로 반지름 radius, 세로 radius * 0.8. */
    juce::Rectangle<float> hitEllipseOf (const Item& item) const;
    int itemAt (juce::Point<float> position) const;

    /** 이 자리의 악기 몸통(층 2 ~ 6)이 그려질 수 있는 곳. 번쩍임, 흔들림, 하이햇 열림을 모두 덮는다. */
    juce::Rectangle<float> bodyBoundsOf (int index) const;

    /** progress(0 ~ 1)만큼 튀어 오른 스틱. */
    Stick stickOf (int index, float progress) const;

    /** 스틱이 보이는 동안 지나가는 곳 전체. 스틱을 그리지 않는 악기(킥, 페달)는 빈 네모. */
    juce::Rectangle<float> stickBoundsOf (int index) const;
    bool isStickVisible (int index) const;

    /** 이 부분만 다시 그린다. */
    void repaintArea (juce::Rectangle<float> area);

    void drawItem (juce::Graphics&, int index, int layer) const;
    void drawLabel (juce::Graphics&, int index) const;

    //==============================================================================
    // 그려 둔 그림. 화면의 실제 화소 크기로 그려 흐려지지 않게 하고, 크기나 화면 배율이 바뀌면 다시 그린다.
    void prepareImages (float scale);
    void renderStage();
    void renderLabels();
    void drawImage (juce::Graphics&, const juce::Image&) const;

    std::vector<Item> items;
    std::vector<Kind> kinds;
    std::vector<Motion> motions;
    std::vector<int> acting;       // 페달로 바뀌어 대신 치는 패드. 바뀌지 않았으면 -1
    std::vector<int> drawOrder;    // 멀리 있는 것부터
    std::vector<Label> labels;     // items와 같은 순서
    bool labelsDirty = true;       // 자리, 크기, 이름이 바뀌면 다시 놓는다

    float imageScale = 0.0f;       // 아래 그림들을 그린 화면 배율 (논리 1px에 실제 화소 몇 개)
    juce::Image floorImage;        // 바닥과 러그. 크기가 바뀔 때만
    juce::Image stageImage;        // 바닥 + 모든 악기의 그림자와 스탠드. 자리가 바뀔 때도
    juce::Image labelImage;        // 이름과 키 캡. 이름이 바뀔 때도
    bool stageDirty = true, labelImageDirty = true;
    juce::Path corners;            // 둥근 모서리 바깥. 창 바탕색으로 덮는다

    bool editing = false;
    bool showLabels = true;
    int dragged = -1;
    int hovered = -1;              // 마우스가 올라가 있는 자리. 이름을 뺀 자리는 이때 이름을 보인다
    juce::Point<float> dragOffset;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitView)
};
