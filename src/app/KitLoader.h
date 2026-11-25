#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

#include "core/Kit.h"

/** 킷 파일을 읽은 결과. */
struct KitLoadResult
{
    std::unique_ptr<hvd::Kit> kit;      // 킷을 전혀 못 읽었으면 비어 있다
    juce::String error;                 // 못 읽은 이유
    juce::StringArray warnings;         // 건너뛴 항목들
    double totalSeconds = 0.0;          // 샘플 길이의 합
    double averageTrimMs = 0.0;         // 샘플 앞에서 잘라 낸 무음의 평균 길이
    juce::int64 memoryBytes = 0;        // 샘플이 차지하는 메모리
};

/** kit.json과 거기 적힌 샘플 파일들을 읽어 엔진이 쓸 수 있는 킷을 만든다.
    샘플 경로는 kit.json이 있는 폴더를 기준으로 한다. */
KitLoadResult loadKit (const juce::File& kitFile);
