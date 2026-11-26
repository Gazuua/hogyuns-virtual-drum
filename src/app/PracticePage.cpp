#include "PracticePage.h"

#include <algorithm>
#include <cmath>

namespace
{

constexpr int listWidth = 372;
constexpr float axisMs = 80.0f;     // 악기별 줄의 가로 범위: ±80ms
constexpr float chartMs = 70.0f;    // 곡의 흐름의 세로 범위: ±70ms
constexpr float onBeatMs = 15.0f;   // 가운뎃값이 이 안이면 "박에 맞음"이라고 적는다. 이보다 작은 차이는 귀로 가리기 어렵고 재는 쪽의 오차와도 비슷하다
constexpr int minSectionHits = 3;   // 타격이 이보다 적은 구간은 그리지 않는다

// 자료를 나타내는 색은 하나만 쓴다. 악기의 색은 이름 옆의 점에만 쓴다 (빨강과 초록이 "나쁨/좋음"으로 읽히지 않게).
const juce::Colour markColour { 0xff64b5f6 };
const juce::Colour tileColour { 0xff2a343d };
const juce::Colour zeroLine { 0xff6b7a86 };
const juce::Colour selectedRow { 0xff3a4a58 };

// 항목 사이에 두는 가운뎃점. (한글과 마찬가지로 UTF-8로 만들어야 한다. 그냥 적으면 글자가 깨진다.)
const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");

struct GroupInfo
{
    const char* name;
    const char* padId;    // 색을 고를 때 쓴다
};

const GroupInfo groupInfos[hvd::numPracticeGroups] = {
    { "킥", "kick" }, { "스네어", "snare" }, { "하이햇", "hihat" }, { "라이드", "ride" }, { "크래시", "crash" }, { "탐", "tom" },
};

// 악기별 줄에서 이름, 타격 수, 설명이 차지하는 너비. 나머지가 띠를 그리는 자리다.
constexpr int nameWidth = 92, countWidth = 60, verdictWidth = 164;

/** 곡 안의 위치를 "1:23" 꼴로. */
juce::String songTime (double seconds)
{
    const auto whole = juce::jmax (0, juce::roundToInt (std::floor (seconds)));
    return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
}

/** "오늘 22:23", "어제 21:02", "11/23 14:20" */
juce::String whenText (juce::Time time, juce::Time now)
{
    const auto sameDay = [] (juce::Time a, juce::Time b) { return a.getYear() == b.getYear() && a.getDayOfYear() == b.getDayOfYear(); };
    const auto clock = time.formatted ("%H:%M");

    if (sameDay (time, now))
        return ko ("오늘 ") + clock;

    if (sameDay (time, now - juce::RelativeTime::days (1.0)))
        return ko ("어제 ") + clock;

    return juce::String (time.getMonth() + 1) + "/" + juce::String (time.getDayOfMonth()) + " " + clock;
}

/** 가운뎃값을 말로: "박에 맞음", "12ms 빠름", "9ms 늦음" */
juce::String verdictOf (float medianMs)
{
    if (std::abs (medianMs) < onBeatMs)
        return ko ("박에 맞음");

    return juce::String (juce::roundToInt (std::abs (medianMs))) + (medianMs < 0.0f ? ko ("ms 빠름") : ko ("ms 늦음"));
}

/** 1070 → "1,070" */
juce::String withCommas (int value)
{
    auto text = juce::String (std::abs (value));

    for (int at = text.length() - 3; at > 0; at -= 3)
        text = text.substring (0, at) + "," + text.substring (at);

    return value < 0 ? "-" + text : text;
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
        g.setColour (ui::padColour (groupInfos[group].padId));
        g.fillRoundedRectangle (nameArea.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat(), 2.0f);
        nameArea.removeFromLeft (8);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (ko (groupInfos[group].name), nameArea, juce::Justification::centredLeft);

        g.setColour (ui::quietText);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (withCommas (item.count) + ko ("타"), row.removeFromLeft (countWidth).withTrimmedRight (14), juce::Justification::centredRight);

        const auto verdictArea = row.removeFromRight (verdictWidth).withTrimmedLeft (14);
        const auto strip = row.toFloat();

        // 박에 맞았다고 보는 범위(±30ms)와 박의 자리(0)
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (-hvd::PracticeReport::tightMs), strip.getY() + 3.0f,
                                                                xOf (hvd::PracticeReport::tightMs), strip.getBottom() - 3.0f));
        g.setColour (zeroLine);
        g.fillRect (centre - 0.5f, strip.getY() + 2.0f, 1.0f, strip.getHeight() - 4.0f);

        // 타격의 가운데 80%가 놓인 범위
        const auto low = xOf (item.lowMs), high = xOf (item.highMs);
        g.setColour (markColour.withAlpha (0.6f));
        g.fillRoundedRectangle (low, strip.getCentreY() - 4.0f, juce::jmax (4.0f, high - low), 8.0f, 4.0f);

        // 가운뎃값
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (xOf (item.medianMs) - 1.5f, strip.getCentreY() - 8.0f, 3.0f, 16.0f, 1.5f);

        g.setFont (juce::FontOptions (13.0f));
        g.drawText (verdictOf (item.medianMs) + dot + juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (juce::roundToInt (item.spreadMs())) + "ms",
                    verdictArea, juce::Justification::centredLeft);
    }

    // 가로축: 가운데가 박, 왼쪽이 빠름, 오른쪽이 늦음
    const auto axis = juce::Rectangle<float> (stripLeft, static_cast<float> (y), stripRight - stripLeft, static_cast<float> (axisHeight));
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (ui::quietText);

    for (const auto ms : { -60, -30, 0, 30, 60 })
    {
        const auto x = xOf (static_cast<float> (ms));
        g.drawText ((ms > 0 ? "+" : "") + juce::String (ms), juce::Rectangle<float> (x - 20.0f, axis.getY(), 40.0f, axis.getHeight()), juce::Justification::centred);
    }

    g.drawText (ko ("빠름"), juce::Rectangle<float> (axis.getX() - 4.0f, axis.getY(), 40.0f, axis.getHeight()), juce::Justification::centredLeft);
    g.drawText (ko ("늦음 (ms)"), juce::Rectangle<float> (axis.getRight() - 56.0f, axis.getY(), 60.0f, axis.getHeight()), juce::Justification::centredRight);
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
    g.setColour (tileColour);
    g.fillRoundedRectangle (plot, 4.0f);

    if (points.empty())
        return;

    // 박에 맞았다고 보는 범위
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    g.fillRect (plot.getX(), yOf (hvd::PracticeReport::tightMs), plot.getWidth(), yOf (-hvd::PracticeReport::tightMs) - yOf (hvd::PracticeReport::tightMs));

    // 세로축: 0이 박
    g.setFont (juce::FontOptions (11.0f));

    for (const auto ms : { -60, -30, 0, 30, 60 })
    {
        const auto y = yOf (static_cast<float> (ms));
        g.setColour (ms == 0 ? zeroLine : ui::panelEdge.withAlpha (0.7f));
        g.fillRect (plot.getX(), y - 0.5f, plot.getWidth(), 1.0f);
        g.setColour (ui::quietText);
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
        g.setColour (ui::panelEdge.withAlpha (0.5f));
        g.fillRect (x - 0.5f, plot.getY(), 1.0f, plot.getHeight());
        g.setColour (ui::quietText);
        g.drawText (songTime (seconds), juce::Rectangle<float> (x - 24.0f, plot.getBottom() + 2.0f, 48.0f, 14.0f), juce::Justification::centred);
    }

    // 마우스가 가리키는 구간
    if (juce::isPositiveAndBelow (hovered, static_cast<int> (sections.size())))
    {
        const auto& section = sections[static_cast<std::size_t> (hovered)];
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (section.startSeconds), plot.getY(), xOf (section.endSeconds), plot.getBottom()));
    }

    // 타격 하나하나
    g.setColour (juce::Colours::white.withAlpha (0.22f));

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
        g.setColour (markColour.withAlpha (0.28f));
        g.fillRoundedRectangle (left, top, right - left, juce::jmax (2.0f, bottom - top), 2.0f);

        g.setColour (markColour);
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
    ui::setupLabel (*this, listCaption, ko ("곡을 틀고 따라 친 기록"), 14.0f, ui::quietText);

    list.setModel (this);
    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, ui::panel);
    list.setOutlineThickness (0);
    ui::noFocus (list);
    addAndMakeVisible (list);

    ui::setupButton (*this, reloadButton, ko ("새로 고침"), [this] { reload(); });
    ui::setupButton (*this, folderButton, ko ("로그 폴더 열기"), [this]
    {
        if (const auto folder = session.getPlayLogFolder(); folder.isDirectory())
            folder.revealToUser();
    });

    addAndMakeVisible (timingRows);
    addAndMakeVisible (flowChart);
    flowChart.onHover = [this] (const hvd::PracticeSection* section) { showHover (section); };

    ui::setupLabel (*this, hoverLabel, {}, 12.0f, ui::quietText);
    showHover (nullptr);
}

juce::Rectangle<int> PracticePage::detailArea() const
{
    return getLocalBounds().withTrimmedLeft (listWidth + 14);
}

PracticePage::DetailLayout PracticePage::layoutDetail() const
{
    auto area = detailArea().reduced (18, 14);

    DetailLayout layout;
    layout.title = area.removeFromTop (26);
    layout.subtitle = area.removeFromTop (20);
    area.removeFromTop (10);
    layout.tiles = area.removeFromTop (62);
    area.removeFromTop (14);
    layout.rowsCaption = area.removeFromTop (20);
    layout.rows = area.removeFromTop (timingRows.getIdealHeight());
    area.removeFromTop (10);
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
    auto left = getLocalBounds().removeFromLeft (listWidth);
    listCaption.setBounds (left.removeFromTop (22));
    left.removeFromTop (22);    // 목록의 머리글 자리 (paint에서 그린다)

    auto buttons = left.removeFromBottom (28);
    reloadButton.setBounds (buttons.removeFromLeft (96));
    buttons.removeFromLeft (6);
    folderButton.setBounds (buttons.removeFromLeft (120));
    left.removeFromBottom (6);
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
    // 목록의 머리글
    auto header = juce::Rectangle<int> (0, 22, listWidth, 22).reduced (8, 0);
    header.removeFromRight (list.getViewport() != nullptr && list.getViewport()->isVerticalScrollBarShown() ? list.getViewport()->getScrollBarThickness() : 0);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (ui::quietText);
    g.drawText (ko ("박에 맞음"), header.removeFromRight (60), juce::Justification::centredRight);
    g.drawText (ko ("타격"), header.removeFromRight (52), juce::Justification::centredRight);
    g.drawText (ko ("언제"), header.removeFromLeft (88), juce::Justification::centredLeft);
    g.drawText (ko ("곡"), header, juce::Justification::centredLeft);

    // 오른쪽: 고른 연주
    const auto panel = detailArea();
    g.setColour (ui::panel);
    g.fillRoundedRectangle (panel.toFloat(), 8.0f);

    if (! juce::isPositiveAndBelow (shown, getNumTakes()))
    {
        g.setColour (ui::quietText);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (ko ("기록이 없습니다.\n\n반주 화면이나 리듬 게임에서 곡을 틀고 치면 쌓입니다. 녹음은 필요 없습니다."),
                          panel.reduced (40), juce::Justification::centred, 6);
        return;
    }

    const auto& row = rows[static_cast<std::size_t> (shown)];
    const auto layout = layoutDetail();

    g.setColour (ui::quietText);
    g.setFont (juce::FontOptions (13.0f));
    auto title = layout.title;
    g.drawText (whenLine, title.removeFromRight (150), juce::Justification::centredRight);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (row.take.getTitle(), title, juce::Justification::centredLeft, true);

    g.setColour (ui::quietText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (subtitleText, layout.subtitle, juce::Justification::centredLeft, true);

    if (! row.hasChart)
    {
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (ko ("채보가 없어 박자를 재지 못했습니다.\n리듬 게임에서 이 곡의 채보를 만들면 나옵니다."),
                          panel.reduced (40).withTrimmedTop (80), juce::Justification::centred, 4);
        return;
    }

    // 요약 수치 네 개
    auto tilesArea = layout.tiles;
    const auto tileWidth = (tilesArea.getWidth() - 3 * 10) / 4;

    for (const auto& tile : tiles)
    {
        auto area = tilesArea.removeFromLeft (tileWidth);
        tilesArea.removeFromLeft (10);

        g.setColour (tileColour);
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
        area = area.reduced (12, 6);

        // 윗줄: 왼쪽에 무엇인지, 오른쪽에 덧붙이는 말. 아랫줄: 값.
        const auto top = area.removeFromTop (16);
        g.setColour (ui::quietText);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (tile.caption, top, juce::Justification::centredLeft);
        g.drawText (tile.note, top, juce::Justification::centredRight);

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText (tile.value, area, juce::Justification::centredLeft, true);
    }

    g.setColour (ui::quietText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (ko ("악기별 박자  ·  띠는 타격의 가운데 80%, 흰 눈금은 가운뎃값"), layout.rowsCaption, juce::Justification::centredLeft);
    g.drawText (ko ("곡의 흐름  ·  선은 네 마디의 가운뎃값, 띠는 가운데 80%, 점은 타격"), layout.flowCaption, juce::Justification::centredLeft);
    g.drawText (trendText, layout.trend, juce::Justification::centredLeft, true);
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
        g.setColour (selectedRow);
        g.fillRoundedRectangle (juce::Rectangle<int> (0, 0, width, height).reduced (1).toFloat(), 4.0f);
    }

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);
    const auto accuracy = area.removeFromRight (60);
    const auto count = area.removeFromRight (52);
    const auto when = area.removeFromLeft (88);

    g.setFont (juce::FontOptions (13.0f));
    g.setColour (ui::quietText);
    g.drawText (whenText (row.take.time, juce::Time::getCurrentTime()), when, juce::Justification::centredLeft);
    g.drawText (withCommas (row.take.numPlayed), count, juce::Justification::centredRight);

    g.setColour (juce::Colours::white);
    g.drawText (row.take.getTitle(), area.withTrimmedRight (6), juce::Justification::centredLeft, true);
    g.drawText (row.hasChart ? juce::String (juce::roundToInt (row.report.all.withinPercent)) + "%" : juce::String ("-"),
                accuracy, juce::Justification::centredRight);
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

    whenLine = whenText (take.time, now);

    subtitleText = ko ("곡의 ") + songTime (take.firstSeconds) + " ~ " + songTime (take.lastSeconds);

    if (take.numByKey > 0)    subtitleText << dot << ko ("키보드 ") << withCommas (take.numByKey) << ko ("타");
    if (take.numByMouse > 0)  subtitleText << dot << ko ("마우스 ") << withCommas (take.numByMouse) << ko ("타");
    if (take.numByMidi > 0)   subtitleText << dot << "MIDI " << withCommas (take.numByMidi) << ko ("타");

    if (take.slowestSpeed < 0.999f)
        subtitleText << dot << ko ("속도 ") << juce::roundToInt (take.slowestSpeed * 100.0f) << "%";

    if (take.fromGame)
        subtitleText << dot << ko ("리듬 게임");

    if (take.numRolled > 0)
        subtitleText << dot << ko ("누르고 있어 이어진 ") << take.numRolled << ko ("타는 박자에서 뺌");

    if (row.hasChart)
    {
        const auto& all = row.report.all;
        const auto span = juce::jmax (1.0, take.lastSeconds - take.firstSeconds);
        const auto onBeat = std::abs (all.medianMs) < onBeatMs;

        tiles[0] = { ko ("친 타격"), withCommas (take.numPlayed), ko ("초당 ") + juce::String (take.numPlayed / span, 1) + ko ("타") };
        tiles[1] = { ko ("박에 맞음"), juce::String (juce::roundToInt (all.withinPercent)) + "%",
                     juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (juce::roundToInt (hvd::PracticeReport::tightMs)) + ko ("ms 안") };
        tiles[2] = { ko ("빠름 · 늦음"), onBeat ? ko ("맞음") : verdictOf (all.medianMs),
                     ko ("가운뎃값 ") + (all.medianMs > 0.0f ? "+" : "") + juce::String (juce::roundToInt (all.medianMs)) + "ms" };
        tiles[3] = { ko ("흩어짐"), juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (juce::roundToInt (all.spreadMs())) + "ms", ko ("가운데 80%") };

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

        if (same.size() < 2)
        {
            trendText = ko ("이 곡의 첫 기록입니다. 다시 치면 비교가 나옵니다.");
        }
        else
        {
            trendText = ko ("이 곡을 친 ") + juce::String (static_cast<int> (same.size())) + ko ("번의 박에 맞은 비율:  ");
            const auto first = same.size() > 8 ? same.size() - 8 : 0;

            for (auto i = first; i < same.size(); ++i)
            {
                const auto percent = juce::String (juce::roundToInt (same[i]->report.all.withinPercent)) + "%";
                trendText << (i > first ? juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  ")) : juce::String())
                          << (same[i] == &row ? "[" + percent + "]" : percent);
            }

            trendText << ko ("   ([ ]가 지금 보는 기록)");
        }
    }

    showHover (nullptr);
    resized();
    repaint();
}

void PracticePage::showHover (const hvd::PracticeSection* section)
{
    if (section == nullptr)
    {
        hoverLabel.setText (ko ("마우스를 올리면 구간의 수치가 나옵니다."), juce::dontSendNotification);
        return;
    }

    const auto& stats = section->stats;
    juce::String text;
    text << songTime (section->startSeconds) << " ~ " << songTime (section->endSeconds) << dot << stats.count << ko ("타");

    if (stats.count >= minSectionHits)
        text << dot << verdictOf (stats.medianMs) << dot << juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) << juce::roundToInt (stats.spreadMs()) << "ms"
             << dot << ko ("박에 맞은 비율 ") << juce::roundToInt (stats.withinPercent) << "%";

    hoverLabel.setText (text, juce::dontSendNotification);
}
