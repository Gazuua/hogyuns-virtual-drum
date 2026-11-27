#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace hvd
{

/** 리듬 게임의 레인 하나. 비슷한 패드들을 한 레인으로 묶는다 (예: 닫힌 하이햇, 열린 하이햇 → 하이햇). */
enum class Lane : std::uint8_t
{
    crash = 0,
    hihat,
    snare,
    tomHigh,
    tomMid,
    tomFloor,
    ride,
    kick
};

constexpr int numLanes = 8;

/** 설정 파일과 채보 파일에서 쓰는 레인 이름. */
const char* laneName (Lane lane) noexcept;

/** 이름으로 레인을 찾는다. 모르는 이름이면 false. */
bool laneFromName (std::string_view name, Lane& lane) noexcept;

/** 글 악보에 쓰는 두 글자 이름: CR, HH, SN, T1, T2, T3, RD, KK. tools/play_log.py의 SHORT와 같다. */
const char* laneShortName (Lane lane) noexcept;

/** 패드 id로 그 패드가 속한 레인을 짐작한다 (instrumentOfPad 위에 탐 순서를 얹는다). 짐작할 수 없으면 false. */
bool laneForPadId (std::string_view padId, Lane& lane) noexcept;

/** 탐 셋 / 크래시·하이햇·라이드. 리듬 게임의 "비슷한 레인도 맞음"과 채점이 같은 무리를 쓴다. */
bool isTomLane (Lane lane) noexcept;
bool isCymbalLane (Lane lane) noexcept;

/** 채보의 노트 하나. */
struct ChartNote
{
    static constexpr std::uint8_t openHat = 1;     // 열린 하이햇
    static constexpr std::uint8_t inferred = 2;    // 소리에서 직접 찾지 못하고 앞뒤 패턴으로 채운 노트

    double seconds = 0.0;      // 곡의 처음부터 잰 시각
    Lane lane = Lane::snare;
    float velocity = 0.8f;     // 0 ~ 1
    double beat = -1.0;        // 박 격자에서의 자리(박 번호 + 박 안에서의 위치, 1/24박 단위). 격자가 없으면 음수
    std::uint8_t flags = 0;
};

/** 곡 하나에서 받아 적은 드럼 채보. */
struct Chart
{
    double duration = 0.0;            // 곡 길이(초)
    float tempo = 0.0f;               // 분당 박 수. 박을 찾지 못했으면 0
    std::vector<double> beats;        // 박의 시각
    int firstDownbeat = 0;            // beats에서 첫 마디가 시작하는 박의 번호
    std::vector<ChartNote> notes;     // 시각 순
};

/** 채보의 단계. full은 받아 적은 그대로이고, 아래 단계일수록 노트를 많이 덜어 낸다.

    easy    킥과 스네어를 4분음표 자리에서만 (심벌 없음, 크래시는 박 위의 것만)
    normal  킥과 스네어는 8분음표 자리, 하이햇과 라이드는 4분음표 자리
    hard    킥과 스네어는 16분음표 자리, 하이햇과 라이드는 8분음표 자리
*/
enum class ChartLevel : std::uint8_t { easy = 0, normal, hard, full };

constexpr int numChartLevels = 4;

/** 박 안에서의 자리를 1/24박 단위로 돌려준다 (0 ~ 23). 격자에 없는 노트(beat가 음수)는 -1. */
int subdivisionOf (const ChartNote& note) noexcept;

/** 셋잇단 느낌의 곡인지: 박 안의 1/3, 2/3 자리 노트가 16분음표 엇박 자리의 두 배보다 많고 여덟 개를 넘는다. */
bool hasTripletFeel (const std::vector<ChartNote>& notes) noexcept;

/** 받아 적은 채보에서 단계에 맞게 노트를 덜어 낸다. */
std::vector<ChartNote> makeChartVariant (const Chart& chart, ChartLevel level);

/** 노트의 빽빽함과 엇박 정도로 난이도를 매긴다. 0.0 ~ 5.0이고 소수 첫째 자리까지다. */
float rateChartDifficulty (const std::vector<ChartNote>& notes);

} // namespace hvd
