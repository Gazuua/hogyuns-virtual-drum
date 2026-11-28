#include "KeyboardView.h"

#include "Text.h"
#include "Theme.h"

namespace
{

// 그림 전체의 크기. 단위는 보통 키 하나의 폭이다.
constexpr float unitsWide = 22.5f;
constexpr float unitsHigh = 6.25f;

// 키가 없는 빈자리: 가운데 줄의 오른쪽, 이동 키 묶음 위 (Del 줄과 ↑ 사이)
constexpr float spareX = 15.25f, spareY = 3.25f, spareW = 3.0f, spareH = 1.0f;

} // namespace

KeyboardView::KeyboardView()
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    caps.resize (hvd::numKeyCodes);
    heldShown.assign (hvd::numKeyCodes, false);
}

const std::vector<KeyboardView::Shape>& KeyboardView::shapes()
{
    // 표준 104키 배열. 좌표의 단위는 보통 키 하나의 폭이다.
    static const std::vector<Shape> all = []
    {
        std::vector<Shape> list;

        const auto add = [&list] (unsigned scan, bool extended, float x, float y, float w = 1.0f, float h = 1.0f)
        {
            list.push_back ({ hvd::makeKeyCode (scan, extended), x, y, w, h });
        };

        const auto run = [&add] (unsigned first, unsigned last, float x, float y)
        {
            for (auto scan = first; scan <= last; ++scan)
                add (scan, false, x + static_cast<float> (scan - first), y);
        };

        // 기능 키 줄
        add (0x01, false, 0.0f, 0.0f);
        run (0x3b, 0x3e, 2.0f, 0.0f);
        run (0x3f, 0x42, 6.5f, 0.0f);
        run (0x43, 0x44, 11.0f, 0.0f);
        add (0x57, false, 13.0f, 0.0f);
        add (0x58, false, 14.0f, 0.0f);
        add (0x46, false, 16.25f, 0.0f);

        // 숫자 줄
        add (0x29, false, 0.0f, 1.25f);
        run (0x02, 0x0d, 1.0f, 1.25f);
        add (0x0e, false, 13.0f, 1.25f, 2.0f);

        // 윗줄
        add (0x0f, false, 0.0f, 2.25f, 1.5f);
        run (0x10, 0x1b, 1.5f, 2.25f);
        add (0x2b, false, 13.5f, 2.25f, 1.5f);

        // 가운데 줄
        add (0x3a, false, 0.0f, 3.25f, 1.75f);
        run (0x1e, 0x28, 1.75f, 3.25f);
        add (0x1c, false, 12.75f, 3.25f, 2.25f);

        // 아랫줄
        add (0x2a, false, 0.0f, 4.25f, 2.25f);
        run (0x2c, 0x35, 2.25f, 4.25f);
        add (0x36, false, 12.25f, 4.25f, 2.75f);

        // 맨 아랫줄
        add (0x1d, false, 0.0f, 5.25f, 1.25f);
        add (0x5b, true, 1.25f, 5.25f, 1.25f);
        add (0x38, false, 2.5f, 5.25f, 1.25f);
        add (0x39, false, 3.75f, 5.25f, 6.25f);
        add (0x38, true, 10.0f, 5.25f, 1.25f);
        add (0x5c, true, 11.25f, 5.25f, 1.25f);
        add (0x5d, true, 12.5f, 5.25f, 1.25f);
        add (0x1d, true, 13.75f, 5.25f, 1.25f);

        // 이동 키
        add (0x52, true, 15.25f, 1.25f);
        add (0x47, true, 16.25f, 1.25f);
        add (0x49, true, 17.25f, 1.25f);
        add (0x53, true, 15.25f, 2.25f);
        add (0x4f, true, 16.25f, 2.25f);
        add (0x51, true, 17.25f, 2.25f);
        add (0x48, true, 16.25f, 4.25f);
        add (0x4b, true, 15.25f, 5.25f);
        add (0x50, true, 16.25f, 5.25f);
        add (0x4d, true, 17.25f, 5.25f);

        // 숫자 패드
        add (0x35, true, 19.5f, 1.25f);
        add (0x37, false, 20.5f, 1.25f);
        add (0x4a, false, 21.5f, 1.25f);
        run (0x47, 0x49, 18.5f, 2.25f);
        add (0x4e, false, 21.5f, 2.25f, 1.0f, 2.0f);
        run (0x4b, 0x4d, 18.5f, 3.25f);
        run (0x4f, 0x51, 18.5f, 4.25f);
        add (0x1c, true, 21.5f, 4.25f, 1.0f, 2.0f);
        add (0x52, false, 18.5f, 5.25f, 2.0f);
        add (0x53, false, 20.5f, 5.25f);

        // 페달(풋스위치)의 스위치. 숫자 패드 위의 빈자리에 둔다.
        for (int i = 0; i < hvd::maxPedalKeys; ++i)
            list.push_back ({ hvd::pedalKey (i), 18.5f + static_cast<float> (i), 0.0f, 1.0f, 1.0f });

        return list;
    }();

    return all;
}

bool KeyboardView::hasKey (hvd::KeyCode key)
{
    for (const auto& shape : shapes())
        if (shape.code == key)
            return true;

    return false;
}

int KeyboardView::heightForWidth (int width)
{
    return juce::roundToInt (static_cast<float> (width) / unitsWide * unitsHigh);
}

float KeyboardView::unit() const
{
    return juce::jmin (static_cast<float> (getWidth()) / unitsWide, static_cast<float> (getHeight()) / unitsHigh);
}

juce::Rectangle<float> KeyboardView::boundsOf (float x, float y, float w, float h) const
{
    const auto u = unit();
    const auto left = (static_cast<float> (getWidth()) - unitsWide * u) * 0.5f;

    return { left + x * u, y * u, w * u - 3.0f, h * u - 3.0f };
}

juce::Rectangle<int> KeyboardView::getSpareArea() const
{
    return boundsOf (spareX, spareY, spareW, spareH).toNearestInt();
}

const KeyboardView::Shape* KeyboardView::shapeAt (juce::Point<float> position) const
{
    for (const auto& shape : shapes())
        if (boundsOf (shape).contains (position))
            return &shape;

    return nullptr;
}

void KeyboardView::setCaps (std::vector<Cap> newCaps)
{
    caps = std::move (newCaps);
    caps.resize (hvd::numKeyCodes);
    repaint();
}

void KeyboardView::setSelected (hvd::KeyCode key)
{
    if (selected != key)
    {
        selected = key;
        repaint();
    }
}

void KeyboardView::refreshHeld()
{
    if (isKeyHeld == nullptr)
        return;

    auto changed = false;

    for (const auto& shape : shapes())
    {
        const auto held = isKeyHeld (shape.code);

        if (heldShown[shape.code] != held)
        {
            heldShown[shape.code] = held;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void KeyboardView::paint (juce::Graphics& g)
{
    const auto& t = ui::theme();
    const auto u = unit();
    const auto labelSize = juce::jlimit (ui::type::caption - 1.0f, ui::type::caption, u * 0.25f);
    const auto textSize = juce::jlimit (ui::type::caption - 1.0f, ui::type::body - 1.0f, u * 0.27f);
    const auto radius = juce::jmin (6.0f, t.controlRadius);

    for (const auto& shape : shapes())
    {
        const auto& cap = caps[shape.code];
        const auto bounds = boundsOf (shape);
        const auto held = heldShown[shape.code];
        const auto bound = cap.text.isNotEmpty();

        // 빈 키는 칸 면, 정해진 키는 악기 색을 옅게 섞은 면, 누르고 있는 키는 악기 색 그대로
        auto fill = bound ? t.padTint (cap.colour) : t.surface;

        if (held)
            fill = bound ? cap.colour : t.accent;

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, radius);

        if (shape.code == selected)
        {
            g.setColour (t.text);
            g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 2.0f);
        }
        else if (bound || ! t.dark)
        {
            g.setColour (bound ? cap.colour.withAlpha (t.dark ? 0.55f : 0.65f) : t.border);
            g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
        }

        const auto onFill = t.textOn (fill);
        auto inner = bounds.reduced (5.0f, 2.0f).toNearestInt();

        g.setColour (held ? onFill.withAlpha (0.75f) : (bound ? t.text.withAlpha (0.7f) : t.textFaint));
        g.setFont (juce::FontOptions (labelSize));
        g.drawText (fromUtf8 (hvd::keyLabel (shape.code)), inner.removeFromTop (juce::roundToInt (labelSize + 3.0f)),
                    juce::Justification::topLeft, true);

        if (bound)
        {
            g.setColour (held ? onFill : t.text);
            g.setFont (juce::FontOptions (textSize, juce::Font::bold));

            auto text = cap.text;

            if (cap.count > 1)
                text << " +" << (cap.count - 1);

            // 글자를 너무 눌러 줄이지 않는다. 다 안 들어가면 말줄임이 붙고, 전체는 툴팁으로 본다.
            g.drawFittedText (text, inner, juce::Justification::centred, 2, 0.85f);
        }
    }
}

void KeyboardView::mouseDown (const juce::MouseEvent& event)
{
    if (const auto* shape = shapeAt (event.position); shape != nullptr && onKeyClicked)
        onKeyClicked (shape->code);
}

juce::String KeyboardView::getTooltip()
{
    const auto* shape = shapeAt (getMouseXYRelative().toFloat());

    if (shape == nullptr)
        return {};

    const auto& cap = caps[shape->code];

    if (cap.text.isEmpty())
        return {};

    return fromUtf8 (hvd::keyLabel (shape->code)) + "  " + (cap.tip.isNotEmpty() ? cap.tip : cap.text);
}
