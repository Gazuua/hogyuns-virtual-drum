#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <vector>

#include "core/Engine.h"
#include "core/Replay.h"
#include "core/TrackPlayer.h"

/** 연주 기록: 친 타격을 패드 id와 시각으로 모아 두고, 다시 재생하거나 MIDI 파일로 주고받는다.
    화면 스레드에서만 쓴다. 화면에 알리는 일(sendChangeMessage)은 Session이 한다. */
class TakeRecorder
{
public:
    struct Hit
    {
        double seconds = 0.0;
        juce::String padId;
        float velocity = 1.0f;
    };

    TakeRecorder (hvd::Engine& engineToUse, hvd::TrackPlayer& trackToUse);

    /** 지금부터 새로 기록한다. 다시 재생 중이면 먼저 멈춘다. */
    void start();

    /** 기록을 마친다. 그 전에 엔진이 낸 타격을 모두 add()로 넘겨 둔다. 기록 중이 아니었으면 false. */
    bool stop();

    /** 엔진이 낸 타격 하나. 기록 중이고 다시 재생 중이 아닐 때만 담는다. */
    void add (const hvd::HitEvent& event, const hvd::Kit& kit);

    bool isRecording() const noexcept             { return recording; }
    bool hasTake() const noexcept                 { return ! take.empty(); }
    int getHitCount() const noexcept              { return static_cast<int> (take.size()); }
    double getLength() const noexcept             { return length; }

    /** 기록을 처음부터 다시 친다. 기록할 때 반주가 나오고 있었으면 같은 자리에서 함께 튼다. 칠 것이 없으면 false. */
    bool play (const hvd::Kit& kit);

    /** 다시 재생을 멈추고 엔진에 미리 넘긴 타격도 지운다. 멈춘 것이 없으면 false. */
    bool stopPlayback();

    bool isPlaying() const noexcept               { return replay.isActive(); }
    double getPlaybackSeconds() const noexcept    { return replay.getElapsedSeconds (engine.getClock()); }

    /** 곧 울릴 타격을 엔진에 넘긴다. poll마다 부른다. 다시 재생이 끝났으면 false. */
    bool pump();

    juce::String exportMidi (const juce::File& file, const hvd::Kit& kit, float tempo) const;

    /** MIDI 파일의 노트를 기록으로 읽는다. padOfNote는 노트 번호의 패드 번호(없으면 -1).
        읽었으면 imported가 true이고, 건너뛴 노트가 있으면 그 수를 알리는 글을 돌려준다. 실패하면 이유를 돌려준다. */
    juce::String importMidi (const juce::File& file, const std::function<int (int note)>& padOfNote, const hvd::Kit& kit, bool& imported);

private:
    hvd::Engine& engine;
    hvd::TrackPlayer& track;

    std::vector<Hit> take;
    bool recording = false;
    std::uint64_t startClock = 0;
    double length = 0.0;
    double trackPosition = -1.0;    // 기록을 시작할 때 반주가 재생 중이었으면 그 위치
    hvd::ReplayPlayer replay;
};
