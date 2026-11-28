#include "KitView.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "Ui.h"

namespace
{

using Point = juce::Point<float>;

struct Placement
{
    const char* id;
    float x, y, size;
};

// 드러머 자리에서 내려다본 배치: 왼쪽에 하이햇, 가운데 아래에 스네어와 킥, 위에 탐과 크래시, 오른쪽에 라이드와 플로어 탐.
constexpr Placement placements[] = {
    { "kick", 0.50f, 0.62f, 0.34f },
    { "snare", 0.305f, 0.70f, 0.27f },
    { "snare_rim", 0.215f, 0.90f, 0.12f },
    { "sidestick", 0.375f, 0.925f, 0.12f },
    { "hihat_closed", 0.125f, 0.56f, 0.27f },
    { "hihat_half", 0.045f, 0.36f, 0.12f },
    { "hihat_open", 0.135f, 0.32f, 0.12f },
    { "hihat_pedal", 0.06f, 0.83f, 0.13f },
    { "tom_high", 0.405f, 0.31f, 0.22f },
    { "tom_mid", 0.575f, 0.30f, 0.24f },
    { "tom_floor", 0.70f, 0.65f, 0.26f },
    { "tom_low", 0.86f, 0.71f, 0.26f },
    { "crash1", 0.25f, 0.20f, 0.31f },
    { "crash2", 0.735f, 0.19f, 0.31f },
    { "china", 0.94f, 0.20f, 0.20f },
    { "ride", 0.855f, 0.40f, 0.32f },
    { "ride_bell", 0.855f, 0.395f, 0.10f },
    { "ride_edge", 0.965f, 0.565f, 0.11f },
    { "crash_bright", 0.40f, 0.085f, 0.13f },
    { "crash_trash", 0.49f, 0.075f, 0.13f },
    { "crash_stack", 0.58f, 0.085f, 0.13f },
};

constexpr float hitAspect = 0.8f;    // 클릭을 받는 타원의 세로 반지름 / 가로 반지름
constexpr float keyCapHeight = 18.0f;    // 키 캡의 높이. 모든 자리에서 같다 (글자 약 12px)
constexpr float editBorder = 2.0f;       // 배치 편집 중 그림 테두리의 굵기

/** 악기 몸통을 그리는 층. 0은 그림자, 1은 스탠드이고, 그 위로 발판, 킥, 북, 심벌, 작은 패드 순서다. */
int bodyLayerOf (KitView::Kind kind)
{
    using Kind = KitView::Kind;

    return kind == Kind::pedal ? 2
         : kind == Kind::kick ? 3
         : kitart::isDrumKind (kind) ? 4
         : kitart::isCymbalKind (kind) ? 5
                                       : 6;
}

} // namespace

//==============================================================================
KitView::KitView()
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

KitView::Kind KitView::kindOf (const juce::String& padId)
{
    const auto id = padId.toLowerCase();

    if (id.startsWith ("kick") || id.startsWith ("bass"))
        return Kind::kick;

    if (id == "snare")
        return Kind::snare;

    if (id.startsWith ("tom"))
        return id.contains ("floor") || id.contains ("low") ? Kind::floorTom : Kind::tom;

    if (id == "hihat" || id == "hihat_closed" || id == "hat")
        return Kind::hihat;

    if (id.startsWith ("hihat") && id.contains ("pedal"))
        return Kind::pedal;

    if (id == "ride")
        return Kind::ride;

    if (id.startsWith ("ride") && id.contains ("bell"))
        return Kind::bell;

    if (id.startsWith ("china"))
        return Kind::china;

    if (id.startsWith ("crash") || id.startsWith ("splash") || id.startsWith ("cymbal"))
        return Kind::crash;

    return Kind::zone;
}

void KitView::defaultPlacement (const juce::String& padId, int index, float& x, float& y, float& size)
{
    for (const auto& placement : placements)
    {
        if (padId == placement.id)
        {
            x = placement.x;
            y = placement.y;
            size = placement.size;
            return;
        }
    }

    // 모르는 패드는 아래쪽에 작게 늘어놓는다. 배치 편집에서 원하는 곳으로 옮기면 된다.
    x = 0.06f + 0.075f * static_cast<float> (index % 12);
    y = 0.95f - 0.11f * static_cast<float> ((index / 12) % 3);
    size = 0.11f;
}

void KitView::setItems (std::vector<Item> newItems)
{
    items = std::move (newItems);
    motions.assign (items.size(), {});
    acting.assign (items.size(), -1);
    kinds.clear();

    for (const auto& item : items)
        kinds.push_back (item.fixed ? Kind::zone : kindOf (item.id));

    // 멀리 있는(위쪽) 것부터 그려 가까운 것이 위에 오게 한다.
    drawOrder.resize (items.size());
    std::iota (drawOrder.begin(), drawOrder.end(), 0);
    std::stable_sort (drawOrder.begin(), drawOrder.end(),
                      [this] (int a, int b) { return items[static_cast<std::size_t> (a)].y < items[static_cast<std::size_t> (b)].y; });

    dragged = -1;
    hovered = -1;
    labelsDirty = true;
    repaint();
}

void KitView::flash (int pad, float strength, int spot)
{
    // 번쩍일 자리를 차례로 찾는다:
    // 1. 그 자리를 클릭한 타격이면 그 자리 (하이햇 닫힘 고정)
    // 2. 페달로 바뀌어 이 패드를 대신 치고 있는 자리 (페달을 뗀 하이햇은 반열림을 친다)
    // 3. 다른 패드로 바뀌지 않은 이 패드의 원래 자리
    // 4. 두 번째 자리 (원래 자리가 다른 패드를 대신 치고 있을 때. 예: 오토 모드가 친 닫힌 하이햇)
    // 5. 이 패드의 모든 자리
    std::vector<std::size_t> targets;

    const auto collect = [this, &targets] (auto&& wanted)
    {
        for (std::size_t i = 0; i < items.size(); ++i)
            if (wanted (items[i], acting[i]))
                targets.push_back (i);
    };

    if (juce::isPositiveAndBelow (spot, static_cast<int> (items.size())) && items[static_cast<std::size_t> (spot)].pad == pad)
        targets.push_back (static_cast<std::size_t> (spot));

    if (targets.empty())
        collect ([pad] (const Item& item, int actingPad) { return ! item.fixed && actingPad == pad; });

    if (targets.empty())
        collect ([pad] (const Item& item, int actingPad) { return ! item.fixed && item.pad == pad && actingPad < 0; });

    if (targets.empty())
        collect ([pad] (const Item& item, int) { return item.fixed && item.pad == pad; });

    if (targets.empty())
        collect ([pad] (const Item& item, int) { return item.pad == pad; });

    for (const auto i : targets)
    {
        auto& motion = motions[i];
        motion.level = juce::jlimit (0.0f, 1.0f, strength);
        motion.strength = motion.level;
        motion.age = 0.0f;
    }
}

void KitView::damp (int pad)
{
    auto changed = false;

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].pad != pad && acting[i] != pad)
            continue;

        auto& motion = motions[i];

        if (motion.age < 4.0f || motion.level > 0.0f)
        {
            motion.level = 0.0f;
            motion.strength = 0.0f;
            motion.age = 100.0f;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

std::vector<KitView::HitArea> KitView::getHitAreas() const
{
    std::vector<HitArea> areas;

    if (editing)
        return areas;

    // itemAt()과 같은 모양과 순서: 작은 것이 먼저다.
    for (std::size_t i = 0; i < items.size(); ++i)
        areas.push_back ({ static_cast<int> (i), hitEllipseOf (items[i]) });

    std::stable_sort (areas.begin(), areas.end(), [this] (const HitArea& a, const HitArea& b)
    {
        return items[static_cast<std::size_t> (a.index)].size < items[static_cast<std::size_t> (b.index)].size;
    });

    return areas;
}

void KitView::setActingPads (const std::vector<int>& pads)
{
    // 하이햇이 열린 정도: 패드 id로 짐작한다.
    const auto opennessOf = [] (const juce::String& padId)
    {
        const auto id = padId.toLowerCase();

        if (! id.startsWith ("hihat") && ! id.startsWith ("hat"))
            return 0.0f;

        if (id.contains ("half"))
            return 0.45f;

        return id.contains ("open") ? 1.0f : 0.0f;
    };

    auto changed = false;

    for (std::size_t i = 0; i < items.size() && i < pads.size(); ++i)
    {
        const auto wanted = items[i].fixed || pads[i] == items[i].pad ? -1 : pads[i];

        if (acting[i] == wanted)
            continue;

        acting[i] = wanted;
        changed = true;

        const auto shown = itemOfPad (wanted);
        motions[i].targetOpenness = kinds[i] == Kind::hihat && shown >= 0 ? opennessOf (items[static_cast<std::size_t> (shown)].id) : 0.0f;
    }

    if (changed)
    {
        labelsDirty = true;    // 이름이 바뀐다
        repaint();
    }
}

int KitView::itemOfPad (int pad) const
{
    if (pad < 0)
        return -1;

    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].pad == pad && ! items[i].fixed)
            return static_cast<int> (i);

    return -1;
}

const juce::String& KitView::shownName (int index) const
{
    const auto& item = items[static_cast<std::size_t> (index)];

    if (const auto shown = itemOfPad (acting[static_cast<std::size_t> (index)]); shown >= 0)
        return items[static_cast<std::size_t> (shown)].name;

    return item.name;
}

bool KitView::fade()
{
    auto changed = false;

    for (std::size_t i = 0; i < motions.size(); ++i)
    {
        auto& motion = motions[i];

        // 하이햇은 페달에 따라 빠르게 열리고 닫힌다.
        if (const auto gap = motion.targetOpenness - motion.openness; std::abs (gap) > 0.003f)
        {
            motion.openness += gap * 0.45f;
            changed = true;
        }
        else
        {
            motion.openness = motion.targetOpenness;
        }

        if (motion.age > 4.0f)
            continue;

        motion.age += 1.0f / 60.0f;
        motion.level = motion.level < 0.02f ? 0.0f : motion.level * 0.88f;

        // 심벌은 번쩍임이 사라진 뒤에도 한동안 흔들린다.
        const auto swinging = kitart::isCymbalKind (kinds[i]) && motion.age < 3.0f;
        changed = changed || motion.level > 0.0f || swinging || motion.age < 0.35f;
    }

    return changed;
}

void KitView::setEditing (bool shouldEdit)
{
    editing = shouldEdit;
    dragged = -1;
    setMouseCursor (editing ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void KitView::setShowingLabels (bool shouldShow)
{
    showLabels = shouldShow;
    repaint();
}

juce::Point<float> KitView::centreOf (const Item& item) const
{
    return { item.x * static_cast<float> (getWidth()), item.y * static_cast<float> (getHeight()) };
}

float KitView::radiusOf (const Item& item) const
{
    return juce::jmax (12.0f, item.size * static_cast<float> (getHeight()) * 0.5f);
}

juce::Rectangle<float> KitView::hitEllipseOf (const Item& item) const
{
    const auto centre = centreOf (item);
    const auto radius = radiusOf (item);
    return { centre.x - radius, centre.y - radius * hitAspect, radius * 2.0f, radius * 2.0f * hitAspect };
}

int KitView::itemAt (juce::Point<float> position) const
{
    // hitEllipseOf()의 타원 안에서, 작은 것(다른 악기 위에 얹힌 패드)부터 찾는다.
    auto best = -1;

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        const auto centre = centreOf (item);
        const auto radius = radiusOf (item);
        const auto dx = (position.x - centre.x) / radius;
        const auto dy = (position.y - centre.y) / (radius * hitAspect);

        if (dx * dx + dy * dy <= 1.0f && (best < 0 || item.size < items[static_cast<std::size_t> (best)].size))
            best = static_cast<int> (i);
    }

    return best;
}

void KitView::resized()
{
    floorImage = {};
    labelsDirty = true;
}

//==============================================================================
void KitView::drawItem (juce::Graphics& g, int index, int layer) const
{
    const auto& item = items[static_cast<std::size_t> (index)];
    const auto& motion = motions[static_cast<std::size_t> (index)];
    const auto kind = kinds[static_cast<std::size_t> (index)];
    const auto centre = centreOf (item);
    const auto radius = radiusOf (item);
    const auto colour = ui::padColour (item.id);

    // 심벌이 달린 방향: 왼쪽 것은 오른쪽으로, 오른쪽 것은 왼쪽으로 조금 기울어 있다.
    const auto lean = kind == Kind::hihat ? 0.0f : juce::jlimit (-0.22f, 0.22f, (0.5f - item.x) * 0.5f);

    if (layer == 0)
    {
        kitart::drawShadowOf (g, kind, centre, radius);
        return;
    }

    if (layer == 1)
    {
        kitart::drawStandOf (g, kind, centre, radius);
        return;
    }

    if (layer != bodyLayerOf (kind))
        return;

    if (! item.hasSound)
        g.beginTransparencyLayer (0.4f);

    switch (kind)
    {
        case Kind::kick:
            kitart::drawKick (g, centre, radius, colour, motion.level, motion.age);
            break;

        case Kind::snare:
        case Kind::tom:
        case Kind::floorTom:
            kitart::drawDrum (g, centre.translated (0.0f, motion.level * radius * 0.03f), radius, kitart::drumLookOf (kind), colour, motion.level, motion.age);
            break;

        case Kind::hihat:
        {
            const auto look = kitart::lookOf (kind);
            const auto wobble = kitart::swing (motion.strength, motion.age, look.swingHertz, look.swingDecay);
            kitart::drawHiHat (g, centre, radius, lean, wobble, motion.openness, colour, motion.level);
            break;
        }

        case Kind::crash:
        case Kind::china:
        case Kind::ride:
        {
            const auto look = kitart::lookOf (kind);
            const auto size = juce::jlimit (0.55f, 1.0f, radius / 70.0f);    // 작은 심벌은 빨리, 조금만 흔들린다
            const auto wobble = kitart::swing (motion.strength, motion.age, look.swingHertz / size, look.swingDecay * size);
            const auto shimmer = kitart::swing (motion.strength, motion.age, look.swingHertz / size, look.swingDecay * size,
                                                juce::MathConstants<float>::halfPi);
            kitart::drawCymbalDisc (g, centre, radius, look, lean, wobble, shimmer, colour, motion.level);
            break;
        }

        case Kind::bell:
            // 라이드의 벨: 평소에는 라이드 그림의 일부이고, 치면 벨만 빛난다.
            kitart::drawBellFlash (g, centre, radius, colour, motion.level);
            break;

        case Kind::pedal:
            kitart::drawFootboard (g, centre, radius, colour, motion.level);
            break;

        case Kind::zone:
            kitart::drawZonePad (g, centre, radius, colour, motion.level);
            break;
    }

    if (! item.hasSound)
        g.endTransparencyLayer();
}

KitView::Label KitView::layoutLabel (int index) const
{
    const auto& item = items[static_cast<std::size_t> (index)];
    const auto kind = kinds[static_cast<std::size_t> (index)];
    const auto centre = centreOf (item);
    const auto radius = radiusOf (item);
    const auto alpha = item.hasSound ? 1.0f : 0.5f;

    const auto nameSize = juce::jlimit (11.0f, 15.0f, 9.5f + radius * 0.07f);
    const auto lightHalo = juce::Colours::white.withAlpha (0.55f * alpha);
    const auto darkHalo = juce::Colours::black.withAlpha (0.75f * alpha);
    const auto brassText = juce::Colour (0xff2e2208).withAlpha (alpha);
    const auto brassHalo = juce::Colour (0xfffff0c0).withAlpha (0.5f * alpha);
    const auto& stage = ui::theme().stage;
    const auto floorText = stage.nameText.withMultipliedAlpha (alpha);
    const auto floorHalo = stage.nameHalo.withMultipliedAlpha (alpha);

    Label label;

    const auto setName = [&label] (juce::Rectangle<float> area, float size, juce::Colour colour, juce::Colour halo)
    {
        label.nameArea = area;
        label.nameSize = size;
        label.nameColour = colour;
        label.nameHalo = halo;
    };

    switch (kind)
    {
        case Kind::kick:
            // 드러머를 향한 헤드의 위쪽에 이름, 페달 옆에 키
            setName (juce::Rectangle<float> (radius * 1.5f, nameSize + 4.0f).withCentre (centre.translated (0.0f, -radius * kitart::kickTilt * 0.42f)),
                     nameSize + 1.0f, kitart::headLabelColour().withAlpha (alpha), lightHalo);
            label.capsCentre = centre.translated (0.0f, radius * kitart::kickTilt + radius * 0.68f);
            break;

        case Kind::snare:
        case Kind::tom:
        case Kind::floorTom:
            setName (juce::Rectangle<float> (radius * 1.6f, nameSize + 4.0f).withCentre (centre.translated (0.0f, -radius * 0.22f)),
                     nameSize, kitart::headLabelColour().withAlpha (alpha), lightHalo);
            label.capsCentre = centre.translated (0.0f, radius * 0.16f);
            break;

        case Kind::hihat:
        case Kind::crash:
        case Kind::china:
        case Kind::ride:
            // 벨 아래쪽에 적는다. 작은 심벌은 이름을 위에, 키를 아래에 따로 적는다.
            if (radius < 40.0f)
            {
                setName (juce::Rectangle<float> (radius * 3.0f, nameSize + 2.0f).withCentre (centre.translated (0.0f, -radius * 0.78f - nameSize * 0.5f)),
                         nameSize - 1.0f, floorText, floorHalo);
                label.capsCentre = centre.translated (0.0f, radius * 0.86f + keyCapHeight * 0.5f);
                label.nameOutside = true;
            }
            else
            {
                setName (juce::Rectangle<float> (radius * 1.5f, nameSize + 4.0f).withCentre (centre.translated (0.0f, radius * 0.33f)),
                         nameSize, brassText, brassHalo);
                label.capsCentre = centre.translated (0.0f, radius * 0.33f + nameSize * 0.5f + keyCapHeight * 0.72f);
            }

            break;

        case Kind::bell:
            // 벨 위쪽에 작게
            label.capsCentre = centre.translated (0.0f, -radius * 1.5f);
            setName (juce::Rectangle<float> (radius * 3.0f, nameSize + 2.0f).withCentre (centre.translated (0.0f, -radius * 1.5f - keyCapHeight * 0.9f)),
                     nameSize - 1.5f, brassText, brassHalo);
            label.nameOutside = true;
            break;

        case Kind::pedal:
        case Kind::zone:
        {
            const auto small = radius < 30.0f;
            setName (juce::Rectangle<float> (radius * 2.6f, nameSize + 2.0f).withCentre (centre.translated (0.0f, small ? -radius - nameSize * 0.5f : -radius * 0.25f)),
                     nameSize - 1.0f, small ? floorText : juce::Colours::white.withAlpha (alpha), small ? floorHalo : darkHalo);
            label.capsCentre = centre.translated (0.0f, small ? 0.0f : radius * 0.25f);
            label.nameOutside = small;
            break;
        }
    }

    label.nameBounds = kitart::nameBounds (shownName (index), label.nameArea, label.nameSize);
    label.capsBounds = kitart::keyCapsBounds (item.keys, label.capsCentre, keyCapHeight);
    return label;
}

void KitView::layoutLabels()
{
    labelsDirty = false;
    labels.clear();

    for (int i = 0; i < static_cast<int> (items.size()); ++i)
        labels.push_back (layoutLabel (i));

    // 큰 악기부터 이름을 놓는다. 작은 악기의 이름이 앞서 놓인 이름이나 키 캡과 겹치면 그 이름은 빼고 키만 보인다.
    std::vector<int> bySize (items.size());
    std::iota (bySize.begin(), bySize.end(), 0);
    std::stable_sort (bySize.begin(), bySize.end(),
                      [this] (int a, int b) { return items[static_cast<std::size_t> (a)].size > items[static_cast<std::size_t> (b)].size; });

    for (std::size_t n = 0; n < bySize.size(); ++n)
    {
        auto& label = labels[static_cast<std::size_t> (bySize[n])];

        for (std::size_t m = 0; m < n && label.showName; ++m)
        {
            const auto& other = labels[static_cast<std::size_t> (bySize[m])];

            if ((other.showName && label.nameBounds.intersects (other.nameBounds)) || label.nameBounds.intersects (other.capsBounds))
                label.showName = false;
        }
    }

    // 악기 밖에 적는 이름이 다른 악기 위에 놓이면 뺀다. 이 자리가 얹혀 있는 악기(라이드 위의 벨)는 셈하지 않는다.
    const auto overlapsBody = [] (juce::Rectangle<float> area, juce::Rectangle<float> ellipse)
    {
        // 타원을 원으로 펴서, 원의 가운데에서 가장 가까운 네모 위의 점이 원 안에 있는지 본다.
        const auto centre = ellipse.getCentre();
        const auto rx = ellipse.getWidth() * 0.5f, ry = ellipse.getHeight() * 0.5f;
        const auto dx = (juce::jlimit (area.getX(), area.getRight(), centre.x) - centre.x) / rx;
        const auto dy = (juce::jlimit (area.getY(), area.getBottom(), centre.y) - centre.y) / ry;
        return rx > 0.0f && ry > 0.0f && dx * dx + dy * dy < 1.0f;
    };

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        auto& label = labels[i];

        if (! label.nameOutside || ! label.showName)
            continue;

        const auto centre = centreOf (items[i]);

        for (std::size_t j = 0; j < items.size() && label.showName; ++j)
        {
            const auto body = hitEllipseOf (items[j]);
            const auto host = overlapsBody (juce::Rectangle<float> (centre, centre), body);

            if (j != i && ! host && overlapsBody (label.nameBounds, body))
                label.showName = false;
        }
    }
}

void KitView::drawLabel (juce::Graphics& g, int index) const
{
    const auto& item = items[static_cast<std::size_t> (index)];
    const auto& label = labels[static_cast<std::size_t> (index)];

    if (label.showName)
        kitart::drawName (g, shownName (index), label.nameArea, label.nameSize, label.nameColour, label.nameHalo);    // 페달로 바뀐 자리는 바뀐 패드의 이름

    kitart::drawKeyCaps (g, item.keys, label.capsCentre, keyCapHeight);
}

void KitView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    if (bounds.isEmpty())
        return;

    juce::Path outline;
    outline.addRoundedRectangle (bounds.toFloat(), ui::theme().panelRadius);
    g.reduceClipRegion (outline);

    // 바닥은 크기가 바뀔 때만 다시 그려 둔다.
    if (floorImage.isNull() || floorImage.getWidth() != bounds.getWidth() || floorImage.getHeight() != bounds.getHeight())
    {
        auto& self = const_cast<KitView&> (*this);
        self.floorImage = juce::Image (juce::Image::RGB, bounds.getWidth(), bounds.getHeight(), false);
        juce::Graphics floorGraphics (self.floorImage);
        kitart::drawFloor (floorGraphics, bounds.toFloat());
    }

    g.drawImageAt (floorImage, 0, 0);

    // 층마다 멀리 있는 것부터: 그림자, 스탠드, 발판, 킥, 북, 심벌, 작은 패드
    for (int layer = 0; layer <= 6; ++layer)
        for (const auto index : drawOrder)
            drawItem (g, index, layer);

    if (showLabels)
    {
        if (labelsDirty || labels.size() != items.size())
            layoutLabels();

        for (const auto index : drawOrder)
            drawLabel (g, index);

        // 이름을 뺀 자리는 마우스를 올렸을 때 이름을 보인다.
        if (juce::isPositiveAndBelow (hovered, static_cast<int> (labels.size())) && ! labels[static_cast<std::size_t> (hovered)].showName)
        {
            const auto& label = labels[static_cast<std::size_t> (hovered)];
            kitart::drawName (g, shownName (hovered), label.nameArea, label.nameSize, label.nameColour, label.nameHalo);
        }
    }

    // 방금 친 악기에는 스틱이 잠깐 보였다가 튀어 오르며 사라진다. 발로 치는 것(킥, 페달)에는 그리지 않는다.
    constexpr float stickSeconds = 0.13f;
    const auto width = static_cast<float> (getWidth()), height = static_cast<float> (getHeight());

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& motion = motions[i];
        const auto kind = kinds[i];

        if (motion.age >= stickSeconds || motion.strength <= 0.0f || kind == Kind::kick || kind == Kind::pedal)
            continue;

        const auto centre = centreOf (items[i]);
        const auto radius = radiusOf (items[i]);
        const auto hand = Point (width * (items[i].x < 0.5f ? 0.40f : 0.60f), height * 1.25f);
        const auto progress = motion.age / stickSeconds;

        // 북은 가운데 조금 아래를, 심벌은 드러머 쪽 가장자리를 친다.
        auto toHand = hand - centre;
        toHand = toHand / juce::jmax (1.0f, toHand.getDistanceFromOrigin());
        const auto target = centre + toHand * (radius * (kitart::isCymbalKind (kind) ? 0.55f : 0.12f));

        kitart::drawStick (g, target + toHand * (height * 0.07f * progress), hand, height * 0.40f, juce::jmax (5.0f, height * 0.016f), 1.0f - progress * progress);
    }

    if (editing)
    {
        // 옮길 수 있는 자리마다 테두리를 두르고, 그림 전체를 강조색 테두리로 감싸 편집 중임을 보인다.
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            g.setColour (ui::theme().stage.nameText.withAlpha (static_cast<int> (i) == dragged ? 0.95f : 0.45f));
            g.drawEllipse (hitEllipseOf (items[i]), 1.2f);
        }

        g.setColour (ui::theme().accent);
        g.drawRoundedRectangle (bounds.toFloat().reduced (1.0f), ui::theme().panelRadius - 1.0f, editBorder);
    }
}

//==============================================================================
void KitView::mouseDown (const juce::MouseEvent& event)
{
    const auto index = itemAt (event.position);

    if (index < 0)
        return;

    if (editing)
    {
        if (event.mods.isLeftButtonDown())
        {
            dragged = index;
            dragOffset = centreOf (items[static_cast<std::size_t> (index)]) - event.position;
            repaint();
        }
    }
    else if (event.mods.isRightButtonDown())
    {
        if (onPadChoked)
            onPadChoked (index);
    }
    else if (event.mods.isLeftButtonDown() && onPadClicked)
    {
        onPadClicked (index);
    }
}

void KitView::mouseDrag (const juce::MouseEvent& event)
{
    if (! editing || dragged < 0 || getWidth() <= 0 || getHeight() <= 0)
        return;

    auto& item = items[static_cast<std::size_t> (dragged)];
    const auto centre = event.position + dragOffset;
    item.x = juce::jlimit (0.02f, 0.98f, centre.x / static_cast<float> (getWidth()));
    item.y = juce::jlimit (0.03f, 0.97f, centre.y / static_cast<float> (getHeight()));
    labelsDirty = true;
    repaint();
}

void KitView::mouseUp (const juce::MouseEvent&)
{
    if (editing && dragged >= 0 && onItemChanged)
        onItemChanged (dragged);

    if (editing)
    {
        // 옮긴 뒤에는 멀고 가까운 순서가 달라졌을 수 있다.
        std::stable_sort (drawOrder.begin(), drawOrder.end(),
                          [this] (int a, int b) { return items[static_cast<std::size_t> (a)].y < items[static_cast<std::size_t> (b)].y; });
        labelsDirty = true;
    }

    dragged = -1;
    repaint();
}

void KitView::mouseMove (const juce::MouseEvent& event)
{
    setHovered (itemAt (event.position));
}

void KitView::mouseExit (const juce::MouseEvent&)
{
    setHovered (-1);
}

void KitView::setHovered (int index)
{
    if (index == hovered)
        return;

    // 이름을 뺀 자리에 들어가거나 나올 때만 다시 그린다.
    const auto hidesName = [this] (int i)
    {
        return juce::isPositiveAndBelow (i, static_cast<int> (labels.size())) && ! labels[static_cast<std::size_t> (i)].showName;
    };

    const auto previous = hovered;
    hovered = index;

    if (showLabels && (hidesName (previous) || hidesName (index)))
        repaint();
}

void KitView::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (! editing)
        return;

    const auto index = itemAt (event.position);

    if (index < 0)
        return;

    auto& item = items[static_cast<std::size_t> (index)];
    item.size = clampSize (item.size * (1.0f + 0.25f * wheel.deltaY));
    labelsDirty = true;

    if (onItemChanged)
        onItemChanged (index);

    repaint();
}
