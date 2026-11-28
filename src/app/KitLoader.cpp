#include "KitLoader.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "Json.h"
#include "SampleCache.h"
#include "core/Engine.h"
#include "core/StandardPads.h"

namespace
{

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

    // 킷이 쓰는 샘플 파일을 먼저 모아 한꺼번에 읽는다. 전에 읽은 파일은 다시 디코드하지 않고 함께 쓰며,
    // 새 파일은 여러 스레드로 나눠 디코드한다. 아래에서 패드를 읽을 때는 그 결과를 꺼내 쓴다.
    std::map<juce::String, DecodedSample> decoded;

    {
        juce::StringArray paths;

        const auto collect = [&paths] (const juce::var& list)
        {
            if (const auto* files = list.getArray())
                for (const auto& file : *files)
                    paths.addIfNotAlreadyThere (file.toString());
        };

        for (const auto& padVar : *padsArray)
        {
            if (const auto* layers = padVar["layers"].getArray())
                for (const auto& layerVar : *layers)
                    collect (layerVar["samples"]);
            else
                collect (padVar["samples"]);
        }

        juce::Array<juce::File> files;

        for (const auto& path : paths)
            files.add (folder.getChildFile (path));

        auto samples = decodeSamples (files);

        for (int i = 0; i < paths.size(); ++i)
            decoded[paths[i]] = std::move (samples[static_cast<std::size_t> (i)]);
    }

    std::map<juce::String, std::uint32_t> loadedSamples;
    juce::int64 trimmedFramesTotal = 0;
    auto trimmedMsTotal = 0.0;

    // 샘플 파일의 번호를 돌려준다. 같은 파일은 한 번만 넣는다. 못 읽은 파일이면 -1.
    const auto loadSample = [&] (const juce::String& relativePath) -> int
    {
        if (const auto found = loadedSamples.find (relativePath); found != loadedSamples.end())
            return static_cast<int> (found->second);

        const auto& file = decoded[relativePath];

        if (file.sample == nullptr)
        {
            result.warnings.add (relativePath + ": " + file.error);
            return -1;
        }

        const auto& sample = *file.sample;
        const auto trimmed = file.trimmedFrames;

        trimmedFramesTotal += trimmed;
        trimmedMsTotal += 1000.0 * trimmed / sample.sampleRate;
        result.totalSeconds += static_cast<double> (sample.frames()) / sample.sampleRate;
        result.memoryBytes += static_cast<juce::int64> ((sample.left.size() + sample.right.size()) * sizeof (float));
        ++(file.reused ? result.reusedFiles : result.decodedFiles);

        const auto index = static_cast<std::uint32_t> (kit->samples.size());
        kit->samples.push_back (file.sample);
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

        // 레이어의 크기: 번갈아 쓰는 샘플들의 크기(dB) 평균
        for (auto& layer : pad.layers)
        {
            auto sum = 0.0f;

            for (const auto index : layer.samples)
                sum += kit->samples[index]->loudness;

            layer.loudness = sum / static_cast<float> (layer.samples.size());
            layer.measured = layer.loudness > -90.0f;
        }

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
