#include "Mapping.h"

#include <algorithm>
#include <cstdlib>

namespace hvd
{

namespace
{

/** 세기 배율: 절대 세기가 적혀 있으면 기준 세기에 대한 비율로 바꾸고, 없으면 기본 배율을 쓴다. */
float scaleOr (float absolute, float base, float fallbackScale) noexcept
{
    if (absolute < 0.0f)
        return fallbackScale;

    return absolute / std::max (base, 0.01f);
}

Interval millisecondsInterval (float milliseconds) noexcept
{
    Interval interval;
    interval.kind = Interval::Kind::milliseconds;
    interval.value = milliseconds;
    return interval;
}

Interval beatsInterval (float beats) noexcept
{
    Interval interval;
    interval.kind = Interval::Kind::beats;
    interval.value = beats;
    return interval;
}

/** 더블, 플램의 간격: 박 표기가 있으면 그것을, 없으면 밀리초를, 그것도 없으면 기본값을 쓴다. */
Interval gapInterval (const BindingSpec& binding, float defaultMs, std::vector<std::string>& warnings)
{
    if (! binding.gap.empty())
    {
        if (const auto beats = parseBeats (binding.gap); beats > 0.0f)
            return beatsInterval (beats);

        warnings.push_back ("cannot read gap '" + binding.gap + "' on key " + binding.key);
    }

    return millisecondsInterval (binding.gapMs > 0.0f ? binding.gapMs : defaultMs);
}

/** 반복 간격: "follow", 박 표기, 밀리초 순서로 본다. */
Interval repeatInterval (const BindingSpec& binding, const Interval& fallback, std::vector<std::string>& warnings)
{
    if (binding.rate == "follow")
    {
        Interval interval;
        interval.kind = Interval::Kind::follow;
        interval.followDivisor = std::max (binding.followDivisor, 0.25f);

        // 따라갈 간격이 없을 때를 위해 밀리초나 기본값은 쓰지 않는다. 그때는 한 번만 친다.
        return interval;
    }

    if (! binding.rate.empty())
    {
        if (const auto beats = parseBeats (binding.rate); beats > 0.0f)
            return beatsInterval (beats);

        warnings.push_back ("cannot read rate '" + binding.rate + "' on key " + binding.key);
    }

    if (binding.intervalMs > 0.0f)
        return millisecondsInterval (binding.intervalMs);

    return fallback;
}

bool buildStroke (const BindingSpec& binding, Stroke& stroke, std::vector<std::string>& warnings)
{
    const auto base = binding.velocity;
    const auto& name = binding.stroke;

    if (name == "single" || name.empty())
    {
        stroke.type = Stroke::Type::single;
    }
    else if (name == "downUp")
    {
        stroke.type = Stroke::Type::downUp;
        stroke.releaseScale = scaleOr (binding.releaseVelocity, base, 0.8f);
    }
    else if (name == "double")
    {
        stroke.type = Stroke::Type::multi;
        stroke.numExtra = 1;
        stroke.extra[0].delay = gapInterval (binding, 85.0f, warnings);
        stroke.extra[0].velocityScale = scaleOr (binding.secondVelocity, base, 0.8f);
    }
    else if (name == "flam")
    {
        // 약한 꾸밈음을 먼저 내고 본음을 조금 뒤에 낸다.
        stroke.type = Stroke::Type::multi;
        stroke.firstScale = scaleOr (binding.graceVelocity, base, 0.35f);
        stroke.numExtra = 1;
        stroke.extra[0].delay = gapInterval (binding, 28.0f, warnings);
        stroke.extra[0].velocityScale = 1.0f;
    }
    else if (name == "drag")
    {
        // 꾸밈음 두 개 뒤에 본음.
        const auto total = binding.gapMs > 0.0f ? binding.gapMs : 55.0f;

        stroke.type = Stroke::Type::multi;
        stroke.firstScale = scaleOr (binding.graceVelocity, base, 0.3f);
        stroke.numExtra = 2;
        stroke.extra[0].delay = millisecondsInterval (total * 0.45f);
        stroke.extra[0].velocityScale = stroke.firstScale * 1.15f;
        stroke.extra[1].delay = millisecondsInterval (total);
        stroke.extra[1].velocityScale = 1.0f;
    }
    else if (name == "repeat")
    {
        stroke.type = Stroke::Type::repeat;
        stroke.interval = repeatInterval (binding, beatsInterval (0.25f), warnings);
        stroke.accentEvery = static_cast<std::uint8_t> (std::clamp (binding.accentEvery, 0, 64));
        stroke.softScale = scaleOr (binding.softVelocity, base, 0.8f);
        stroke.rampPerHit = binding.ramp > 0.0f ? binding.ramp : 1.0f;
        stroke.swellMs = binding.swellMs > 0.0f ? binding.swellMs : 0.0f;
        stroke.swellStart = binding.swellStart >= 0.0f ? std::clamp (binding.swellStart, 0.01f, 1.0f) : stroke.swellStart;
        stroke.timingJitter = binding.jitter >= 0.0f ? binding.jitter : 0.012f;
        stroke.velocityJitter = binding.velocityJitter >= 0.0f ? binding.velocityJitter : 0.06f;
    }
    else if (name == "buzz")
    {
        stroke.type = Stroke::Type::buzz;
        stroke.interval = repeatInterval (binding, millisecondsInterval (85.0f), warnings);
        stroke.swellMs = binding.swellMs > 0.0f ? binding.swellMs : 0.0f;
        stroke.swellStart = binding.swellStart >= 0.0f ? std::clamp (binding.swellStart, 0.01f, 1.0f) : stroke.swellStart;
        stroke.timingJitter = binding.jitter >= 0.0f ? binding.jitter : 0.10f;
        stroke.velocityJitter = binding.velocityJitter >= 0.0f ? binding.velocityJitter : 0.12f;
    }
    else if (name == "pattern")
    {
        stroke.type = Stroke::Type::pattern;
        stroke.stepBeats = 1.0f / std::clamp (binding.division, 0.25f, 16.0f);

        for (const auto symbol : binding.steps)
        {
            std::uint8_t level = 0;

            switch (symbol)
            {
                case 'X':            level = 255; break;
                case 'x':            level = 190; break;
                case 'o':            level = 115; break;
                case '.': case '-':  level = 0;   break;
                default:             continue;    // 빈칸과 마디선(|)은 건너뛴다
            }

            if (stroke.numSteps < stroke.steps.size())
                stroke.steps[stroke.numSteps++] = level;
        }

        if (stroke.numSteps == 0)
        {
            warnings.push_back ("pattern on key " + binding.key + " has no steps");
            return false;
        }
    }
    else
    {
        warnings.push_back ("unknown stroke '" + name + "' on key " + binding.key);
        return false;
    }

    return true;
}

} // namespace

float parseBeats (const std::string& text) noexcept
{
    const auto slash = text.find ('/');

    if (slash == std::string::npos || slash == 0 || slash + 1 >= text.size())
        return 0.0f;

    char* end = nullptr;
    const auto numerator = std::strtof (text.c_str(), &end);

    if (end != text.c_str() + slash)
        return 0.0f;

    const auto* denominatorText = text.c_str() + slash + 1;
    const auto denominator = std::strtof (denominatorText, &end);

    if (end == denominatorText || numerator <= 0.0f || denominator <= 0.0f)
        return 0.0f;

    auto beats = 4.0f * numerator / denominator;    // 온음표가 4박

    if (*end == 't')
        beats *= 2.0f / 3.0f;                       // 셋잇단
    else if (*end == 'd')
        beats *= 1.5f;                              // 점음표
    else if (*end != '\0')
        return 0.0f;

    return beats;
}

std::shared_ptr<const Mapping> resolveMapping (const MappingSpec& spec,
                                               const Kit& kit,
                                               std::vector<std::string>& warnings)
{
    auto mapping = std::make_shared<Mapping>();
    mapping->name = spec.name;
    mapping->tempo = std::clamp (spec.tempo, 30.0f, 300.0f);
    mapping->beatsPerBar = std::clamp (spec.beatsPerBar, 1, 16);
    mapping->accentScale = std::clamp (spec.accentScale, 1.0f, 4.0f);
    mapping->ghostScale = std::clamp (spec.ghostScale, 0.05f, 1.0f);
    mapping->chordAccentMs = std::clamp (spec.chordAccentMs, 0.0f, 200.0f);
    mapping->keysForPad.resize (kit.pads.size());

    const auto findPad = [&] (const std::string& padId, const std::string& keyName) -> int
    {
        const auto index = kit.findPad (padId);

        if (index < 0)
            warnings.push_back ("unknown pad '" + padId + "' on key " + keyName);

        return index;
    };

    for (const auto& binding : spec.bindings)
    {
        const auto key = keyFromName (binding.key);

        if (! key.has_value())
        {
            warnings.push_back ("unknown key: " + binding.key);
            continue;
        }

        auto& actions = mapping->onPress[*key];
        const auto velocity = std::clamp (binding.velocity, 0.0f, 1.0f);

        if (binding.action == "hit")
        {
            Stroke stroke;

            if (! buildStroke (binding, stroke, warnings))
                continue;

            if (binding.pads.empty())
                warnings.push_back ("key " + binding.key + " has no pads");

            for (const auto& padId : binding.pads)
            {
                const auto padIndex = findPad (padId, binding.key);

                if (padIndex < 0)
                    continue;

                KeyAction action;
                action.type = KeyAction::Type::hit;
                action.pad = static_cast<std::uint16_t> (padIndex);
                action.velocity = velocity;
                action.stroke = stroke;
                actions.push_back (action);

                auto& keys = mapping->keysForPad[static_cast<std::size_t> (padIndex)];

                if (std::find (keys.begin(), keys.end(), *key) == keys.end())
                    keys.push_back (*key);
            }
        }
        else if (binding.action == "choke")
        {
            if (binding.pads.empty())
                warnings.push_back ("key " + binding.key + " has no pads");

            for (const auto& padId : binding.pads)
            {
                if (const auto padIndex = findPad (padId, binding.key); padIndex >= 0)
                {
                    KeyAction action;
                    action.type = KeyAction::Type::choke;
                    action.pad = static_cast<std::uint16_t> (padIndex);
                    actions.push_back (action);
                }
            }
        }
        else if (binding.action == "accent" || binding.action == "ghost"
                 || binding.action == "tapTempo" || binding.action == "metronome")
        {
            KeyAction action;
            action.type = binding.action == "accent"   ? KeyAction::Type::accent
                        : binding.action == "ghost"    ? KeyAction::Type::ghost
                        : binding.action == "tapTempo" ? KeyAction::Type::tapTempo
                                                       : KeyAction::Type::metronome;
            actions.push_back (action);
        }
        else if (binding.action == "pedal")
        {
            Pedal pedal;
            pedal.key = *key;
            pedal.pressVelocity = velocity;
            pedal.releaseVelocity = binding.releaseVelocity >= 0.0f ? std::clamp (binding.releaseVelocity, 0.0f, 1.0f)
                                                                    : velocity;

            if (! binding.pads.empty())
            {
                pedal.pressPad = findPad (binding.pads.front(), binding.key);

                // 누를 때 치는 패드는 화면에 이 키를 적는다.
                if (pedal.pressPad >= 0)
                {
                    auto& keys = mapping->keysForPad[static_cast<std::size_t> (pedal.pressPad)];

                    if (std::find (keys.begin(), keys.end(), *key) == keys.end())
                        keys.push_back (*key);
                }
            }

            if (! binding.releasePad.empty())
                pedal.releasePad = findPad (binding.releasePad, binding.key);

            if (! binding.splash.empty())
            {
                pedal.splashPad = findPad (binding.splash, binding.key);

                if (pedal.splashPad >= 0)
                {
                    auto& keys = mapping->keysForPad[static_cast<std::size_t> (pedal.splashPad)];

                    if (std::find (keys.begin(), keys.end(), *key) == keys.end())
                        keys.push_back (*key);
                }
            }

            if (binding.splashMs >= 0.0f)
                pedal.splashMs = std::clamp (binding.splashMs, 20.0f, 1000.0f);

            const auto resolveSwaps = [&] (const auto& pairs, auto& resolved)
            {
                for (const auto& [from, to] : pairs)
                {
                    const auto fromIndex = findPad (from, binding.key);
                    const auto toIndex = findPad (to, binding.key);

                    if (fromIndex >= 0 && toIndex >= 0)
                        resolved.emplace_back (static_cast<std::uint16_t> (fromIndex), static_cast<std::uint16_t> (toIndex));
                }
            };

            resolveSwaps (binding.swap, pedal.swaps);
            resolveSwaps (binding.releasedSwap, pedal.releasedSwaps);

            KeyAction action;
            action.type = KeyAction::Type::pedal;
            action.pedal = static_cast<std::uint16_t> (mapping->pedals.size());
            mapping->pedals.push_back (std::move (pedal));
            actions.push_back (action);
        }
        else
        {
            warnings.push_back ("unknown action '" + binding.action + "' on key " + binding.key);
        }
    }

    return mapping;
}

} // namespace hvd
