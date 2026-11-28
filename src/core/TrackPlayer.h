#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "SpscQueue.h"

namespace hvd
{

/** 메모리에 올린 반주 한 곡. 16비트 스테레오이며 만든 뒤에는 바꾸지 않는다. */
struct TrackData
{
    std::vector<std::int16_t> samples;    // 왼쪽, 오른쪽 순서로 번갈아 담는다
    double sampleRate = 44100.0;

    std::size_t frames() const noexcept    { return samples.size() / 2; }
    double seconds() const noexcept        { return sampleRate > 0.0 ? static_cast<double> (frames()) / sampleRate : 0.0; }
};

/** 반주를 재생한다. 속도를 바꿔도 음높이가 변하지 않고(WSOLA 방식), 구간을 반복할 수 있다.

    스레드 규칙:
    - render()는 오디오 스레드에서, prepare()와 detachTrack()은 오디오가 멈춰 있을 때 부른다.
    - setTrack()과 popRetiredTrack()은 화면 스레드 하나에서만 부른다.
    - 나머지 설정과 조회는 어느 스레드에서나 부를 수 있다.
*/
class TrackPlayer
{
public:
    /** 출력 장치의 샘플레이트를 알려 준다. 필요한 메모리는 여기서만 잡는다. */
    void prepare (double deviceSampleRate);

    /** 재생할 곡을 바꾼다. nullptr이면 비운다. 이전 곡은 popRetiredTrack()으로 돌려준다. */
    bool setTrack (const TrackData* newTrack) noexcept;
    const TrackData* popRetiredTrack() noexcept;
    const TrackData* detachTrack() noexcept;

    void setPlaying (bool shouldPlay) noexcept    { playing.store (shouldPlay, std::memory_order_relaxed); }
    bool isPlaying() const noexcept               { return playing.load (std::memory_order_relaxed); }

    void setGain (float newGain) noexcept         { gain.store (newGain, std::memory_order_relaxed); }
    float getGain() const noexcept                { return gain.load (std::memory_order_relaxed); }

    /** 재생 속도 배율. 0.5(절반) ~ 1.5 */
    void setSpeed (float newSpeed) noexcept;
    float getSpeed() const noexcept               { return speed.load (std::memory_order_relaxed); }

    void seek (double seconds) noexcept           { seekRequest.store (seconds < 0.0 ? 0.0 : seconds, std::memory_order_relaxed); }

    /** seek()과 같지만 0보다 앞(음수)으로도 갈 수 있다. 곡이 시작하기 전까지는 무음이 나온다 (리듬 게임의 준비 시간). */
    void seekWithLeadIn (double seconds) noexcept
    {
        // 이미 만들어 둔 소리(직전에 틀던 자리의 몇십 ms)를 버리고 새 자리에서 바로 시작하게 한다.
        flushOnSeek.store (true, std::memory_order_relaxed);
        seekRequest.store (seconds < -60.0 ? -60.0 : seconds, std::memory_order_relaxed);
    }

    /** start ~ end 구간을 되풀이한다. enabled가 false이면 끝까지 재생하고 멈춘다. */
    void setLoop (double startSeconds, double endSeconds, bool enabled) noexcept;

    /** 반복 구간. 꺼져 있어도 마지막에 정한 구간은 남는다. */
    double getLoopStart() const noexcept          { return loopStart.load (std::memory_order_relaxed); }
    double getLoopEnd() const noexcept            { return loopEnd.load (std::memory_order_relaxed); }
    bool isLoopEnabled() const noexcept           { return loopEnabled.load (std::memory_order_relaxed); }

    double getPosition() const noexcept           { return position.load (std::memory_order_relaxed); }

    /** 마지막 render()가 내보낸 첫 샘플의 곡 안 위치(초). getPosition()과 달리 샘플 단위로 정확하고,
        준비 시간 동안에는 음수다. 오디오 스레드가 render() 직후에 읽어 엔진 시계와 짝지어 둔다. */
    double getRenderStartPosition() const noexcept { return renderStart.load (std::memory_order_relaxed); }

    /** 마지막 render()에서 곡이 진행됐는지. */
    bool wasRenderActive() const noexcept         { return renderActive.load (std::memory_order_relaxed); }
    double getLength() const noexcept             { return length.load (std::memory_order_relaxed); }

    /** left, right에 반주 소리를 더한다. 한쪽이 nullptr이면 그쪽은 건너뛴다. */
    void render (float* left, float* right, int numFrames) noexcept;

private:
    static constexpr int maxWindow = 4096;
    static constexpr int maxHop = maxWindow / 2;
    static constexpr int capacity = maxHop * 4;

    static constexpr double noSeek = -1.0e9;
    static constexpr int maxMarks = 40;

    /** 늘이거나 줄인 신호의 한 조각이 원본의 어디에서 왔는지. 지금 나가는 소리의 정확한 위치를 아는 데 쓴다. */
    struct FrameMark
    {
        int base = 0;                 // stretched 안에서 이 조각이 시작하는 자리
        std::int64_t source = 0;      // 원본에서의 시작 프레임
    };

    void applyTrack (const TrackData* newTrack) noexcept;
    double audibleFrame() const noexcept;
    void generateFrame() noexcept;
    int findBestOffset (std::int64_t templateStart, std::int64_t nominal) noexcept;

    double deviceRate = 48000.0;

    // 오디오 스레드 쪽 상태
    const TrackData* track = nullptr;
    int window = 2048;
    int hop = 1024;
    double analysisPosition = 0.0;      // 다음에 읽을 원본 위치 (프레임)
    std::int64_t previousStart = 0;     // 직전에 실제로 읽은 조각의 시작
    bool hasTail = false;               // 겹쳐 더할 앞 조각의 뒤쪽 절반이 있는지
    bool skipSearch = true;             // 위치를 건너뛴 직후에는 맞춤 탐색을 하지 않는다
    bool rewindPending = false;         // 끝까지 재생했으니 소리가 사라지면 처음으로 돌아간다
    float currentGain = 0.0f;
    double readPosition = 1.0;          // 늘이거나 줄인 신호에서 다음에 읽을 위치
    int filled = 0;

    std::vector<float> windowTable;                    // 한(Hann) 창, maxWindow 길이
    std::array<std::vector<float>, 2> stretched;       // 늘이거나 줄인 신호 (원본 샘플레이트)
    std::array<std::vector<float>, 2> tail;
    std::vector<float> templateMono, candidateMono;    // 맞춤 탐색용 작업 공간
    std::array<FrameMark, maxMarks> marks {};
    int numMarks = 0;

    SpscQueue<const TrackData*, 8> incoming;
    SpscQueue<const TrackData*, 8> retired;

    std::atomic<bool> playing { false };
    std::atomic<float> gain { 0.8f };
    std::atomic<float> speed { 1.0f };
    std::atomic<double> seekRequest { noSeek };
    std::atomic<bool> flushOnSeek { false };
    std::atomic<double> loopStart { 0.0 };
    std::atomic<double> loopEnd { 0.0 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<double> position { 0.0 };
    std::atomic<double> length { 0.0 };
    std::atomic<double> renderStart { 0.0 };
    std::atomic<bool> renderActive { false };
};

} // namespace hvd
