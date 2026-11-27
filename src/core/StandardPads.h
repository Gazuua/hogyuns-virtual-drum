#pragma once

#include <string_view>

#include "Chart.h"

namespace hvd
{

/** 기본 킷의 패드 하나와 그 기본값. tools/build_default_kit.py의 PADS와 같은 목록이다. */
struct StandardPad
{
    const char* id;
    int midiNote;        // 킷 파일에 midiNote가 없을 때 쓰는 번호 (General MIDI 드럼 맵에 가깝게)
    Lane lane;           // laneForPadId와 같은 값
    bool preferred;      // 리듬 게임이 그 레인을 대신 칠 때 고르는 패드
    float x, y, size;    // 드럼 그림의 기본 자리 (그림의 너비·높이에 대한 비율)
};

// 드러머 자리에서 내려다본 배치: 왼쪽에 하이햇, 가운데 아래에 스네어와 킥, 위에 탐과 크래시, 오른쪽에 라이드와 플로어 탐.
inline constexpr StandardPad standardPads[] = {
    { "kick",          36, Lane::kick,     true,  0.50f,  0.62f,  0.34f },
    { "snare",         38, Lane::snare,    true,  0.305f, 0.70f,  0.27f },
    { "snare_rim",     40, Lane::snare,    false, 0.215f, 0.90f,  0.12f },
    { "sidestick",     37, Lane::snare,    false, 0.375f, 0.925f, 0.12f },
    { "hihat_closed",  42, Lane::hihat,    true,  0.125f, 0.56f,  0.27f },
    { "hihat_half",    23, Lane::hihat,    false, 0.045f, 0.36f,  0.12f },
    { "hihat_open",    46, Lane::hihat,    false, 0.135f, 0.32f,  0.12f },
    { "hihat_pedal",   44, Lane::hihat,    false, 0.06f,  0.83f,  0.13f },
    { "tom_high",      48, Lane::tomHigh,  true,  0.405f, 0.31f,  0.22f },
    { "tom_mid",       45, Lane::tomMid,   true,  0.575f, 0.30f,  0.24f },
    { "tom_floor",     43, Lane::tomFloor, true,  0.70f,  0.65f,  0.26f },
    { "tom_low",       41, Lane::tomFloor, false, 0.86f,  0.71f,  0.26f },
    { "crash1",        49, Lane::crash,    true,  0.25f,  0.20f,  0.31f },
    { "crash2",        57, Lane::crash,    false, 0.735f, 0.19f,  0.31f },
    { "china",         52, Lane::crash,    false, 0.94f,  0.20f,  0.20f },
    { "ride",          51, Lane::ride,     true,  0.855f, 0.40f,  0.32f },
    { "ride_bell",     53, Lane::ride,     false, 0.855f, 0.395f, 0.10f },
    { "ride_edge",     59, Lane::ride,     false, 0.965f, 0.565f, 0.11f },
    { "crash_bright",  55, Lane::crash,    false, 0.40f,  0.085f, 0.13f },
    { "crash_trash",   58, Lane::crash,    false, 0.49f,  0.075f, 0.13f },
    { "crash_stack",   54, Lane::crash,    false, 0.58f,  0.085f, 0.13f },
};

/** id가 같은 표준 패드. 없으면 nullptr. */
inline const StandardPad* findStandardPad (std::string_view id) noexcept
{
    for (const auto& pad : standardPads)
        if (id == pad.id)
            return &pad;

    return nullptr;
}

/** 레인을 대신 칠 때 고르는 패드의 id (crash1, hihat_closed …). */
inline const char* preferredPadOf (Lane lane) noexcept
{
    for (const auto& pad : standardPads)
        if (pad.preferred && pad.lane == lane)
            return pad.id;

    return "";
}

} // namespace hvd
