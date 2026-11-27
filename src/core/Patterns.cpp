#include "Patterns.h"

#include <algorithm>

namespace hvd
{

std::vector<ChartNote> makePracticePattern (double tempo, std::vector<double>& beats)
{
    const auto beat = 60.0 / tempo;

    beats.clear();

    for (int i = 0; i <= 34; ++i)
        beats.push_back (beat * i);

    std::vector<ChartNote> notes;

    const auto add = [&notes] (double seconds, Lane lane, float velocity, double beatPosition)
    {
        ChartNote note;
        note.seconds = seconds;
        note.lane = lane;
        note.velocity = velocity;
        note.beat = beatPosition;
        notes.push_back (note);
    };

    for (int bar = 0; bar < 8; ++bar)
    {
        const auto fill = bar == 3 || bar == 7;

        for (int eighth = 0; eighth < 8; ++eighth)
        {
            const auto position = bar * 4.0 + eighth * 0.5;
            const auto time = position * beat;

            if (fill && eighth >= 4)
            {
                const Lane toms[] = { Lane::tomHigh, Lane::tomHigh, Lane::tomMid, Lane::tomFloor };
                add (time, toms[eighth - 4], 0.9f, position);
                continue;
            }

            if (eighth == 0 && bar % 4 == 0)
                add (time, Lane::crash, 0.95f, position);
            else
                add (time, bar >= 4 ? Lane::ride : Lane::hihat, eighth % 2 == 0 ? 0.85f : 0.6f, position);

            if (eighth == 0 || eighth == 4 || (eighth == 3 && bar % 2 == 1))
                add (time, Lane::kick, 0.9f, position);

            if (eighth == 2 || eighth == 6)
                add (time, Lane::snare, 0.9f, position);
        }
    }

    return notes;
}

std::vector<PatternHit> makeRockPattern (double tempo)
{
    std::vector<PatternHit> hits;
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

    std::stable_sort (hits.begin(), hits.end(), [] (const PatternHit& a, const PatternHit& b) { return a.seconds < b.seconds; });
    return hits;
}

} // namespace hvd
