#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "KeyCode.h"
#include "Kit.h"
#include "Stroke.h"

namespace hvd
{

/** 키 배열 파일의 한 줄. 패드는 아직 이름으로 적혀 있고, 적지 않은 값은 기본값을 쓴다. */
struct BindingSpec
{
    std::string key;                  // 예: "KeyJ"

    // "hit": 패드를 친다, "choke": 울리는 패드를 멈춘다,
    // "accent" / "ghost": 누르는 동안 다른 키의 세기를 올리거나 내린다,
    // "pedal": 누르는 동안 패드를 다른 패드로 바꾼다, "tapTempo", "metronome"
    std::string action = "hit";

    std::vector<std::string> pads;    // 대상 패드 id들
    float velocity = 1.0f;            // 0 ~ 1

    // 타격 방식: single, downUp, double, flam, drag, repeat, buzz, pattern
    std::string stroke = "single";

    std::string rate;                 // 반복 간격: "1/16", "1/8t", "follow"
    float intervalMs = 0.0f;          // 반복 간격을 밀리초로 줄 때
    std::string gap;                  // 더블, 플램의 간격을 박으로 줄 때: "1/32"
    float gapMs = 0.0f;               // 더블, 플램의 간격을 밀리초로 줄 때

    // 아래 값들은 음수이면 "적지 않음"이다.
    float secondVelocity = -1.0f;     // 더블의 둘째 타 세기
    float releaseVelocity = -1.0f;    // 누름+뗌에서 뗄 때의 세기, 페달을 뗄 때의 세기
    float graceVelocity = -1.0f;      // 플램, 드래그의 꾸밈음 세기
    float softVelocity = -1.0f;       // 반복에서 악센트가 아닌 타격의 세기
    float ramp = -1.0f;               // 반복에서 타격마다 세기에 곱할 값
    float swellMs = -1.0f;            // 반복과 버즈 롤이 점점 커지는 데 걸리는 시간. 다 커진 뒤에는 그 세기로 이어진다
    float swellStart = -1.0f;         // 커지기 시작할 때의 세기 배율 (0 ~ 1). 적지 않으면 0.4
    float jitter = -1.0f;             // 간격을 흔드는 비율
    float velocityJitter = -1.0f;     // 세기를 흔드는 비율
    int accentEvery = -1;             // 반복에서 몇 번째마다 악센트를 줄지
    float followDivisor = 1.0f;       // follow에서 잰 간격을 나눌 값

    std::string steps;                // 패턴: "X.x.o..." (X 세게, x 보통, o 약하게, . 쉼)
    float division = 4.0f;            // 패턴: 한 박을 몇 스텝으로 나누는지

    // pedal: 누르고 있는 동안 왼쪽 패드를 오른쪽 패드로 바꿔 친다. 뗄 때 releasePad를 친다.
    std::vector<std::pair<std::string, std::string>> swap;
    std::string releasePad;
};

struct MappingSpec
{
    std::string name;
    float tempo = 120.0f;
    int beatsPerBar = 4;
    float accentScale = 1.5f;         // 악센트 보조 키를 누르는 동안 세기에 곱할 값
    float ghostScale = 0.4f;          // 고스트 보조 키를 누르는 동안 세기에 곱할 값
    float chordAccentMs = 30.0f;      // 같은 패드의 두 키를 이 시간 안에 누르면 악센트. 0이면 끈다
    std::vector<BindingSpec> bindings;
};

/** 키 하나가 눌렸을 때 할 일 하나. */
struct KeyAction
{
    enum class Type : std::uint8_t { hit, choke, accent, ghost, pedal, tapTempo, metronome };

    Type type = Type::hit;
    std::uint16_t pad = 0;
    float velocity = 1.0f;
    Stroke stroke;
    std::uint16_t pedal = 0;          // type이 pedal일 때 Mapping::pedals의 번호
};

/** 페달 키 하나. */
struct Pedal
{
    int pressPad = -1;                // 누를 때 칠 패드. 없으면 -1
    float pressVelocity = 1.0f;
    int releasePad = -1;              // 뗄 때 칠 패드. 없으면 -1
    float releaseVelocity = 1.0f;
    std::vector<std::pair<std::uint16_t, std::uint16_t>> swaps;    // (원래 패드, 바꿔 칠 패드)
};

/** 킷의 패드 번호로 풀어 놓은 키 배열. 입력 스레드가 읽기만 한다. */
struct Mapping
{
    std::string name;
    float tempo = 120.0f;
    int beatsPerBar = 4;
    float accentScale = 1.5f;
    float ghostScale = 0.4f;
    float chordAccentMs = 30.0f;

    std::array<std::vector<KeyAction>, numKeyCodes> onPress;
    std::vector<Pedal> pedals;
    std::vector<std::vector<KeyCode>> keysForPad;    // 화면 표시용: 패드를 치는 키들
};

/** "1/16", "1/8t"(셋잇단), "1/8d"(점음표) 같은 표기를 박 수로 바꾼다. 4분음표가 1박이다.
    읽지 못하면 0을 돌려준다. */
float parseBeats (const std::string& text) noexcept;

/** 이름으로 적힌 키 배열을 킷에 맞춰 푼다. 모르는 키나 패드는 건너뛰고 warnings에 적는다. */
std::shared_ptr<const Mapping> resolveMapping (const MappingSpec& spec,
                                               const Kit& kit,
                                               std::vector<std::string>& warnings);

} // namespace hvd
