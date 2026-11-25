#include "KeymapLoader.h"

namespace
{

bool isNumber (const juce::var& value)
{
    return value.isDouble() || value.isInt() || value.isInt64();
}

float numberOr (const juce::var& object, const char* key, float fallback)
{
    const auto& value = object[key];
    return isNumber (value) ? static_cast<float> (static_cast<double> (value)) : fallback;
}

std::string textOf (const juce::var& object, const char* key)
{
    return object[key].toString().toStdString();
}

} // namespace

KeymapLoadResult loadKeymap (const juce::File& keymapFile)
{
    KeymapLoadResult result;

    if (! keymapFile.existsAsFile())
    {
        result.error = "keymap file not found: " + keymapFile.getFullPathName();
        return result;
    }

    juce::var root;
    const auto parsed = juce::JSON::parse (keymapFile.loadFileAsString(), root);

    if (parsed.failed())
    {
        result.error = keymapFile.getFileName() + ": " + parsed.getErrorMessage();
        return result;
    }

    const auto* bindings = root["bindings"].getArray();

    if (bindings == nullptr)
    {
        result.error = keymapFile.getFileName() + " has no \"bindings\" list";
        return result;
    }

    auto& spec = result.spec;
    spec.name = textOf (root, "name");
    spec.tempo = numberOr (root, "tempo", spec.tempo);
    spec.beatsPerBar = static_cast<int> (numberOr (root, "beatsPerBar", static_cast<float> (spec.beatsPerBar)));
    spec.accentScale = numberOr (root, "accentScale", spec.accentScale);
    spec.ghostScale = numberOr (root, "ghostScale", spec.ghostScale);
    spec.chordAccentMs = numberOr (root, "chordAccentMs", spec.chordAccentMs);

    for (const auto& bindingVar : *bindings)
    {
        hvd::BindingSpec binding;
        binding.key = textOf (bindingVar, "key");

        if (binding.key.empty())
        {
            result.warnings.add ("a binding without \"key\" was skipped");
            continue;
        }

        if (const auto action = textOf (bindingVar, "action"); ! action.empty())
            binding.action = action;

        // 패드 하나는 "pad", 여러 개는 "pads"로 적는다. 둘 다 있으면 합친다.
        if (const auto pad = textOf (bindingVar, "pad"); ! pad.empty())
            binding.pads.push_back (pad);

        if (const auto* pads = bindingVar["pads"].getArray())
            for (const auto& pad : *pads)
                if (const auto id = pad.toString(); id.isNotEmpty())
                    binding.pads.push_back (id.toStdString());

        binding.velocity = numberOr (bindingVar, "velocity", binding.velocity);

        if (const auto stroke = textOf (bindingVar, "stroke"); ! stroke.empty())
            binding.stroke = stroke;

        binding.rate = textOf (bindingVar, "rate");
        binding.intervalMs = numberOr (bindingVar, "intervalMs", binding.intervalMs);
        binding.gap = textOf (bindingVar, "gap");
        binding.gapMs = numberOr (bindingVar, "gapMs", binding.gapMs);
        binding.secondVelocity = numberOr (bindingVar, "secondVelocity", binding.secondVelocity);
        binding.releaseVelocity = numberOr (bindingVar, "releaseVelocity", binding.releaseVelocity);
        binding.graceVelocity = numberOr (bindingVar, "graceVelocity", binding.graceVelocity);
        binding.softVelocity = numberOr (bindingVar, "softVelocity", binding.softVelocity);
        binding.ramp = numberOr (bindingVar, "ramp", binding.ramp);
        binding.swellMs = numberOr (bindingVar, "swellMs", binding.swellMs);
        binding.swellStart = numberOr (bindingVar, "swellStart", binding.swellStart);
        binding.jitter = numberOr (bindingVar, "jitter", binding.jitter);
        binding.velocityJitter = numberOr (bindingVar, "velocityJitter", binding.velocityJitter);
        binding.accentEvery = static_cast<int> (numberOr (bindingVar, "accentEvery", static_cast<float> (binding.accentEvery)));
        binding.followDivisor = numberOr (bindingVar, "followDivisor", binding.followDivisor);
        binding.steps = textOf (bindingVar, "steps");
        binding.division = numberOr (bindingVar, "division", binding.division);
        binding.releasePad = textOf (bindingVar, "releasePad");

        // "swap": { "원래 패드": "바꿔 칠 패드", ... }
        if (const auto* swaps = bindingVar["swap"].getDynamicObject())
            for (const auto& entry : swaps->getProperties())
                binding.swap.emplace_back (entry.name.toString().toStdString(), entry.value.toString().toStdString());

        spec.bindings.push_back (std::move (binding));
    }

    result.ok = true;
    return result;
}
