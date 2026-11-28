#pragma once

#include <juce_core/juce_core.h>

#include <vector>

#include "core/Chart.h"
#include "core/Engine.h"
#include "core/KeyCode.h"
#include "core/Kit.h"
#include "core/PracticeStats.h"
#include "core/SpeedTrainer.h"
#include "core/TrackPlayer.h"

class ChartLibrary;

/** 반주의 반복 구간을 돌며 속도를 올린다 (규칙은 hvd::SpeedTrainer). 바퀴마다 사람이 친 타격의 박자를
    곡의 채보로 잰다 (연습 기록과 같은 analysePractice). Session이 갖고, 화면 스레드에서
    타격마다 addHit()를, poll마다 update()를 부른다. */
class LoopTrainer
{
public:
    LoopTrainer (hvd::TrackPlayer& trackToDrive, const ChartLibrary& chartsToRead);

    /** 켠다. 반복 구간이 없으면 false. songFile은 지금 열린 곡이다 (채보를 찾는다). */
    bool start (const juce::File& songFile);
    void stop();
    bool isOn() const noexcept                         { return on; }

    /** 엔진이 낸 타격 하나. songSeconds는 그 소리가 곡에 놓인 자리, engineSeconds는 엔진 시계(초). 사람이 친 것만 센다. */
    void addHit (const hvd::HitEvent& event, const hvd::Kit& kit, double songSeconds, double engineSeconds);

    /** 바퀴가 끝났으면 박자를 재어 속도를 정한다. 구간이 꺼지거나 다른 곡이 열리면 그만둔다. */
    void update (const juce::File& currentSong);

    /** 화면에 적을 말. 예: "75%  ·  지난 바퀴 82%  ·  80% 넘으면 올림" */
    juce::String getStatusText() const;

    const hvd::SpeedTrainer& getRule() const noexcept  { return rule; }
    bool hasTiming() const noexcept                    { return hasChart; }

private:
    void finishLap();

    struct LastHit
    {
        hvd::KeyCode key = hvd::invalidKey;
        double seconds = -1.0;
    };

    hvd::TrackPlayer& track;
    const ChartLibrary& charts;
    hvd::SpeedTrainer rule;
    hvd::Chart chart;
    juce::File song;
    bool on = false, hasChart = false;
    double lastPosition = 0.0;

    std::vector<hvd::PracticeHit> lapHits;    // 이번 바퀴에 박자를 잴 타격
    int lapPlayed = 0;                        // 이번 바퀴에 사람이 친 타격 (누르고 있어 이어진 것은 뺀다)
    std::vector<LastHit> lastHits;            // 패드마다 마지막 타격 (누르고 있어 이어진 타격을 가려낸다)
};
