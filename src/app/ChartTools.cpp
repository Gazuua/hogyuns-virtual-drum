#include "ChartTools.h"

#include <cmath>

#include "KitLoader.h"
#include "core/DrumTranscriber.h"
#include "core/Engine.h"

//==============================================================================
namespace
{

juce::String describeChart (const hvd::Chart& chart)
{
    juce::String text;
    text << "duration " << juce::String (chart.duration, 1) << " s, tempo " << juce::String (chart.tempo, 1)
         << ", beats " << static_cast<int> (chart.beats.size()) << ", notes " << static_cast<int> (chart.notes.size()) << "\n";

    const char* const levelNames[] = { "easy", "normal", "hard", "full" };

    for (int level = 0; level < hvd::numChartLevels; ++level)
    {
        const auto notes = hvd::makeChartVariant (chart, static_cast<hvd::ChartLevel> (level));
        text << juce::String (levelNames[level]).paddedRight (' ', 7) << " " << juce::String (static_cast<int> (notes.size())).paddedLeft (' ', 5)
             << " notes, stars " << juce::String (hvd::rateChartDifficulty (notes), 1) << "\n";
    }

    return text;
}

struct PlannedHit
{
    double seconds;
    const char* pad;
    float velocity;
    hvd::Lane lane;
};

/** 16마디짜리 록 패턴. 앞 여덟 마디는 하이햇, 뒤 여덟 마디는 라이드이고 필인이 두 번 들어 있다. */
std::vector<PlannedHit> makeRockPattern (double tempo)
{
    using hvd::Lane;

    std::vector<PlannedHit> hits;
    const auto beat = 60.0 / tempo;
    const auto at = [beat] (int bar, double beatInBar) { return 1.0 + (bar * 4.0 + beatInBar) * beat; };

    for (int bar = 0; bar < 16; ++bar)
    {
        const auto ride = bar >= 8;
        const auto fill = bar == 7 || bar == 15;

        for (int eighth = 0; eighth < 8; ++eighth)
        {
            const auto beatInBar = eighth * 0.5;

            if (fill && eighth >= 4)
                break;

            const auto time = at (bar, beatInBar);
            const auto crash = (bar == 0 || bar == 8) && eighth == 0;

            if (crash)
                hits.push_back ({ time, bar == 0 ? "crash1" : "crash2", 0.95f, Lane::crash });
            else if (ride)
                hits.push_back ({ time, "ride", eighth % 2 == 0 ? 0.8f : 0.6f, Lane::ride });
            else if (eighth == 7 && bar == 3)
                hits.push_back ({ time, "hihat_open", 0.8f, Lane::hihat });
            else
                hits.push_back ({ time, "hihat_closed", eighth % 2 == 0 ? 0.85f : 0.6f, Lane::hihat });

            if (eighth == 0 || eighth == 4 || (eighth == 3 && bar % 2 == 1))
                hits.push_back ({ time, "kick", 0.9f, Lane::kick });

            if (eighth == 2 || eighth == 6)
                hits.push_back ({ time, "snare", 0.9f, Lane::snare });
        }

        if (bar == 7)
        {
            const char* const pads[] = { "tom_high", "tom_high", "tom_high", "tom_high", "tom_mid", "tom_mid", "tom_floor", "tom_floor" };
            const Lane lanes[] = { Lane::tomHigh, Lane::tomHigh, Lane::tomHigh, Lane::tomHigh, Lane::tomMid, Lane::tomMid, Lane::tomFloor, Lane::tomFloor };

            for (int i = 0; i < 8; ++i)
                hits.push_back ({ at (bar, 2.0 + 0.25 * i), pads[i], 0.9f, lanes[i] });
        }
        else if (bar == 15)
        {
            const char* const pads[] = { "snare", "snare", "snare", "snare", "tom_mid", "tom_mid", "tom_floor", "tom_floor" };
            const Lane lanes[] = { Lane::snare, Lane::snare, Lane::snare, Lane::snare, Lane::tomMid, Lane::tomMid, Lane::tomFloor, Lane::tomFloor };

            for (int i = 0; i < 8; ++i)
                hits.push_back ({ at (bar, 2.0 + 0.25 * i), pads[i], 0.9f, lanes[i] });
        }
    }

    std::stable_sort (hits.begin(), hits.end(), [] (const PlannedHit& a, const PlannedHit& b) { return a.seconds < b.seconds; });
    return hits;
}

struct StereoAudio
{
    std::vector<float> left, right;
};

/** 킷으로 hits를 연주한 소리를 만든다. */
StereoAudio renderHits (const hvd::Kit& kit, const std::vector<PlannedHit>& hits, double sampleRate)
{
    hvd::Engine engine;
    engine.prepare (sampleRate);

    hvd::EngineCommand setKit;
    setKit.type = hvd::EngineCommand::Type::setKit;
    setKit.kit = &kit;
    engine.postFromControl (setKit);

    const auto total = static_cast<std::size_t> ((hits.empty() ? 1.0 : hits.back().seconds + 3.0) * sampleRate);
    StereoAudio audio;
    audio.left.assign (total, 0.0f);
    audio.right.assign (total, 0.0f);

    constexpr int block = 512;
    std::vector<float> left (block), right (block);
    std::size_t next = 0;

    for (std::size_t position = 0; position < total; position += block)
    {
        const auto count = static_cast<int> (std::min<std::size_t> (block, total - position));

        // 이 블록 안에서 울릴 타격을 정확한 시각으로 넘긴다.
        while (next < hits.size() && hits[next].seconds * sampleRate < static_cast<double> (position + block))
        {
            if (const auto pad = kit.findPad (hits[next].pad); pad >= 0)
            {
                hvd::EngineCommand command;
                command.type = hvd::EngineCommand::Type::hitAt;
                command.pad = static_cast<std::uint16_t> (pad);
                command.velocity = hits[next].velocity;
                command.time = static_cast<std::uint64_t> (std::llround (hits[next].seconds * sampleRate));
                engine.postFromControl (command);
            }

            ++next;
        }

        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        float* channels[] = { left.data(), right.data() };
        engine.render (channels, 2, count);

        std::copy_n (left.begin(), count, audio.left.begin() + static_cast<std::ptrdiff_t> (position));
        std::copy_n (right.begin(), count, audio.right.begin() + static_cast<std::ptrdiff_t> (position));
    }

    engine.detachKit();
    return audio;
}

struct LaneScore
{
    int wanted = 0, found = 0, matched = 0;
    double errorSum = 0.0, signedSum = 0.0;
};

/** 받아 적은 채보를 정답(hits)과 견줘 레인별로 채점하고 report에 적는다. 기준을 넘으면 true. */
bool scoreChart (const hvd::Chart& chart, const std::vector<PlannedHit>& hits, double tempo, juce::String& report)
{
    std::array<LaneScore, hvd::numLanes> scores {};
    std::vector<bool> used (chart.notes.size(), false);

    for (const auto& note : chart.notes)
        ++scores[static_cast<std::size_t> (note.lane)].found;

    for (const auto& hit : hits)
    {
        auto& score = scores[static_cast<std::size_t> (hit.lane)];
        ++score.wanted;

        auto best = chart.notes.size();

        for (std::size_t i = 0; i < chart.notes.size(); ++i)
        {
            const auto distance = std::abs (chart.notes[i].seconds - hit.seconds);

            if (! used[i] && chart.notes[i].lane == hit.lane && distance <= 0.035
                && (best == chart.notes.size() || distance < std::abs (chart.notes[best].seconds - hit.seconds)))
                best = i;
        }

        if (best != chart.notes.size())
        {
            used[best] = true;
            ++score.matched;
            score.errorSum += std::abs (chart.notes[best].seconds - hit.seconds);
            score.signedSum += chart.notes[best].seconds - hit.seconds;
        }
    }

    auto wantedTotal = 0, matchedTotal = 0, foundTotal = 0;

    for (int lane = 0; lane < hvd::numLanes; ++lane)
    {
        const auto& score = scores[static_cast<std::size_t> (lane)];
        wantedTotal += score.wanted;
        matchedTotal += score.matched;
        foundTotal += score.found;

        report << juce::String (hvd::laneName (static_cast<hvd::Lane> (lane))).paddedRight (' ', 10)
               << " wanted " << juce::String (score.wanted).paddedLeft (' ', 3)
               << "  found " << juce::String (score.found).paddedLeft (' ', 3)
               << "  matched " << juce::String (score.matched).paddedLeft (' ', 3)
               << "  error " << juce::String (score.matched > 0 ? score.errorSum / score.matched * 1000.0 : 0.0, 1) << " ms"
               << " (signed " << juce::String (score.matched > 0 ? score.signedSum / score.matched * 1000.0 : 0.0, 1) << ")\n";
    }

    const auto recall = static_cast<double> (matchedTotal) / std::max (1, wantedTotal);
    const auto precision = foundTotal > 0 ? static_cast<double> (matchedTotal) / foundTotal : 0.0;
    const auto tempoOk = std::abs (chart.tempo - tempo) < tempo * 0.02;
    const auto passed = recall >= 0.85 && precision >= 0.85 && tempoOk;

    report << "recall " << juce::String (recall * 100.0, 1) << "%, precision " << juce::String (precision * 100.0, 1) << "%"
           << ", tempo " << (tempoOk ? "ok" : "WRONG") << (passed ? "" : "   FAILED") << "\n";

    // 하이햇과 라이드, 탐 셋을 각각 한 무리로 보고 다시 센다 (제때 그 종류의 악기를 찾았는지).
    const auto family = [] (hvd::Lane lane)
    {
        switch (lane)
        {
            case hvd::Lane::hihat:
            case hvd::Lane::ride:        return 1;
            case hvd::Lane::tomHigh:
            case hvd::Lane::tomMid:
            case hvd::Lane::tomFloor:    return 2;
            case hvd::Lane::crash:       return 3;
            case hvd::Lane::snare:       return 4;
            case hvd::Lane::kick:        return 5;
        }

        return 0;
    };

    std::fill (used.begin(), used.end(), false);
    auto familyMatched = 0;

    for (const auto& hit : hits)
    {
        auto best = chart.notes.size();

        for (std::size_t i = 0; i < chart.notes.size(); ++i)
        {
            const auto distance = std::abs (chart.notes[i].seconds - hit.seconds);

            if (! used[i] && family (chart.notes[i].lane) == family (hit.lane) && distance <= 0.035
                && (best == chart.notes.size() || distance < std::abs (chart.notes[best].seconds - hit.seconds)))
                best = i;
        }

        if (best != chart.notes.size())
        {
            used[best] = true;
            ++familyMatched;
        }
    }

    report << "counting hi-hat with ride and the three toms together: recall "
           << juce::String (100.0 * familyMatched / std::max (1, wantedTotal), 1) << "%, precision "
           << juce::String (foundTotal > 0 ? 100.0 * familyMatched / foundTotal : 0.0, 1) << "%\n";

    return passed;
}

} // namespace

juce::String chartToTab (const hvd::Chart& chart)
{
    // 줄: 크래시, 라이드, 하이햇, 스네어, 탐 셋, 킥. 칸: 16분음표. 격자 사이의 노트는 '~'로 적는다.
    constexpr hvd::Lane rows[] = { hvd::Lane::crash, hvd::Lane::ride, hvd::Lane::hihat, hvd::Lane::snare,
                                   hvd::Lane::tomHigh, hvd::Lane::tomMid, hvd::Lane::tomFloor, hvd::Lane::kick };
    const char* const names[] = { "CR", "RD", "HH", "SN", "T1", "T2", "T3", "KK" };

    if (chart.beats.size() < 8)
        return "(no beat grid)\n";

    const auto numBars = (static_cast<int> (chart.beats.size()) - chart.firstDownbeat + 3) / 4;
    std::vector<std::array<std::string, 8>> bars (static_cast<std::size_t> (std::max (1, numBars)));

    for (auto& bar : bars)
        for (auto& row : bar)
            row.assign (16, '.');

    for (const auto& note : chart.notes)
    {
        const auto position = note.beat - chart.firstDownbeat;

        if (note.beat < 0.0 || position < 0.0)
            continue;

        const auto bar = static_cast<int> (position / 4.0 + 1.0e-6);
        const auto slot = (position - bar * 4.0) * 4.0;
        const auto nearest = static_cast<int> (std::lround (slot));

        if (bar >= numBars || nearest > 15)
            continue;

        for (std::size_t row = 0; row < 8; ++row)
        {
            if (rows[row] != note.lane)
                continue;

            auto mark = note.velocity >= 0.45f ? 'x' : 'o';

            if (std::abs (slot - nearest) > 0.02)
                mark = '~';
            else if ((note.flags & hvd::ChartNote::inferred) != 0)
                mark = '+';
            else if ((note.flags & hvd::ChartNote::openHat) != 0)
                mark = 'O';

            bars[static_cast<std::size_t> (bar)][row][static_cast<std::size_t> (nearest)] = mark;
        }
    }

    // 네 마디씩 옆으로 늘어놓는다. 아무도 치지 않는 줄은 적지 않는다.
    juce::String text;

    for (int first = 0; first < numBars; first += 4)
    {
        const auto barSeconds = chart.beats[static_cast<std::size_t> (std::min (static_cast<int> (chart.beats.size()) - 1, chart.firstDownbeat + first * 4))];
        text << "bar " << (first + 1) << "  (" << juce::String (barSeconds, 1) << " s)\n";

        for (std::size_t row = 0; row < 8; ++row)
        {
            juce::String line;
            auto used = false;

            for (int bar = first; bar < std::min (numBars, first + 4); ++bar)
            {
                const auto& cells = bars[static_cast<std::size_t> (bar)][row];
                used = used || cells.find_first_not_of ('.') != std::string::npos;
                line << "|" << cells.substr (0, 4) << " " << cells.substr (4, 4) << " " << cells.substr (8, 4) << " " << cells.substr (12, 4);
            }

            if (used)
                text << names[row] << " " << line << "|\n";
        }
    }

    return text;
}

juce::String transcribeToReport (const juce::File& audio, bool isolatedDrums, bool logEvents, const juce::File& chartFile)
{
    juce::String report;
    report << "file: " << audio.getFullPathName() << "\nmode: " << (isolatedDrums ? "isolated drums" : "full mix") << "\n";

    std::vector<float> left, right;
    auto sampleRate = 0.0;

    if (const auto error = decodeStereo (audio, left, right, sampleRate); error.isNotEmpty())
        return report + "FAILED: " + error + "\n";

    hvd::TranscribeOptions options;
    options.isolatedDrums = isolatedDrums;
    options.logEvents = logEvents;

    std::string log;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto chart = hvd::transcribeDrums (left.data(), right.data(), left.size(), sampleRate, options, nullptr, nullptr, &log);
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - started;

    report << "sample rate " << sampleRate << ", analysed in " << juce::String (elapsed / 1000.0, 2) << " s\n"
           << describeChart (chart) << juce::String (log) << "\n" << chartToTab (chart);

    if (chartFile != juce::File())
    {
        auto* header = new juce::DynamicObject();
        header->setProperty ("version", 1);
        header->setProperty ("file", audio.getFullPathName());
        header->setProperty ("method", isolatedDrums ? "stem" : "mix");
        writeChartFile (chartFile, chart, juce::var (header));
    }

    return report;
}

bool writeTestSong (const juce::File& dataRoot, const juce::File& target, double tempo)
{
    const auto loaded = loadKit (dataRoot.getChildFile ("kits").getChildFile ("default").getChildFile ("kit.json"));

    if (loaded.kit == nullptr)
        return false;

    constexpr double sampleRate = 44100.0;
    const auto audio = renderHits (*loaded.kit, makeRockPattern (tempo), sampleRate);

    juce::AudioBuffer<float> buffer (2, static_cast<int> (audio.left.size()));
    buffer.copyFrom (0, 0, audio.left.data(), static_cast<int> (audio.left.size()));
    buffer.copyFrom (1, 0, audio.right.data(), static_cast<int> (audio.right.size()));

    target.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = target.createOutputStream();

    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    const auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                     .withNumChannels (2)
                                                                                     .withBitsPerSample (16));

    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

bool scoreStemAgainstPattern (const juce::File& stem, double tempo, juce::String& report)
{
    report << "file: " << stem.getFullPathName() << "\npattern tempo " << tempo << "\n";

    std::vector<float> left, right;
    auto sampleRate = 0.0;

    if (const auto error = decodeStereo (stem, left, right, sampleRate); error.isNotEmpty())
    {
        report << "FAILED: " << error << "\n";
        return false;
    }

    hvd::TranscribeOptions options;
    options.isolatedDrums = true;

    std::string log;
    const auto chart = hvd::transcribeDrums (left.data(), right.data(), left.size(), sampleRate, options, nullptr, nullptr, &log);
    const auto passed = scoreChart (chart, makeRockPattern (tempo), tempo, report);

    report << describeChart (chart) << juce::String (log) << chartToTab (chart);
    return passed;
}

bool runChartTest (const juce::File& dataRoot, juce::String& report)
{
    report << "[chart test] default kit plays a known pattern, the transcriber writes it down\n";

    const auto loaded = loadKit (dataRoot.getChildFile ("kits").getChildFile ("default").getChildFile ("kit.json"));

    if (loaded.kit == nullptr)
    {
        report << "FAILED: " << loaded.error << "\n";
        return false;
    }

    constexpr double sampleRate = 44100.0;
    auto ok = true;

    for (const auto tempo : { 100.0, 140.0, 180.0 })
    {
        const auto hits = makeRockPattern (tempo);
        const auto audio = renderHits (*loaded.kit, hits, sampleRate);

        hvd::TranscribeOptions options;
        options.isolatedDrums = true;
        options.logEvents = false;

        std::string log;
        const auto chart = hvd::transcribeDrums (audio.left.data(), audio.right.data(), audio.left.size(), sampleRate, options, nullptr, nullptr, &log);

        report << "\ntempo " << tempo << ": found " << juce::String (chart.tempo, 1) << "\n";

        const auto passed = scoreChart (chart, hits, tempo, report);
        ok = ok && passed;
        report << describeChart (chart);

        report << juce::String (log) << chartToTab (chart);
    }

    return ok;
}
