#include "DrumTranscriber.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

// 드럼 받아 적기.
//
// 1. 소리를 짧은 구간(프레임)으로 잘라 14개 주파수 대역의 크기를 잰다. 완성곡이면 그 전에 길게 이어지는 소리(화성)와
//    갑자기 나타났다 사라지는 소리(타악기)를 갈라 타악기 쪽만 남긴다.
// 2. 낮은 대역, 가운데 대역, 높은 대역에서 따로 "크기가 갑자기 커진 순간"을 찾는다.
// 3. 그 순간마다 대역별로 얼마나 커졌는지, 얼마나 오래 남는지를 재서 킥, 스네어, 탐, 심벌로 나눈다.
// 4. 박을 찾아 격자를 만들고, 노트마다 격자 위의 자리를 적는다.
// 5. 스네어나 킥에 가려 따로 들리지 않는 하이햇을 앞뒤 패턴으로 채우고, 길게 울리는 구간은 라이드로 본다.

namespace hvd
{

namespace
{

constexpr double pi = 3.14159265358979323846;

constexpr int numBands = 14;
constexpr double bandEdges[numBands + 1] = { 35, 65, 100, 150, 220, 330, 500, 800, 1300, 2200, 3600, 6000, 9000, 13000, 18000 };

struct BandRange
{
    int first, last;
};

constexpr BandRange lowBands { 0, 2 };           // 35 ~ 150 Hz: 킥, 플로어 탐
constexpr BandRange bodyBands { 3, 4 };          // 150 ~ 330 Hz: 스네어와 탐의 몸통
constexpr BandRange midBands { 5, 8 };           // 330 Hz ~ 2.2 kHz
constexpr BandRange presenceBands { 9, 10 };     // 2.2 ~ 6 kHz: 스네어 와이어의 잡음
constexpr BandRange highBands { 11, 13 };        // 6 kHz 이상: 심벌
constexpr BandRange lowBodyBands { 0, 4 };

//==============================================================================
/** 크기가 2의 거듭제곱인 복소수 FFT. */
class Fft
{
public:
    explicit Fft (int sizeToUse) : size (sizeToUse)
    {
        auto bits = 0;

        while ((1 << bits) < size)
            ++bits;

        reversed.resize (static_cast<std::size_t> (size));

        for (int i = 0; i < size; ++i)
        {
            auto r = 0;

            for (int b = 0; b < bits; ++b)
                if ((i & (1 << b)) != 0)
                    r |= 1 << (bits - 1 - b);

            reversed[static_cast<std::size_t> (i)] = r;
        }

        cosTable.resize (static_cast<std::size_t> (size / 2));
        sinTable.resize (static_cast<std::size_t> (size / 2));

        for (int i = 0; i < size / 2; ++i)
        {
            cosTable[static_cast<std::size_t> (i)] = static_cast<float> (std::cos (2.0 * pi * i / size));
            sinTable[static_cast<std::size_t> (i)] = static_cast<float> (-std::sin (2.0 * pi * i / size));
        }
    }

    void forward (float* re, float* im) const noexcept
    {
        for (int i = 0; i < size; ++i)
        {
            const auto j = reversed[static_cast<std::size_t> (i)];

            if (j > i)
            {
                std::swap (re[i], re[j]);
                std::swap (im[i], im[j]);
            }
        }

        for (int length = 2; length <= size; length <<= 1)
        {
            const auto half = length / 2;
            const auto step = size / length;

            for (int start = 0; start < size; start += length)
            {
                for (int k = 0; k < half; ++k)
                {
                    const auto wr = cosTable[static_cast<std::size_t> (k * step)];
                    const auto wi = sinTable[static_cast<std::size_t> (k * step)];
                    const auto a = start + k;
                    const auto b = a + half;
                    const auto tr = re[b] * wr - im[b] * wi;
                    const auto ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
    }

private:
    int size;
    std::vector<int> reversed;
    std::vector<float> cosTable, sinTable;
};

//==============================================================================
float percentile (std::vector<float> values, double fraction)
{
    if (values.empty())
        return 0.0f;

    const auto index = static_cast<std::size_t> (std::clamp (fraction, 0.0, 1.0) * static_cast<double> (values.size() - 1));
    std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (index), values.end());
    return values[index];
}

float medianOf (float* values, int count) noexcept
{
    std::nth_element (values, values + count / 2, values + count);
    return values[count / 2];
}

//==============================================================================
/** 프레임마다 잰 대역별 크기. */
struct Frames
{
    int count = 0;
    int hop = 256;
    int fftSize = 2048;
    double sampleRate = 44100.0;
    std::vector<float> amp;         // count × numBands
    std::vector<float> lowPeak;     // 프레임마다 40 ~ 450 Hz에서 가장 센 주파수(Hz). 없으면 0
    std::vector<float> highLeft, highRight;    // 스테레오일 때: 채널별 5 kHz 이상의 크기 (심벌의 좌우 위치를 재는 데 쓴다)
    bool stereo = false;

    double fps() const noexcept                          { return sampleRate / hop; }
    double secondsAt (double frame) const noexcept       { return frame * hop / sampleRate; }

    float at (int frame, int band) const noexcept
    {
        return frame >= 0 && frame < count ? amp[static_cast<std::size_t> (frame) * numBands + static_cast<std::size_t> (band)] : 0.0f;
    }

    /** 대역 묶음의 크기(제곱 합의 제곱근). */
    float group (int frame, BandRange range) const noexcept
    {
        auto sum = 0.0f;

        for (int band = range.first; band <= range.last; ++band)
        {
            const auto value = at (frame, band);
            sum += value * value;
        }

        return std::sqrt (sum);
    }

    float groupMax (int from, int to, BandRange range) const noexcept
    {
        auto result = 0.0f;

        for (int frame = from; frame <= to; ++frame)
            result = std::max (result, group (frame, range));

        return result;
    }

    float groupMin (int from, int to, BandRange range) const noexcept
    {
        auto result = 1.0e30f;

        for (int frame = from; frame <= to; ++frame)
            result = std::min (result, group (frame, range));

        return result;
    }
};

Frames analyseFrames (const float* left, const float* right, std::size_t numSamples, double sampleRate, bool separatePercussion,
                      std::atomic<float>* progress, const std::atomic<bool>* cancel)
{
    Frames frames;
    frames.sampleRate = sampleRate;
    frames.stereo = right != nullptr;
    frames.fftSize = sampleRate < 30000.0 ? 1024 : 2048;
    frames.hop = frames.fftSize / 8;
    frames.count = static_cast<int> (numSamples / static_cast<std::size_t> (frames.hop)) + 1;
    frames.amp.assign (static_cast<std::size_t> (frames.count) * numBands, 0.0f);
    frames.lowPeak.assign (static_cast<std::size_t> (frames.count), 0.0f);

    if (frames.stereo)
    {
        frames.highLeft.assign (static_cast<std::size_t> (frames.count), 0.0f);
        frames.highRight.assign (static_cast<std::size_t> (frames.count), 0.0f);
    }

    const auto size = frames.fftSize;
    const auto numBins = size / 2 + 1;
    const auto binHz = sampleRate / size;

    std::vector<int> bandOfBin (static_cast<std::size_t> (numBins), -1);

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto hz = bin * binHz;

        for (int band = 0; band < numBands; ++band)
            if (hz >= bandEdges[band] && hz < bandEdges[band + 1])
                bandOfBin[static_cast<std::size_t> (bin)] = band;
    }

    const auto lowFirst = std::max (1, static_cast<int> (std::ceil (40.0 / binHz)));
    const auto lowLast = std::min (numBins - 2, static_cast<int> (std::floor (450.0 / binHz)));

    std::vector<float> window (static_cast<std::size_t> (size));

    for (int i = 0; i < size; ++i)
        window[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * i / size));

    const Fft fft (size);
    std::vector<float> re (static_cast<std::size_t> (size)), im (static_cast<std::size_t> (size));

    std::vector<float> re2 (frames.stereo ? static_cast<std::size_t> (size) : 0), im2 (re2.size());
    const auto sideFirstBin = static_cast<int> (std::ceil (5000.0 / binHz));

    // 프레임 t는 샘플 t*hop을 가운데에 둔다. 스테레오이면 두 채널을 따로 변환한 뒤 평균해서 모노의 스펙트럼을 얻는다.
    const auto computeMagnitudes = [&] (int frame, float* magnitudes)
    {
        const auto start = static_cast<std::int64_t> (frame) * frames.hop - size / 2;

        const auto load = [&] (const float* channel, float* real, float* imaginary)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto index = start + i;
                real[i] = index >= 0 && index < static_cast<std::int64_t> (numSamples)
                              ? channel[static_cast<std::size_t> (index)] * window[static_cast<std::size_t> (i)]
                              : 0.0f;
                imaginary[i] = 0.0f;
            }

            fft.forward (real, imaginary);
        };

        load (left, re.data(), im.data());

        if (! frames.stereo)
        {
            for (int bin = 0; bin < numBins; ++bin)
            {
                const auto r = re[static_cast<std::size_t> (bin)];
                const auto m = im[static_cast<std::size_t> (bin)];
                magnitudes[bin] = std::sqrt (r * r + m * m);
            }

            return;
        }

        load (right, re2.data(), im2.data());
        auto sumLeft = 0.0f, sumRight = 0.0f;

        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto index = static_cast<std::size_t> (bin);
            const auto lr = re[index], li = im[index], rr = re2[index], ri = im2[index];
            const auto r = 0.5f * (lr + rr), m = 0.5f * (li + ri);
            magnitudes[bin] = std::sqrt (r * r + m * m);

            if (bin >= sideFirstBin)
            {
                sumLeft += lr * lr + li * li;
                sumRight += rr * rr + ri * ri;
            }
        }

        frames.highLeft[static_cast<std::size_t> (frame)] = std::sqrt (sumLeft);
        frames.highRight[static_cast<std::size_t> (frame)] = std::sqrt (sumRight);
    };

    const auto reduce = [&] (const float* magnitudes, int frame)
    {
        std::array<float, numBands> energy {};

        for (int bin = 0; bin < numBins; ++bin)
            if (const auto band = bandOfBin[static_cast<std::size_t> (bin)]; band >= 0)
                energy[static_cast<std::size_t> (band)] += magnitudes[bin] * magnitudes[bin];

        for (int band = 0; band < numBands; ++band)
            frames.amp[static_cast<std::size_t> (frame) * numBands + static_cast<std::size_t> (band)] = std::sqrt (energy[static_cast<std::size_t> (band)]);

        auto best = -1;
        auto bestValue = 1.0e-7f;

        for (int bin = lowFirst; bin <= lowLast; ++bin)
        {
            if (magnitudes[bin] > bestValue)
            {
                bestValue = magnitudes[bin];
                best = bin;
            }
        }

        if (best > 0)
        {
            // 봉우리 양옆 값으로 포물선을 맞춰 주파수를 조금 더 정밀하게 잡는다.
            const auto a = std::log (magnitudes[best - 1] + 1.0e-9f);
            const auto b = std::log (magnitudes[best] + 1.0e-9f);
            const auto c = std::log (magnitudes[best + 1] + 1.0e-9f);
            const auto curve = a - 2.0f * b + c;
            const auto shift = std::abs (curve) > 1.0e-9f ? std::clamp (0.5f * (a - c) / curve, -0.5f, 0.5f) : 0.0f;
            frames.lowPeak[static_cast<std::size_t> (frame)] = static_cast<float> ((best + shift) * binHz);
        }
    };

    const auto report = [progress] (double fraction)
    {
        if (progress != nullptr)
            progress->store (static_cast<float> (fraction), std::memory_order_relaxed);
    };

    const auto cancelled = [cancel]
    {
        return cancel != nullptr && cancel->load (std::memory_order_relaxed);
    };

    if (! separatePercussion)
    {
        std::vector<float> magnitudes (static_cast<std::size_t> (numBins));

        for (int frame = 0; frame < frames.count; ++frame)
        {
            computeMagnitudes (frame, magnitudes.data());
            reduce (magnitudes.data(), frame);

            if ((frame & 255) == 0)
            {
                if (cancelled())
                    return {};

                report (0.8 * frame / frames.count);
            }
        }

        return frames;
    }

    // 완성곡: 화성 성분과 타악기 성분을 가른다 (중앙값 필터 방식).
    // 화성은 시간 방향으로 이어지고 타악기는 주파수 방향으로 넓게 퍼진다는 점을 쓴다.
    constexpr int reach = 32;                     // 시간 방향으로 앞뒤 32프레임 (약 0.19초)
    constexpr int timeStep = 4;                   // 그 사이에서 4프레임마다 하나씩 본다
    constexpr int ringSize = reach * 2 + 1;
    constexpr int binReach = 8;

    std::vector<std::vector<float>> ring (ringSize, std::vector<float> (static_cast<std::size_t> (numBins), 0.0f));
    std::vector<float> masked (static_cast<std::size_t> (numBins));
    std::array<float, 2 * reach / timeStep + 1> timeValues {};
    std::array<float, 2 * binReach + 1> binValues {};

    for (int latest = 0; latest < frames.count + reach; ++latest)
    {
        auto& slot = ring[static_cast<std::size_t> (latest % ringSize)];

        if (latest < frames.count)
            computeMagnitudes (latest, slot.data());
        else
            std::fill (slot.begin(), slot.end(), 0.0f);

        const auto centre = latest - reach;

        if (centre < 0)
            continue;

        const auto& current = ring[static_cast<std::size_t> (centre % ringSize)];

        for (int bin = 0; bin < numBins; ++bin)
        {
            auto numTime = 0;

            for (int offset = -reach; offset <= reach; offset += timeStep)
                if (const auto frame = centre + offset; frame >= 0)
                    timeValues[static_cast<std::size_t> (numTime++)] = ring[static_cast<std::size_t> (frame % ringSize)][static_cast<std::size_t> (bin)];

            auto numBin = 0;

            for (int other = std::max (0, bin - binReach); other <= std::min (numBins - 1, bin + binReach); ++other)
                binValues[static_cast<std::size_t> (numBin++)] = current[static_cast<std::size_t> (other)];

            const auto harmonic = medianOf (timeValues.data(), numTime);
            const auto percussive = medianOf (binValues.data(), numBin);
            const auto p2 = percussive * percussive;
            masked[static_cast<std::size_t> (bin)] = current[static_cast<std::size_t> (bin)] * p2 / (p2 + harmonic * harmonic + 1.0e-12f);
        }

        reduce (masked.data(), centre);

        if ((latest & 127) == 0)
        {
            if (cancelled())
                return {};

            report (0.8 * latest / (frames.count + reach));
        }
    }

    return frames;
}

//==============================================================================
/** 대역 묶음 셋(낮음, 가운데, 높음)의 "갑자기 커진 정도".

    넷째 곡선은 높은 대역을 눌러 담지 않고 잰 것이다. 로그로 눌러 담은 셋째 곡선은 조용한 데서 나는 여린 하이햇을
    잘 잡지만, 라이드나 크래시가 계속 울리는 위에 얹히는 타격은 아주 작게 보여 놓친다. 넷째 곡선이 그것을 잡는다. */
struct OnsetCurves
{
    std::array<std::vector<float>, 4> curves;
};

OnsetCurves computeOnsetCurves (const Frames& frames)
{
    const auto count = frames.count;

    // 대역마다 그 곡에서의 큰 값(98번째 백분위)으로 나눠, 대역끼리 견줄 수 있게 한다.
    std::array<float, numBands> reference {};
    auto largest = 0.0f;
    std::vector<float> column (static_cast<std::size_t> (count));

    for (int band = 0; band < numBands; ++band)
    {
        for (int frame = 0; frame < count; ++frame)
            column[static_cast<std::size_t> (frame)] = frames.at (frame, band);

        reference[static_cast<std::size_t> (band)] = percentile (column, 0.98);
        largest = std::max (largest, reference[static_cast<std::size_t> (band)]);
    }

    for (auto& value : reference)
        value = std::max (value, largest * 1.0e-3f + 1.0e-9f);

    std::vector<float> compressed (static_cast<std::size_t> (count) * numBands);

    for (int frame = 0; frame < count; ++frame)
        for (int band = 0; band < numBands; ++band)
            compressed[static_cast<std::size_t> (frame) * numBands + static_cast<std::size_t> (band)]
                = std::log1p (20.0f * frames.at (frame, band) / reference[static_cast<std::size_t> (band)]);

    OnsetCurves result;

    for (auto& curve : result.curves)
        curve.assign (static_cast<std::size_t> (count), 0.0f);

    constexpr int lag = 3;

    for (int frame = lag; frame < count; ++frame)
    {
        for (int band = 0; band < numBands; ++band)
        {
            const auto now = compressed[static_cast<std::size_t> (frame) * numBands + static_cast<std::size_t> (band)];
            const auto before = compressed[static_cast<std::size_t> (frame - lag) * numBands + static_cast<std::size_t> (band)];
            const auto rise = std::max (0.0f, now - before);
            const auto which = band <= lowBands.last ? 0 : (band >= highBands.first ? 2 : 1);
            result.curves[static_cast<std::size_t> (which)][static_cast<std::size_t> (frame)] += rise;

            if (which == 2)
                result.curves[3][static_cast<std::size_t> (frame)]
                    += std::max (0.0f, frames.at (frame, band) - frames.at (frame - lag, band)) / reference[static_cast<std::size_t> (band)];
        }
    }

    return result;
}

/** 곡선에서 봉우리를 고른다. 주변 평균보다 뚜렷하게 높은 국소 최댓값만 남긴다. */
std::vector<int> pickPeaks (const std::vector<float>& curve, double fps, float deltaRelative, float alpha)
{
    const auto count = static_cast<int> (curve.size());
    std::vector<int> peaks;

    if (count < 16)
        return peaks;

    // 기준 크기: 가장 큰 0.5%의 평균
    auto sorted = curve;
    const auto top = std::max<std::size_t> (4, sorted.size() / 200);
    std::partial_sort (sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t> (top), sorted.end(), std::greater<float>());

    auto scale = 0.0f;

    for (std::size_t i = 0; i < top; ++i)
        scale += sorted[i];

    scale /= static_cast<float> (top);

    if (scale <= 1.0e-6f)
        return peaks;

    const auto delta = deltaRelative * scale;
    const auto half = std::max (4, static_cast<int> (std::lround (0.12 * fps)));
    constexpr int reach = 4;

    std::vector<double> prefix (static_cast<std::size_t> (count) + 1, 0.0);

    for (int i = 0; i < count; ++i)
        prefix[static_cast<std::size_t> (i) + 1] = prefix[static_cast<std::size_t> (i)] + curve[static_cast<std::size_t> (i)];

    for (int frame = reach; frame < count - reach; ++frame)
    {
        const auto value = curve[static_cast<std::size_t> (frame)];

        if (value <= delta)
            continue;

        auto isPeak = true;

        for (int j = 1; j <= reach && isPeak; ++j)
            isPeak = value > curve[static_cast<std::size_t> (frame - j)] && value >= curve[static_cast<std::size_t> (frame + j)];

        if (! isPeak)
            continue;

        const auto from = std::max (0, frame - half);
        const auto to = std::min (count, frame + half + 1);
        const auto mean = static_cast<float> ((prefix[static_cast<std::size_t> (to)] - prefix[static_cast<std::size_t> (from)]) / (to - from));

        if (value > delta + alpha * mean)
            peaks.push_back (frame);
    }

    return peaks;
}

//==============================================================================
/** 타격 하나(같은 순간에 친 악기들)와 거기서 잰 값. */
struct Event
{
    int frame = 0;                   // 크기가 가장 빨리 커진 프레임
    int peakFrame = 0;               // 크기가 가장 큰 프레임
    bool firedLow = false, firedMid = false, firedHigh = false;

    std::array<float, numBands> rise {};    // 대역별로 커진 크기
    float low = 0.0f, body = 0.0f, mid = 0.0f, presence = 0.0f, high = 0.0f;    // 대역 묶음별 합
    float f0 = 0.0f;                 // 낮은 쪽 봉우리의 주파수(Hz)
    float lowSustain = 0.0f;         // 0.1초 뒤 낮은 대역이 남은 비율
    float noiseRatio = 0.0f;         // 0.05초 뒤에 남은 2 kHz 이상의 잡음 / 타격 순간의 2 kHz 아래 크기 (스네어 와이어)
    float noiseRise = 0.0f;          // 위와 같되, 치기 전부터 있던 잡음(심벌의 여운)은 뺀 값
    bool ringKnown = false;          // 다음 심벌을 치기 전까지 여운을 잴 시간이 있었는지
    bool ringing = false;            // 다음 심벌을 칠 때까지 높은 대역이 꺼지지 않고 남아 있는지 (라이드, 열린 하이햇)
    bool clearlyOpen = false;        // 뚜렷하게 오래 남는지 (열린 하이햇으로 표시해도 될 만큼)
    bool sideKnown = false;          // 스테레오라서 좌우 위치를 쟀는지
    float side = 0.0f;               // 높은 대역이 커진 쪽: -1 왼쪽 ~ 1 오른쪽
    float highAt58 = 0.0f, highAt174 = 0.0f, highAt464 = 0.0f;    // 그 시간 뒤 6 kHz 이상이 남은 비율 (타격 전 크기를 뺀 값)
    float kickAmount = 0.0f, snareAmount = 0.0f;                  // 이 곡의 보통 킥, 보통 스네어를 1로 본 크기
    double seconds = 0.0;

    bool kick = false, snare = false, cymbal = false, crash = false, open = false, longRing = false;
    int tom = -1;                    // 0 높은 탐, 1 가운데 탐, 2 플로어 탐
    float kickVelocity = 0.0f, snareVelocity = 0.0f, cymbalVelocity = 0.0f, tomVelocity = 0.0f;

    float membrane() const noexcept    { return low + body + mid; }
};

struct Tuning
{
    float deltaLow, deltaMid, deltaHigh, alpha;
    float kickMin, snareMin, noiseMin, cymbalMin;
    bool allowToms;
};

constexpr Tuning stemTuning { 0.10f, 0.08f, 0.06f, 1.3f, 0.25f, 0.16f, 0.05f, 0.10f, true };
constexpr Tuning mixTuning { 0.16f, 0.14f, 0.12f, 1.6f, 0.35f, 0.30f, 0.04f, 0.20f, false };

constexpr BandRange membraneBands { 0, 8 };      // 2.2 kHz 아래: 북의 울림
constexpr BandRange noiseBands { 9, 13 };        // 2.2 kHz 이상

std::vector<Event> findEvents (const Frames& frames, const OnsetCurves& curves, const Tuning& tuning)
{
    struct Peak
    {
        int frame, which;
    };

    std::vector<Peak> peaks;
    const float deltas[4] = { tuning.deltaLow, tuning.deltaMid, tuning.deltaHigh, tuning.deltaHigh * 1.5f };

    // 넷째 곡선(눌러 담지 않은 높은 대역)에서 찾은 것도 높은 대역의 타격으로 친다.
    for (int which = 0; which < 4; ++which)
        for (const auto frame : pickPeaks (curves.curves[static_cast<std::size_t> (which)], frames.fps(), deltas[which], tuning.alpha))
            peaks.push_back ({ frame, std::min (which, 2) });

    std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.frame < b.frame; });

    // 세 곡선의 봉우리가 5프레임(약 29ms) 안에 모여 있으면 한 번의 타격으로 본다.
    // 같은 타격이라도 높은 대역은 낮은 대역보다 늦게 봉우리가 생기곤 한다. 특히 크래시는 소리가 천천히 커지므로,
    // 북을 친 뒤에 따라오는 높은 대역의 봉우리는 8프레임(약 46ms)까지 같은 타격으로 본다.
    std::vector<Event> events;

    for (std::size_t i = 0; i < peaks.size();)
    {
        Event event;
        event.frame = peaks[i].frame;

        auto j = i;

        for (; j < peaks.size(); ++j)
        {
            const auto gap = peaks[j].frame - event.frame;
            const auto trailingHigh = peaks[j].which == 2 && (event.firedLow || event.firedMid) && ! event.firedHigh;

            if (gap > (trailingHigh ? 8 : 5))
                break;

            event.firedLow = event.firedLow || peaks[j].which == 0;
            event.firedMid = event.firedMid || peaks[j].which == 1;
            event.firedHigh = event.firedHigh || peaks[j].which == 2;
        }

        events.push_back (event);
        i = j;
    }

    // 타격마다 값을 잰다. 창이 길어서(약 46ms) 타격의 에너지는 봉우리 프레임의 몇 프레임 뒤에 다 들어온다.
    for (auto& event : events)
    {
        const auto t = event.frame;

        for (int band = 0; band < numBands; ++band)
        {
            auto before = 1.0e30f, after = 0.0f;

            for (int frame = t - 6; frame <= t - 3; ++frame)
                before = std::min (before, frames.at (frame, band));

            // 높은 대역은 조금 더 뒤까지 본다 (늦게 커지는 심벌).
            const auto lastFrame = band >= highBands.first ? t + 11 : t + 7;

            for (int frame = t + 1; frame <= lastFrame; ++frame)
                after = std::max (after, frames.at (frame, band));

            event.rise[static_cast<std::size_t> (band)] = std::max (0.0f, after - before);
        }

        const auto sum = [&event] (BandRange range)
        {
            auto total = 0.0f;

            for (int band = range.first; band <= range.last; ++band)
                total += event.rise[static_cast<std::size_t> (band)];

            return total;
        };

        event.low = sum (lowBands);
        event.body = sum (bodyBands);
        event.mid = sum (midBands);
        event.presence = sum (presenceBands);
        event.high = sum (highBands);

        // 전체 크기가 가장 큰 프레임
        auto best = 0.0f;
        event.peakFrame = t + 2;

        for (int frame = t + 1; frame <= t + 6; ++frame)
        {
            const auto total = frames.group (frame, { 0, numBands - 1 });

            if (total > best)
            {
                best = total;
                event.peakFrame = frame;
            }
        }

        const auto pk = event.peakFrame;

        // 낮은 쪽 봉우리 주파수: 몇 프레임의 중앙값
        std::array<float, 7> pitches {};
        auto numPitches = 0;

        for (int frame = pk; frame <= pk + 6; ++frame)
            if (frame >= 0 && frame < frames.count && frames.lowPeak[static_cast<std::size_t> (frame)] > 0.0f)
                pitches[static_cast<std::size_t> (numPitches++)] = frames.lowPeak[static_cast<std::size_t> (frame)];

        event.f0 = numPitches > 0 ? medianOf (pitches.data(), numPitches) : 0.0f;

        const auto ratio = [] (float later, float peak) { return peak > 1.0e-9f ? std::max (0.0f, later) / peak : 0.0f; };

        event.lowSustain = ratio (frames.groupMin (pk + 15, pk + 18, lowBodyBands), frames.groupMax (pk - 1, pk + 2, lowBodyBands));
        const auto membranePeak = frames.groupMax (pk - 1, pk + 2, membraneBands);
        const auto noiseLater = frames.groupMin (pk + 7, pk + 9, noiseBands);
        event.noiseRatio = ratio (noiseLater, membranePeak);
        event.noiseRise = ratio (noiseLater - frames.groupMin (t - 6, t - 3, noiseBands), membranePeak);

        // 심벌이 얼마나 오래 남는지. 앞에서 치던 심벌의 여운을 빼고 이번 타격으로 늘어난 만큼만 본다.
        const auto highBefore = frames.groupMin (t - 6, t - 3, highBands);
        const auto highSpan = frames.groupMax (pk - 1, pk + 2, highBands) - highBefore;
        event.highAt58 = ratio (frames.groupMin (pk + 8, pk + 10, highBands) - highBefore, highSpan);
        event.highAt174 = ratio (frames.groupMin (pk + 24, pk + 30, highBands) - highBefore, highSpan);
        event.highAt464 = ratio (frames.groupMin (pk + 64, pk + 80, highBands) - highBefore, highSpan);

        if (frames.stereo)
        {
            // 이번 타격으로 높은 대역이 어느 쪽에서 더 커졌는지
            const auto gain = [&frames, t] (const std::vector<float>& channel)
            {
                auto before = 1.0e30f, after = 0.0f;

                for (int frame = std::max (0, t - 6); frame <= std::min (frames.count - 1, t - 3); ++frame)
                    before = std::min (before, channel[static_cast<std::size_t> (frame)]);

                for (int frame = std::max (0, t + 1); frame <= std::min (frames.count - 1, t + 11); ++frame)
                    after = std::max (after, channel[static_cast<std::size_t> (frame)]);

                return before < 1.0e29f ? std::max (0.0f, after - before) : 0.0f;
            };

            const auto gainLeft = gain (frames.highLeft), gainRight = gain (frames.highRight);

            if (gainLeft + gainRight > 1.0e-9f)
            {
                event.side = (gainRight - gainLeft) / (gainRight + gainLeft);
                event.sideKnown = true;
            }
        }
    }

    // 심벌의 여운: 다음 심벌을 치기 직전(길어야 0.17초 뒤)에 높은 대역이 얼마나 남아 있는지 본다.
    // 닫힌 하이햇은 금방 꺼지고, 라이드와 열린 하이햇은 남는다. 기다린 시간이 길수록 기준을 낮춘다.
    const auto secondsPerFrame = 1.0 / frames.fps();

    for (std::size_t i = 0; i < events.size(); ++i)
    {
        auto& event = events[i];

        if (! event.firedHigh)
            continue;

        auto last = event.peakFrame + static_cast<int> (0.174 / secondsPerFrame);

        for (auto j = i + 1; j < events.size(); ++j)
        {
            if (events[j].firedHigh)
            {
                last = std::min (last, events[j].frame - 4);
                break;
            }
        }

        const auto delay = (last - event.peakFrame) * secondsPerFrame;

        if (delay < 0.05)
            continue;    // 너무 빨리 다음 타격이 와서 알 수 없다

        const auto peak = frames.groupMax (event.peakFrame - 1, event.peakFrame + 2, highBands);
        const auto later = frames.groupMin (last - 2, last, highBands);
        const auto position = std::clamp ((delay - 0.058) / (0.174 - 0.058), 0.0, 1.0);
        const auto threshold = 0.37 * std::pow (0.13 / 0.37, position);

        event.ringKnown = true;
        event.ringing = peak > 1.0e-9f && later / peak >= static_cast<float> (threshold);
        event.clearlyOpen = delay >= 0.11 && peak > 1.0e-9f && later / peak >= static_cast<float> (2.2 * threshold);
    }

    return events;
}

/** 탐들의 주파수를 최대 세 무리로 나눠 높은 탐(0), 가운데 탐(1), 플로어 탐(2)으로 정한다. */
void assignToms (std::vector<Event>& events)
{
    std::vector<float> pitches;

    for (const auto& event : events)
        if (event.tom >= 0)
            pitches.push_back (event.f0);

    if (pitches.empty())
        return;

    std::sort (pitches.begin(), pitches.end());

    // 주파수가 7% 넘게 벌어진 곳에서 무리를 가른다.
    std::vector<std::pair<float, float>> groups;    // (가장 낮은 값, 가장 높은 값)
    groups.emplace_back (pitches.front(), pitches.front());

    for (const auto pitch : pitches)
    {
        if (pitch > groups.back().second * 1.07f)
            groups.emplace_back (pitch, pitch);
        else
            groups.back().second = pitch;
    }

    // 셋을 넘으면 가장 가까운 이웃끼리 합친다.
    while (groups.size() > 3)
    {
        std::size_t closest = 0;
        auto smallest = 1.0e9f;

        for (std::size_t i = 0; i + 1 < groups.size(); ++i)
        {
            if (const auto gap = groups[i + 1].first / groups[i].second; gap < smallest)
            {
                smallest = gap;
                closest = i;
            }
        }

        groups[closest].second = groups[closest + 1].second;
        groups.erase (groups.begin() + static_cast<std::ptrdiff_t> (closest) + 1);
    }

    std::array<int, 3> labels { 1, 1, 1 };

    if (groups.size() == 1)
    {
        const auto centre = 0.5f * (groups[0].first + groups[0].second);
        labels[0] = centre < 100.0f ? 2 : (centre < 155.0f ? 1 : 0);
    }
    else if (groups.size() == 2)
    {
        labels[0] = groups[0].second < 125.0f ? 2 : 1;
        labels[1] = labels[0] == 2 ? (groups[1].first > 130.0f ? 0 : 1) : 0;
    }
    else
    {
        labels = { 2, 1, 0 };
    }

    for (auto& event : events)
    {
        if (event.tom < 0)
            continue;

        for (std::size_t i = 0; i < groups.size(); ++i)
            if (event.f0 >= groups[i].first - 0.01f && event.f0 <= groups[i].second + 0.01f)
                event.tom = labels[i];
    }
}

/** 2.2 kHz 아래 아홉 대역의 모양. 킥과 스네어의 본으로 쓴다. */
using Shape = std::array<float, 9>;

float dot (const Shape& a, const Shape& b) noexcept
{
    auto sum = 0.0f;

    for (std::size_t i = 0; i < a.size(); ++i)
        sum += a[i] * b[i];

    return sum;
}

Shape shapeOf (const Event& event) noexcept
{
    Shape shape {};

    for (std::size_t i = 0; i < shape.size(); ++i)
        shape[i] = event.rise[i];

    return shape;
}

/** seeds의 평균 모양. 높은 대역이 가장 적게 섞인 절반만 써서, 함께 친 심벌의 영향을 줄인다. */
Shape averageShape (std::vector<const Event*> seeds)
{
    std::sort (seeds.begin(), seeds.end(), [] (const Event* a, const Event* b) { return a->high < b->high; });
    seeds.resize (std::max<std::size_t> (1, (seeds.size() + 1) / 2));

    Shape shape {};

    for (const auto* seed : seeds)
        for (std::size_t i = 0; i < shape.size(); ++i)
            shape[i] += seed->rise[i] / static_cast<float> (seeds.size());

    return shape;
}

/** 타격마다 무엇을 쳤는지 정한다. 심벌의 좌우 위치로 하이햇과 라이드를 가를 수 있었으면 true를 돌려준다.
    그때는 Event::longRing이 "라이드 쪽에서 났다"는 뜻이 된다. */
bool classifyEvents (std::vector<Event>& events, const Tuning& tuning, std::string* log)
{
    std::vector<float> lows, upperParts;

    for (const auto& event : events)
    {
        if (event.firedLow)
            lows.push_back (event.low);

        if (event.firedMid)
            upperParts.push_back (event.body + event.mid);
    }

    const auto refLow = percentile (lows, 0.9);
    const auto refUpper = percentile (upperParts, 0.9);

    // 킥의 주파수: 낮은 대역이 크게 뛴 타격들의 중앙값. 탐은 이보다 뚜렷하게 높다.
    std::vector<float> kickPitches;

    for (const auto& event : events)
        if (event.firedLow && event.low >= 0.4f * refLow && event.low >= 1.5f * (event.body + event.mid) && event.f0 > 0.0f)
            kickPitches.push_back (event.f0);

    const auto kickPitch = kickPitches.empty() ? 55.0f : percentile (kickPitches, 0.5);

    // 1. 킥과 스네어의 본을 이 곡에서 뜬다: 한쪽 성격이 뚜렷한 타격들의 평균 모양.
    std::vector<const Event*> kickSeeds, snareSeeds;

    for (const auto& event : events)
    {
        const auto upper = event.body + event.mid;

        if (event.firedLow && event.low >= 0.4f * refLow && event.low >= 1.5f * upper && event.f0 <= kickPitch * 1.25f)
            kickSeeds.push_back (&event);
        else if (event.firedMid && upper >= 0.4f * refUpper && upper >= 1.2f * event.low && event.noiseRatio >= tuning.noiseMin)
            snareSeeds.push_back (&event);
    }

    // 본을 뜰 타격이 모자라면 흔한 모양을 이 곡의 크기에 맞춰 쓴다.
    Shape kickShape { 0.6f, 1.0f, 0.5f, 0.25f, 0.15f, 0.1f, 0.08f, 0.06f, 0.05f };
    Shape snareShape { 0.0f, 0.05f, 0.2f, 0.9f, 1.0f, 0.6f, 0.5f, 0.45f, 0.4f };

    if (kickSeeds.size() >= 4)
    {
        kickShape = averageShape (kickSeeds);
    }
    else
    {
        const auto scale = refLow / (kickShape[0] + kickShape[1] + kickShape[2]);

        for (auto& value : kickShape)
            value *= scale;
    }

    if (snareSeeds.size() >= 4)
    {
        snareShape = averageShape (snareSeeds);
    }
    else
    {
        auto total = 0.0f;

        for (std::size_t i = 3; i < snareShape.size(); ++i)
            total += snareShape[i];

        const auto scale = refUpper / total;

        for (auto& value : snareShape)
            value *= scale;
    }

    // 스네어의 주파수: 탐과 가르는 데 쓴다.
    std::vector<float> snarePitches;

    for (const auto* seed : snareSeeds)
        if (seed->f0 > 0.0f)
            snarePitches.push_back (seed->f0);

    const auto snarePitch = snarePitches.size() >= 4 ? percentile (snarePitches, 0.5) : 0.0f;

    // 2. 타격마다 "킥 몇 개분 + 스네어 몇 개분"으로 풀어 본다 (음수가 되지 않게 맞춘다).
    //    스네어의 낮은 울림을 킥으로, 킥의 몸통을 스네어로 잘못 세지 않게 해 준다.
    const auto kk = dot (kickShape, kickShape), ss = dot (snareShape, snareShape), ks = dot (kickShape, snareShape);
    const auto determinant = kk * ss - ks * ks;

    for (auto& event : events)
    {
        if (kk <= 0.0f || ss <= 0.0f)
            continue;

        const auto shape = shapeOf (event);
        const auto bk = dot (shape, kickShape), bs = dot (shape, snareShape);

        auto kickAmount = determinant > 1.0e-12f ? (bk * ss - bs * ks) / determinant : bk / kk;
        auto snareAmount = determinant > 1.0e-12f ? (bs * kk - bk * ks) / determinant : bs / ss;

        if (kickAmount < 0.0f)
        {
            kickAmount = 0.0f;
            snareAmount = std::max (0.0f, bs / ss);
        }
        else if (snareAmount < 0.0f)
        {
            snareAmount = 0.0f;
            kickAmount = std::max (0.0f, bk / kk);
        }

        event.kickAmount = kickAmount;
        event.snareAmount = snareAmount;
    }

    // 3. 탐: 음높이가 있는 낮은 소리가 남고, 킥보다 뚜렷하게 높으며, 스네어 같은 잡음이 새로 생기지 않는다.
    const auto membraneReference = std::max (refLow, refUpper);

    for (auto& event : events)
    {
        const auto likeSnare = snarePitch > 0.0f && std::abs (event.f0 - snarePitch) < 0.12f * snarePitch && event.snareAmount >= 0.5f;

        // 킥보다 조금만 높으면 여운이 남을 때만, 뚜렷하게 높으면(1.4배) 킥의 본에 잘 맞지 않을 때 탐으로 본다.
        // 드럼 분리를 거치면 탐의 여운이 줄어들어서 여운만으로는 가를 수 없다.
        const auto sustained = event.lowSustain >= 0.15f && event.f0 >= std::max (60.0f, kickPitch * 1.2f);
        const auto clearlyHigher = event.f0 >= std::max (60.0f, kickPitch * 1.4f) && event.kickAmount < 0.5f;

        if (tuning.allowToms && (event.firedLow || event.firedMid) && ! likeSnare
            && (sustained || clearlyHigher)
            && event.noiseRise < tuning.noiseMin
            && membraneReference > 0.0f && event.membrane() >= 0.2f * membraneReference)
        {
            event.tom = 1;
            event.tomVelocity = std::clamp (std::pow (event.membrane() / (1.2f * membraneReference), 0.7f), 0.2f, 1.0f);
        }
    }

    assignToms (events);

    for (auto& event : events)
    {
        if (event.tom >= 0)
            continue;

        event.kick = event.firedLow && event.kickAmount >= tuning.kickMin;

        // 스네어: 모양이 뚜렷하게 스네어이면 인정한다. 약한 것(고스트 노트)은 와이어의 잡음이 이어질 때만 인정한다.
        // 킥과 함께 잡힌 약한 스네어는 킥의 울림이 본과 조금 달라서 남은 것일 때가 많으므로 버린다.
        const auto strongEnough = event.kick ? event.snareAmount >= 0.6f
                                             : (event.snareAmount >= 0.5f
                                                || (event.snareAmount >= tuning.snareMin && event.noiseRatio >= tuning.noiseMin));
        event.snare = (event.firedMid || event.firedLow) && strongEnough;

        if (event.kick)
            event.kickVelocity = std::clamp (std::pow (event.kickAmount, 0.6f), 0.2f, 1.0f);

        if (event.snare)
            event.snareVelocity = std::clamp (std::pow (event.snareAmount, 0.7f), 0.12f, 1.0f);
    }

    // 4. 심벌. 스네어의 잡음과 킥의 타격음에도 높은 대역이 들어 있으므로, 그런 타격에서는 평소보다 뚜렷하게 클 때만 인정한다.
    std::vector<float> highs, snareRatios, kickHighs;

    for (const auto& event : events)
    {
        if (event.firedHigh && ! event.snare && event.tom < 0)
            highs.push_back (event.high);

        if (event.snare && event.presence > 0.0f)
            snareRatios.push_back (event.high / event.presence);

        if (event.kick && ! event.snare)
            kickHighs.push_back (event.high);
    }

    const auto refHigh = percentile (highs, 0.9);
    const auto strongHigh = percentile (highs, 0.92);
    const auto snareBaseline = snareRatios.size() >= 8 ? percentile (snareRatios, 0.25) : 1.0e9f;
    const auto kickBaseline = kickHighs.size() >= 8 ? percentile (kickHighs, 0.25) : 0.0f;

    for (auto& event : events)
    {
        if (! event.firedHigh || refHigh <= 0.0f || event.high < tuning.cymbalMin * refHigh || event.tom >= 0)
            continue;

        if (event.snare)
            event.cymbal = event.presence > 0.0f && event.high / event.presence >= 1.6f * snareBaseline;
        else if (event.kick)
            event.cymbal = event.high >= 0.3f * refHigh || event.high >= 2.0f * kickBaseline + 0.05f * refHigh;
        else
            event.cymbal = true;

        if (! event.cymbal)
            continue;

        event.cymbalVelocity = std::clamp (std::pow (event.high / refHigh, 0.7f), 0.15f, 1.0f);
        // 크래시: 그 곡의 심벌 가운데 손꼽히게 세고 길게 남는다. 세지 않아도 아주 오래 남으면 크래시로 본다.
        const auto accented = event.high >= 0.85f * strongHigh && event.highAt174 >= 0.4f && event.highAt464 >= 0.2f;
        const auto veryLong = event.high >= 0.5f * refHigh && event.highAt174 >= 0.8f && event.highAt464 >= 0.6f;
        event.crash = accented || veryLong;
        event.longRing = event.ringKnown && event.ringing;
        event.open = ! event.crash && event.clearlyOpen;
    }

    // 5. 하이햇과 라이드는 소리만으로는 가르기 어렵다 (드럼 분리를 거치면 여운이 줄어든다).
    //    하지만 스테레오 녹음에서는 두 심벌이 좌우 다른 자리에 있으므로, 심벌만 친 타격들의 좌우 위치가
    //    두 무리로 뚜렷이 갈리면 많은 쪽을 하이햇, 적은 쪽을 라이드로 본다.
    std::vector<float> sides;

    for (const auto& event : events)
        if (event.cymbal && ! event.crash && ! event.kick && ! event.snare && event.tom < 0 && event.sideKnown)
            sides.push_back (event.side);

    auto bySide = false;
    auto hatSide = 0.0f, rideSide = 0.0f;

    if (sides.size() >= 60)
    {
        auto first = percentile (sides, 0.15), second = percentile (sides, 0.85);
        auto countFirst = 0, countSecond = 0;
        auto spread = 0.0f;

        for (int iteration = 0; iteration < 12; ++iteration)
        {
            auto sumFirst = 0.0f, sumSecond = 0.0f;
            countFirst = countSecond = 0;
            spread = 0.0f;

            for (const auto value : sides)
            {
                if (std::abs (value - first) <= std::abs (value - second))
                {
                    sumFirst += value;
                    ++countFirst;
                    spread += std::abs (value - first);
                }
                else
                {
                    sumSecond += value;
                    ++countSecond;
                    spread += std::abs (value - second);
                }
            }

            if (countFirst == 0 || countSecond == 0)
                break;

            first = sumFirst / static_cast<float> (countFirst);
            second = sumSecond / static_cast<float> (countSecond);
        }

        spread /= static_cast<float> (sides.size());
        const auto distance = std::abs (first - second);
        const auto smaller = std::min (countFirst, countSecond);

        if (distance >= 0.3f && distance >= 3.0f * spread && smaller * 100 >= static_cast<int> (sides.size()) * 15)
        {
            bySide = true;
            hatSide = countFirst >= countSecond ? first : second;
            rideSide = countFirst >= countSecond ? second : first;
        }
    }

    if (bySide)
    {
        for (auto& event : events)
        {
            if (! event.cymbal)
                continue;

            // 스네어와 함께 친 심벌은 스네어의 잡음이 가운데에 섞여 위치를 믿을 수 없다.
            // 킥은 높은 대역이 거의 없으므로, 킥과 함께 친 심벌은 구간을 정하는 데 쓴다.
            const auto alone = ! event.snare && event.tom < 0 && ! event.crash;
            event.ringKnown = alone && event.sideKnown;
            event.longRing = event.ringKnown && std::abs (event.side - rideSide) < std::abs (event.side - hatSide);
        }
    }

    if (log != nullptr)
    {
        char line[400];
        std::snprintf (line, sizeof (line), "references: low %.4f upper %.4f high %.4f, kick pitch %.1f Hz, snare pitch %.1f Hz, seeds: kick %d snare %d, snareBaseline %.3f kickBaseline %.4f\n",
                       refLow, refUpper, refHigh, kickPitch, snarePitch, static_cast<int> (kickSeeds.size()), static_cast<int> (snareSeeds.size()),
                       snareBaseline < 1.0e8f ? snareBaseline : -1.0f, kickBaseline);
        *log += line;

        std::snprintf (line, sizeof (line), "cymbal sides: %d cymbal-only hits, %s (hi-hat at %+.2f, ride at %+.2f)\n",
                       static_cast<int> (sides.size()), bySide ? "two groups" : "one group", hatSide, rideSide);
        *log += line;
    }

    return bySide;
}

/** 드럼만 있는 소리에서 타격의 시각을 더 정밀하게 잡는다: 근처에서 에너지가 가장 가파르게 커지는 곳을 찾고,
    거기서 거슬러 올라가 소리가 커지기 시작한 곳을 타격 시각으로 본다. */
double refineOnset (const float* samples, std::size_t numSamples, double sampleRate, double approxSeconds, bool emphasiseHigh)
{
    const auto block = std::max (16, static_cast<int> (std::lround (sampleRate * 0.0015)));
    const auto centre = static_cast<std::int64_t> (std::llround (approxSeconds * sampleRate));
    const auto first = centre - static_cast<std::int64_t> (sampleRate * 0.028);
    const auto numBlocks = static_cast<int> (sampleRate * 0.046) / block;
    constexpr int margin = 10;    // 앞쪽으로 더 재 두는 블록 수 (타격 전의 크기를 알기 위해)

    if (numBlocks < 6)
        return approxSeconds;

    std::vector<float> energy (static_cast<std::size_t> (numBlocks + margin + 3), 0.0f);

    for (int b = 0; b < numBlocks + margin + 3; ++b)
    {
        const auto start = first + static_cast<std::int64_t> (b - margin) * block;
        auto sum = 0.0f;

        for (int i = 0; i < block; ++i)
        {
            const auto index = start + i;

            if (index < 1 || index >= static_cast<std::int64_t> (numSamples))
                continue;

            const auto value = emphasiseHigh ? samples[static_cast<std::size_t> (index)] - samples[static_cast<std::size_t> (index) - 1]
                                             : samples[static_cast<std::size_t> (index)];
            sum += value * value;
        }

        energy[static_cast<std::size_t> (b)] = sum;
    }

    auto best = -1;
    auto bestRise = 0.0f;
    auto total = 0.0f;

    for (int b = margin; b < numBlocks + margin; ++b)
    {
        const auto index = static_cast<std::size_t> (b);
        const auto rise = (energy[index] + energy[index + 1]) - (energy[index - 1] + energy[index - 2]);
        total += energy[index];

        if (rise > bestRise)
        {
            bestRise = rise;
            best = b;
        }
    }

    // 뚜렷한 상승이 없으면 처음 추정을 그대로 쓴다.
    if (best < 0 || bestRise < 0.08f * total)
        return approxSeconds;

    // 타격 전의 크기와 타격 뒤의 크기 사이 10% 지점을 처음 넘는 블록을 찾는다.
    auto before = energy[static_cast<std::size_t> (best - 4)];

    for (int b = best - 9; b <= best - 4; ++b)
        before = std::min (before, energy[static_cast<std::size_t> (b)]);

    const auto after = std::max ({ energy[static_cast<std::size_t> (best)], energy[static_cast<std::size_t> (best + 1)], energy[static_cast<std::size_t> (best + 2)] });
    const auto threshold = before + 0.1f * (after - before);
    auto start = best;

    while (start > best - 6 && energy[static_cast<std::size_t> (start - 1)] >= threshold)
        --start;

    return static_cast<double> (first + static_cast<std::int64_t> (start - margin) * block) / sampleRate;
}

//==============================================================================
/** 박의 시각을 찾는다. 찾지 못하면 빈 목록을 돌려준다. */
std::vector<double> trackBeats (const Frames& frames, const OnsetCurves& curves, const std::vector<Event>& events, float& tempo)
{
    tempo = 0.0f;

    const auto count = frames.count;
    const auto fps = frames.fps();

    if (count < static_cast<int> (fps * 8.0))
        return {};

    // 타격 세기 곡선: 세 곡선을 더하고, 1초 평균을 빼서 양수만 남긴다.
    std::vector<float> strength (static_cast<std::size_t> (count));

    for (int i = 0; i < count; ++i)
        strength[static_cast<std::size_t> (i)] = 1.2f * curves.curves[0][static_cast<std::size_t> (i)]
                                                 + curves.curves[1][static_cast<std::size_t> (i)]
                                                 + 0.7f * curves.curves[2][static_cast<std::size_t> (i)];

    {
        std::vector<double> prefix (static_cast<std::size_t> (count) + 1, 0.0);

        for (int i = 0; i < count; ++i)
            prefix[static_cast<std::size_t> (i) + 1] = prefix[static_cast<std::size_t> (i)] + strength[static_cast<std::size_t> (i)];

        const auto half = static_cast<int> (fps);
        auto squares = 0.0;

        for (int i = 0; i < count; ++i)
        {
            const auto from = std::max (0, i - half);
            const auto to = std::min (count, i + half + 1);
            const auto mean = static_cast<float> ((prefix[static_cast<std::size_t> (to)] - prefix[static_cast<std::size_t> (from)]) / (to - from));
            auto& value = strength[static_cast<std::size_t> (i)];
            value = std::max (0.0f, value - mean);
            squares += value * value;
        }

        const auto deviation = static_cast<float> (std::sqrt (squares / count));

        if (deviation <= 1.0e-9f)
            return {};

        for (auto& value : strength)
            value /= deviation;
    }

    // 템포: 자기상관에서 가장 뚜렷한 주기를 고른다 (50 ~ 240 BPM).
    const auto lagMin = static_cast<int> (fps * 60.0 / 240.0);
    const auto lagMax = static_cast<int> (fps * 60.0 / 50.0);
    const auto lagLimit = std::min (count - 1, lagMax * 4 + 2);
    std::vector<float> correlation (static_cast<std::size_t> (lagLimit) + 1, 0.0f);

    for (int lag = lagMin / 2; lag <= lagLimit; ++lag)
    {
        auto sum = 0.0;

        for (int i = 0; i + lag < count; ++i)
            sum += strength[static_cast<std::size_t> (i)] * strength[static_cast<std::size_t> (i + lag)];

        correlation[static_cast<std::size_t> (lag)] = static_cast<float> (sum / (count - lag));
    }

    const auto correlationAt = [&correlation, lagLimit] (int lag)
    {
        return lag >= 0 && lag <= lagLimit ? correlation[static_cast<std::size_t> (lag)] : 0.0f;
    };

    auto bestLag = 0;
    auto bestScore = -1.0e9f;

    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        const auto bpm = fps * 60.0 / lag;
        const auto octaves = std::log2 (bpm / 120.0) / 0.8;
        const auto prior = static_cast<float> (std::exp (-0.5 * octaves * octaves));
        const auto score = (correlationAt (lag) + 0.5f * correlationAt (lag * 2) + 0.25f * correlationAt (lag * 4)) * prior;

        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    if (bestLag <= 0)
        return {};

    // 스네어가 보통 두 박마다(2, 4박) 나온다는 점으로 박의 단위를 바로잡는다.
    {
        std::vector<double> snareTimes;

        for (const auto& event : events)
            if (event.snare && event.snareVelocity >= 0.5f)
                snareTimes.push_back (event.frame);

        auto everyBeat = 0, everyTwo = 0, everyFour = 0;

        for (std::size_t i = 1; i < snareTimes.size(); ++i)
        {
            const auto beats = (snareTimes[i] - snareTimes[i - 1]) / bestLag;
            everyBeat += std::abs (beats - 1.0) < 0.12 ? 1 : 0;
            everyTwo += std::abs (beats - 2.0) < 0.2 ? 1 : 0;
            everyFour += std::abs (beats - 4.0) < 0.3 ? 1 : 0;
        }

        auto wanted = bestLag;

        if (everyFour > everyTwo * 3 / 2 && everyFour > everyBeat && fps * 60.0 / (bestLag * 2) >= 55.0)
            wanted = bestLag * 2;
        else if (everyBeat > everyTwo * 3 / 2 && everyBeat > everyFour && fps * 60.0 / (bestLag / 2.0) <= 215.0)
            wanted = bestLag / 2;

        if (wanted != bestLag)
        {
            // 바꾼 주기 근처에서 자기상관이 가장 큰 곳으로 맞춘다.
            auto refined = wanted;

            for (int lag = std::max (lagMin / 2, wanted - 3); lag <= std::min (lagLimit, wanted + 3); ++lag)
                if (correlationAt (lag) > correlationAt (refined))
                    refined = lag;

            bestLag = refined;
        }
    }

    // 주기를 소수점까지: 봉우리 양옆 값으로 포물선을 맞춘다.
    auto period = static_cast<double> (bestLag);

    {
        const auto a = correlationAt (bestLag - 1), b = correlationAt (bestLag), c = correlationAt (bestLag + 1);
        const auto curve = a - 2.0f * b + c;

        if (std::abs (curve) > 1.0e-9f)
            period += std::clamp (0.5 * (a - c) / curve, -0.5, 0.5);
    }

    // 박 추적 (동적 계획법): 타격이 센 곳에 박을 놓되, 박 사이 간격이 주기에서 벗어날수록 점수를 깎는다.
    constexpr float tightness = 100.0f;
    std::vector<float> score (static_cast<std::size_t> (count));
    std::vector<int> previous (static_cast<std::size_t> (count), -1);

    for (int i = 0; i < count; ++i)
    {
        const auto from = std::max (0, static_cast<int> (i - 2.0 * period));
        const auto to = static_cast<int> (i - 0.5 * period);
        auto best = 0.0f;
        auto bestIndex = -1;

        for (int j = from; j <= to; ++j)
        {
            const auto ratio = std::log (static_cast<float> (i - j) / static_cast<float> (period));
            const auto value = score[static_cast<std::size_t> (j)] - tightness * ratio * ratio;

            if (value > best)
            {
                best = value;
                bestIndex = j;
            }
        }

        score[static_cast<std::size_t> (i)] = strength[static_cast<std::size_t> (i)] + best;
        previous[static_cast<std::size_t> (i)] = bestIndex;
    }

    // 끝에서 가장 점수가 높은 곳부터 거슬러 올라간다.
    auto last = count - 1;

    for (int i = std::max (0, static_cast<int> (count - 1.5 * period)); i < count; ++i)
        if (score[static_cast<std::size_t> (i)] > score[static_cast<std::size_t> (last)])
            last = i;

    std::vector<int> beatFrames;

    for (auto i = last; i >= 0; i = previous[static_cast<std::size_t> (i)])
        beatFrames.push_back (i);

    std::reverse (beatFrames.begin(), beatFrames.end());

    if (beatFrames.size() < 8)
        return {};

    // 크기가 커지는 프레임은 실제 타격보다 조금 앞선다. 가까이에 킥이나 스네어가 있으면 그 시각에 맞추고, 없으면 평균만큼 민다.
    constexpr double biasFrames = 1.0;
    std::vector<double> beats;
    beats.reserve (beatFrames.size() + 16);

    std::size_t cursor = 0;

    for (const auto frame : beatFrames)
    {
        auto seconds = frames.secondsAt (frame + biasFrames);

        while (cursor < events.size() && events[cursor].frame < frame - 4)
            ++cursor;

        for (auto k = cursor; k < events.size() && events[k].frame <= frame + 4; ++k)
        {
            if (events[k].kick || events[k].snare)
            {
                seconds = events[k].seconds;
                break;
            }
        }

        beats.push_back (seconds);
    }

    // 간격이 뒤집힌 곳이 있으면 버린다.
    for (std::size_t i = 1; i < beats.size();)
    {
        if (beats[i] - beats[i - 1] < 0.4 * frames.secondsAt (period))
            beats.erase (beats.begin() + static_cast<std::ptrdiff_t> (i));
        else
            ++i;
    }

    // 곡의 처음과 끝까지 같은 간격으로 이어 붙인다.
    const auto duration = frames.secondsAt (count);
    const auto edgePeriod = [&beats] (bool atStart)
    {
        std::vector<float> gaps;
        const auto n = beats.size();

        for (std::size_t i = 1; i < std::min<std::size_t> (n, 9); ++i)
            gaps.push_back (static_cast<float> (atStart ? beats[i] - beats[i - 1] : beats[n - i] - beats[n - i - 1]));

        return static_cast<double> (percentile (gaps, 0.5));
    };

    if (const auto gap = edgePeriod (true); gap > 0.1)
        while (beats.front() - gap > -0.5 * gap)
            beats.insert (beats.begin(), beats.front() - gap);

    if (const auto gap = edgePeriod (false); gap > 0.1)
        while (beats.back() + gap < duration + 0.5 * gap)
            beats.push_back (beats.back() + gap);

    // 템포: 박 간격의 중앙값
    std::vector<float> gaps;

    for (std::size_t i = 1; i < beats.size(); ++i)
        gaps.push_back (static_cast<float> (beats[i] - beats[i - 1]));

    const auto typical = percentile (gaps, 0.5);
    tempo = typical > 0.0f ? 60.0f / typical : 0.0f;
    return beats;
}

/** 시각을 박 격자 위의 자리로 바꾼다. 16분음표나 셋잇단 자리에 가까우면 그 자리로 맞춘다. */
double beatPositionOf (const std::vector<double>& beats, double seconds)
{
    if (beats.size() < 2 || seconds < beats.front() || seconds >= beats.back())
        return -1.0;

    const auto next = std::upper_bound (beats.begin(), beats.end(), seconds);
    const auto index = static_cast<std::size_t> (next - beats.begin()) - 1;
    const auto length = beats[index + 1] - beats[index];
    const auto fraction = (seconds - beats[index]) / length;

    auto sub = static_cast<int> (std::lround (fraction * 24.0));

    if (sub % 6 != 0 && sub % 8 != 0)
    {
        const auto tolerance = std::max (0.02, 0.07 * length);
        const auto nearestSixteenth = static_cast<int> (std::lround (fraction * 4.0)) * 6;
        const auto nearestTriplet = static_cast<int> (std::lround (fraction * 3.0)) * 8;

        if (std::abs (fraction - nearestSixteenth / 24.0) * length <= tolerance)
            sub = nearestSixteenth;
        else if (std::abs (fraction - nearestTriplet / 24.0) * length <= tolerance)
            sub = nearestTriplet;
    }

    return static_cast<double> (index) + sub / 24.0;
}

/** 첫 마디가 시작하는 박을 고른다 (4/4로 본다): 킥이 1, 3박에, 스네어가 2, 4박에, 크래시가 1박에 오는 쪽. */
int findDownbeat (const std::vector<ChartNote>& notes)
{
    std::array<double, 4> scores {};

    for (const auto& note : notes)
    {
        if (note.beat < 0.0)
            continue;

        const auto whole = std::floor (note.beat + 1.0e-6);

        if (note.beat - whole > 0.01)
            continue;    // 박 위의 노트만 본다

        const auto beat = static_cast<int> (whole);

        for (int phase = 0; phase < 4; ++phase)
        {
            const auto inBar = ((beat - phase) % 4 + 4) % 4;

            if (note.lane == Lane::kick && (inBar == 0 || inBar == 2))
                scores[static_cast<std::size_t> (phase)] += note.velocity * (inBar == 0 ? 1.1 : 1.0);
            else if (note.lane == Lane::snare && (inBar == 1 || inBar == 3))
                scores[static_cast<std::size_t> (phase)] += note.velocity;
            else if (note.lane == Lane::crash && inBar == 0)
                scores[static_cast<std::size_t> (phase)] += 2.0;
        }
    }

    return static_cast<int> (std::max_element (scores.begin(), scores.end()) - scores.begin());
}

//==============================================================================
/** 타격들을 노트로 바꾼다. */
std::vector<ChartNote> buildNotes (const std::vector<Event>& events, const std::vector<double>& beats)
{
    std::vector<ChartNote> notes;
    notes.reserve (events.size() * 2);

    for (const auto& event : events)
    {
        const auto beat = beatPositionOf (beats, event.seconds);

        const auto add = [&] (Lane lane, float velocity, std::uint8_t flags)
        {
            ChartNote note;
            note.seconds = event.seconds;
            note.lane = lane;
            note.velocity = velocity;
            note.beat = beat;
            note.flags = flags;
            notes.push_back (note);
        };

        if (event.kick)
            add (Lane::kick, event.kickVelocity, 0);

        if (event.snare)
            add (Lane::snare, event.snareVelocity, 0);

        if (event.tom >= 0)
            add (event.tom == 0 ? Lane::tomHigh : (event.tom == 1 ? Lane::tomMid : Lane::tomFloor), event.tomVelocity, 0);

        if (event.cymbal)
            add (event.crash ? Lane::crash : Lane::hihat, event.cymbalVelocity,
                 static_cast<std::uint8_t> ((event.open ? ChartNote::openHat : 0) | (event.longRing ? 0x80 : 0) | (event.ringKnown ? 0x40 : 0)));
    }

    return notes;
}

bool isTimekeeping (const ChartNote& note)
{
    return note.lane == Lane::hihat || note.lane == Lane::ride;
}

/** 같은 레인에서 너무 붙은 노트는 센 쪽만 남긴다. */
void removeDoubles (std::vector<ChartNote>& notes)
{
    std::stable_sort (notes.begin(), notes.end(), [] (const ChartNote& a, const ChartNote& b) { return a.seconds < b.seconds; });

    std::array<int, numLanes> lastIndex;
    lastIndex.fill (-1);
    std::vector<bool> remove (notes.size(), false);

    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        auto& last = lastIndex[static_cast<std::size_t> (notes[i].lane)];

        if (last >= 0 && notes[i].seconds - notes[static_cast<std::size_t> (last)].seconds < 0.045)
        {
            if (notes[i].velocity > notes[static_cast<std::size_t> (last)].velocity)
            {
                remove[static_cast<std::size_t> (last)] = true;
                last = static_cast<int> (i);
            }
            else
            {
                remove[i] = true;
            }
        }
        else
        {
            last = static_cast<int> (i);
        }
    }

    std::size_t kept = 0;

    for (std::size_t i = 0; i < notes.size(); ++i)
        if (! remove[i])
            notes[kept++] = notes[i];

    notes.resize (kept);
}

/** 마디마다 심벌 패턴을 살핀다: 길게 울리는 구간은 라이드로 옮기고, 킥이나 스네어에 가려진 하이햇을 채운다. */
void refineCymbals (std::vector<ChartNote>& notes, const std::vector<double>& beats, int firstDownbeat, bool bySide)
{
    if (beats.size() < 8)
    {
        for (auto& note : notes)
            note.flags &= 0x3f;

        return;
    }

    // 크래시가 한 박 안쪽 간격으로 네 번 넘게 이어지면 크래시를 라이드처럼 두드리는 구간이다. 첫 번째만 크래시로 둔다.
    {
        std::vector<std::size_t> run;

        const auto flush = [&notes, &run]
        {
            if (run.size() >= 4)
                for (std::size_t i = 1; i < run.size(); ++i)
                {
                    notes[run[i]].lane = Lane::ride;
                    notes[run[i]].flags |= 0x80;
                }

            run.clear();
        };

        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            if (notes[i].lane != Lane::crash || notes[i].beat < 0.0)
                continue;

            if (! run.empty() && notes[i].beat - notes[run.back()].beat > 1.01)
                flush();

            run.push_back (i);
        }

        flush();
    }

    const auto numBars = (static_cast<int> (beats.size()) - firstDownbeat) / 4 + 1;

    struct Bar
    {
        int timekeeping = 0, judged = 0, ringing = 0;
        std::array<bool, 16> cymbalAt {};     // 16분음표 자리마다 심벌(크래시 포함)이 있는지
        std::array<bool, 16> coveredAt {};    // 그 자리에 킥이 있는지
        std::array<bool, 16> snareAt {};
        std::array<bool, 16> tomAt {};
        std::array<double, 16> drumSeconds {};    // 그 자리에서 친 북의 시각 (없으면 0)
        float velocitySum = 0.0f;
        bool ride = false;
    };

    std::vector<Bar> bars (static_cast<std::size_t> (std::max (1, numBars)));

    const auto barOf = [firstDownbeat, numBars] (double beat, int& slot)
    {
        const auto position = beat - firstDownbeat;

        if (position < 0.0)
            return -1;

        const auto bar = static_cast<int> (std::floor (position / 4.0 + 1.0e-6));
        const auto inBar = position - bar * 4.0;
        const auto sixteenth = inBar * 4.0;
        slot = std::abs (sixteenth - std::round (sixteenth)) < 0.02 ? static_cast<int> (std::lround (sixteenth)) % 16 : -1;
        return bar < numBars ? bar : -1;
    };

    auto lastCrash = -100.0;

    for (const auto& note : notes)
    {
        if (note.beat < 0.0)
            continue;

        auto slot = -1;
        const auto bar = barOf (note.beat, slot);

        if (bar < 0)
            continue;

        auto& info = bars[static_cast<std::size_t> (bar)];

        if (note.lane == Lane::crash)
            lastCrash = note.seconds;

        if (isTimekeeping (note))
        {
            ++info.timekeeping;
            info.velocitySum += note.velocity;

            // 여운으로 볼 때: 크래시를 친 직후에는 그 여운 때문에 무엇이든 길게 울리는 것처럼 보이므로 세지 않는다.
            if ((note.flags & 0x40) != 0 && (bySide || note.seconds - lastCrash > 1.2))
            {
                ++info.judged;
                info.ringing += (note.flags & 0x80) != 0 ? 1 : 0;
            }
        }

        if (slot >= 0)
        {
            if (isTimekeeping (note) || note.lane == Lane::crash)
                info.cymbalAt[static_cast<std::size_t> (slot)] = true;
            else if (note.lane == Lane::kick)
                info.coveredAt[static_cast<std::size_t> (slot)] = true;
            else if (note.lane == Lane::snare)
                info.snareAt[static_cast<std::size_t> (slot)] = true;
            else
                info.tomAt[static_cast<std::size_t> (slot)] = true;

            if (! isTimekeeping (note) && note.lane != Lane::crash)
                info.drumSeconds[static_cast<std::size_t> (slot)] = note.seconds;
        }
    }

    // 길게 울리는 심벌이 대부분인 마디가 두 마디 넘게 이어지면 라이드 구간으로 본다.
    // 마디 하나만 보면 들쭉날쭉하므로 앞뒤 두 마디까지 합쳐서 본다.
    for (std::size_t i = 0; i < bars.size(); ++i)
    {
        auto judged = 0, ringing = 0;

        for (auto j = i >= 2 ? i - 2 : 0; j < std::min (bars.size(), i + 3); ++j)
        {
            judged += bars[j].judged;
            ringing += bars[j].ringing;
        }

        // 좌우 위치로 갈랐으면 절반을 넘는 쪽을 따르고, 여운만으로 볼 때는 거의 전부가 길게 울릴 때만 라이드로 본다.
        const auto needed = bySide ? 60 : 85;
        const auto enough = bySide ? bars[i].judged >= 2 : bars[i].judged >= 3;
        bars[i].ride = enough && judged >= 6 && ringing * 100 >= judged * needed;
    }

    // 크래시 여운 때문에 알 수 없던 마디는 바로 뒤(없으면 바로 앞)의 알 수 있는 마디를 따른다.
    // 구간이 바뀌는 첫 박에 크래시를 치고 나서 라이드로 넘어가는 일이 흔하기 때문이다.
    {
        const auto known = [&bars, bySide] (std::size_t i) { return bars[i].judged >= (bySide ? 2 : 3); };
        std::vector<bool> decided (bars.size());

        for (std::size_t i = 0; i < bars.size(); ++i)
        {
            decided[i] = bars[i].ride;

            if (known (i) || bars[i].timekeeping < 3)
                continue;

            if (i + 1 < bars.size() && known (i + 1))
                decided[i] = bars[i + 1].ride;
            else if (i + 2 < bars.size() && known (i + 2))
                decided[i] = bars[i + 2].ride;
            else if (i > 0 && known (i - 1))
                decided[i] = bars[i - 1].ride;
        }

        for (std::size_t i = 0; i < bars.size(); ++i)
            bars[i].ride = decided[i];
    }

    // 세 마디보다 짧은 구간은 앞뒤 구간에 합친다 (드러머는 보통 여러 마디 단위로 하이햇과 라이드를 오간다).
    for (int pass = 0; pass < 2; ++pass)
    {
        for (std::size_t start = 0; start < bars.size();)
        {
            auto end = start;

            while (end < bars.size() && bars[end].ride == bars[start].ride)
                ++end;

            if (end - start < 3 && start > 0 && end < bars.size())
                for (auto i = start; i < end; ++i)
                    bars[i].ride = ! bars[i].ride;

            start = end;
        }
    }

    for (auto& note : notes)
    {
        if (note.lane == Lane::hihat && note.beat >= 0.0)
        {
            auto slot = -1;

            if (const auto bar = barOf (note.beat, slot); bar >= 0 && bars[static_cast<std::size_t> (bar)].ride)
            {
                note.lane = Lane::ride;
                note.flags &= static_cast<std::uint8_t> (~ChartNote::openHat);
            }
        }

        note.flags &= 0x3f;
    }

    // 가려진 하이햇 채우기. 스네어의 잡음은 하이햇을 가리지만 킥은 가리지 않으므로, 스네어 자리만 채운다.
    std::vector<ChartNote> added;

    const auto cymbalNear = [&bars] (int barIndex, int slot)
    {
        // 마디를 넘어가는 자리도 볼 수 있게 한다.
        const auto global = barIndex * 16 + slot;

        if (global < 0 || global >= static_cast<int> (bars.size()) * 16)
            return false;

        return static_cast<bool> (bars[static_cast<std::size_t> (global / 16)].cymbalAt[static_cast<std::size_t> (global % 16)]);
    };

    for (const auto& note : notes)
    {
        if (note.lane != Lane::snare || note.beat < 0.0)
            continue;

        auto slot = -1;
        const auto barIndex = barOf (note.beat, slot);

        if (barIndex < 0 || slot < 0)
            continue;

        const auto& bar = bars[static_cast<std::size_t> (barIndex)];

        if (bar.cymbalAt[static_cast<std::size_t> (slot)] || bar.timekeeping < 2)
            continue;

        // 이 마디에서 심벌이 어느 단위로 이어지는지: 스네어가 없는 자리 가운데 심벌이 있는 비율을 본다.
        const auto coverage = [&bar] (int step, int offset)
        {
            auto open = 0, filled = 0;

            for (int s = offset; s < 16; s += step)
            {
                if (bar.snareAt[static_cast<std::size_t> (s)])
                    continue;

                ++open;
                filled += bar.cymbalAt[static_cast<std::size_t> (s)] ? 1 : 0;
            }

            return open >= 2 ? static_cast<float> (filled) / static_cast<float> (open) : -1.0f;
        };

        const auto sixteenths = coverage (4, 1) >= 0.5f && coverage (4, 3) >= 0.5f;    // 16분음표 엇박 자리
        const auto eighths = coverage (4, 2) >= 0.5f;                                 // 8분음표 엇박 자리
        const auto quarters = coverage (4, 0) >= 0.5f;                                // 박 위

        // 같은 단위로 바로 앞과 바로 뒤에 실제로 들린 심벌이 있어야 한다 (필인처럼 심벌을 쉬는 곳에는 채우지 않는다).
        const auto between = [&cymbalNear, barIndex, slot] (int step)
        {
            return cymbalNear (barIndex, slot - step) && cymbalNear (barIndex, slot + step);
        };

        auto fill = false;

        if (slot % 4 == 0)
            fill = (quarters && eighths && between (2)) || (quarters && ! eighths && between (4));
        else if (slot % 2 == 0)
            fill = eighths && between (2);

        // 16분음표로 두 손이 번갈아 치는 패턴에서는 스네어를 칠 때 하이햇을 치지 않는다.
        if (sixteenths)
            fill = false;

        if (! fill)
            continue;

        ChartNote hat;
        hat.seconds = note.seconds;
        hat.lane = bar.ride ? Lane::ride : Lane::hihat;
        hat.velocity = std::clamp (bar.velocitySum / static_cast<float> (bar.timekeeping), 0.2f, 1.0f);
        hat.beat = note.beat;
        hat.flags = ChartNote::inferred;
        added.push_back (hat);

        bars[static_cast<std::size_t> (barIndex)].cymbalAt[static_cast<std::size_t> (slot)] = true;
    }

    // 고르게 이어지는 심벌에서 한 칸만 빠진 곳은 못 들은 것으로 보고 채운다:
    // 8분음표로 이어지다가 하나만 빠졌거나, 4분음표로 이어지다가 하나만 빠진 자리.
    {
        std::vector<bool> heard (bars.size() * 16);

        for (std::size_t i = 0; i < heard.size(); ++i)
            heard[i] = bars[i / 16].cymbalAt[i % 16];

        const auto heardAt = [&heard] (int global) { return global >= 0 && global < static_cast<int> (heard.size()) && heard[static_cast<std::size_t> (global)]; };

        for (int global = 0; global < static_cast<int> (heard.size()); global += 2)
        {
            const auto barIndex = global / 16, slot = global % 16;
            const auto& bar = bars[static_cast<std::size_t> (barIndex)];

            if (heardAt (global) || bar.tomAt[static_cast<std::size_t> (slot)] || bar.timekeeping < 3)
                continue;

            // 양옆에 있고, 한쪽으로 한 칸 더 가도 있어야 한다. (4분음표로 치는 것을 8분음표로 바꿔 버리지 않게)
            const auto eighthGap = heardAt (global - 2) && heardAt (global + 2) && (heardAt (global - 4) || heardAt (global + 4));
            const auto quarterGap = slot % 4 == 0 && heardAt (global - 4) && heardAt (global + 4)
                                    && ! heardAt (global - 2) && ! heardAt (global + 2)
                                    && (heardAt (global - 8) || heardAt (global + 8));

            if (! eighthGap && ! quarterGap)
                continue;

            // 그 자리의 시각: 같은 자리에 친 북이 있으면 그 시각, 없으면 박 사이를 나눠서 구한다.
            const auto position = firstDownbeat + barIndex * 4 + slot / 4.0;
            const auto beatIndex = static_cast<std::size_t> (position);

            if (beatIndex + 1 >= beats.size())
                continue;

            auto seconds = bar.drumSeconds[static_cast<std::size_t> (slot)];

            if (seconds <= 0.0)
                seconds = beats[beatIndex] + (position - static_cast<double> (beatIndex)) * (beats[beatIndex + 1] - beats[beatIndex]);

            ChartNote hat;
            hat.seconds = seconds;
            hat.lane = bar.ride ? Lane::ride : Lane::hihat;
            hat.velocity = std::clamp (0.9f * bar.velocitySum / static_cast<float> (bar.timekeeping), 0.2f, 1.0f);
            hat.beat = position;
            hat.flags = ChartNote::inferred;
            added.push_back (hat);
        }
    }

    notes.insert (notes.end(), added.begin(), added.end());
}

} // namespace

//==============================================================================
Chart transcribeDrums (const float* left, const float* right, std::size_t numSamples, double sampleRate,
                       const TranscribeOptions& options,
                       std::atomic<float>* progress,
                       const std::atomic<bool>* cancel,
                       std::string* debugLog)
{
    Chart chart;
    chart.duration = sampleRate > 0.0 ? static_cast<double> (numSamples) / sampleRate : 0.0;

    if (left == nullptr || sampleRate < 8000.0 || chart.duration < 2.0)
        return chart;

    // 타격의 시각을 파형에서 다시 잡을 때 쓸 모노 신호
    std::vector<float> mixed;

    if (right != nullptr && options.isolatedDrums)
    {
        mixed.resize (numSamples);

        for (std::size_t i = 0; i < numSamples; ++i)
            mixed[i] = 0.5f * (left[i] + right[i]);
    }

    const auto* samples = mixed.empty() ? left : mixed.data();

    const auto& tuning = options.isolatedDrums ? stemTuning : mixTuning;
    const auto frames = analyseFrames (left, right, numSamples, sampleRate, ! options.isolatedDrums, progress, cancel);

    if (frames.count == 0)
        return chart;

    const auto curves = computeOnsetCurves (frames);
    auto events = findEvents (frames, curves, tuning);
    const auto bySide = classifyEvents (events, tuning, debugLog);

    if (progress != nullptr)
        progress->store (0.88f, std::memory_order_relaxed);

    // 타격의 시각. 드럼만 있는 소리에서는 파형에서 직접 다시 잡는다.
    constexpr double biasFrames = 1.0;

    for (auto& event : events)
    {
        event.seconds = frames.secondsAt (event.frame + biasFrames);

        if (options.isolatedDrums && (event.kick || event.snare || event.cymbal || event.tom >= 0))
            event.seconds = refineOnset (samples, numSamples, sampleRate, event.seconds + 0.004,
                                         event.cymbal && ! event.kick && ! event.snare && event.tom < 0);
    }

    chart.beats = trackBeats (frames, curves, events, chart.tempo);
    chart.notes = buildNotes (events, chart.beats);
    removeDoubles (chart.notes);

    if (chart.tempo > 0.0f)
    {
        // 16분음표나 셋잇단 자리에서 벗어난 약한 하이햇은 잘못 잡힌 것일 때가 많아 버린다.
        std::erase_if (chart.notes, [] (const ChartNote& note)
        {
            if (note.lane != Lane::hihat || note.beat < 0.0 || note.velocity >= 0.6f)
                return false;

            const auto sub = static_cast<int> (std::lround ((note.beat - std::floor (note.beat)) * 24.0)) % 24;
            return sub % 6 != 0 && sub % 8 != 0;
        });
    }

    chart.firstDownbeat = findDownbeat (chart.notes);
    refineCymbals (chart.notes, chart.beats, chart.firstDownbeat, bySide);
    removeDoubles (chart.notes);

    if (debugLog != nullptr)
    {
        char line[320];
        std::array<int, numLanes> perLane {};
        auto inferred = 0;

        for (const auto& note : chart.notes)
        {
            ++perLane[static_cast<std::size_t> (note.lane)];
            inferred += (note.flags & ChartNote::inferred) != 0 ? 1 : 0;
        }

        std::snprintf (line, sizeof (line), "frames %d (%.1f per second), events %d, tempo %.1f, beats %d, first downbeat %d\n",
                       frames.count, frames.fps(), static_cast<int> (events.size()), static_cast<double> (chart.tempo),
                       static_cast<int> (chart.beats.size()), chart.firstDownbeat);
        *debugLog += line;

        std::snprintf (line, sizeof (line), "notes %d: crash %d, hihat %d, snare %d, tom high %d, tom mid %d, tom floor %d, ride %d, kick %d (filled in %d)\n",
                       static_cast<int> (chart.notes.size()), perLane[0], perLane[1], perLane[2], perLane[3], perLane[4], perLane[5], perLane[6], perLane[7], inferred);
        *debugLog += line;

        if (options.logEvents)
        {
            *debugLog += "seconds  LMH  low     body    mid     pres    high    f0   lowSus noise  h58  h174 h464  kickAmt snareAmt labels\n";

            for (const auto& event : events)
            {
                std::snprintf (line, sizeof (line), "%7.3f  %c%c%c  %7.3f %7.3f %7.3f %7.3f %7.3f %5.0f %.2f   %.3f  %.2f %.2f %.2f  %.2f    %.2f     %s%s%s%s%s%s\n",
                               event.seconds,
                               event.firedLow ? 'L' : '-', event.firedMid ? 'M' : '-', event.firedHigh ? 'H' : '-',
                               event.low, event.body, event.mid, event.presence, event.high, event.f0,
                               event.lowSustain, event.noiseRatio, event.highAt58, event.highAt174, event.highAt464,
                               event.kickAmount, event.snareAmount,
                               event.kick ? "kick " : "", event.snare ? "snare " : "",
                               event.tom == 0 ? "tomH " : (event.tom == 1 ? "tomM " : (event.tom == 2 ? "tomF " : "")),
                               event.cymbal ? (event.crash ? "crash " : "cym ") : "", event.open ? "open " : "", event.longRing ? "ring" : "");
                *debugLog += line;
            }
        }
    }

    if (progress != nullptr)
        progress->store (1.0f, std::memory_order_relaxed);

    return chart;
}

} // namespace hvd
