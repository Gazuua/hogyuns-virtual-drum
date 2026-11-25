#include "AudioHost.h"

#include <algorithm>
#include <cmath>

#include "Text.h"

namespace
{

const juce::String lowLatencyTypeName ("Windows Audio (Low Latency Mode)");
constexpr int scratchFrames = 4096;    // 이보다 큰 장치 버퍼는 나눠서 처리한다

/** 0.9를 넘는 부분만 부드럽게 눌러, 여러 소리가 겹쳐도 거칠게 찢어지지 않게 한다.
    드럼은 그 앞의 리미터가 0.9 아래로 붙잡으므로, 여기까지 오는 것은 드럼에 반주나 메트로놈이
    더해져 넘칠 때뿐이다. 마지막 안전망이다. */
inline float softClip (float value) noexcept
{
    constexpr float knee = 0.9f;
    const auto magnitude = std::abs (value);

    if (magnitude <= knee)
        return value;

    const auto shaped = knee + (1.0f - knee) * std::tanh ((magnitude - knee) / (1.0f - knee));
    return value < 0.0f ? -shaped : shaped;
}

} // namespace

AudioHost::AudioHost (hvd::Engine& engineToUse, hvd::TrackPlayer& trackToUse)
    : engine (engineToUse),
      track (trackToUse)
{
    for (auto* buffer : { &mixLeft, &mixRight, &clickBuffer, &recordLeft, &recordRight })
        buffer->assign (scratchFrames, 0.0f);
}

AudioHost::~AudioHost()
{
    stop();
}

juce::File AudioHost::getSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("HogyunsVirtualDrum")
               .getChildFile ("audio-device.xml");
}

juce::String AudioHost::start (bool rememberSettings)
{
    stop();
    remember = rememberSettings;

    std::unique_ptr<juce::XmlElement> state;

    if (remember && getSettingsFile().existsAsFile())
        state = juce::parseXML (getSettingsFile());

    const auto firstRun = state == nullptr;

    // 저장된 설정이 없으면 저지연 공유 모드로 바로 연다. 일반 공유 모드로 열었다가 바꾸면
    // JUCE가 장치를 닫고 1.5초를 기다리므로, 처음부터 원하는 방식을 지정한다.
    if (firstRun)
        state = createLowLatencyState();

    auto error = manager.initialise (0, 2, state.get(), true);

    // 지정한 설정으로 열지 못했으면(장치가 바뀌었거나 설정 파일이 잘못됨) 기본 장치로 다시 연다.
    if (manager.getCurrentAudioDevice() == nullptr)
        error = manager.initialise (0, 2, nullptr, true);

    if (firstRun)
        useSmallestBuffer();

    manager.addAudioCallback (this);

    if (remember)
    {
        manager.addChangeListener (this);
        saveSettings();
    }

    started = true;
    return error;
}

void AudioHost::stop()
{
    stopRecording();

    if (! started)
        return;

    if (remember)
        saveSettings();

    manager.removeChangeListener (this);
    manager.removeAudioCallback (this);
    manager.closeAudioDevice();
    started = false;
}

std::unique_ptr<juce::XmlElement> AudioHost::createLowLatencyState()
{
    for (auto* type : manager.getAvailableDeviceTypes())
    {
        if (type->getTypeName() != lowLatencyTypeName)
            continue;

        type->scanForDevices();
        const auto names = type->getDeviceNames (false);

        if (names.isEmpty())
            return {};

        // 장치 이름을 비워 두면 아무 장치도 열리지 않으므로 기본 출력 장치의 이름을 적는다.
        auto state = std::make_unique<juce::XmlElement> ("DEVICESETUP");
        state->setAttribute ("deviceType", lowLatencyTypeName);
        state->setAttribute ("audioOutputDeviceName",
                             names[juce::jlimit (0, names.size() - 1, type->getDefaultDeviceIndex (false))]);
        state->setAttribute ("audioDeviceBufferSize", 128);
        return state;
    }

    return {};
}

void AudioHost::useSmallestBuffer()
{
    auto* device = manager.getCurrentAudioDevice();

    // 저지연 모드로 열리지 않았으면(지원하지 않는 장치) 기본 버퍼를 그대로 둔다.
    if (device == nullptr || device->getTypeName() != lowLatencyTypeName)
        return;

    auto smallest = 0;

    for (const auto size : device->getAvailableBufferSizes())
        if (smallest == 0 || size < smallest)
            smallest = size;

    if (smallest > 0 && smallest != device->getCurrentBufferSizeSamples())
    {
        auto setup = manager.getAudioDeviceSetup();
        setup.bufferSize = smallest;
        manager.setAudioDeviceSetup (setup, true);
    }
}

void AudioHost::saveSettings()
{
    if (const auto xml = manager.createStateXml())
    {
        const auto file = getSettingsFile();
        file.getParentDirectory().createDirectory();
        xml->writeTo (file);
    }
}

void AudioHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    saveSettings();
}

AudioHost::Status AudioHost::getStatus() const
{
    Status status;

    if (auto* device = manager.getCurrentAudioDevice())
    {
        status.open = true;
        status.typeName = device->getTypeName();
        status.deviceName = device->getName();
        status.sampleRate = device->getCurrentSampleRate();
        status.bufferSize = device->getCurrentBufferSizeSamples();
        status.outputLatency = device->getOutputLatencyInSamples();
    }

    return status;
}

juce::String AudioHost::describe() const
{
    const auto status = getStatus();

    if (! status.open)
        return ko ("열린 오디오 장치가 없습니다");

    const auto bufferMs = status.sampleRate > 0.0 ? 1000.0 * status.bufferSize / status.sampleRate : 0.0;

    juce::String text;
    text << status.typeName << " · " << status.deviceName << " · "
         << juce::String (status.sampleRate, 0) << " Hz · "
         << ko ("버퍼 ") << status.bufferSize << ko ("샘플 (") << juce::String (bufferMs, 2) << " ms)";
    return text;
}

void AudioHost::resetCounters() noexcept
{
    callbackCount.store (0, std::memory_order_relaxed);
    lateCallbacks.store (0, std::memory_order_relaxed);
    peak.store (0.0f, std::memory_order_relaxed);
    maxPeak.store (0.0f, std::memory_order_relaxed);
}

juce::String AudioHost::startRecording (const juce::File& file, bool includeTrack)
{
    stopRecording();

    const auto status = getStatus();

    if (! status.open)
        return ko ("열린 오디오 장치가 없습니다");

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

    if (stream == nullptr)
        return ko ("파일을 만들 수 없습니다: ") + file.getFullPathName();

    juce::WavAudioFormat wav;
    auto fileWriter = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (status.sampleRate)
                                                                                   .withNumChannels (2)
                                                                                   .withBitsPerSample (24));

    if (fileWriter == nullptr)
        return ko ("WAV 파일을 쓸 수 없습니다");

    if (! writerThread.isThreadRunning())
        writerThread.startThread();

    // 디스크에는 따로 둔 스레드가 쓴다. 오디오 스레드는 잠금 없는 버퍼에 넣기만 한다.
    writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (fileWriter.release(), writerThread, 131072);
    recordingFile = file;
    recordingSampleRate = status.sampleRate;
    recordedFrames.store (0, std::memory_order_relaxed);
    recordIncludesTrack.store (includeTrack, std::memory_order_relaxed);
    activeWriter.store (writer.get(), std::memory_order_release);
    return {};
}

void AudioHost::stopRecording()
{
    if (writer == nullptr)
        return;

    activeWriter.store (nullptr, std::memory_order_release);

    // 오디오 콜백이 이 기록기를 쓰고 있을 수 있으므로, 콜백이 두 번 지나갈 때까지 기다린 뒤에 닫는다.
    if (manager.getCurrentAudioDevice() != nullptr)
    {
        const auto seen = callbackSerial.load (std::memory_order_relaxed);

        for (int i = 0; i < 100 && callbackSerial.load (std::memory_order_relaxed) - seen < 2; ++i)
            juce::Thread::sleep (2);
    }

    writer.reset();    // 남은 내용을 디스크에 쓰고 파일을 닫는다
}

AudioHost::TrackSync AudioHost::getTrackSync() const noexcept
{
    TrackSync sync;

    // 오디오 스레드가 쓰는 도중이면(홀수) 다시 읽는다.
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const auto before = syncSequence.load (std::memory_order_acquire);
        sync.clock = syncClock.load (std::memory_order_relaxed);
        sync.position = syncPosition.load (std::memory_order_relaxed);
        sync.speed = syncSpeed.load (std::memory_order_relaxed);
        sync.active = syncActive.load (std::memory_order_relaxed);

        if ((before & 1u) == 0 && syncSequence.load (std::memory_order_acquire) == before)
            break;
    }

    return sync;
}

double AudioHost::getRecordedSeconds() const noexcept
{
    return static_cast<double> (recordedFrames.load (std::memory_order_relaxed)) / recordingSampleRate;
}

void AudioHost::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const auto sampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    const auto bufferSize = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;

    engine.prepare (sampleRate);
    track.prepare (sampleRate);
    drumLimiter.prepare (sampleRate);
    expectedPeriodSeconds = sampleRate > 0.0 ? bufferSize / sampleRate : 0.0;
    lastCallbackTicks = 0;
}

void AudioHost::audioDeviceStopped()
{
}

void AudioHost::audioDeviceIOCallbackWithContext (const float* const* /*inputChannelData*/,
                                                  int /*numInputChannels*/,
                                                  float* const* outputChannelData,
                                                  int numOutputChannels,
                                                  int numSamples,
                                                  const juce::AudioIODeviceCallbackContext& /*context*/)
{
    // 오디오 스레드: 여기서는 메모리 할당, 잠금, 파일 접근을 하지 않는다.
    juce::ScopedNoDenormals noDenormals;

    // 콜백이 제때 오지 않으면 소리가 끊겼을 가능성이 높다. 간격이 두 배를 넘으면 센다.
    const auto now = juce::Time::getHighResolutionTicks();

    if (lastCallbackTicks != 0 && expectedPeriodSeconds > 0.0)
    {
        const auto interval = juce::Time::highResolutionTicksToSeconds (now - lastCallbackTicks);

        if (interval > expectedPeriodSeconds * 2.0 + 0.001)
            lateCallbacks.fetch_add (1, std::memory_order_relaxed);
    }

    lastCallbackTicks = now;

    for (int done = 0; done < numSamples; done += scratchFrames)
        processChunk (outputChannelData, numOutputChannels, done, std::min (scratchFrames, numSamples - done));

    callbackCount.fetch_add (1, std::memory_order_relaxed);
    callbackSerial.fetch_add (1, std::memory_order_relaxed);
}

void AudioHost::processChunk (float* const* outputs, int numOutputChannels, int offset, int numSamples) noexcept
{
    const auto count = static_cast<std::size_t> (numSamples);
    auto* left = mixLeft.data();
    auto* right = mixRight.data();
    auto* click = clickBuffer.data();

    std::fill_n (left, count, 0.0f);
    std::fill_n (right, count, 0.0f);
    std::fill_n (click, count, 0.0f);

    // 1. 드럼. 메트로놈은 녹음에 들어가지 않게 따로 받는다.
    const auto clockAtStart = engine.getClock();
    float* const drums[] = { left, right };
    engine.render (drums, 2, numSamples, click);

    const auto gain = masterGain.load (std::memory_order_relaxed);

    for (std::size_t i = 0; i < count; ++i)
    {
        left[i] *= gain;
        right[i] *= gain;
    }

    // 센 타격이 겹쳐 넘치는 꼭대기는 여기서 붙잡는다. 반주까지 더한 뒤에 걸면 킥을 칠 때마다
    // 곡의 음량이 출렁이므로 드럼에만 건다. 미리 보기가 없어서 지연은 늘지 않는다.
    drumLimiter.process (left, right, numSamples);

    auto* recorder = activeWriter.load (std::memory_order_acquire);
    const auto withTrack = recordIncludesTrack.load (std::memory_order_relaxed);

    const auto record = [this, recorder, left, right, count, numSamples]() noexcept
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            recordLeft[i] = softClip (left[i]);
            recordRight[i] = softClip (right[i]);
        }

        const float* const channels[] = { recordLeft.data(), recordRight.data() };
        recorder->write (channels, numSamples);
        recordedFrames.fetch_add (numSamples, std::memory_order_relaxed);
    };

    if (recorder != nullptr && ! withTrack)
        record();

    // 2. 반주
    track.render (left, right, numSamples);

    // 이 조각의 첫 샘플에서 엔진 시계와 반주 위치가 각각 얼마였는지 적어 둔다.
    const auto sequence = syncSequence.load (std::memory_order_relaxed);
    syncSequence.store (sequence + 1, std::memory_order_release);
    syncClock.store (clockAtStart, std::memory_order_relaxed);
    syncPosition.store (track.getRenderStartPosition(), std::memory_order_relaxed);
    syncSpeed.store (track.getSpeed(), std::memory_order_relaxed);
    syncActive.store (track.wasRenderActive(), std::memory_order_relaxed);
    syncSequence.store (sequence + 2, std::memory_order_release);

    if (recorder != nullptr && withTrack)
        record();

    // 3. 메트로놈을 더하고, 넘치는 부분을 부드럽게 누른다.
    auto blockPeak = 0.0f;

    for (std::size_t i = 0; i < count; ++i)
    {
        const auto clickValue = click[i] * gain;
        left[i] = softClip (left[i] + clickValue);
        right[i] = softClip (right[i] + clickValue);
        blockPeak = std::max ({ blockPeak, std::abs (left[i]), std::abs (right[i]) });
    }

    // 4. 장치 버퍼로 옮긴다. 출력이 한 채널뿐이면 양쪽을 반씩 섞는다.
    const auto muted = outputMuted.load (std::memory_order_relaxed);

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        auto* output = outputs[channel];

        if (output == nullptr)
            continue;

        output += offset;

        if (muted)
        {
            std::fill_n (output, count, 0.0f);
        }
        else if (numOutputChannels == 1)
        {
            for (std::size_t i = 0; i < count; ++i)
                output[i] = 0.5f * (left[i] + right[i]);
        }
        else if (channel < 2)
        {
            std::copy_n (channel == 0 ? left : right, count, output);
        }
        else
        {
            std::fill_n (output, count, 0.0f);
        }
    }

    if (blockPeak > peak.load (std::memory_order_relaxed))
        peak.store (blockPeak, std::memory_order_relaxed);

    if (blockPeak > maxPeak.load (std::memory_order_relaxed))
        maxPeak.store (blockPeak, std::memory_order_relaxed);
}
