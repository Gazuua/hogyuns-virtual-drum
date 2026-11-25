#include "KitLoader.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/Engine.h"

namespace
{

constexpr double maxSampleSeconds = 60.0;

/** 패드 id로 고르는 기본 MIDI 노트 번호 (General MIDI 드럼 맵에 가깝게). */
int defaultMidiNote (const juce::String& padId)
{
    static const std::map<juce::String, int> notes {
        { "kick", 36 }, { "snare", 38 }, { "snare_rim", 40 }, { "sidestick", 37 },
        { "hihat_closed", 42 }, { "hihat_half", 23 }, { "hihat_open", 46 }, { "hihat_pedal", 44 },
        { "tom_high", 48 }, { "tom_mid", 45 }, { "tom_floor", 43 }, { "tom_low", 41 },
        { "crash1", 49 }, { "crash2", 57 }, { "china", 52 },
        { "ride", 51 }, { "ride_bell", 53 }, { "ride_edge", 59 },
        { "crash_bright", 55 }, { "crash_trash", 58 }, { "crash_stack", 54 }
    };

    const auto found = notes.find (padId);
    return found != notes.end() ? found->second : -1;
}

float numberOr (const juce::var& object, const char* key, float fallback)
{
    const auto& value = object[key];
    return (value.isDouble() || value.isInt() || value.isInt64()) ? static_cast<float> (static_cast<double> (value))
                                                                  : fallback;
}

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

} // namespace

KitLoadResult loadKit (const juce::File& kitFile)
{
    KitLoadResult result;

    if (! kitFile.existsAsFile())
    {
        result.error = "kit file not found: " + kitFile.getFullPathName();
        return result;
    }

    juce::var root;
    const auto parsed = juce::JSON::parse (kitFile.loadFileAsString(), root);

    if (parsed.failed())
    {
        result.error = "kit.json: " + parsed.getErrorMessage();
        return result;
    }

    const auto* padsArray = root["pads"].getArray();

    if (padsArray == nullptr)
    {
        result.error = "kit.json has no \"pads\" list";
        return result;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto kit = std::make_unique<hvd::Kit>();
    kit->name = root["name"].toString().toStdString();
    kit->source = root["source"].toString().toStdString();
    kit->license = root["license"].toString().toStdString();

    // "humanize": { "volume": 0.04, "pitchCents": 4 }
    const auto& humanize = root["humanize"];
    kit->humanizeVolume = juce::jlimit (0.0f, 0.5f, numberOr (humanize, "volume", 0.0f));
    kit->humanizePitchCents = juce::jlimit (0.0f, 100.0f, numberOr (humanize, "pitchCents", 0.0f));

    const auto folder = kitFile.getParentDirectory();
    const auto kitCurve = numberOr (root, "velocityCurve", 1.0f);

    std::map<juce::String, std::uint32_t> loadedSamples;
    juce::int64 trimmedFramesTotal = 0;
    auto trimmedMsTotal = 0.0;

    // 샘플 파일을 읽어 번호를 돌려준다. 같은 파일은 한 번만 읽는다. 실패하면 -1.
    const auto loadSample = [&] (const juce::String& relativePath) -> int
    {
        if (const auto found = loadedSamples.find (relativePath); found != loadedSamples.end())
            return static_cast<int> (found->second);

        hvd::Sample sample;
        juce::String error;
        auto trimmed = 0;

        if (! decodeFile (formats, folder.getChildFile (relativePath), sample, trimmed, error))
        {
            result.warnings.add (relativePath + ": " + error);
            return -1;
        }

        trimmedFramesTotal += trimmed;
        trimmedMsTotal += 1000.0 * trimmed / sample.sampleRate;
        result.totalSeconds += static_cast<double> (sample.frames()) / sample.sampleRate;
        result.memoryBytes += static_cast<juce::int64> ((sample.left.size() + sample.right.size()) * sizeof (float));

        const auto index = static_cast<std::uint32_t> (kit->samples.size());
        kit->samples.push_back (std::move (sample));
        loadedSamples[relativePath] = index;
        return static_cast<int> (index);
    };

    // 초크 그룹 이름을 번호로 바꾼다. 처음 보는 이름이면 새로 만든다. 자리가 없으면 -1.
    const auto groupIndex = [&] (const juce::String& name) -> int
    {
        const auto text = name.toStdString();

        for (std::size_t i = 0; i < kit->groupNames.size(); ++i)
            if (kit->groupNames[i] == text)
                return static_cast<int> (i);

        if (kit->groupNames.size() >= static_cast<std::size_t> (hvd::Kit::maxGroups))
        {
            result.warnings.add ("too many choke groups, ignoring: " + name);
            return -1;
        }

        kit->groupNames.push_back (text);
        return static_cast<int> (kit->groupNames.size() - 1);
    };

    const auto readSamples = [&] (const juce::var& list, hvd::Layer& layer)
    {
        if (const auto* files = list.getArray())
            for (const auto& file : *files)
                if (const auto index = loadSample (file.toString()); index >= 0)
                    layer.samples.push_back (static_cast<std::uint32_t> (index));
    };

    for (const auto& padVar : *padsArray)
    {
        if (kit->pads.size() >= static_cast<std::size_t> (hvd::Engine::maxPads))
        {
            result.warnings.add ("too many pads, the rest are ignored");
            break;
        }

        hvd::Pad pad;
        pad.id = padVar["id"].toString().toStdString();

        if (pad.id.empty())
        {
            result.warnings.add ("a pad without \"id\" was skipped");
            continue;
        }

        if (kit->findPad (pad.id) >= 0)
        {
            result.warnings.add ("duplicate pad id skipped: " + juce::String (pad.id));
            continue;
        }

        const auto name = padVar["name"].toString();
        pad.name = name.isNotEmpty() ? name.toStdString() : pad.id;
        pad.volume = juce::jlimit (0.0f, 4.0f, numberOr (padVar, "volume", 1.0f));
        pad.pan = juce::jlimit (-1.0f, 1.0f, numberOr (padVar, "pan", 0.0f));
        pad.pitchRatio = std::pow (2.0f, juce::jlimit (-24.0f, 24.0f, numberOr (padVar, "pitch", 0.0f)) / 12.0f);
        pad.velocityCurve = juce::jlimit (0.0f, 4.0f, numberOr (padVar, "velocityCurve", kitCurve));
        pad.chokeFadeMs = juce::jlimit (1.0f, 2000.0f, numberOr (padVar, "chokeFadeMs", 20.0f));
        pad.reverbSend = juce::jlimit (0.0f, 1.0f, numberOr (padVar, "reverbSend", 1.0f));
        pad.startSeconds = juce::jlimit (0.0f, 5000.0f, numberOr (padVar, "startMs", 0.0f)) * 0.001f;

        const auto note = static_cast<int> (numberOr (padVar, "midiNote", static_cast<float> (defaultMidiNote (juce::String (pad.id)))));
        pad.midiNote = juce::isPositiveAndBelow (note, 128) ? note : -1;

        if (const auto group = padVar["group"].toString(); group.isNotEmpty())
        {
            pad.group = groupIndex (group);

            if (pad.group >= 0)
                pad.cutsMask = 1u << pad.group;    // 따로 적지 않으면 자기 그룹을 끊는다
        }

        if (const auto* cuts = padVar["cuts"].getArray())
        {
            pad.cutsMask = 0;

            for (const auto& cut : *cuts)
                if (const auto index = groupIndex (cut.toString()); index >= 0)
                    pad.cutsMask |= 1u << index;
        }

        if (const auto* layers = padVar["layers"].getArray())
        {
            for (const auto& layerVar : *layers)
            {
                hvd::Layer layer;
                layer.upTo = juce::jlimit (0.0f, 1.0f, numberOr (layerVar, "upTo", 1.0f));
                layer.nominal = juce::jlimit (0.01f, 1.0f, numberOr (layerVar, "nominal", layer.upTo));
                readSamples (layerVar["samples"], layer);

                if (! layer.samples.empty())
                    pad.layers.push_back (std::move (layer));
            }

            std::stable_sort (pad.layers.begin(), pad.layers.end(),
                              [] (const hvd::Layer& a, const hvd::Layer& b) { return a.upTo < b.upTo; });
        }
        else
        {
            hvd::Layer layer;
            readSamples (padVar["samples"], layer);

            if (! layer.samples.empty())
                pad.layers.push_back (std::move (layer));
        }

        if (pad.layers.empty())
            result.warnings.add ("pad has no playable samples: " + juce::String (pad.id));

        kit->pads.push_back (std::move (pad));
    }

    if (! kit->samples.empty())
        result.averageTrimMs = trimmedMsTotal / static_cast<double> (kit->samples.size());

    juce::ignoreUnused (trimmedFramesTotal);
    result.kit = std::move (kit);
    return result;
}
