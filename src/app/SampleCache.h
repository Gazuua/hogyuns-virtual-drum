#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <vector>

#include "core/Kit.h"

/** 샘플 파일 하나를 읽은 결과. */
struct DecodedSample
{
    std::shared_ptr<const hvd::Sample> sample;    // 못 읽었으면 nullptr
    int trimmedFrames = 0;                        // 앞에서 잘라 낸 무음 (원래 파일의 프레임 수)
    juce::String error;                           // 못 읽은 이유
    bool reused = false;                          // 전에 읽어 둔 것을 그대로 썼다
};

/** 샘플 파일들을 메모리에 올린다. 결과는 files와 같은 순서다. 어느 스레드에서나 부를 수 있다 (오디오 스레드는 빼고).

    전에 읽은 파일이 그대로면(경로, 크기, 수정 시각이 같으면) 다시 디코드하지 않고 그 샘플을 함께 쓴다.
    샘플은 만든 뒤 바꾸지 않으므로 여러 킷이 함께 써도 된다. 새로 읽을 파일은 여러 스레드로 나눠 디코드한다. */
std::vector<DecodedSample> decodeSamples (const juce::Array<juce::File>& files);

/** 어느 킷도 쓰지 않게 된 샘플을 메모리 상한(sampleCacheUnusedBytes) 아래로 줄인다. 오래 안 쓴 것부터 지운다.
    킷을 지운 뒤에 부른다. 킷이 쓰고 있는 샘플은 그 킷이 지워질 때까지 남는다. */
void trimSampleCache();

/** 읽어 둔 샘플을 모두 잊는다 (검사용). */
void clearSampleCache();

/** 어느 킷도 쓰지 않는 샘플을 이만큼까지 남겨 둔다. 킷이나 소리를 바꿨다가 되돌릴 때 다시 읽지 않으려는 것이다. */
constexpr juce::int64 sampleCacheUnusedBytes = 512 * 1024 * 1024;

struct SampleCacheStats
{
    int numSamples = 0;
    juce::int64 bytes = 0;          // 모든 샘플
    juce::int64 unusedBytes = 0;    // 그 가운데 어느 킷도 쓰지 않는 것
};

SampleCacheStats getSampleCacheStats();
