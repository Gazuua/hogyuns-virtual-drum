#include "LoopTrainer.h"

#include "ChartLibrary.h"
#include "Format.h"
#include "Text.h"

LoopTrainer::LoopTrainer (hvd::TrackPlayer& trackToDrive, const ChartLibrary& chartsToRead)
    : track (trackToDrive),
      charts (chartsToRead)
{
}

bool LoopTrainer::start (const juce::File& songFile)
{
    const auto loopStart = track.getLoopStart(), loopEnd = track.getLoopEnd();

    if (songFile == juce::File() || loopEnd <= loopStart)
        return false;

    song = songFile;
    hasChart = charts.loadChartForFile (song, chart) && chart.beats.size() >= 8;

    rule.start (track.getSpeed());
    track.setSpeed (rule.speed);
    track.setLoop (loopStart, loopEnd, true);

    on = true;
    lastPosition = track.getPosition();
    lapHits.clear();
    lapPlayed = 0;
    lastHits.clear();
    return true;
}

void LoopTrainer::stop()
{
    on = false;
    lapHits.clear();
    lapPlayed = 0;
}

void LoopTrainer::addHit (const hvd::HitEvent& event, const hvd::Kit& kit, double songSeconds, double engineSeconds)
{
    const auto source = event.origin.source;

    if (! on || ! track.isPlaying() || event.pad >= kit.pads.size()
        || (source != hvd::HitSource::key && source != hvd::HitSource::mouse && source != hvd::HitSource::midi))
        return;

    // 같은 키로 같은 패드를 아주 촘촘하게 잇따라 친 것은 누르고 있어서 이어진 타격이다 (연습 기록과 같은 규칙).
    if (lastHits.size() < kit.pads.size())
        lastHits.resize (kit.pads.size());

    auto& last = lastHits[event.pad];
    const auto rolled = source == hvd::HitSource::key && event.origin.key != hvd::invalidKey && event.origin.key == last.key
                        && engineSeconds - last.seconds < hvd::heldRollGapSeconds;
    last = { source == hvd::HitSource::key ? event.origin.key : hvd::invalidKey, engineSeconds };

    if (rolled)
        return;

    ++lapPlayed;

    if (hvd::Lane lane; hasChart && hvd::laneForPadId (kit.pads[event.pad].id, lane))
        lapHits.push_back ({ songSeconds, lane, track.getSpeed() });
}

void LoopTrainer::update (const juce::File& currentSong)
{
    if (! on)
        return;

    // 구간 반복을 끄거나 다른 곡을 열면(리듬 게임 포함) 그만둔다.
    if (currentSong != song || ! track.isLoopEnabled())
    {
        stop();
        return;
    }

    // 구간 끝 가까이에서 시작 쪽으로 돌아갔으면 한 바퀴를 마친 것이다.
    const auto position = track.getPosition();
    const auto nearEnd = lastPosition >= track.getLoopEnd() - 0.3;

    if (track.isPlaying() && position < lastPosition - 0.05 && nearEnd)
        finishLap();

    lastPosition = position;
}

void LoopTrainer::finishLap()
{
    auto percent = -1.0f;

    if (hasChart && ! lapHits.empty())
    {
        const auto report = hvd::analysePractice (lapHits, chart);

        if (report.all.count > 0)
            percent = report.all.withinPercent;
    }

    if (rule.finishLap (lapPlayed, percent))
        track.setSpeed (rule.speed);

    lapHits.clear();
    lapPlayed = 0;
}

juce::String LoopTrainer::getStatusText() const
{
    if (! on)
        return {};

    const auto dot = ui::separator();
    auto text = ui::formatPercent (rule.speed * 100.0f);

    if (rule.atTarget())
        return text + dot + ko ("목표 속도");

    if (! hasChart)
        return text + dot + ko ("채보 없음, 두 바퀴마다 올림");

    if (rule.lastPercent >= 0.0f)
        text << dot << ko ("지난 바퀴 ") << ui::formatPercent (rule.lastPercent);

    return text + dot + ui::formatPercent (rule.passPercent) + ko (" 넘으면 올림");
}
