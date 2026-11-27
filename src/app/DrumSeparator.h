#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

/** 드럼 분리 도구(tools/demucs-env의 파이썬으로 도는 tools/separate_drums.py)를 띄우고 일을 맡긴다.

    도구는 하나만 띄워 여러 스레드가 함께 쓴다. 일은 큐 폴더에 파일로 넣고, 도구는 이름 순서대로 하나씩 처리한다.
    공개 함수는 어느 스레드에서 불러도 된다.
*/
class DrumSeparator final
{
public:
    /** 일을 맡긴 쪽. 기다리는 동안 멈출지 묻고 진행을 알린다. */
    struct Caller
    {
        virtual ~Caller() = default;
        virtual bool shouldStop() const = 0;
        virtual void pause (int milliseconds) = 0;              // 기다린다. 누가 깨우면 일찍 돌아온다
        virtual void setStage (const juce::String& stage) = 0;
        virtual void setFraction (float fraction) = 0;          // 지금 단계의 진행 정도 (0 ~ 1)
    };

    /** dataRoot 아래 tools/ 의 도구를 쓴다. */
    explicit DrumSeparator (const juce::File& dataRoot);
    ~DrumSeparator();

    bool isInstalled() const;

    /** 도구가 계산에 쓰는 장치: "cuda"(그래픽 카드) 또는 "cpu". 아직 띄운 적이 없으면 비어 있다. */
    juce::String getDevice() const;

    /** 도구를 띄우고 준비될 때까지 기다린다. 이미 떠 있으면 바로 돌아온다. 실패하면 이유를 돌려준다.
        다른 스레드가 띄우는 동안에는 여기서 기다린다. */
    juce::String start (Caller& caller);

    /** input(16비트 WAV)을 드럼(drums)과 나머지(backing)로 나눠 쓴다. start() 뒤에 부른다.
        실패하면 이유를, caller가 멈추라고 하면 "cancelled"를 돌려준다. */
    juce::String separate (const juce::File& input, const juce::File& drums, const juce::File& backing, Caller& caller);

    /** 도구를 끝낸다. 맡긴 일은 "도구가 중간에 끝났습니다"로 끝난다. */
    void stop();

private:
    void stopUnlocked();

    const juce::File python, script;

    // 도구 하나를 모든 스레드가 함께 쓴다 (lock으로 지킨다).
    juce::CriticalSection lock;
    juce::ChildProcess process;
    juce::File queue;
    juce::String device;
    std::atomic<int> jobCount { 0 };

    JUCE_DECLARE_NON_COPYABLE (DrumSeparator)
};
