#include "LoudnessTool.h"

#include <cmath>
#include <map>
#include <optional>
#include <vector>

#include "Backups.h"
#include "Documents.h"
#include "KeymapLoader.h"
#include "KitLoader.h"
#include "core/Engine.h"
#include "core/Loudness.h"
#include "core/Mapping.h"

namespace
{

constexpr double renderRate = 48000.0;
constexpr float mouseVelocity = 0.9f;    // 연주 화면의 패드를 마우스로 칠 때의 세기

/** 스네어 기준 목표(dB). 2026-11-26에 프리셋의 음량을 맞출 때 쓴 값이다. 표에 없는 패드는 목표가 없다. */
std::optional<float> targetOf (const juce::String& id)
{
    static const std::map<juce::String, float> exact {
        { "snare", 0.0f }, { "kick", 0.7f }, { "snare_rim", 1.0f }, { "sidestick", -8.0f },
        { "hihat_closed", -6.0f }, { "hihat_half", -2.5f }, { "hihat_open", -2.0f }, { "hihat_pedal", -10.0f },
        { "ride", -3.0f }, { "ride_bell", -2.0f }, { "ride_edge", -1.7f },
    };

    if (const auto found = exact.find (id); found != exact.end())
        return found->second;

    if (id.startsWith ("tom"))
        return -1.0f;

    if (id.startsWith ("crash") || id.startsWith ("china"))
        return 3.0f;

    return std::nullopt;
}

/** 패드를 치는 세기: 키 배열에서 그 패드를 한 번 치는 키의 세기 가운데 가장 센 것. 없으면 마우스. */
std::vector<float> playingVelocities (const hvd::Kit& kit, const juce::File& keymapFile, juce::String& note)
{
    std::vector<float> velocities (kit.pads.size(), 0.0f);
    const auto keymap = loadKeymap (keymapFile);

    if (keymap.ok)
    {
        std::vector<std::string> warnings;

        if (const auto mapping = hvd::resolveMapping (keymap.spec, kit, warnings))
            for (const auto& actions : mapping->onPress)
                for (const auto& action : actions)
                    if (action.type == hvd::KeyAction::Type::hit && action.stroke.type == hvd::Stroke::Type::single && action.pad < velocities.size())
                        velocities[action.pad] = std::max (velocities[action.pad], action.velocity);

        note = "keymap " + keymapFile.getFileName();
    }
    else
    {
        note = "keymap not read (" + keymap.error + "), every pad at the mouse velocity";
    }

    for (auto& velocity : velocities)
        if (velocity <= 0.0f)
            velocity = mouseVelocity;

    return velocities;
}

/** 패드 하나를 그 세기로 한 번 쳐서 2초 동안 낸 소리의 크기. 엔진은 패드마다 새로 만든다 (앞 소리가 섞이지 않게). */
float measurePad (const hvd::Kit& kit, int pad, float velocity, bool smooth)
{
    hvd::Engine engine;
    engine.prepare (renderRate);
    engine.setSmoothLayerSteps (smooth);

    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::setKit;
    command.kit = &kit;
    engine.postFromControl (command);

    command = {};
    command.type = hvd::EngineCommand::Type::hit;
    command.pad = static_cast<std::uint16_t> (pad);
    command.velocity = velocity;
    engine.postFromControl (command);

    constexpr int block = 512;
    const auto total = static_cast<int> (2.0 * renderRate);
    std::vector<float> left (static_cast<std::size_t> (total), 0.0f), right (left.size(), 0.0f);

    for (int done = 0; done < total; done += block)
    {
        float* channels[] = { left.data() + done, right.data() + done };
        engine.render (channels, 2, std::min (block, total - done));
    }

    return hvd::momentaryMaxLoudness (left.data(), right.data(), left.size(), renderRate);
}

struct Measured
{
    std::vector<float> velocity, loudness;
};

Measured measureKit (const hvd::Kit& kit, const juce::File& keymapFile, bool smooth, juce::String& note)
{
    Measured result;
    result.velocity = playingVelocities (kit, keymapFile, note);

    for (std::size_t i = 0; i < kit.pads.size(); ++i)
        result.loudness.push_back (kit.pads[i].layers.empty() ? -100.0f : measurePad (kit, static_cast<int> (i), result.velocity[i], smooth));

    return result;
}

juce::String dbText (float db)    { return (db > 0.0f ? "+" : "") + juce::String (db, 1); }

/** 세기 레이어의 경계마다 크기가 얼마나 뛰는지: 잇기 전 → 잇고 나서 (레이어마다 잰 크기와 엔진의 음량 계산으로). */
juce::String layerSteps (const hvd::Pad& pad)
{
    juce::String text;

    for (std::size_t k = 0; k + 1 < pad.layers.size(); ++k)
    {
        const auto& low = pad.layers[k];
        const auto& high = pad.layers[k + 1];

        if (! low.measured || ! high.measured)
            continue;

        const auto edge = low.upTo;
        const auto above = std::min (1.0f, edge + 0.001f);
        const auto levelAt = [&pad] (std::size_t layer, float velocity, bool smooth)
        {
            const auto& l = pad.layers[layer];
            return l.loudness + 20.0f * pad.velocityCurve * std::log10 (velocity / std::max (l.nominal, 0.01f))
                 + (smooth ? hvd::layerStepCorrectionDb (pad, layer, velocity, pad.velocityCurve) : 0.0f);
        };

        text << "  " << juce::String (edge, 2) << ": " << dbText (levelAt (k + 1, above, false) - levelAt (k, edge, false))
             << " -> " << dbText (levelAt (k + 1, above, true) - levelAt (k, edge, true)) << " dB";
    }

    return text;
}

} // namespace

juce::String runLoudnessTool (const LoudnessOptions& options, bool& ok)
{
    ok = false;
    juce::String report;

    auto loaded = loadKit (options.kitFile);

    if (loaded.kit == nullptr)
        return "kit not read: " + loaded.error + "\n";

    const auto& kit = *loaded.kit;
    juce::String note;
    const auto measured = measureKit (kit, options.keymapFile, options.smoothLayers, note);
    report << "kit " << options.kitFile.getParentDirectory().getFileName() << ", " << note
           << (options.smoothLayers ? juce::String() : juce::String (", layer steps not smoothed")) << "\n";

    // 맞출 크기: 다른 킷의 같은 패드, 또는 스네어 기준 목표
    std::map<juce::String, float> wanted;
    const auto snare = kit.findPad ("snare");
    const auto snareLoudness = snare >= 0 ? measured.loudness[static_cast<std::size_t> (snare)] : 0.0f;

    if (options.referenceKitFile != juce::File())
    {
        auto reference = loadKit (options.referenceKitFile);

        if (reference.kit == nullptr)
            return report + "reference kit not read: " + reference.error + "\n";

        juce::String referenceNote;
        const auto referenceMeasured = measureKit (*reference.kit, options.keymapFile, options.smoothLayers, referenceNote);

        for (std::size_t i = 0; i < reference.kit->pads.size(); ++i)
            wanted[juce::String (reference.kit->pads[i].id)] = referenceMeasured.loudness[i];

        report << "match " << options.referenceKitFile.getParentDirectory().getFileName() << "\n";
    }
    else if (options.useTargets && snare >= 0)
    {
        for (const auto& pad : kit.pads)
            if (const auto target = targetOf (juce::String (pad.id)))
                wanted[juce::String (pad.id)] = snareLoudness + *target;

        report << "targets relative to the snare\n";
    }

    report << "\npad              velocity    LUFS  vs snare  target    volume\n";

    KitDocument document;
    const auto canWrite = options.apply && document.load (options.kitFile);
    const auto before = options.kitFile.loadFileAsString();
    auto changed = 0;

    for (std::size_t i = 0; i < kit.pads.size(); ++i)
    {
        const auto& pad = kit.pads[i];
        const auto id = juce::String (pad.id);
        const auto loudness = measured.loudness[i];
        const auto target = targetOf (id);

        report << id.paddedRight (' ', 17) << juce::String (measured.velocity[i], 2).paddedLeft (' ', 8)
               << juce::String (loudness, 1).paddedLeft (' ', 8)
               << (snare >= 0 ? dbText (loudness - snareLoudness) : juce::String ("-")).paddedLeft (' ', 10)
               << (target ? dbText (*target) : juce::String ("-")).paddedLeft (' ', 8)
               << juce::String (pad.volume, 3).paddedLeft (' ', 10);

        // 바꿀 패드이고 맞출 크기가 있으면 음량을 새로 정한다.
        const auto found = wanted.find (id);
        const auto selected = options.pads.isEmpty() || options.pads.contains (id);

        if (found != wanted.end() && selected && loudness > -90.0f && pad.layers.size() > 0)
        {
            const auto volume = juce::jlimit (0.0f, hvd::maxPadVolume, pad.volume * std::pow (10.0f, (found->second - loudness) / 20.0f));
            const auto rounded = std::round (volume * 1000.0f) / 1000.0f;

            if (std::abs (rounded - pad.volume) >= 0.001f)
            {
                report << " -> " << juce::String (rounded, 3) << " (" << dbText (found->second - loudness) << " dB)";

                if (canWrite)
                {
                    document.setPadNumber (id, "volume", rounded);
                    ++changed;
                }
            }
        }

        if (options.showLayers && pad.layers.size() > 1)
            report << "\n    layers" << layerSteps (pad);

        report << "\n";
    }

    if (options.apply)
    {
        if (! canWrite)
            return report + "\nkit.json not read, nothing written\n";

        if (changed > 0)
        {
            // 바꾸기 전 내용을 남긴다 (킷 화면의 되돌리기에 나온다).
            FileBackups backups;
            backups.setFolders (options.backupsRoot, options.dataRoot);
            backups.keep (options.kitFile, before, true);

            if (! document.save())
                return report + "\nkit.json not written\n";
        }

        report << "\nwritten: " << changed << " pads\n";
    }

    ok = true;
    return report;
}
