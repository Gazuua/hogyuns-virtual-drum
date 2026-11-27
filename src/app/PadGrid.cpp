#include "PadGrid.h"

namespace
{

constexpr int numColumns = 6;
constexpr int gap = 8;
constexpr int maxTileHeight = 120;

const juce::Colour tileColour { 0xff28323b };
const juce::Colour tileEdgeColour { 0xff3d4a55 };
const juce::Colour hitColour { 0xffffb74d };
const juce::Colour nameColour { 0xfff2f4f5 };
const juce::Colour keysColour { 0xff9fb0bd };
const juce::Colour litNameColour { 0xff1d1404 };    // 켜진 타일 위에서 읽히는 어두운 글자색
const juce::Colour litKeysColour { 0xff4a3510 };

} // namespace

PadGrid::PadGrid()
{
    // 키보드 입력은 입력 스레드가 직접 받으므로 이 컴포넌트가 포커스를 가져갈 필요가 없다.
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

void PadGrid::setTiles (std::vector<Tile> newTiles)
{
    tiles = std::move (newTiles);
    brightness.assign (tiles.size(), 0.0f);
    repaint();
}

void PadGrid::flash (int index, float strength)
{
    if (index >= 0 && index < static_cast<int> (brightness.size()))
        brightness[static_cast<std::size_t> (index)] = juce::jlimit (0.0f, 1.0f, strength);
}

bool PadGrid::fade()
{
    auto changed = false;

    for (auto& value : brightness)
    {
        if (value <= 0.0f)
            continue;

        value = value < 0.02f ? 0.0f : value * 0.86f;
        changed = true;
    }

    return changed;
}

juce::Rectangle<int> PadGrid::getTileBounds (int index) const
{
    const auto numRows = juce::jmax (1, (static_cast<int> (tiles.size()) + numColumns - 1) / numColumns);
    const auto tileWidth = (getWidth() - gap * (numColumns - 1)) / numColumns;
    const auto tileHeight = juce::jmin (maxTileHeight, (getHeight() - gap * (numRows - 1)) / numRows);

    return { (index % numColumns) * (tileWidth + gap),
             (index / numColumns) * (tileHeight + gap),
             tileWidth,
             tileHeight };
}

void PadGrid::paint (juce::Graphics& g)
{
    for (int i = 0; i < static_cast<int> (tiles.size()); ++i)
    {
        const auto& tile = tiles[static_cast<std::size_t> (i)];
        const auto level = brightness[static_cast<std::size_t> (i)];
        const auto bounds = getTileBounds (i).toFloat();

        const auto fill = tileColour.interpolatedWith (hitColour, level);

        g.setColour (fill.withMultipliedAlpha (tile.hasSound ? 1.0f : 0.45f));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (tileEdgeColour.interpolatedWith (hitColour, level));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        auto text = getTileBounds (i).reduced (12, 9);

        // 타일이 밝아지면 글자를 어두운 색으로 바꿔 어느 밝기에서도 읽히게 한다.
        const auto lit = fill.getPerceivedBrightness() > 0.45f;

        g.setColour (lit ? litNameColour : nameColour);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (tile.name, text.removeFromTop (22), juce::Justification::centredLeft, true);

        // 키 이름을 왼쪽부터 늘어놓고, 자리가 모자라면 키 이름 단위로 줄을 바꾼다. 최대 세 줄.
        const juce::Font keyFont { juce::FontOptions (13.0f) };
        constexpr int lineHeight = 18;
        constexpr int keyGap = 12;
        constexpr int maxLines = 3;

        juce::StringArray lines;
        juce::String line;
        auto lineWidth = 0;

        for (const auto& key : tile.keys)
        {
            const auto keyWidth = juce::GlyphArrangement::getStringWidthInt (keyFont, key);

            if (line.isNotEmpty() && lineWidth + keyGap + keyWidth > text.getWidth())
            {
                lines.add (line);
                line.clear();
                lineWidth = 0;
            }

            if (line.isNotEmpty())
            {
                line << "   ";
                lineWidth += keyGap;
            }

            line << key;
            lineWidth += keyWidth;
        }

        if (line.isNotEmpty())
            lines.add (line);

        // 세 줄을 넘으면 마지막 줄 끝에 말줄임표를 붙여 더 있음을 알린다.
        if (lines.size() > maxLines)
        {
            lines.removeRange (maxLines, lines.size() - maxLines);
            lines.getReference (maxLines - 1) << "  ...";
        }

        g.setColour (lit ? litKeysColour : keysColour);
        g.setFont (keyFont);

        auto keyArea = text.removeFromBottom (lineHeight * lines.size());

        for (const auto& textLine : lines)
            g.drawText (textLine, keyArea.removeFromTop (lineHeight), juce::Justification::centredLeft, true);
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& event)
{
    for (int i = 0; i < static_cast<int> (tiles.size()); ++i)
    {
        if (getTileBounds (i).contains (event.getPosition()))
        {
            if (event.mods.isRightButtonDown())
            {
                if (onPadChoked)
                    onPadChoked (i);
            }
            else if (event.mods.isLeftButtonDown() && onPadClicked)
            {
                onPadClicked (i);
            }

            return;
        }
    }
}
