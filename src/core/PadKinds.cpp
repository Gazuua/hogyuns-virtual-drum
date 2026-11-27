#include "PadKinds.h"

#include <initializer_list>

namespace hvd
{

namespace
{

char lowered (char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char> (c - 'A' + 'a') : c;
}

bool startsWithAny (std::string_view id, std::initializer_list<std::string_view> prefixes) noexcept
{
    for (const auto prefix : prefixes)
        if (startsWithIgnoringCase (id, prefix))
            return true;

    return false;
}

} // namespace

bool startsWithIgnoringCase (std::string_view text, std::string_view prefix) noexcept
{
    if (text.size() < prefix.size())
        return false;

    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (lowered (text[i]) != prefix[i])
            return false;

    return true;
}

bool containsIgnoringCase (std::string_view text, std::string_view part) noexcept
{
    for (std::size_t start = 0; start + part.size() <= text.size(); ++start)
        if (startsWithIgnoringCase (text.substr (start), part))
            return true;

    return false;
}

Instrument instrumentOfPad (std::string_view id) noexcept
{
    if (startsWithAny (id, { "kick", "bass", "bd" }))                         return Instrument::kick;
    if (startsWithAny (id, { "snare", "sidestick", "rim", "clap", "sd" }))    return Instrument::snare;
    if (startsWithAny (id, { "hihat", "hat", "hh" }))                         return Instrument::hihat;
    if (startsWithAny (id, { "ride" }))                                       return Instrument::ride;
    if (startsWithAny (id, { "crash", "splash", "cymbal" }))                  return Instrument::crash;
    if (startsWithAny (id, { "china" }))                                      return Instrument::china;
    if (startsWithAny (id, { "tom" }))                                        return Instrument::tom;

    return Instrument::other;
}

bool isCymbal (Instrument instrument) noexcept
{
    return instrument == Instrument::hihat || instrument == Instrument::ride
        || instrument == Instrument::crash || instrument == Instrument::china;
}

std::string padFamily (std::string_view padId)
{
    std::string id (padId);

    for (auto& c : id)
        c = lowered (c);

    if (id == "sidestick")
        return "snare";

    for (const std::string_view suffix : { "_bell", "_edge", "_bow", "_rim", "_closed", "_half", "_open", "_pedal", "_splash", "_buzz" })
        if (id.size() >= suffix.size() && id.compare (id.size() - suffix.size(), suffix.size(), suffix) == 0)
            return id.substr (0, id.size() - suffix.size());

    return id;
}

float hihatOpenness (std::string_view padId) noexcept
{
    if (instrumentOfPad (padId) != Instrument::hihat)
        return 0.0f;

    if (containsIgnoringCase (padId, "half"))
        return 0.45f;

    return containsIgnoringCase (padId, "open") ? 1.0f : 0.0f;
}

std::vector<std::vector<std::uint16_t>> chokeSetsOf (const Kit& kit)
{
    std::vector<std::string> families;
    families.reserve (kit.pads.size());

    for (const auto& pad : kit.pads)
        families.push_back (padFamily (pad.id));

    std::vector<std::vector<std::uint16_t>> sets;
    sets.reserve (kit.pads.size());

    for (std::size_t i = 0; i < kit.pads.size(); ++i)
    {
        const auto group = kit.pads[i].group;
        std::vector<std::uint16_t> set;

        for (std::size_t j = 0; j < kit.pads.size(); ++j)
            if (families[j] == families[i] || (group >= 0 && kit.pads[j].group == group))
                set.push_back (static_cast<std::uint16_t> (j));

        sets.push_back (std::move (set));
    }

    return sets;
}

} // namespace hvd
