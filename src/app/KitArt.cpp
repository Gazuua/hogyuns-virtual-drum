#include "KitArt.h"

#include <algorithm>
#include <cmath>

#include "Theme.h"

namespace kitart
{

namespace
{

constexpr float pi = juce::MathConstants<float>::pi;

const juce::Colour shellLight { 0xffc8414f };
const juce::Colour shellMid { 0xff8f1b28 };
const juce::Colour shellDark { 0xff3b0a11 };
const juce::Colour chromeLight { 0xfff3f6f8 };
const juce::Colour chromeMid { 0xff9ea8b0 };
const juce::Colour chromeDark { 0xff4a535b };
const juce::Colour headLight { 0xfff4f1e8 };
const juce::Colour headEdge { 0xffcbc5b6 };
const juce::Colour labelDark { 0xff23282c };

/** 타원으로 납작하게 만드는 변환. 이 안에서 원을 그리면 타원이 되고, 원형 그라데이션도 함께 납작해진다. */
juce::AffineTransform squash (Point centre, float ratio)
{
    return juce::AffineTransform::scale (1.0f, ratio, centre.x, centre.y);
}

juce::Rectangle<float> circle (Point centre, float radius)
{
    return { centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f };
}

void fillRadial (juce::Graphics& g, Point centre, float radius, juce::Colour inner, juce::Colour outer, Point focus)
{
    g.setGradientFill (juce::ColourGradient (inner, focus.x, focus.y, outer, focus.x + radius, focus.y, true));
    g.fillEllipse (circle (centre, radius));
}

juce::ColourGradient chrome (float top, float bottom)
{
    juce::ColourGradient gradient (chromeLight, 0.0f, top, chromeDark, 0.0f, bottom, false);
    gradient.addColour (0.35, chromeMid);
    gradient.addColour (0.55, chromeLight.withMultipliedBrightness (0.9f));
    return gradient;
}

void drawShadow (juce::Graphics& g, Point centre, float radiusX, float radiusY, float alpha)
{
    g.saveState();
    g.addTransform (squash (centre, radiusY / radiusX));
    fillRadial (g, centre, radiusX, juce::Colours::black.withAlpha (alpha), juce::Colours::transparentBlack, centre);
    g.restoreState();
}

/** 크롬 막대 하나. */
void drawRod (juce::Graphics& g, Point from, Point to, float thickness)
{
    g.setColour (chromeDark);
    g.drawLine ({ from, to }, thickness);
    g.setColour (chromeLight.withAlpha (0.8f));
    g.drawLine ({ from.translated (-thickness * 0.18f, 0.0f), to.translated (-thickness * 0.18f, 0.0f) }, thickness * 0.35f);
}

void drawDrumLegs (juce::Graphics& g, Point centre, float radius, float depth)
{
    // 플로어 탐의 다리 셋
    const float offsets[] = { -0.86f, 0.0f, 0.86f };

    for (const auto offset : offsets)
    {
        const auto x = centre.x + radius * offset;
        const auto top = centre.y + radius * drumTilt * std::sqrt (std::max (0.0f, 1.0f - offset * offset)) + depth * 0.35f;
        const auto bottom = centre.y + depth + radius * (offset == 0.0f ? 1.0f : 0.62f);
        drawRod (g, { x, top }, { x + radius * 0.06f * offset, bottom }, juce::jmax (2.0f, radius * 0.045f));

        g.setColour (juce::Colour (0xff15181b));
        g.fillEllipse (juce::Rectangle<float> (radius * 0.14f, radius * 0.07f).withCentre ({ x + radius * 0.06f * offset, bottom }));
    }
}

void drawCymbalStand (juce::Graphics& g, Point centre, float radius)
{
    // 심벌 아래로 보이는 스탠드의 기둥과 삼각 다리
    const auto foot = centre.translated (0.0f, radius * 1.05f);
    drawRod (g, centre, foot, juce::jmax (2.5f, radius * 0.05f));

    for (const auto side : { -1.0f, 0.0f, 1.0f })
    {
        const auto end = foot.translated (radius * 0.42f * side, radius * (side == 0.0f ? 0.30f : 0.16f));
        drawRod (g, foot.translated (0.0f, -radius * 0.18f), end, juce::jmax (2.0f, radius * 0.035f));
        g.setColour (juce::Colour (0xff15181b));
        g.fillEllipse (juce::Rectangle<float> (radius * 0.12f, radius * 0.07f).withCentre (end));
    }
}

/** 친 순간: 헤드가 악기 색으로 번쩍이고 고리가 퍼져 나간다. squash 안에서 부른다. */
void drawHeadFlash (juce::Graphics& g, Point centre, float head, float radius, juce::Colour glow, float level, float age,
                    Point focus, float peakAlpha, float ringStart)
{
    if (level <= 0.0f)
        return;

    fillRadial (g, centre, head, glow.withAlpha (peakAlpha * level), glow.withAlpha (0.12f * level), focus);

    if (age < 0.30f)
    {
        const auto ring = head * (ringStart + 2.6f * age);

        if (ring < head)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f * (1.0f - age / 0.30f)));
            g.drawEllipse (circle (centre, ring), radius * 0.03f);
        }
    }
}

} // namespace

float swing (float strength, float age, float hertz, float decaySeconds, float phase)
{
    return strength * std::exp (-age / decaySeconds) * std::sin (2.0f * pi * hertz * age + phase);
}

void drawDrum (juce::Graphics& g, Point centre, float radius, const DrumLook& look, juce::Colour glow, float level, float age)
{
    const auto radiusY = radius * drumTilt;
    const auto depth = radius * look.depth;

    // 쉘: 위쪽 헤드에서 아래로 내려오는 원통의 옆면
    juce::Path shell;
    shell.startNewSubPath (centre.x - radius, centre.y);
    shell.lineTo (centre.x - radius, centre.y + depth);
    shell.addCentredArc (centre.x, centre.y + depth, radius, radiusY, 0.0f, 1.5f * pi, 0.5f * pi);
    shell.lineTo (centre.x + radius, centre.y);
    shell.closeSubPath();

    juce::ColourGradient lacquer (shellDark, centre.x - radius, 0.0f, shellDark, centre.x + radius, 0.0f, false);
    lacquer.addColour (0.16, shellMid);
    lacquer.addColour (0.30, shellLight);
    lacquer.addColour (0.48, shellMid);
    lacquer.addColour (0.82, shellMid.darker (0.45f));
    g.setGradientFill (lacquer);
    g.fillPath (shell);

    // 아래쪽 테
    juce::Path bottomHoop;
    bottomHoop.addCentredArc (centre.x, centre.y + depth - radius * 0.03f, radius, radiusY, 0.0f, 1.5f * pi, 0.5f * pi, true);
    g.setGradientFill (chrome (centre.y + depth, centre.y + depth + radiusY));
    g.strokePath (bottomHoop, juce::PathStrokeType (juce::jmax (2.0f, radius * 0.07f)));

    // 러그: 앞쪽에서 보이는 것만. 옆으로 갈수록 좁아 보인다.
    for (int i = 0; i < look.numLugs; ++i)
    {
        const auto angle = juce::jmap (static_cast<float> (i), 0.0f, static_cast<float> (look.numLugs - 1), -1.15f, 1.15f);
        const auto x = centre.x + radius * std::sin (angle);
        const auto top = centre.y + radiusY * std::cos (angle) + depth * 0.22f;
        const auto width = juce::jmax (2.0f, radius * 0.10f * std::cos (angle));
        const auto lug = juce::Rectangle<float> (x - width * 0.5f, top, width, depth * 0.52f);

        g.setGradientFill (chrome (lug.getY(), lug.getBottom()));
        g.fillRoundedRectangle (lug, width * 0.35f);
    }

    // 위쪽 테: 헤드 둘레에 살짝 솟아 있어 아래쪽으로 두께가 보인다.
    g.setGradientFill (chrome (centre.y - radiusY, centre.y + radiusY + radius * 0.08f));
    g.fillEllipse (centre.x - radius, centre.y - radiusY + radius * 0.05f, radius * 2.0f, radiusY * 2.0f);
    g.setColour (chromeLight);
    g.fillEllipse (centre.x - radius, centre.y - radiusY, radius * 2.0f, radiusY * 2.0f);

    // 헤드
    g.saveState();
    g.addTransform (squash (centre, drumTilt));

    const auto head = radius * 0.92f;
    fillRadial (g, centre, head, headLight, headEdge, centre.translated (-head * 0.25f, -head * 0.3f));

    // 가운데의 친 자국
    fillRadial (g, centre, head * 0.42f, juce::Colour (0x30706a5c), juce::Colour (0x00706a5c), centre);

    // 테 안쪽으로 떨어지는 그림자
    g.setColour (juce::Colours::black.withAlpha (0.20f));
    g.drawEllipse (circle (centre, head - radius * 0.02f), radius * 0.05f);

    drawHeadFlash (g, centre, head, radius, glow, level, age, centre, 0.75f, 0.18f);
    g.restoreState();
}

void drawKick (juce::Graphics& g, Point centre, float radius, juce::Colour glow, float level, float age)
{
    const auto radiusY = radius * kickTilt;
    const auto length = radius * 0.92f;            // 화면에서 위로 뻗은 쉘의 길이
    const auto farCentre = centre.translated (0.0f, -length);
    const auto farRadius = radius * 0.95f;
    const auto farRadiusY = radiusY * 0.93f;

    // 다리(스퍼)
    for (const auto side : { -1.0f, 1.0f })
        drawRod (g, { centre.x + side * farRadius * 0.92f, farCentre.y + farRadiusY * 0.2f },
                 { centre.x + side * radius * 1.22f, farCentre.y + farRadiusY * 0.95f }, juce::jmax (2.0f, radius * 0.035f));

    // 쉘의 윗면: 누운 원통을 위에서 본 모습
    juce::Path shell;
    shell.startNewSubPath (centre.x - radius, centre.y);
    shell.lineTo (farCentre.x - farRadius, farCentre.y);
    shell.addCentredArc (farCentre.x, farCentre.y, farRadius, farRadiusY, 0.0f, 1.5f * pi, 2.5f * pi);
    shell.lineTo (centre.x + radius, centre.y);
    shell.closeSubPath();

    juce::ColourGradient lacquer (shellDark, centre.x - radius, 0.0f, shellDark, centre.x + radius, 0.0f, false);
    lacquer.addColour (0.2, shellMid);
    lacquer.addColour (0.42, shellLight);
    lacquer.addColour (0.6, shellMid);
    g.setGradientFill (lacquer);
    g.fillPath (shell);

    // 먼 쪽 테
    juce::Path farHoop;
    farHoop.addCentredArc (farCentre.x, farCentre.y + radius * 0.03f, farRadius, farRadiusY, 0.0f, 1.5f * pi, 2.5f * pi, true);
    g.setColour (shellDark.brighter (0.25f));
    g.strokePath (farHoop, juce::PathStrokeType (juce::jmax (2.5f, radius * 0.07f)));

    // 윗면의 러그
    for (const auto offset : { -0.62f, -0.22f, 0.22f, 0.62f })
    {
        const auto width = juce::jmax (3.0f, radius * 0.075f * (1.0f - 0.45f * std::abs (offset)));
        const auto x = centre.x + radius * offset * 0.97f;

        for (const auto along : { 0.30f, 0.80f })
        {
            const auto rise = radiusY * std::sqrt (std::max (0.0f, 1.0f - offset * offset));
            const auto lug = juce::Rectangle<float> (x - width * 0.5f, centre.y - rise - length * along, width, length * 0.20f);
            g.setGradientFill (chrome (lug.getY(), lug.getBottom()));
            g.fillRoundedRectangle (lug, width * 0.3f);
        }
    }

    // 가까운 쪽 나무 테와 헤드 (드러머를 향한 면)
    g.setColour (shellDark.brighter (0.1f));
    g.fillEllipse (centre.x - radius, centre.y - radiusY + radius * 0.04f, radius * 2.0f, radiusY * 2.0f);

    g.saveState();
    g.addTransform (squash (centre, kickTilt));

    juce::ColourGradient hoop (shellLight, centre.x, centre.y - radius, shellDark, centre.x, centre.y + radius, false);
    hoop.addColour (0.4, shellMid);
    g.setGradientFill (hoop);
    g.fillEllipse (circle (centre, radius));

    const auto head = radius * 0.89f;
    fillRadial (g, centre, head, headLight, headEdge.darker (0.08f), centre.translated (-head * 0.2f, -head * 0.35f));

    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawEllipse (circle (centre, head - radius * 0.015f), radius * 0.04f);

    // 비터가 닿는 자리
    const auto patchCentre = centre.translated (0.0f, head * 0.12f);
    g.setColour (juce::Colour (0xff3a3d40));
    g.fillEllipse (circle (patchCentre, head * 0.20f));

    drawHeadFlash (g, centre, head, radius, glow, level, age, patchCentre, 0.78f, 0.2f);
    g.restoreState();

    // 페달과 비터
    const auto headBottom = centre.y + radiusY;
    juce::Path board;
    board.startNewSubPath (centre.x - radius * 0.13f, headBottom - radius * 0.02f);
    board.lineTo (centre.x + radius * 0.13f, headBottom - radius * 0.02f);
    board.lineTo (centre.x + radius * 0.17f, headBottom + radius * 0.52f);
    board.lineTo (centre.x - radius * 0.17f, headBottom + radius * 0.52f);
    board.closeSubPath();

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6c757d), 0.0f, headBottom, juce::Colour (0xff22272b), 0.0f, headBottom + radius * 0.52f, false));
    g.fillPath (board);

    g.setColour (juce::Colours::white.withAlpha (0.18f));

    for (int line = 1; line <= 4; ++line)
    {
        const auto y = headBottom + radius * 0.10f * static_cast<float> (line);
        g.drawLine (centre.x - radius * 0.11f, y, centre.x + radius * 0.11f, y, 1.2f);
    }

    const auto strike = std::exp (-age / 0.05f);    // 친 직후에 비터가 헤드 쪽으로 가 있다
    const auto beater = Point (centre.x, patchCentre.y * kickTilt + centre.y * (1.0f - kickTilt) + radius * 0.16f * (1.0f - strike));
    drawRod (g, { centre.x, headBottom + radius * 0.04f }, beater, juce::jmax (2.0f, radius * 0.03f));
    g.setColour (juce::Colour (0xffe8e2d2));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.15f, radius * 0.12f).withCentre (beater));
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (juce::Rectangle<float> (radius * 0.15f, radius * 0.12f).withCentre (beater), 1.0f);
}

CymbalLook lookOf (Kind kind)
{
    switch (kind)
    {
        case Kind::ride:     return { juce::Colour (0xffe9cc7a), juce::Colour (0xffb8913f), juce::Colour (0xff6f531d), 0.24f, 2.6f, 0.9f, 0.5f, false };
        case Kind::china:    return { juce::Colour (0xffe0b56a), juce::Colour (0xffa9793a), juce::Colour (0xff5d3d18), 0.20f, 3.6f, 0.8f, 1.0f, true };
        case Kind::hihat:    return { juce::Colour (0xffeed68c), juce::Colour (0xffc19c48), juce::Colour (0xff735722), 0.21f, 9.0f, 0.16f, 0.35f, false };
        default:             break;
    }

    return { juce::Colour (0xfff6dd92), juce::Colour (0xffcaa64e), juce::Colour (0xff7c5e22), 0.17f, 3.1f, 1.0f, 1.0f, false };
}

/** 심벌 한 장. tiltAngle은 드러머 쪽으로 기울여 단 각도(라디안)다. */
void drawCymbalDisc (juce::Graphics& g, Point centre, float radius, const CymbalLook& look, float tiltAngle, float wobble, float shimmer,
                     juce::Colour glow, float level)
{
    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (tiltAngle + wobble * 0.13f * look.swingAmount, centre.x, centre.y));

    // 두께: 아래쪽 가장자리가 살짝 보인다.
    g.setColour (look.dark.darker (0.5f));
    g.fillEllipse (centre.x - radius, centre.y - radius * cymbalTilt + radius * 0.035f, radius * 2.0f, radius * cymbalTilt * 2.0f);

    g.addTransform (squash (centre, cymbalTilt * (1.0f + 0.09f * shimmer * look.swingAmount)));

    // 원판
    juce::ColourGradient brass (look.bright, centre.x - radius * 0.25f, centre.y - radius * 0.3f, look.dark, centre.x + radius, centre.y, true);
    brass.addColour (0.55, look.mid);
    g.setGradientFill (brass);
    g.fillEllipse (circle (centre, radius));

    if (look.upturnedEdge)
    {
        // 차이나의 꺾인 가장자리
        g.setColour (look.dark.withAlpha (0.55f));
        g.drawEllipse (circle (centre, radius * 0.90f), radius * 0.18f);
        g.setColour (look.bright.withAlpha (0.45f));
        g.drawEllipse (circle (centre, radius * 0.81f), radius * 0.02f);
    }

    // 깎은 홈 (동심원)
    for (int ring = 0; ring < 9; ++ring)
    {
        const auto ratio = 0.95f - 0.085f * static_cast<float> (ring);

        if (ratio <= look.bell + 0.04f)
            break;

        g.setColour ((ring % 2 == 0 ? look.dark : look.bright).withAlpha (ring % 2 == 0 ? 0.20f : 0.16f));
        g.drawEllipse (circle (centre, radius * ratio), radius * 0.012f);
    }

    // 빛이 반사되는 부채꼴 두 줄기. 흔들리면 함께 돈다.
    for (const auto direction : { -0.55f, 2.45f })
    {
        const auto angle = direction + shimmer * 0.35f;
        juce::Path wedge;
        wedge.addPieSegment (circle (centre, radius * 0.985f), angle - 0.20f, angle + 0.20f, 0.0f);

        const auto tip = centre.getPointOnCircumference (radius, angle);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), centre.x, centre.y,
                                                 juce::Colours::white.withAlpha (0.36f), tip.x, tip.y, false));
        g.fillPath (wedge);
    }

    // 가장자리
    g.setColour (look.dark.darker (0.3f).withAlpha (0.8f));
    g.drawEllipse (circle (centre, radius * 0.992f), radius * 0.014f);

    // 벨
    const auto bell = radius * look.bell;
    g.setColour (look.dark.withAlpha (0.45f));
    g.fillEllipse (circle (centre.translated (0.0f, bell * 0.10f), bell * 1.06f));
    fillRadial (g, centre, bell, look.bright.brighter (0.5f), look.mid, centre.translated (-bell * 0.3f, -bell * 0.4f));

    // 가운데의 펠트와 나사
    g.setColour (juce::Colour (0xff2b2e31));
    g.fillEllipse (circle (centre, bell * 0.36f));
    g.setColour (chromeLight);
    g.fillEllipse (circle (centre, bell * 0.15f));

    if (level > 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.30f * level));
        g.fillEllipse (circle (centre, radius));
        g.setColour (glow.withAlpha (0.85f * level));
        g.drawEllipse (circle (centre, radius * 0.97f), radius * 0.05f);
    }

    g.restoreState();
}

/** 하이햇 페달이나 킥 페달 같은 발판. */
void drawFootboard (juce::Graphics& g, Point centre, float radius, juce::Colour glow, float level)
{
    const auto press = level * radius * 0.06f;
    const auto top = centre.y - radius * 0.95f + press;
    const auto bottom = centre.y + radius * 0.95f;

    juce::Path plate;
    plate.startNewSubPath (centre.x - radius * 0.36f, top);
    plate.lineTo (centre.x + radius * 0.36f, top);
    plate.lineTo (centre.x + radius * 0.50f, bottom);
    plate.lineTo (centre.x - radius * 0.50f, bottom);
    plate.closeSubPath();

    const auto rounded = plate.createPathWithRoundedCorners (radius * 0.14f);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillPath (rounded, juce::AffineTransform::translation (radius * 0.06f, radius * 0.08f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff848d95), 0.0f, top, juce::Colour (0xff262b30), 0.0f, bottom, false));
    g.fillPath (rounded);

    g.setColour (juce::Colours::white.withAlpha (0.22f));

    for (int line = 1; line <= 5; ++line)
    {
        const auto t = static_cast<float> (line) / 7.0f;
        const auto y = juce::jmap (t, top, bottom);
        const auto half = radius * juce::jmap (t, 0.28f, 0.40f);
        g.drawLine (centre.x - half, y, centre.x + half, y, 1.3f);
    }

    // 뒤꿈치 받침
    g.setColour (juce::Colour (0xff1b1f23));
    g.fillRoundedRectangle (centre.x - radius * 0.50f, bottom - radius * 0.34f, radius, radius * 0.34f, radius * 0.10f);

    if (level > 0.0f)
    {
        g.setColour (glow.withAlpha (0.55f * level));
        g.fillPath (rounded);
    }

    g.setColour (glow.withAlpha (0.55f + 0.45f * level));
    g.strokePath (rounded, juce::PathStrokeType (1.5f));
}

/** 따로 떨어진 악기가 아닌 연주 방법(림샷, 열린 하이햇 등)을 치는 작은 고무 패드. */
void drawZonePad (juce::Graphics& g, Point centre, float radius, juce::Colour colour, float level)
{
    constexpr float tilt = 0.86f;

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (centre.x - radius, centre.y - radius * tilt + radius * 0.14f, radius * 2.0f, radius * tilt * 2.0f);

    g.saveState();
    g.addTransform (squash (centre, tilt));
    fillRadial (g, centre, radius, juce::Colour (0xff3a424a), juce::Colour (0xff161a1e), centre.translated (-radius * 0.3f, -radius * 0.4f));

    if (level > 0.0f)
        fillRadial (g, centre, radius, colour.withAlpha (0.9f * level), colour.withAlpha (0.35f * level), centre);

    g.setColour (colour.withAlpha (0.75f + 0.25f * level));
    g.drawEllipse (circle (centre, radius * 0.94f), juce::jmax (1.5f, radius * 0.07f));
    g.restoreState();
}

/** 키 이름을 자판의 키처럼 그린다. 가운데가 centre인 한 줄. */
void drawKeyCaps (juce::Graphics& g, const juce::StringArray& keys, Point centre, float height)
{
    if (keys.isEmpty())
        return;

    // 글자가 11px 아래로 내려가지 않게 캡의 높이에 하한을 둔다.
    height = juce::jmax (height, 11.0f / 0.68f);
    const auto fontSize = height * 0.68f;
    std::vector<float> widths;
    auto total = 0.0f;

    for (const auto& key : keys)
    {
        widths.push_back (juce::jmax (height, static_cast<float> (key.length()) * fontSize * 0.62f + height * 0.55f));
        total += widths.back() + 3.0f;
    }

    auto x = centre.x - (total - 3.0f) * 0.5f;
    g.setFont (juce::FontOptions (fontSize, juce::Font::bold));

    for (int i = 0; i < keys.size(); ++i)
    {
        const auto cap = juce::Rectangle<float> (x, centre.y - height * 0.5f, widths[static_cast<std::size_t> (i)], height);

        const auto& stage = ui::theme().stage;
        g.setColour (stage.keycap);
        g.fillRoundedRectangle (cap, height * 0.25f);
        g.setColour (stage.keycapEdge);
        g.drawRoundedRectangle (cap.reduced (0.5f), height * 0.25f, 1.0f);
        g.setColour (stage.keycapText);
        g.drawText (keys[i], cap, juce::Justification::centred, false);

        x += cap.getWidth() + 3.0f;
    }
}

/** 어두운 테두리를 두른 글자. 밝은 헤드와 황동 위에서도 읽힌다. */
void drawName (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float fontSize, juce::Colour colour, juce::Colour halo)
{
    g.setFont (juce::FontOptions (juce::jmax (11.0f, fontSize), juce::Font::bold));
    g.setColour (halo);

    for (const auto offset : { Point (-1.0f, 0.0f), Point (1.0f, 0.0f), Point (0.0f, -1.0f), Point (0.0f, 1.0f) })
        g.drawFittedText (text, area.translated (offset.x, offset.y).toNearestInt(), juce::Justification::centred, 1, 0.7f);

    g.setColour (colour);
    g.drawFittedText (text, area.toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

/** 치는 순간에 잠깐 보이는 스틱. tip은 닿는 곳, hand는 손이 있는 쪽(화면 아래)이다. */
void drawStick (juce::Graphics& g, Point tip, Point hand, float length, float thickness, float alpha)
{
    const auto offset = hand - tip;
    const auto distance = offset.getDistanceFromOrigin();

    if (distance < 1.0f || alpha <= 0.0f)
        return;

    const auto along = offset / distance;
    const auto across = Point (-along.y, along.x);
    const auto butt = tip + along * length;

    juce::Path stick;
    stick.startNewSubPath (tip + across * (thickness * 0.28f));
    stick.lineTo (butt + across * (thickness * 0.5f));
    stick.lineTo (butt - across * (thickness * 0.5f));
    stick.lineTo (tip - across * (thickness * 0.28f));
    stick.closeSubPath();

    g.setColour (juce::Colours::black.withAlpha (0.30f * alpha));
    g.fillPath (stick, juce::AffineTransform::translation (thickness * 0.5f, thickness * 0.8f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff0d9ad).withAlpha (alpha), tip.x, tip.y,
                                             juce::Colour (0xffb58a4f).withAlpha (alpha), butt.x, butt.y, false));
    g.fillPath (stick);

    g.setColour (juce::Colour (0xfff7e8c8).withAlpha (alpha));
    g.fillEllipse (circle (tip, thickness * 0.42f));
}

bool isCymbalKind (Kind kind)
{
    return kind == Kind::hihat || kind == Kind::crash || kind == Kind::china || kind == Kind::ride;
}

bool isDrumKind (Kind kind)
{
    return kind == Kind::snare || kind == Kind::tom || kind == Kind::floorTom;
}

DrumLook drumLookOf (Kind kind)
{
    switch (kind)
    {
        case Kind::snare:       return { 0.36f, 5, false };
        case Kind::floorTom:    return { 0.95f, 5, true };
        default:                break;
    }

    return { 0.60f, 5, false };
}

juce::Colour headLabelColour()
{
    return labelDark;
}

//==============================================================================
void drawFloor (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto width = bounds.getWidth(), height = bounds.getHeight();
    const auto& stage = ui::theme().stage;

    // 나무 바닥: 가까울수록 널이 넓어 보인다.
    g.fillAll (stage.floor);
    constexpr int numPlanks = 13;

    for (int plank = 0; plank < numPlanks; ++plank)
    {
        const auto top = height * std::pow (static_cast<float> (plank) / numPlanks, 1.5f);
        const auto bottom = height * std::pow (static_cast<float> (plank + 1) / numPlanks, 1.5f);
        const auto variance = ui::theme().dark ? 0.18f : 0.06f;    // 밝은 바닥은 널마다의 차이를 줄인다
        const auto shade = 1.0f - variance + variance * static_cast<float> ((plank * 7) % 5) / 4.0f;

        g.setGradientFill (juce::ColourGradient (stage.plankNear.withMultipliedBrightness (shade), 0.0f, top,
                                                 stage.plankFar.withMultipliedBrightness (shade), 0.0f, bottom, false));
        g.fillRect (0.0f, top, width, bottom - top);
        g.setColour (stage.plankLine);
        g.fillRect (0.0f, bottom - 1.0f, width, 1.0f);
    }

    // 러그: 드럼 세트 밑에 까는 깔개
    juce::Path rug;
    rug.startNewSubPath (width * 0.13f, height * 0.20f);
    rug.lineTo (width * 0.87f, height * 0.20f);
    rug.lineTo (width * 0.975f, height * 0.985f);
    rug.lineTo (width * 0.025f, height * 0.985f);
    rug.closeSubPath();

    const auto rounded = rug.createPathWithRoundedCorners (16.0f);

    g.setColour (stage.rugShadow);
    g.fillPath (rounded, juce::AffineTransform::translation (0.0f, 5.0f));
    g.setGradientFill (juce::ColourGradient (stage.rugFar, 0.0f, height * 0.2f, stage.rugNear, 0.0f, height, false));
    g.fillPath (rounded);

    // 러그의 테두리 무늬
    for (const auto inset : { 0.035f, 0.055f })
    {
        juce::Path border;
        border.startNewSubPath (width * (0.13f + inset * 0.75f), height * (0.20f + inset));
        border.lineTo (width * (0.87f - inset * 0.75f), height * (0.20f + inset));
        border.lineTo (width * (0.975f - inset), height * (0.985f - inset * 0.8f));
        border.lineTo (width * (0.025f + inset), height * (0.985f - inset * 0.8f));
        border.closeSubPath();

        g.setColour (stage.rugTrim.withMultipliedAlpha (inset < 0.04f ? 1.0f : 0.45f));
        g.strokePath (border.createPathWithRoundedCorners (10.0f), juce::PathStrokeType (inset < 0.04f ? 2.0f : 1.0f));
    }

    // 무대 조명: 가운데가 밝고 가장자리는 어둡다.
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (1.0f, height / width, width * 0.5f, height * 0.5f));
    g.setGradientFill (juce::ColourGradient (stage.light, width * 0.5f, height * 0.5f,
                                             stage.vignette, width * 1.08f, height * 0.5f, true));
    g.fillRect (-width, -height * 3.0f, width * 3.0f, height * 7.0f);
    g.restoreState();
}

void drawShadowOf (juce::Graphics& g, Kind kind, Point centre, float radius)
{
    if (kind == Kind::kick)
        drawShadow (g, centre.translated (0.0f, -radius * 0.35f), radius * 1.45f, radius * 1.25f, 0.55f);
    else if (isDrumKind (kind))
        drawShadow (g, centre.translated (radius * 0.05f, radius * (drumLookOf (kind).depth + 0.25f)), radius * 1.3f, radius * 0.9f, 0.55f);
    else if (isCymbalKind (kind))
        drawShadow (g, centre.translated (radius * 0.12f, radius * 0.55f), radius * 1.1f, radius * 0.7f, 0.38f);
}

void drawStandOf (juce::Graphics& g, Kind kind, Point centre, float radius)
{
    if (isCymbalKind (kind) && radius > 26.0f)
        drawCymbalStand (g, centre, radius);
    else if (kind == Kind::floorTom)
        drawDrumLegs (g, centre, radius, radius * drumLookOf (kind).depth);
}

void drawHiHat (juce::Graphics& g, Point centre, float radius, float lean, float wobble, float openness, juce::Colour glow, float level)
{
    const auto look = lookOf (Kind::hihat);

    // 페달을 떼 열려 있으면 위 심벌이 클러치와 함께 올라간다.
    const auto lift = radius * 0.26f * openness;
    const auto top = centre.translated (0.0f, -radius * 0.02f - lift);
    drawCymbalDisc (g, centre.translated (0.0f, radius * 0.07f), radius * 0.99f, look, 0.0f, 0.0f, 0.0f, glow, 0.0f);
    drawCymbalDisc (g, top, radius, look, lean, wobble, wobble, glow, level);
    drawRod (g, top.translated (0.0f, -radius * 0.02f), centre.translated (0.0f, -radius * 0.50f - lift), juce::jmax (2.0f, radius * 0.045f));
    g.setGradientFill (chrome (top.y - radius * 0.14f, top.y));
    g.fillRoundedRectangle (juce::Rectangle<float> (radius * 0.16f, radius * 0.13f).withCentre (top.translated (0.0f, -radius * 0.07f)), radius * 0.03f);
}

void drawBellFlash (juce::Graphics& g, Point centre, float radius, juce::Colour glow, float level)
{
    if (level <= 0.0f)
        return;

    g.saveState();
    g.addTransform (squash (centre, cymbalTilt));
    fillRadial (g, centre, radius * 1.15f, juce::Colours::white.withAlpha (0.85f * level), glow.withAlpha (0.0f), centre);
    g.restoreState();
}

} // namespace kitart