#pragma once

#include <array>
#include <functional>
#include <map>
#include <vector>

#include "PracticeLog.h"
#include "Session.h"
#include "Ui.h"

/** 악기마다 박자가 어땠는지 한 줄씩 보여 준다: 가운데(0)가 박, 왼쪽이 빠름, 오른쪽이 늦음.
    띠는 타격의 가운데 80%가 놓인 범위이고, 흰 눈금은 가운뎃값이다. */
class TimingRows final : public juce::Component
{
public:
    void setStats (const std::array<hvd::TimingStats, hvd::numPracticeGroups>& newStats);
    void paint (juce::Graphics&) override;

    static constexpr int rowHeight = 26;
    static constexpr int axisHeight = 18;

    /** 타격이 있는 악기 수에 맞는 높이. */
    int getIdealHeight() const;

private:
    std::array<hvd::TimingStats, hvd::numPracticeGroups> stats {};
};

/** 곡이 흐르는 동안 박자가 어떻게 변했는지 보여 준다: 가로는 곡의 시간, 세로는 빠름(아래)과 늦음(위).
    점은 타격 하나하나, 선은 구간(네 마디)마다의 가운뎃값, 띠는 그 구간의 가운데 80%다. */
class FlowChart final : public juce::Component
{
public:
    struct Point
    {
        float seconds = 0.0f;
        float offsetMs = 0.0f;
    };

    void setData (std::vector<Point> newPoints, std::vector<hvd::PracticeSection> newSections);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** 마우스가 가리키는 구간이 바뀔 때 불린다. 가리키는 구간이 없으면 nullptr. */
    std::function<void (const hvd::PracticeSection*)> onHover;

private:
    juce::Rectangle<float> plotArea() const;
    float xOf (double seconds) const;
    float yOf (float offsetMs) const;

    std::vector<Point> points;
    std::vector<hvd::PracticeSection> sections;
    double firstSeconds = 0.0, lastSeconds = 1.0;
    int hovered = -1;
};

/** 연습 기록 화면: 곡을 틀고 따라 친 연주들(연주 로그에서 읽는다)과, 고른 연주의 박자를 보여 준다. */
class PracticePage final : public ui::Page,
                           private juce::ListBoxModel
{
public:
    explicit PracticePage (Session& sessionToUse);

    void paint (juce::Graphics&) override;
    void resized() override;
    void pageShown() override;

    /** 로그 폴더를 다시 읽는다. */
    void reload();

    int getNumTakes() const noexcept    { return static_cast<int> (rows.size()); }

private:
    struct Row
    {
        PracticeTake take;
        hvd::PracticeReport report;
        bool hasChart = false;
    };

    struct ParsedLog
    {
        juce::int64 size = -1;
        juce::Time modified;
        std::vector<PracticeTake> takes;
    };

    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    /** 요약 수치 하나: 작은 제목, 큰 값, 덧붙이는 말. */
    struct Tile
    {
        juce::String caption, value, note;
    };

    /** 오른쪽(고른 연주)에 놓이는 것들의 자리. paint와 resized가 함께 쓴다. */
    struct DetailLayout
    {
        juce::Rectangle<int> title, subtitle, tiles, rowsCaption, rows, flowCaption, chart, hover, trend;
    };

    void showTake (int index);
    void showHover (const hvd::PracticeSection* section);
    juce::Rectangle<int> detailArea() const;
    DetailLayout layoutDetail() const;

    Session& session;
    std::vector<Row> rows;                          // 새것부터
    std::map<juce::String, ParsedLog> parsedLogs;   // 로그 파일 경로 → 읽어 둔 것 (바뀌지 않은 파일은 다시 읽지 않는다)
    int shown = -1;

    // 고른 연주에 대해 적을 글
    juce::String whenLine, subtitleText, trendText;
    std::array<Tile, 4> tiles;

    juce::Label listCaption, hoverLabel;
    juce::ListBox list;
    juce::TextButton reloadButton, folderButton;
    TimingRows timingRows;
    FlowChart flowChart;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PracticePage)
};
