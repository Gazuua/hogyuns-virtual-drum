#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

#include "MusicFile.h"    // decodeStereo는 이리로 옮겼다. 부르던 곳이 그대로 쓰게 둔다
#include "core/Chart.h"

/** 채보를 JSON 글로 적는다 / JSON 글에서 읽는다. */
juce::String chartToJson (const hvd::Chart& chart, const juce::var& header);
bool chartFromJson (const juce::String& text, hvd::Chart& chart, juce::var* header = nullptr);

/** 채보를 마디마다 한 줄씩, 16분음표 칸으로 나눠 글로 그린다 (조정할 때 눈으로 보려고). */
juce::String chartToTab (const hvd::Chart& chart);

/** --transcribe: 소리 파일 하나를 받아 적어 보고서를 만든다. chartFile을 주면 채보도 저장한다. */
juce::String transcribeToReport (const juce::File& audio, bool isolatedDrums, bool logEvents, const juce::File& chartFile);

/** 기본 킷으로 16마디짜리 록 패턴을 연주한 소리를 WAV 파일로 쓴다 (자체 검사용 곡). */
bool writeTestSong (const juce::File& dataRoot, const juce::File& target, double tempo);

/** --chartscore: writeTestSong()으로 만든 패턴이 들어 있는 소리(드럼만 분리한 것)를 받아 적고, 패턴과 견줘 채점한다.
    실제 곡의 반주에 그 패턴을 섞은 뒤 드럼을 분리한 소리를 주면, 분리부터 받아 적기까지 전체가 얼마나 맞는지 알 수 있다. */
bool scoreStemAgainstPattern (const juce::File& stem, double tempo, juce::String& report);

/** --charttest: 기본 킷으로 아는 패턴을 연주해 그 소리를 받아 적고, 얼마나 맞는지 잰다. 기준을 넘으면 true. */
bool runChartTest (const juce::File& dataRoot, juce::String& report);
