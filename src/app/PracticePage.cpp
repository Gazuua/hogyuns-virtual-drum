#include "PracticePage.h"

#include <algorithm>
#include <cmath>

#include "core/PadKinds.h"

namespace
{

constexpr int listWidth = 280;
constexpr int listRowHeight = 44;
constexpr float axisMs = 80.0f;     // 악기별 줄의 가로 범위: ±80ms
constexpr float chartMs = 70.0f;    // 흐름의 세로 범위: ±70ms
constexpr int minSectionHits = 3;   // 타격이 이보다 적은 구간은 그리지 않는다

// 자료를 나타내는 색은 하나만 쓴다. 악기의 색은 이름 옆의 점에만 쓴다 (빨강과 초록이 "나쁨/좋음"으로 읽히지 않게).
juce::Colour markColour()    { return ui::theme().info; }
juce::Colour tileColour()    { return ui::theme().surfaceSunken; }
juce::Colour zeroLine()      { return ui::theme().textFaint; }

struct GroupInfo
{
    const char* name;
    hvd::Instrument instrument;    // 색을 고를 때 쓴다
};

constexpr GroupInfo groupInfos[hvd::numPracticeGroups] = {
    { "킥", hvd::Instrument::kick }, { "스네어", hvd::Instrument::snare }, { "하이햇", hvd::Instrument::hihat },
    { "라이드", hvd::Instrument::ride }, { "크래시", hvd::Instrument::crash }, { "탐", hvd::Instrument::tom },
};

// 악기별 줄에서 이름, 타격 수, 수치가 차지하는 너비. 나머지가 띠를 그리는 자리다.
constexpr int nameWidth = 92, countWidth = 60, verdictWidth = 164;

/** 가운뎃값이 치우치지 않았는지 (박에 맞음과는 다른, 여러 타격의 기준). */
bool isCentred (float medianMs)
{
    return std::abs (medianMs) < ui::timing::centredMs;
}

/** 치우침이 없다는 표시(✓)를 box 안에 그린다. */
void drawCheck (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour)
{
    juce::Path tick;
    tick.startNewSubPath (box.getX(), box.getCentreY());
    tick.lineTo (box.getX() + box.getWidth() * 0.38f, box.getBottom());
    tick.lineTo (box.getRight(), box.getY());
    g.setColour (colour);
    g.strokePath (tick, juce::PathStrokeType (juce::jmax (1.6f, box.getWidth() * 0.16f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace

//==============================================================================
void TimingRows::setStats (const std::array<hvd::TimingStats, hvd::numPracticeGroups>& newStats)
{
    stats = newStats;
    repaint();
}

int TimingRows::getIdealHeight() const
{
    const auto used = static_cast<int> (std::count_if (stats.begin(), stats.end(), [] (const hvd::TimingStats& item) { return item.count > 0; }));
    return used * rowHeight + axisHeight;
}

void TimingRows::paint (juce::Graphics& g)
{
    const auto stripLeft = static_cast<float> (nameWidth + countWidth);
    const auto stripRight = static_cast<float> (getWidth() - verdictWidth);
    const auto centre = (stripLeft + stripRight) * 0.5f;
    const auto half = juce::jmax (10.0f, (stripRight - stripLeft) * 0.5f);
    const auto xOf = [centre, half] (float ms) { return centre + juce::jlimit (-axisMs, axisMs, ms) / axisMs * half; };

    auto y = 0;

    for (std::size_t group = 0; group < stats.size(); ++group)
    {
        const auto& item = stats[group];

        if (item.count == 0)
            continue;

        auto row = juce::Rectangle<int> (0, y, getWidth(), rowHeight);
        y += rowHeight;

        // 악기 이름: 색 점은 어느 악기인지 알려 주는 표시이고, 글자는 글자 색 그대로다.
        auto nameArea = row.removeFromLeft (nameWidth);
        g.setColour (ui::instrumentColour (groupInfos[group].instrument));
        g.fillRoundedRectangle (nameArea.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat(), 2.0f);
        nameArea.removeFromLeft (8);
        g.setColour (ui::theme().text);
        g.setFont (juce::FontOptions (ui::type::body));
        g.drawText (ko (groupInfos[group].name), nameArea, juce::Justification::centredLeft);

        g.setColour (ui::theme().textMuted);
        g.setFont (juce::FontOptions (ui::type::caption));
        g.drawText (ui::withCommas (item.count) + ko ("타"), row.removeFromLeft (countWidth).withTrimmedRight (14), juce::Justification::centredRight);

        auto verdictArea = row.removeFromRight (verdictWidth).withTrimmedLeft (14);
        const auto strip = row.toFloat();

        // 박에 맞았다고 보는 범위와 박의 자리(0)
        g.setColour (ui::theme().text.withAlpha (0.05f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (-ui::timing::onBeatMs), strip.getY() + 3.0f,
                                                                xOf (ui::timing::onBeatMs), strip.getBottom() - 3.0f));
        g.setColour (zeroLine());
        g.fillRect (centre - 0.5f, strip.getY() + 2.0f, 1.0f, strip.getHeight() - 4.0f);

        // 타격의 가운데 80%가 놓인 범위
        const auto low = xOf (item.lowMs), high = xOf (item.highMs);
        g.setColour (markColour().withAlpha (0.6f));
        g.fillRoundedRectangle (low, strip.getCentreY() - 4.0f, juce::jmax (4.0f, high - low), 8.0f, 4.0f);

        // 가운뎃값
        g.setColour (ui::theme().text);
        g.fillRoundedRectangle (xOf (item.medianMs) - 1.5f, strip.getCentreY() - 8.0f, 3.0f, 16.0f, 1.5f);

        // 치우침(가운뎃값)과 흩어짐을 숫자로. 치우침이 작으면 ✓
        const auto font = juce::Font (juce::FontOptions (ui::type::caption));
        const auto lean = ui::formatLean (item.medianMs);
        const auto leanWidth = juce::GlyphArrangement::getStringWidthInt (font, lean);
        g.setFont (font);
        g.drawText (lean, verdictArea.removeFromLeft (leanWidth + 2), juce::Justification::centredLeft, false);

        if (isCentred (item.medianMs))
            drawCheck (g, verdictArea.removeFromLeft (16).toFloat().withSizeKeepingCentre (9.0f, 7.0f).translated (3.0f, 0.0f), ui::theme().good);

        g.setColour (ui::theme().textMuted);
        g.drawText (ui::separator() + ui::plusMinus() + ui::formatMs (item.spreadMs()), verdictArea, juce::Justification::centredLeft, true);
    }

    // 가로축: 가운데가 박, 왼쪽이 빠름, 오른쪽이 늦음
    const auto axis = juce::Rectangle<float> (stripLeft, static_cast<float> (y), stripRight - stripLeft, static_cast<float> (axisHeight));
    g.setFont (juce::FontOptions (ui::type::small));
    g.setColour (ui::theme().textMuted);

    for (const auto ms : { -60, -30, 0, 30, 60 })
    {
        const auto x = xOf (static_cast<float> (ms));
        g.drawText ((ms > 0 ? "+" : "") + juce::String (ms), juce::Rectangle<float> (x - 20.0f, axis.getY(), 40.0f, axis.getHeight()), juce::Justification::centred);
    }

    // 띠의 바깥 양쪽에: 왼쪽이 빠름, 오른쪽이 늦음 (단위 ms)
    g.drawText (ko ("빠름"), juce::Rectangle<float> (axis.getX() - 64.0f, axis.getY(), 56.0f, axis.getHeight()), juce::Justification::centredRight);
    g.drawText (ko ("늦음 (ms)"), juce::Rectangle<float> (axis.getRight() + 14.0f, axis.getY(), 80.0f, axis.getHeight()), juce::Justification::centredLeft);
}

//==============================================================================
void FlowChart::setData (std::vector<Point> newPoints, std::vector<hvd::PracticeSection> newSections)
{
    points = std::move (newPoints);
    sections = std::move (newSections);
    hovered = -1;

    firstSeconds = 0.0;
    lastSeconds = 1.0;

    if (! sections.empty())
    {
        firstSeconds = sections.front().startSeconds;
        lastSeconds = sections.back().endSeconds;
    }

    for (const auto& point : points)
    {
        firstSeconds = juce::jmin (firstSeconds, static_cast<double> (point.seconds));
        lastSeconds = juce::jmax (lastSeconds, static_cast<double> (point.seconds));
    }

    repaint();
}

juce::Rectangle<float> FlowChart::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (40.0f).withTrimmedRight (6.0f).withTrimmedTop (4.0f).withTrimmedBottom (18.0f);
}

float FlowChart::xOf (double seconds) const
{
    const auto plot = plotArea();
    return plot.getX() + static_cast<float> ((seconds - firstSeconds) / juce::jmax (1.0, lastSeconds - firstSeconds)) * plot.getWidth();
}

float FlowChart::yOf (float offsetMs) const
{
    // 늦은 것이 위, 빠른 것이 아래
    const auto plot = plotArea();
    return plot.getCentreY() - juce::jlimit (-chartMs, chartMs, offsetMs) / chartMs * plot.getHeight() * 0.5f;
}

void FlowChart::paint (juce::Graphics& g)
{
    const auto plot = plotArea();
    g.setColour (tileColour());
    g.fillRoundedRectangle (plot, 4.0f);

    if (points.empty())
        return;

    // 박에 맞았다고 보는 범위
    g.setColour (ui::theme().text.withAlpha (0.045f));
    g.fillRect (plot.getX(), yOf (ui::timing::onBeatMs), plot.getWidth(), yOf (-ui::timing::onBeatMs) - yOf (ui::timing::onBeatMs));

    // 세로축: 0이 박
    g.setFont (juce::FontOptions (ui::type::small));

    for (const auto ms : { -60, -30, 0, 30, 60 })
    {
        const auto y = yOf (static_cast<float> (ms));
        g.setColour (ms == 0 ? zeroLine() : ui::theme().border.withAlpha (0.7f));
        g.fillRect (plot.getX(), y - 0.5f, plot.getWidth(), 1.0f);
        g.setColour (ui::theme().textMuted);
        g.drawText ((ms > 0 ? "+" : "") + juce::String (ms), juce::Rectangle<float> (0.0f, y - 8.0f, plot.getX() - 6.0f, 16.0f), juce::Justification::centredRight);
    }

    g.drawText (ko ("늦음"), juce::Rectangle<float> (plot.getX() + 6.0f, plot.getY() + 2.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
    g.drawText (ko ("빠름"), juce::Rectangle<float> (plot.getX() + 6.0f, plot.getBottom() - 16.0f, 40.0f, 14.0f), juce::Justification::centredLeft);

    // 가로축: 곡의 시간
    const auto length = lastSeconds - firstSeconds;
    const auto step = length <= 90.0 ? 15.0 : (length <= 300.0 ? 30.0 : 60.0);

    for (auto seconds = std::ceil (firstSeconds / step) * step; seconds <= lastSeconds; seconds += step)
    {
        const auto x = xOf (seconds);
        g.setColour (ui::theme().border.withAlpha (0.5f));
        g.fillRect (x - 0.5f, plot.getY(), 1.0f, plot.getHeight());
        g.setColour (ui::theme().textMuted);
        g.drawText (ui::formatMinutes (seconds), juce::Rectangle<float> (x - 24.0f, plot.getBottom() + 2.0f, 48.0f, 14.0f), juce::Justification::centred);
    }

    // 마우스가 가리키는 구간
    if (juce::isPositiveAndBelow (hovered, static_cast<int> (sections.size())))
    {
        const auto& section = sections[static_cast<std::size_t> (hovered)];
        g.setColour (ui::theme().text.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (section.startSeconds), plot.getY(), xOf (section.endSeconds), plot.getBottom()));
    }

    // 타격 하나하나
    g.setColour (ui::theme().text.withAlpha (0.22f));

    for (const auto& point : points)
        g.fillEllipse (xOf (point.seconds) - 1.2f, yOf (point.offsetMs) - 1.2f, 2.4f, 2.4f);

    // 구간마다: 가운데 80%의 띠와 가운뎃값. 구간끼리 잇지 않고 계단처럼 그린다 (구간 사이의 값을 지어내지 않는다).
    for (const auto& section : sections)
    {
        if (section.stats.count < minSectionHits)
            continue;

        const auto left = xOf (section.startSeconds) + 1.0f, right = xOf (section.endSeconds) - 1.0f;

        if (right - left < 2.0f)
            continue;

        const auto top = yOf (section.stats.highMs), bottom = yOf (section.stats.lowMs);
        g.setColour (markColour().withAlpha (0.28f));
        g.fillRoundedRectangle (left, top, right - left, juce::jmax (2.0f, bottom - top), 2.0f);

        g.setColour (markColour());
        g.fillRect (left, yOf (section.stats.medianMs) - 1.0f, right - left, 2.0f);
    }
}

void FlowChart::mouseMove (const juce::MouseEvent& event)
{
    auto found = -1;

    if (plotArea().contains (event.position))
        for (std::size_t i = 0; i < sections.size(); ++i)
            if (event.position.x >= xOf (sections[i].startSeconds) && event.position.x < xOf (sections[i].endSeconds))
                found = static_cast<int> (i);

    if (found != hovered)
    {
        hovered = found;
        repaint();

        if (onHover != nullptr)
            onHover (found >= 0 ? &sections[static_cast<std::size_t> (found)] : nullptr);
    }
}

void FlowChart::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0)
    {
        hovered = -1;
        repaint();

        if (onHover != nullptr)
            onHover (nullptr);
    }
}

//==============================================================================
PracticePage::PracticePage (Session& sessionToUse)
    : session (sessionToUse)
{
    ui::setupLabel (*this, countLabel, {}, ui::Text::caption);

    list.setModel (this);
    list.setRowHeight (listRowHeight);
    list.setOutlineThickness (0);
    ui::noFocus (list);
    addAndMakeVisible (list);

    reloadButton.onClick = [this] { reload(); };
    folderButton.onClick = [this]
    {
        if (const auto folder = session.getPlayLogFolder(); folder.isDirectory())
            folder.revealToUser();
    };

    addAndMakeVisible (reloadButton);
    addAndMakeVisible (folderButton);

    addAndMakeVisible (timingRows);
    addAndMakeVisible (flowChart);
    flowChart.onHover = [this] (const hvd::PracticeSection* section) { showHover (section); };

    ui::setupLabel (*this, hoverLabel, {}, ui::Text::caption);
    showHover (nullptr);
}

juce::Rectangle<int> PracticePage::detailArea() const
{
    return getLocalBounds().withTrimmedLeft (listWidth + ui::space::medium);
}

PracticePage::DetailLayout PracticePage::layoutDetail() const
{
    auto area = detailArea().reduced (ui::space::large);

    DetailLayout layout;
    layout.title = area.removeFromTop (26);
    layout.subtitle = area.removeFromTop (20);
    area.removeFromTop (ui::space::medium);
    layout.tiles = area.removeFromTop (80);
    area.removeFromTop (ui::space::large);
    layout.rowsCaption = area.removeFromTop (20);
    layout.rows = area.removeFromTop (timingRows.getIdealHeight());
    area.removeFromTop (ui::space::medium);
    layout.flowCaption = area.removeFromTop (20);
    layout.trend = area.removeFromBottom (20);
    area.removeFromBottom (2);
    layout.hover = area.removeFromBottom (18);
    area.removeFromBottom (2);
    layout.chart = area;
    return layout;
}

void PracticePage::resized()
{
    // 왼쪽: 머리 줄(연주 수, 새로 고침, 폴더)과 목록
    auto left = getLocalBounds().removeFromLeft (listWidth);
    auto header = left.removeFromTop (ui::space::control);
    folderButton.setBounds (header.removeFromRight (ui::space::control));
    reloadButton.setBounds (header.removeFromRight (ui::space::control));
    countLabel.setBounds (header);
    left.removeFromTop (ui::space::tight);
    list.setBounds (left);

    const auto layout = layoutDetail();
    const auto showDetail = juce::isPositiveAndBelow (shown, getNumTakes()) && rows[static_cast<std::size_t> (shown)].hasChart;

    timingRows.setVisible (showDetail);
    flowChart.setVisible (showDetail);
    hoverLabel.setVisible (showDetail);
    timingRows.setBounds (layout.rows);
    flowChart.setBounds (layout.chart);
    hoverLabel.setBounds (layout.hover.withTrimmedLeft (40));
}

void PracticePage::paint (juce::Graphics& g)
{
    // 오른쪽: 고른 연주
    const auto panel = detailArea();
    ui::fillPanel (g, panel);

    if (! juce::isPositiveAndBelow (shown, getNumTakes()))
    {
        g.setColour (ui::theme().textMuted);
        g.setFont (juce::FontOptions (ui::type::body));
        g.drawFittedText (ko ("기록 없음\n\n반주 화면이나 리듬 게임에서 곡을 틀고 치면 쌓입니다. 녹음은 필요 없습니다."),
                          panel.reduced (40), juce::Justification::centred, 6);
        return;
    }

    const auto& row = rows[static_cast<std::size_t> (shown)];
    const auto layout = layoutDetail();

    g.setColour (ui::theme().textMuted);
    g.setFont (juce::FontOptions (ui::type::caption));
    auto title = layout.title;
    g.drawText (whenLine, title.removeFromRight (150), juce::Justification::centredRight);

    g.setColour (ui::theme().text);
    g.setFont (juce::FontOptions (ui::type::section, juce::Font::bold));
    g.drawText (row.take.getTitle(), title, juce::Justification::centredLeft, true);

    g.setColour (ui::theme().textMuted);
    g.setFont (juce::FontOptions (ui::type::caption));
    g.drawText (subtitleText, layout.subtitle, juce::Justification::centredLeft, true);

    if (! row.hasChart)
    {
        g.setFont (juce::FontOptions (ui::type::body));
        g.drawFittedText (ko ("채보가 없어 박자를 재지 못했습니다.\n리듬 게임에서 이 곡의 채보를 만들면 나옵니다."),
                          panel.reduced (40).withTrimmedTop (80), juce::Justification::centred, 4);
        return;
    }

    // 요약 수치 네 개: 타격 · 박에 맞음 · 치우침 · 흩어짐
    auto tilesArea = layout.tiles;
    const auto tileWidth = (tilesArea.getWidth() - 3 * ui::space::small) / 4;

    for (const auto& tile : tiles)
    {
        auto area = tilesArea.removeFromLeft (tileWidth);
        tilesArea.removeFromLeft (ui::space::small);

        g.setColour (tileColour());
        g.fillRoundedRectangle (area.toFloat(), juce::jmin (8.0f, ui::theme().controlRadius));
        area = area.reduced (ui::space::medium, ui::space::small);

        // 윗줄: 무엇인지. 가운데: 값. 아랫줄: 덧붙이는 말 (좁은 창에서도 글자가 겹치지 않게 줄을 나눈다).
        g.setColour (ui::theme().textMuted);
        g.setFont (juce::FontOptions (ui::type::caption));
        g.drawText (tile.caption, area.removeFromTop (16), juce::Justification::centredLeft, true);
        g.drawText (tile.note, area.removeFromBottom (16), juce::Justification::centredLeft, true);

        const auto valueFont = juce::Font (juce::FontOptions (ui::type::display, juce::Font::bold));
        g.setColour (ui::theme().text);
        g.setFont (valueFont);
        g.drawText (tile.value, area, juce::Justification::centredLeft, true);

        if (tile.checked)
        {
            const auto x = static_cast<float> (area.getX()) + juce::GlyphArrangement::getStringWidth (valueFont, tile.value) + 10.0f;
            drawCheck (g, juce::Rectangle<float> (x, static_cast<float> (area.getCentreY()) - 6.0f, 15.0f, 11.0f), ui::theme().good);
        }
    }

    g.setColour (ui::theme().textMuted);
    g.setFont (juce::FontOptions (ui::type::caption));
    g.drawText (ko ("악기별"), layout.rowsCaption, juce::Justification::centredLeft);
    g.drawText (ko ("흐름"), layout.flowCaption, juce::Justification::centredLeft);
    paintLegend (g, layout.rowsCaption);
    g.setColour (ui::theme().textMuted);
    g.setFont (juce::FontOptions (ui::type::caption));
    g.drawText (trendText, layout.trend, juce::Justification::centredLeft, true);
}

void PracticePage::paintLegend (juce::Graphics& g, juce::Rectangle<int> area) const
{
    // 오른쪽 위의 범례 하나: 띠 = 가운데 80%, 눈금 = 가운뎃값, 점 = 타격, 옅은 칸 = 박에 맞음
    const auto& t = ui::theme();
    const auto font = juce::Font (juce::FontOptions (ui::type::caption));
    g.setFont (font);

    struct Entry
    {
        int kind;
        juce::String text;
    };

    const Entry entries[] = { { 0, ko ("가운데 80%") }, { 1, ko ("가운뎃값") }, { 2, ko ("타격") },
                              { 3, ko ("박에 맞음 ") + ui::plusMinus() + ui::formatMs (ui::timing::onBeatMs) } };

    for (int i = static_cast<int> (std::size (entries)) - 1; i >= 0; --i)
    {
        const auto& entry = entries[i];
        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (font, entry.text);
        g.setColour (t.textMuted);
        g.drawText (entry.text, area.removeFromRight (textWidth), juce::Justification::centredLeft, false);

        auto mark = area.removeFromRight (22).toFloat().withTrimmedRight (6.0f);
        const auto c = mark.getCentre();

        switch (entry.kind)
        {
            case 0:  g.setColour (markColour().withAlpha (0.6f)); g.fillRoundedRectangle (mark.withSizeKeepingCentre (14.0f, 7.0f), 3.5f); break;
            case 1:  g.setColour (t.text); g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, 13.0f).withCentre (c), 1.5f); break;
            case 2:  g.setColour (t.text.withAlpha (0.5f)); g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (c)); break;
            default: g.setColour (t.text.withAlpha (0.12f)); g.fillRect (mark.withSizeKeepingCentre (14.0f, 12.0f)); break;
        }

        area.removeFromRight (ui::space::medium);
    }
}

void PracticePage::pageShown()
{
    reload();
}

void PracticePage::reload()
{
    // 고른 연주를 다시 찾을 수 있게 적어 둔다.
    juce::File shownLog;
    juce::Time shownTime;

    if (juce::isPositiveAndBelow (shown, getNumTakes()))
    {
        shownLog = rows[static_cast<std::size_t> (shown)].take.logFile;
        shownTime = rows[static_cast<std::size_t> (shown)].take.time;
    }

    session.flushPlayLog();

    const auto folder = session.getPlayLogFolder();
    const auto writing = session.getPlayLogFile();    // 지금 적고 있는 파일은 늘 다시 읽는다
    std::map<juce::String, ParsedLog> fresh;
    rows.clear();

    if (folder.isDirectory())
    {
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "play-*.jsonl", juce::File::findFiles))
        {
            const auto file = entry.getFile();
            const auto key = file.getFullPathName();
            ParsedLog parsed;

            if (const auto found = parsedLogs.find (key);
                found != parsedLogs.end() && file != writing && found->second.size == file.getSize()
                    && found->second.modified == file.getLastModificationTime())
            {
                parsed = found->second;
            }
            else
            {
                parsed.size = file.getSize();
                parsed.modified = file.getLastModificationTime();
                parsed.takes = readPracticeTakes (file);
            }

            for (const auto& take : parsed.takes)
            {
                Row row;
                row.take = take;
                rows.push_back (std::move (row));
            }

            fresh[key] = std::move (parsed);
        }
    }

    parsedLogs = std::move (fresh);

    // 곡마다 채보를 한 번만 읽어 박자를 잰다.
    std::map<juce::String, std::pair<bool, hvd::Chart>> charts;

    for (auto& row : rows)
    {
        const auto key = row.take.song.getFullPathName();
        auto found = charts.find (key);

        if (found == charts.end())
        {
            hvd::Chart chart;
            const auto ok = session.getCharts().loadChartForFile (row.take.song, chart) && chart.beats.size() >= 8;
            found = charts.emplace (key, std::make_pair (ok, std::move (chart))).first;
        }

        row.hasChart = found->second.first;

        if (row.hasChart)
            row.report = hvd::analysePractice (row.take.hits, found->second.second);
    }

    std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.take.time > b.take.time; });

    auto select = rows.empty() ? -1 : 0;

    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].take.logFile == shownLog && rows[i].take.time == shownTime)
            select = static_cast<int> (i);

    list.updateContent();
    countLabel.setText (rows.empty() ? juce::String() : ko ("연주 ") + ui::withCommas (static_cast<int> (rows.size())), juce::dontSendNotification);
    shown = -1;

    if (select >= 0)
        list.selectRow (select);    // selectedRowsChanged가 불린다

    showTake (select);
}

int PracticePage::getNumRows()
{
    return getNumTakes();
}

void PracticePage::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow (rowNumber, getNumTakes()))
        return;

    const auto& row = rows[static_cast<std::size_t> (rowNumber)];

    if (rowIsSelected)
    {
        g.setColour (ui::theme().selection);
        g.fillRoundedRectangle (juce::Rectangle<int> (0, 0, width, height).reduced (2, 1).toFloat(), juce::jmax (3.0f, ui::theme().controlRadius - 2.0f));
    }

    // 두 줄: 위에 곡 이름, 아래에 언제 · 타격 수. 오른쪽에 박에 맞은 비율.
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (ui::space::small, 4);
    const auto accuracy = area.removeFromRight (48);

    g.setColour (ui::theme().text);
    g.setFont (juce::FontOptions (ui::type::body));
    g.drawText (row.take.getTitle(), area.removeFromTop (area.getHeight() / 2).withTrimmedRight (ui::space::small), juce::Justification::centredLeft, true);
    g.drawText (row.hasChart ? ui::formatPercent (row.report.all.withinPercent) : ui::dash(), accuracy, juce::Justification::centredRight);

    g.setColour (ui::theme().textMuted);
    g.setFont (juce::FontOptions (ui::type::caption));
    g.drawText (ui::formatWhen (row.take.time, juce::Time::getCurrentTime()) + ui::separator() + ui::withCommas (row.take.numPlayed) + ko ("타"),
                area, juce::Justification::centredLeft, true);
}

juce::String PracticePage::getTooltipForRow (int rowNumber)
{
    if (! juce::isPositiveAndBelow (rowNumber, getNumTakes()) || ! rows[static_cast<std::size_t> (rowNumber)].hasChart)
        return ko ("채보가 없어 박자를 재지 못함");

    return ko ("박에 맞음 ") + ui::formatPercent (rows[static_cast<std::size_t> (rowNumber)].report.all.withinPercent);
}

void PracticePage::selectedRowsChanged (int lastRowSelected)
{
    if (lastRowSelected != shown)
        showTake (lastRowSelected);
}

void PracticePage::showTake (int index)
{
    shown = juce::isPositiveAndBelow (index, getNumTakes()) ? index : -1;

    if (shown < 0)
    {
        resized();
        repaint();
        return;
    }

    const auto& row = rows[static_cast<std::size_t> (shown)];
    const auto& take = row.take;
    const auto now = juce::Time::getCurrentTime();

    whenLine = ui::formatWhen (take.time, now);

    const auto dot = ui::separator();
    subtitleText = ko ("곡의 ") + ui::formatMinutes (take.firstSeconds) + " ~ " + ui::formatMinutes (take.lastSeconds);

    if (take.numByKey > 0)    subtitleText << dot << ko ("키보드 ") << ui::withCommas (take.numByKey) << ko ("타");
    if (take.numByMouse > 0)  subtitleText << dot << ko ("마우스 ") << ui::withCommas (take.numByMouse) << ko ("타");
    if (take.numByMidi > 0)   subtitleText << dot << "MIDI " << ui::withCommas (take.numByMidi) << ko ("타");

    if (take.slowestSpeed < 0.999f)
        subtitleText << dot << ko ("속도 ") << ui::formatPercent (take.slowestSpeed * 100.0f);

    if (take.fromGame)
        subtitleText << dot << ko ("리듬 게임");

    if (take.numRolled > 0)
        subtitleText << dot << ko ("이어 친 ") << take.numRolled << ko ("타는 뺌");

    if (row.hasChart)
    {
        const auto& all = row.report.all;
        const auto span = juce::jmax (1.0, take.lastSeconds - take.firstSeconds);

        tiles[0] = { ko ("타격"), ui::withCommas (take.numPlayed), ko ("초당 ") + juce::String (take.numPlayed / span, 1) + ko ("타") };
        tiles[1] = { ko ("박에 맞음"), ui::formatPercent (all.withinPercent), ui::plusMinus() + ui::formatMs (ui::timing::onBeatMs) + ko (" 안") };
        tiles[2] = { ko ("치우침"), ui::formatLean (all.medianMs), ko ("가운뎃값"), isCentred (all.medianMs) };
        tiles[3] = { ko ("흩어짐"), ui::plusMinus() + ui::formatMs (all.spreadMs()), ko ("가운데 80%") };

        timingRows.setStats (row.report.groups);

        std::vector<FlowChart::Point> points;
        points.reserve (take.hits.size());

        for (std::size_t i = 0; i < take.hits.size() && i < row.report.offsetsMs.size(); ++i)
            points.push_back ({ static_cast<float> (take.hits[i].seconds), row.report.offsetsMs[i] });

        flowChart.setData (std::move (points), row.report.sections);

        // 같은 곡을 친 다른 기록들과 견준다 (오래된 것부터).
        std::vector<const Row*> same;

        for (const auto& other : rows)
            if (other.hasChart && other.take.song == take.song)
                same.push_back (&other);

        std::sort (same.begin(), same.end(), [] (const Row* a, const Row* b) { return a->take.time < b->take.time; });

        // 같은 곡을 두 번 넘게 쳤을 때만 박에 맞은 비율의 추이를 보인다. [ ]가 지금 보는 연주다.
        trendText.clear();

        if (same.size() >= 2)
        {
            trendText = ko ("박에 맞음 추이 (") + juce::String (static_cast<int> (same.size())) + ko ("번)   ");
            const auto first = same.size() > 8 ? same.size() - 8 : 0;

            for (auto i = first; i < same.size(); ++i)
            {
                const auto percent = ui::formatPercent (same[i]->report.all.withinPercent);
                trendText << (i > first ? juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  ")) : juce::String())
                          << (same[i] == &row ? "[" + percent + "]" : percent);
            }
        }
    }

    showHover (nullptr);
    resized();
    repaint();
}

void PracticePage::showHover (const hvd::PracticeSection* section)
{
    // 그래프 위에 마우스를 올린 동안에만 그 구간의 수치를 적는다.
    if (section == nullptr)
    {
        hoverLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto dot = ui::separator();
    const auto& stats = section->stats;
    juce::String text;
    text << ui::formatMinutes (section->startSeconds) << " ~ " << ui::formatMinutes (section->endSeconds) << dot << stats.count << ko ("타");

    if (stats.count >= minSectionHits)
        text << dot << ko ("치우침 ") << ui::formatLean (stats.medianMs) << dot << ko ("흩어짐 ") << ui::plusMinus() << ui::formatMs (stats.spreadMs())
             << dot << ko ("박에 맞음 ") << ui::formatPercent (stats.withinPercent);

    hoverLabel.setText (text, juce::dontSendNotification);
}
