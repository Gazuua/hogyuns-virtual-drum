#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include "Effects.h"
#include "Kit.h"
#include "SpscQueue.h"
#include "Stroke.h"

namespace hvd
{

/** 연주 중에 바꿀 수 있는 패드 설정. 킷 파일의 값에서 시작하고 편집 화면이 바꾼다. */
struct PadParams
{
    float volume = 1.0f;        // 배율
    float pan = 0.0f;           // -1(왼쪽) ~ 1(오른쪽)
    float pitchRatio = 1.0f;    // 재생 속도 배율
    float reverbSend = 1.0f;    // 리버브로 보내는 양 0 ~ 1
    float startSeconds = 0.0f;  // 샘플의 앞부분을 이만큼 건너뛰고 재생한다
    float velocityCurve = 1.0f; // 세기를 음량으로 바꾸는 지수
    bool mute = false;          // true면 쳐도 소리를 내지 않는다
};

/** 타격이 어디서 비롯됐는지. */
enum class HitSource : std::uint8_t
{
    screen,      // 편집 화면에서 들어 보려고 누른 것. 따로 밝히지 않은 지시도 이것이다
    key,         // 키보드나 페달의 키
    midi,        // MIDI 입력
    replay,      // 기록해 둔 연주를 다시 재생
    autoplay,    // 리듬 게임의 오토 모드
    mouse        // 연주 화면의 패드를 마우스로 눌러 친 것
};

/** 타격의 출처. 지시에 적어 보내면 엔진은 풀이하지 않고 HitEvent에 그대로 실어 돌려준다 (연주 로그가 쓴다). */
struct HitOrigin
{
    HitSource source = HitSource::screen;
    std::uint16_t key = 0;      // source가 key일 때 그 키의 번호(KeyCode). 아니면 0
};

/** 엔진이 실제로 낸 타격 하나. 화면, 녹화, MIDI 쪽에서 꺼내 쓴다. */
struct HitEvent
{
    std::uint64_t time = 0;     // 엔진이 시작한 뒤로 흐른 샘플 수
    std::uint16_t pad = 0;
    float velocity = 0.0f;
    HitOrigin origin;
};

/** 다른 스레드가 엔진에 보내는 지시 하나. */
struct EngineCommand
{
    enum class Type : std::uint8_t
    {
        hit,                     // pad를 velocity 세기로 친다. delaySamples 뒤에 울린다
        hitAt,                   // pad를 엔진 시계가 time이 될 때 친다 (리플레이). 이미 지났으면 바로 친다
        chokePad,                // pad에서 울리고 있는 소리를 멈춘다
        allSoundOff,             // 모든 소리와 이어 치기를 멈춘다
        setKit,                  // 킷을 바꾼다. 이전 킷은 popRetiredKit()으로 돌려준다
        runnerStart,             // runner 설정대로 pad를 이어 치기 시작한다 (반복, 버즈 롤, 패턴)
        runnerStop,              // runnerId의 이어 치기를 멈춘다
        allRunnersStop,          // 모든 이어 치기를 멈춘다
        setLiveVelocityScale,    // 반복과 버즈 롤의 세기에 곱할 값을 바꾼다 (세기 보조 키)
        setPadParams             // pad의 음량, 팬, 피치, 뮤트를 padParams로 바꾼다
    };

    Type type = Type::hit;
    std::uint16_t pad = 0;
    float velocity = 1.0f;
    std::uint32_t delaySamples = 0;
    std::uint64_t time = 0;
    std::uint32_t runnerId = 0;
    const Kit* kit = nullptr;
    RunnerParams runner;
    PadParams padParams;
    HitOrigin origin;            // hit, hitAt, runnerStart가 내는 타격의 출처
};

/** 샘플을 재생하는 사운드 엔진.

    스레드 규칙:
    - render()와 prepare()는 오디오 스레드(또는 오디오가 멈춰 있을 때)에서만 부른다.
    - postFromInput()은 입력 스레드 하나에서만, postFromControl()은 화면 스레드 하나에서만,
      postFromAux()는 그 밖의 스레드 하나(MIDI 입력)에서만 부른다.
    - popRetiredKit()은 화면 스레드에서 부른다.
    - 템포와 메트로놈 설정은 어느 스레드에서나 바꿀 수 있다.

    render() 안에서는 메모리 할당, 잠금, 시스템 호출을 하지 않는다.
*/
class Engine
{
public:
    static constexpr int maxVoices = 96;
    static constexpr int maxPads = 128;
    static constexpr int maxScheduled = 512;
    static constexpr int maxRunners = 32;
    static constexpr int maxBlockFrames = 1024;    // 이보다 큰 블록은 나눠서 처리한다

    Engine() = default;

    void prepare (double newSampleRate) noexcept;

    bool postFromInput (const EngineCommand& command) noexcept;
    bool postFromControl (const EngineCommand& command) noexcept;
    bool postFromAux (const EngineCommand& command) noexcept;

    /** 채널별로 나뉜 출력 버퍼에 소리를 더한다. 이미 들어 있던 소리는 건드리지 않는다.

        clickOutput을 주면 메트로놈 소리를 outputs에 섞지 않고 거기에(한 채널) 더한다.
        녹음에 메트로놈이 들어가지 않게 할 때 쓴다. */
    void render (float* const* outputs, int numChannels, int numFrames, float* clickOutput = nullptr) noexcept;

    /** 엔진이 내려놓은 킷을 하나 돌려준다. 없으면 nullptr. 받은 쪽이 지운다. */
    const Kit* popRetiredKit() noexcept;

    /** 오디오가 멈춘 뒤에만 부른다. 지금 쓰던 킷을 돌려주고 엔진을 비운다. */
    const Kit* detachKit() noexcept;

    //==============================================================================
    // 템포와 메트로놈. 어느 스레드에서나 부를 수 있다.
    void setTempo (float beatsPerMinute) noexcept;
    float getTempo() const noexcept                       { return tempo.load (std::memory_order_relaxed); }
    void setBeatsPerBar (int beats) noexcept;
    int getBeatsPerBar() const noexcept                   { return beatsPerBar.load (std::memory_order_relaxed); }
    void setMetronomeOn (bool shouldBeOn) noexcept        { metronomeOn.store (shouldBeOn, std::memory_order_relaxed); }
    bool isMetronomeOn() const noexcept                   { return metronomeOn.load (std::memory_order_relaxed); }
    void setMetronomeGain (float gain) noexcept           { metronomeGain.store (gain, std::memory_order_relaxed); }

    /** 메트로놈이 지금까지 친 박의 수. 화면의 박 표시에 쓴다. */
    std::uint32_t getBeatCount() const noexcept           { return beatCount.load (std::memory_order_relaxed); }

    /** 메트로놈이 마지막 박을 친 시각(엔진 시계). 타격이 박에서 얼마나 벗어났는지 잴 때 쓴다. */
    std::uint64_t getLastBeatTime() const noexcept        { return lastBeatTime.load (std::memory_order_relaxed); }

    //==============================================================================
    // 드럼 전체에 거는 이펙트. 어느 스레드에서나 바꿀 수 있다.
    void setFxSettings (const FxSettings& settings) noexcept;
    FxSettings getFxSettings() const noexcept;
    float getGainReductionDb() const noexcept             { return gainReduction.load (std::memory_order_relaxed); }

    double getSampleRate() const noexcept                 { return sampleRateForOthers.load (std::memory_order_relaxed); }

    /** 엔진이 시작한 뒤로 흐른 샘플 수. HitEvent::time과 같은 시계다. */
    std::uint64_t getClock() const noexcept               { return clockForOthers.load (std::memory_order_relaxed); }

    /** 엔진이 낸 타격을 순서대로 하나 꺼낸다. 없으면 false. 한 스레드에서만 부른다. */
    bool popHitEvent (HitEvent& event) noexcept;

    /** 마지막으로 부른 뒤 pad가 낸 가장 큰 출력 크기를 돌려주고 0으로 되돌린다. */
    float takePadPeak (int pad) noexcept;

    //==============================================================================
    int getActiveVoiceCount() const noexcept              { return activeVoices.load (std::memory_order_relaxed); }
    int getActiveRunnerCount() const noexcept             { return activeRunners.load (std::memory_order_relaxed); }
    std::uint32_t getHitCount (int pad) const noexcept;
    float getLastVelocity (int pad) const noexcept;
    std::uint32_t getDroppedCommandCount() const noexcept { return droppedCommands.load (std::memory_order_relaxed); }

private:
    struct Voice
    {
        const Sample* sample = nullptr;    // nullptr이면 쉬는 보이스
        double position = 0.0;
        double step = 1.0;
        float gainLeft = 0.0f;
        float gainRight = 0.0f;
        float send = 0.0f;                 // 리버브로 보내는 양
        float fade = 1.0f;                 // 초크되면 1에서 0으로 줄어든다
        float fadeStep = 0.0f;             // 0이면 초크되지 않은 상태
        float chokeFadeStep = 0.0f;        // 초크될 때 쓸 fadeStep
        int startOffset = 0;               // 이번 블록에서 몇 프레임 뒤에 시작하는지
        int fadeDelay = 0;                 // 초크가 시작되기까지 남은 프레임
        int group = -1;
        std::uint16_t pad = 0;
        std::uint64_t serial = 0;          // 시작 순서. 가장 오래된 보이스를 찾는 데 쓴다
    };

    struct ScheduledHit
    {
        std::uint64_t time = 0;
        std::uint16_t pad = 0;
        float velocity = 1.0f;
        HitOrigin origin;
    };

    /** 엔진이 스스로 이어 치는 일 하나. */
    struct Runner
    {
        bool active = false;
        std::uint32_t id = 0;
        std::uint16_t pad = 0;
        float velocity = 1.0f;
        HitOrigin origin;
        RunnerParams params;
        double nextTime = 0.0;             // 다음 타격의 시각 (샘플 단위, 소수 가능)
        double startTime = 0.0;
        std::uint32_t count = 0;           // 지금까지 낸 타격(또는 스텝) 수
    };

    /** 드럼 버스와 리버브 센드. 크기가 커서 힙에 둔다. */
    struct Buses
    {
        std::array<float, maxBlockFrames> left {}, right {}, sendLeft {}, sendRight {};
    };

    void handle (const EngineCommand& command) noexcept;
    void renderBlock (float* left, float* right, float* click, int numFrames) noexcept;
    void runRunners (int numFrames) noexcept;
    void emitRunnerHit (Runner& runner, int offset) noexcept;
    void scheduleHit (std::uint64_t time, std::uint16_t pad, float velocity, HitOrigin origin) noexcept;
    void fireScheduled (int numFrames) noexcept;
    void startVoice (std::uint16_t padIndex, float velocity, int offset, HitOrigin origin) noexcept;
    void chokePad (std::uint16_t padIndex, int offset) noexcept;
    void stopAllVoices() noexcept;
    void stopAllRunners() noexcept;
    Voice& allocateVoice() noexcept;
    void mixVoice (Voice& voice, int numFrames) noexcept;
    void renderMetronome (float* left, float* right, int numFrames) noexcept;
    double samplesPerBeat() const noexcept;
    float nextRandom() noexcept;           // -1 ~ 1

    static void beginFade (Voice& voice, int offset) noexcept;

    double sampleRate = 48000.0;
    const Kit* kit = nullptr;
    std::uint64_t clock = 0;
    std::uint64_t serialCounter = 0;
    std::uint32_t randomState = 0x2545f491u;
    float liveVelocityScale = 1.0f;

    std::array<Voice, maxVoices> voices {};
    std::array<ScheduledHit, maxScheduled> scheduled {};
    int numScheduled = 0;
    std::array<Runner, maxRunners> runners {};
    std::array<std::uint32_t, maxPads> roundRobin {};
    std::array<PadParams, maxPads> padParams {};
    std::unique_ptr<Buses> buses = std::make_unique<Buses>();
    MasterFx fx;

    // 메트로놈 (오디오 스레드 쪽 상태)
    bool metronomeWasOn = false;
    double samplesToNextBeat = 0.0;
    int beatInBar = 0;
    int clickRemaining = 0;
    double clickPhase = 0.0;
    double clickPhaseStep = 0.0;
    float clickGain = 0.0f;
    float clickDecay = 0.0f;

    // 큐는 크기가 커서(지시 하나가 100바이트를 넘는다) 엔진 객체 밖, 힙에 둔다.
    // 엔진을 만들 때 한 번만 할당하므로 오디오 스레드에서는 할당이 일어나지 않는다.
    using InputQueue = SpscQueue<EngineCommand, 1024>;
    using ControlQueue = SpscQueue<EngineCommand, 256>;
    using AuxQueue = SpscQueue<EngineCommand, 256>;
    using RetiredKitQueue = SpscQueue<const Kit*, 16>;
    using HitEventQueue = SpscQueue<HitEvent, 4096>;

    std::unique_ptr<InputQueue> inputQueue = std::make_unique<InputQueue>();
    std::unique_ptr<ControlQueue> controlQueue = std::make_unique<ControlQueue>();
    std::unique_ptr<AuxQueue> auxQueue = std::make_unique<AuxQueue>();
    std::unique_ptr<RetiredKitQueue> retiredKits = std::make_unique<RetiredKitQueue>();
    std::unique_ptr<HitEventQueue> hitEvents = std::make_unique<HitEventQueue>();

    std::array<std::atomic<std::uint32_t>, maxPads> hitCounts {};
    std::array<std::atomic<float>, maxPads> lastVelocities {};
    std::array<std::atomic<float>, maxPads> padPeaks {};
    std::atomic<int> activeVoices { 0 };
    std::atomic<int> activeRunners { 0 };
    std::atomic<std::uint32_t> droppedCommands { 0 };

    std::atomic<float> tempo { 120.0f };
    std::atomic<int> beatsPerBar { 4 };
    std::atomic<bool> metronomeOn { false };
    std::atomic<float> metronomeGain { 0.35f };
    std::atomic<std::uint32_t> beatCount { 0 };
    std::atomic<std::uint64_t> lastBeatTime { 0 };
    std::atomic<float> gainReduction { 0.0f };

    // 이펙트 설정. FxSettings의 항목 순서대로 담는다. 아무도 정하지 않았으면 기본값을 쓴다.
    static constexpr std::size_t numFxValues = 7;
    std::array<std::atomic<float>, numFxValues> fxValues {};
    std::atomic<bool> fxCustom { false };
    std::atomic<double> sampleRateForOthers { 48000.0 };
    std::atomic<std::uint64_t> clockForOthers { 0 };
};

} // namespace hvd
