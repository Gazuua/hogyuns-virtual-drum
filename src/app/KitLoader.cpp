#include "KitLoader.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "Json.h"
#include "core/Engine.h"
#include "core/StandardPads.h"

namespace
{

constexpr double maxSampleSeconds = 60.0;

/** 패드 id로 고르는 기본 MIDI 노트 번호. 표준 패드가 아니면 -1. */
int defaultMidiNote (const std::string& padId)
{
    const auto* standard = hvd::findStandardPad (padId);
    return standard != nullptr ? standard->midiNote : -1;
}

/** 패드에 적힌 숫자 하나. 적지 않았으면 표의 기본값. */
double padNumber (const juce::var& pad, const hvd::PadField& field)
{
    return json::numberOr (pad[field.name], field.fallback);
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
    kit->humanizeVolume = juce::jlimit (0.0f, 0.5f, json::numberOr (humanize, "volume", 0.0f));
    kit->humanizePitchCents = juce::jlimit (0.0f, 100.0f, json::numberOr (humanize, "pitchCents", 0.0f));

    const auto folder = kitFile.getParentDirectory();
    const auto kitCurve = json::numberOr (root[hvd::padField::velocityCurve.name], hvd::padField::velocityCurve.fallback);

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
        // 숫자 값의 이름, 한도, 기본값은 hvd::padFields에 있다. 세기 곡선을 적지 않았으면 킷 전체의 값을 쓴다.
        namespace field = hvd::padField;
        const auto params = hvd::makePadParams (padNumber (padVar, field::volume), padNumber (padVar, field::pan),
                                                padNumber (padVar, field::pitch), padNumber (padVar, field::reverbSend),
                                                padNumber (padVar, field::startMs),
                                                json::numberOr (padVar[field::velocityCurve.name], kitCurve));
        pad.volume = params.volume;
        pad.pan = params.pan;
        pad.pitchRatio = params.pitchRatio;
        pad.velocityCurve = params.velocityCurve;
        pad.reverbSend = params.reverbSend;
        pad.startSeconds = params.startSeconds;
        pad.chokeFadeMs = static_cast<float> (field::chokeFadeMs.clamp (padNumber (padVar, field::chokeFadeMs)));

        const auto note = static_cast<int> (json::numberOr (padVar, "midiNote", static_cast<float> (defaultMidiNote (pad.id))));
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
                layer.upTo = juce::jlimit (0.0f, 1.0f, json::numberOr (layerVar, "upTo", 1.0f));
                layer.nominal = juce::jlimit (0.01f, 1.0f, json::numberOr (layerVar, "nominal", layer.upTo));
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

    // "sympathy": { "kick": 1.0, "tom_floor": 0.5 } — 그 패드들을 치면 이 패드가 함께 작게 울린다 (스네어 울림).
    for (const auto& padVar : *padsArray)
    {
        const auto* sympathy = padVar["sympathy"].getDynamicObject();
        const auto resonator = kit->findPad (padVar["id"].toString().toStdString());

        if (sympathy == nullptr || resonator < 0)
            continue;

        for (const auto& entry : sympathy->getProperties())
        {
            const auto trigger = kit->findPad (entry.name.toString().toStdString());

            if (trigger < 0 || trigger == resonator)
            {
                result.warnings.add ("unknown pad in \"sympathy\": " + entry.name.toString());
                continue;
            }

            auto& pad = kit->pads[static_cast<std::size_t> (trigger)];
            pad.sympatheticPad = resonator;
            pad.sympatheticAmount = juce::jlimit (0.0f, 4.0f, static_cast<float> (static_cast<double> (entry.value)));
        }
    }

    if (! kit->samples.empty())
        result.averageTrimMs = trimmedMsTotal / static_cast<double> (kit->samples.size());

    juce::ignoreUnused (trimmedFramesTotal);
    result.kit = std::move (kit);
    return result;
}
