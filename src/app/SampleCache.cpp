#include "SampleCache.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>

namespace
{

constexpr double maxSampleSeconds = 60.0;

// 디코드에 쓸 스레드 수의 한도. 이 PC(16코어)에서 기본 킷(238개)을 처음 읽는 데 4개 190ms, 8개 110ms, 16개 150ms였다.
constexpr int maxDecodeThreads = 8;

/** 소리가 시작되는 프레임을 찾는다. 타격 앞의 무음은 그대로 지연이 되므로 잘라 낸다. */
int findOnset (const juce::AudioBuffer<float>& buffer)
{
    const auto frames = buffer.getNumSamples();
    auto peak = 0.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = std::max (peak, buffer.getMagnitude (channel, 0, frames));

    if (peak <= 0.0f)
        return 0;

    const auto threshold = std::max (peak * 0.01f, 1.0e-4f);

    for (int i = 0; i < frames; ++i)
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            if (std::abs (buffer.getSample (channel, i)) > threshold)
                return std::max (0, i - 24);    // 어택이 잘리지 않게 조금 앞에서 시작한다

    return 0;
}

bool decodeFile (juce::AudioFormatManager& formats, const juce::File& file,
                 hvd::Sample& sample, int& trimmedFrames, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "unsupported or unreadable audio file";
        return false;
    }

    if (reader->sampleRate <= 0.0 || reader->lengthInSamples < 4)
    {
        error = "empty audio file";
        return false;
    }

    const auto maxFrames = static_cast<juce::int64> (maxSampleSeconds * reader->sampleRate);
    const auto frames = static_cast<int> (std::min (reader->lengthInSamples, maxFrames));
    const auto channels = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));

    juce::AudioBuffer<float> buffer (channels, frames);

    if (! reader->read (&buffer, 0, frames, 0, true, true))
    {
        error = "could not decode";
        return false;
    }

    trimmedFrames = findOnset (buffer);

    if (frames - trimmedFrames < 4)
    {
        error = "no sound in file";
        return false;
    }

    const auto* left = buffer.getReadPointer (0);
    sample.sampleRate = reader->sampleRate;
    sample.left.assign (left + trimmedFrames, left + frames);
    sample.right.clear();

    if (channels == 2)
    {
        const auto* right = buffer.getReadPointer (1);

        // 양쪽이 똑같으면 한 벌만 둔다.
        if (! std::equal (left, left + frames, right))
            sample.right.assign (right + trimmedFrames, right + frames);
    }

    return true;
}

juce::int64 bytesOf (const hvd::Sample& sample)
{
    return static_cast<juce::int64> ((sample.left.size() + sample.right.size()) * sizeof (float));
}

/** 파일이 그대로인지 알아보는 값. 크기나 수정 시각이 바뀌면 다시 읽는다. */
struct FileStamp
{
    juce::int64 size = 0;
    juce::int64 modified = 0;

    static FileStamp of (const juce::File& file)
    {
        return { file.getSize(), file.getLastModificationTime().toMilliseconds() };
    }

    bool operator== (const FileStamp&) const = default;
};

struct Entry
{
    FileStamp stamp;
    std::shared_ptr<const hvd::Sample> sample;
    int trimmedFrames = 0;
    std::uint64_t lastUsed = 0;    // 마지막으로 쓴 차례 (Cache::useCount)
};

struct Cache
{
    std::mutex lock;
    std::map<juce::String, Entry> entries;    // 열쇠는 소문자로 바꾼 전체 경로 (Windows는 대소문자를 가리지 않는다)
    std::uint64_t useCount = 0;
};

Cache& cache()
{
    static Cache instance;
    return instance;
}

juce::String keyOf (const juce::File& file)
{
    return file.getFullPathName().toLowerCase();
}

/** 어느 킷도 쓰지 않는(캐시만 들고 있는) 샘플을 오래 안 쓴 것부터 지워 limit 아래로 줄인다. lock을 잡고 부른다. */
void trimLocked (Cache& state, juce::int64 limit)
{
    std::vector<std::map<juce::String, Entry>::iterator> unused;
    juce::int64 unusedBytes = 0;

    for (auto it = state.entries.begin(); it != state.entries.end(); ++it)
    {
        if (it->second.sample.use_count() == 1)
        {
            unused.push_back (it);
            unusedBytes += bytesOf (*it->second.sample);
        }
    }

    std::sort (unused.begin(), unused.end(), [] (const auto& a, const auto& b) { return a->second.lastUsed < b->second.lastUsed; });

    for (const auto& it : unused)
    {
        if (unusedBytes <= limit)
            break;

        unusedBytes -= bytesOf (*it->second.sample);
        state.entries.erase (it);
    }
}

/** files[indices[k]]를 여러 스레드로 나눠 디코드해 results에 넣는다. */
void decodeInParallel (const juce::Array<juce::File>& files, const std::vector<int>& indices, std::vector<DecodedSample>& results)
{
    std::atomic<std::size_t> next { 0 };

    const auto work = [&]
    {
        juce::AudioFormatManager formats;    // 스레드마다 따로 둔다
        formats.registerBasicFormats();

        for (auto k = next.fetch_add (1); k < indices.size(); k = next.fetch_add (1))
        {
            const auto index = static_cast<std::size_t> (indices[k]);
            auto sample = std::make_shared<hvd::Sample>();

            if (decodeFile (formats, files.getReference (static_cast<int> (index)), *sample, results[index].trimmedFrames, results[index].error))
                results[index].sample = std::move (sample);
        }
    };

    const auto hardware = static_cast<int> (std::thread::hardware_concurrency());
    const auto numThreads = std::min ({ std::max (1, hardware - 1), maxDecodeThreads, static_cast<int> (indices.size()) });

    std::vector<std::thread> helpers;

    for (int i = 1; i < numThreads; ++i)
        helpers.emplace_back (work);

    work();

    for (auto& helper : helpers)
        helper.join();
}

} // namespace

std::vector<DecodedSample> decodeSamples (const juce::Array<juce::File>& files)
{
    std::vector<DecodedSample> results (static_cast<std::size_t> (files.size()));
    std::vector<FileStamp> stamps;
    std::vector<int> missing;

    stamps.reserve (results.size());

    for (const auto& file : files)
        stamps.push_back (FileStamp::of (file));

    auto& state = cache();

    {
        const std::scoped_lock hold (state.lock);
        ++state.useCount;

        for (int i = 0; i < files.size(); ++i)
        {
            const auto index = static_cast<std::size_t> (i);
            const auto found = state.entries.find (keyOf (files.getReference (i)));

            if (found != state.entries.end() && found->second.stamp == stamps[index])
            {
                results[index].sample = found->second.sample;
                results[index].trimmedFrames = found->second.trimmedFrames;
                results[index].reused = true;
                found->second.lastUsed = state.useCount;
            }
            else
            {
                missing.push_back (i);
            }
        }
    }

    // 잠그지 않고 디코드한다. 다른 스레드가 같은 파일을 함께 읽을 수도 있는데, 그러면 먼저 넣은 것을 쓴다.
    if (! missing.empty())
        decodeInParallel (files, missing, results);

    {
        const std::scoped_lock hold (state.lock);

        for (const auto i : missing)
        {
            auto& result = results[static_cast<std::size_t> (i)];

            if (result.sample == nullptr)
                continue;    // 못 읽은 파일은 기억하지 않는다. 다음에 다시 읽어 본다

            auto& entry = state.entries[keyOf (files.getReference (i))];

            if (entry.sample != nullptr && entry.stamp == stamps[static_cast<std::size_t> (i)])
                result.sample = entry.sample;
            else
                entry = { stamps[static_cast<std::size_t> (i)], result.sample, result.trimmedFrames, state.useCount };
        }

        trimLocked (state, sampleCacheUnusedBytes);
    }

    return results;
}

void trimSampleCache()
{
    auto& state = cache();
    const std::scoped_lock hold (state.lock);
    trimLocked (state, sampleCacheUnusedBytes);
}

void clearSampleCache()
{
    auto& state = cache();
    const std::scoped_lock hold (state.lock);
    state.entries.clear();
}

SampleCacheStats getSampleCacheStats()
{
    auto& state = cache();
    const std::scoped_lock hold (state.lock);
    SampleCacheStats stats;

    for (const auto& [key, entry] : state.entries)
    {
        const auto bytes = bytesOf (*entry.sample);
        ++stats.numSamples;
        stats.bytes += bytes;

        if (entry.sample.use_count() == 1)
            stats.unusedBytes += bytes;
    }

    return stats;
}
