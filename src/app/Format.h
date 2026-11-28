#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

#include "Text.h"
#include "core/PracticeStats.h"

/** 화면에 숫자와 시각을 적는 꼴. 단위는 숫자에 붙인다("100%", "12ms", "-3.0dB"). BPM만 띄운다. */
namespace ui
{

//==============================================================================
/** 박자를 말할 때의 기준. 화면마다 같은 값과 같은 말을 쓴다. */
namespace timing
{
    /** 타격 하나가 이 안(±ms)이면 "박에 맞음". 연주 화면, 반주 화면의 박자 연습, 연습 기록이 함께 쓴다.
        연습 기록의 "박에 맞음 n%"는 코어가 세므로 코어의 값을 그대로 따른다. */
    inline constexpr float onBeatMs = hvd::PracticeReport::tightMs;

    /** 여러 타격의 가운뎃값이 이 안이면 치우침이 없다고 보고 표시(✓)를 붙인다. 타격 하나의 기준(onBeatMs)과는 다르다. */
    inline constexpr float centredMs = 15.0f;
}

//==============================================================================
inline juce::String separator()    { return juce::String::fromUTF8 ("  \xc2\xb7  "); }    // "  ·  "
inline juce::String plusMinus()    { return juce::String::fromUTF8 ("\xc2\xb1"); }        // "±"
inline juce::String dash()         { return juce::String::fromUTF8 ("\xe2\x80\x94"); }    // "—" (값이 없을 때)

/** 초를 "1:23.4" 꼴로. */
inline juce::String formatTime (double seconds)
{
    seconds = juce::jmax (0.0, seconds);
    const auto minutes = static_cast<int> (seconds / 60.0);
    const auto rest = seconds - minutes * 60.0;
    return juce::String (minutes) + ":" + juce::String (rest, 1).paddedLeft ('0', 4);
}

/** 초를 "1:23" 꼴로 (초 아래는 버린다). */
inline juce::String formatMinutes (double seconds)
{
    const auto whole = juce::jmax (0, static_cast<int> (std::floor (seconds)));
    return juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
}

/** "오늘 22:23", "어제 21:02", "11/23 14:20" */
inline juce::String formatWhen (juce::Time time, juce::Time now)
{
    const auto sameDay = [] (juce::Time a, juce::Time b) { return a.getYear() == b.getYear() && a.getDayOfYear() == b.getDayOfYear(); };
    const auto clock = time.formatted ("%H:%M");

    if (sameDay (time, now))
        return ko ("오늘 ") + clock;

    if (sameDay (time, now - juce::RelativeTime::days (1.0)))
        return ko ("어제 ") + clock;

    return juce::String (time.getMonth() + 1) + "/" + juce::String (time.getDayOfMonth()) + " " + clock;
}

/** 1070 → "1,070" */
inline juce::String withCommas (int value)
{
    auto text = juce::String (std::abs (value));

    for (int at = text.length() - 3; at > 0; at -= 3)
        text = text.substring (0, at) + "," + text.substring (at);

    return value < 0 ? "-" + text : text;
}

/** 0.83 같은 비율이 아니라 83 같은 백분율 값을 "83%"로. */
inline juce::String formatPercent (double percent)
{
    return juce::String (juce::roundToInt (percent)) + "%";
}

/** "12ms" (반올림, 부호 없음) */
inline juce::String formatMs (float ms)
{
    return juce::String (juce::roundToInt (std::abs (ms))) + "ms";
}

/** "+12ms", "-3ms", "0ms" */
inline juce::String formatSignedMs (float ms)
{
    const auto rounded = juce::roundToInt (ms);
    return (rounded > 0 ? "+" : "") + juce::String (rounded) + "ms";
}

/** 빠르거나 늦은 정도를 말로: "12ms 빠름", "9ms 늦음", "0ms" */
inline juce::String formatLean (float ms)
{
    const auto rounded = juce::roundToInt (ms);

    if (rounded == 0)
        return "0ms";

    return formatMs (ms) + (rounded < 0 ? ko (" 빠름") : ko (" 늦음"));
}

/** 타격 하나를 말로: 기준(timing::onBeatMs) 안이면 "박에 맞음", 아니면 "12ms 빠름" 꼴. */
inline juce::String formatHitTiming (float ms)
{
    return std::abs (ms) <= timing::onBeatMs ? ko ("박에 맞음") : formatLean (ms);
}

/** "-3.0dB" */
inline juce::String formatDb (float decibels, int decimals = 1)
{
    return juce::String (decibels, decimals) + "dB";
}

/** 팬 값(-1 ~ 1)을 "C", "L20", "R100" 꼴로. */
inline juce::String formatPan (double pan)
{
    const auto percent = juce::roundToInt (pan * 100.0);

    if (percent == 0)
        return "C";

    return (percent < 0 ? "L" : "R") + juce::String (std::abs (percent));
}

/** 별점 "★ 3.5". 값이 없으면(음수) "—". */
inline juce::String formatStars (float stars)
{
    return stars < 0.0f ? dash() : juce::String::fromUTF8 ("\xe2\x98\x85 ") + juce::String (stars, 1);
}

} // namespace ui
