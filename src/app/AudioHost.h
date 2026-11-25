#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <vector>

#include "core/Engine.h"
#include "core/PeakLimiter.h"
#include "core/TrackPlayer.h"

/** 오디오 출력 장치를 열고, 장치가 부를 때마다 엔진(드럼)과 반주 재생기가 만든 소리를 내보낸다.
    내보내는 소리를 WAV 파일로 녹음할 수도 있다.

    드럼에는 전체 음량을 곱한 뒤 리미터(hvd::PeakLimiter)를 걸어, 센 타격이 겹쳐도 꼭대기가 0.9를
    넘지 않게 한다. 반주는 그 뒤에 더하므로 전체 음량과 리미터의 영향을 받지 않는다. */
class AudioHost final : private juce::AudioIODeviceCallback,
                        private juce::ChangeListener
{
public:
    AudioHost (hvd::Engine& engineToUse, hvd::TrackPlayer& trackToUse);
    ~AudioHost() override;

    /** 출력 장치를 연다. 실패하면 이유를 돌려주고, 성공하면 빈 문자열을 돌려준다.

        rememberSettings가 true이면 지난번에 쓰던 장치 설정을 불러오고, 설정을 바꿀 때마다 저장한다.
        저장된 설정이 없으면 저지연 공유 모드를 가장 작은 버퍼로 연다. */
    juce::String start (bool rememberSettings);
    void stop();

    juce::AudioDeviceManager& getDeviceManager() noexcept    { return manager; }

    struct Status
    {
        bool open = false;
        juce::String typeName, deviceName;
        double sampleRate = 0.0;
        int bufferSize = 0;
        int outputLatency = 0;
    };

    Status getStatus() const;

    /** 장치 방식, 버퍼 크기, 지연을 한 줄로 적는다. */
    juce::String describe() const;

    int getCallbackCount() const noexcept        { return callbackCount.load (std::memory_order_relaxed); }
    int getLateCallbackCount() const noexcept    { return lateCallbacks.load (std::memory_order_relaxed); }
    void resetCounters() noexcept;

    /** 마지막으로 부른 뒤의 최대 출력 크기(0 ~ 1)를 돌려주고 0으로 되돌린다. */
    float takePeak() noexcept                    { return peak.exchange (0.0f, std::memory_order_relaxed); }

    /** resetCounters()를 부른 뒤의 최대 출력 크기. takePeak()와 달리 읽어도 지워지지 않는다. */
    float getMaxPeak() const noexcept            { return maxPeak.load (std::memory_order_relaxed); }

    /** true이면 모든 처리는 그대로 하되 장치로는 무음을 내보낸다. 소리 없이 검사할 때 쓴다.
        출력 크기 측정과 녹음에는 영향을 주지 않는다. */
    void setOutputMuted (bool shouldMute) noexcept  { outputMuted.store (shouldMute, std::memory_order_relaxed); }

    double getCpuUsage() const                   { return manager.getCpuUsage(); }

    /** 전체 음량(드럼과 메트로놈에 곱하는 값)의 기본값과 한도. Session과 믹서 화면도 이 값을 쓴다.
        기본값은 2026-11-25에 0.8에서 두 배로 올렸다(사용자 요청: 전체적으로 소리가 작다). */
    static constexpr float defaultMasterGain = 1.6f;
    static constexpr float maxMasterGain = 3.0f;

    void setMasterGain (float newGain) noexcept  { masterGain.store (newGain, std::memory_order_relaxed); }

    /** 엔진 시계와 반주 위치를 한 순간에 짝지은 값. 타격의 시각(엔진 시계)을 곡 안의 위치로 바꿀 때 쓴다. */
    struct TrackSync
    {
        std::uint64_t clock = 0;      // 엔진 시계
        double position = 0.0;        // 그때 장치로 나간 반주의 위치(초). 준비 시간 동안에는 음수
        float speed = 1.0f;           // 재생 속도 배율
        bool active = false;          // 반주가 진행 중인지

        /** 엔진 시계가 time일 때의 반주 위치. */
        double positionAt (std::uint64_t time, double sampleRate) const noexcept
        {
            if (! active || sampleRate <= 0.0)
                return position;

            const auto elapsed = static_cast<double> (static_cast<std::int64_t> (time - clock)) / sampleRate;
            return position + elapsed * static_cast<double> (speed);
        }
    };

    TrackSync getTrackSync() const noexcept;
    float getMasterGain() const noexcept         { return masterGain.load (std::memory_order_relaxed); }

    //==============================================================================
    /** 내보내는 소리를 WAV(24비트)로 녹음하기 시작한다. 메트로놈 소리는 녹음하지 않는다.
        includeTrack이 false이면 반주를 빼고 드럼만 녹음한다. 실패하면 이유를 돌려준다. */
    juce::String startRecording (const juce::File& file, bool includeTrack);
    void stopRecording();

    bool isRecording() const noexcept            { return activeWriter.load (std::memory_order_relaxed) != nullptr; }
    double getRecordedSeconds() const noexcept;
    juce::File getRecordingFile() const          { return recordingFile; }

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    void processChunk (float* const* outputs, int numOutputChannels, int offset, int numSamples) noexcept;

    std::unique_ptr<juce::XmlElement> createLowLatencyState();
    void useSmallestBuffer();
    void saveSettings();
    static juce::File getSettingsFile();

    hvd::Engine& engine;
    hvd::TrackPlayer& track;
    juce::AudioDeviceManager manager;
    bool started = false;
    bool remember = false;

    std::atomic<float> masterGain { defaultMasterGain };
    std::atomic<float> peak { 0.0f };
    std::atomic<float> maxPeak { 0.0f };
    std::atomic<bool> outputMuted { false };
    std::atomic<int> callbackCount { 0 };
    std::atomic<int> lateCallbacks { 0 };
    std::atomic<std::uint32_t> callbackSerial { 0 };    // 녹음을 멈출 때 콜백이 한 바퀴 돌았는지 확인하는 데 쓴다

    // 엔진 시계와 반주 위치의 짝. 쓰는 동안에는 syncSequence가 홀수다.
    std::atomic<std::uint32_t> syncSequence { 0 };
    std::atomic<std::uint64_t> syncClock { 0 };
    std::atomic<double> syncPosition { 0.0 };
    std::atomic<float> syncSpeed { 1.0f };
    std::atomic<bool> syncActive { false };

    // 녹음
    juce::TimeSliceThread writerThread { "HVD recording" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    std::atomic<bool> recordIncludesTrack { true };
    std::atomic<juce::int64> recordedFrames { 0 };
    double recordingSampleRate = 48000.0;
    juce::File recordingFile;

    // 오디오 스레드에서만 쓴다.
    juce::int64 lastCallbackTicks = 0;
    double expectedPeriodSeconds = 0.0;
    std::vector<float> mixLeft, mixRight, clickBuffer, recordLeft, recordRight;
    hvd::PeakLimiter drumLimiter;    // 전체 음량을 곱한 드럼에 건다. 반주를 더하기 전이다

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioHost)
};
