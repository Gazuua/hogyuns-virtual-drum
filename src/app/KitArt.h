#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** 드럼 그림의 악기 그리기. 드러머 자리에서 내려다본 북, 심벌, 발판, 바닥, 이름표를 그린다.
    상태는 갖지 않는다. 자리와 움직임은 KitView가 정해서 넘긴다. */
namespace kitart
{

using Point = juce::Point<float>;

/** 패드가 어떤 악기 모양으로 그려지는지. */
enum class Kind { kick, snare, tom, floorTom, hihat, crash, china, ride, bell, pedal, zone };

bool isCymbalKind (Kind);
bool isDrumKind (Kind);

// 내려다보는 각도 때문에 수평 원판은 세로로 납작한 타원으로 보인다.
inline constexpr float drumTilt = 0.74f;
inline constexpr float cymbalTilt = 0.70f;
inline constexpr float kickTilt = 0.60f;

/** 북 몸통의 모양. */
struct DrumLook
{
    float depth;         // 쉘의 높이 (반지름에 대한 비율)
    int numLugs;
    bool legs;
};

DrumLook drumLookOf (Kind);

/** 심벌의 색과 흔들림. */
struct CymbalLook
{
    juce::Colour bright, mid, dark;
    float bell;          // 벨의 크기 (반지름에 대한 비율)
    float swingHertz, swingDecay, swingAmount;
    bool upturnedEdge;   // 차이나: 가장자리가 위로 꺾여 있다
};

CymbalLook lookOf (Kind);

/** 치고 난 뒤의 흔들림. -1 ~ 1 사이에서 점점 줄어든다. */
float swing (float strength, float age, float hertz, float decaySeconds, float phase = 0.0f);

/** 밝은 헤드 위에 적는 이름의 색. */
juce::Colour headLabelColour();

//==============================================================================
/** 나무 바닥, 러그, 무대 조명. */
void drawFloor (juce::Graphics&, juce::Rectangle<float> bounds);

/** 악기 아래 바닥에 지는 그림자 (그림의 맨 아래 층). */
void drawShadowOf (juce::Graphics&, Kind, Point centre, float radius);

/** 심벌 스탠드와 플로어 탐의 다리 (그림자 다음 층). */
void drawStandOf (juce::Graphics&, Kind, Point centre, float radius);

void drawDrum (juce::Graphics&, Point centre, float radius, const DrumLook&, juce::Colour glow, float level, float age);
void drawKick (juce::Graphics&, Point centre, float radius, juce::Colour glow, float level, float age);

/** 심벌 한 장. tiltAngle은 드러머 쪽으로 기울여 단 각도(라디안)다. */
void drawCymbalDisc (juce::Graphics&, Point centre, float radius, const CymbalLook&, float tiltAngle, float wobble, float shimmer,
                     juce::Colour glow, float level);

/** 하이햇: 아래 심벌, 그 위에 살짝 떠 있는 위 심벌, 가운데를 지나는 막대. openness(0 ~ 1)만큼 위 심벌이 올라간다. */
void drawHiHat (juce::Graphics&, Point centre, float radius, float lean, float wobble, float openness, juce::Colour glow, float level);

/** 라이드의 벨을 쳤을 때 벨만 빛난다. */
void drawBellFlash (juce::Graphics&, Point centre, float radius, juce::Colour glow, float level);

/** 하이햇 페달이나 킥 페달 같은 발판. */
void drawFootboard (juce::Graphics&, Point centre, float radius, juce::Colour glow, float level);

/** 따로 떨어진 악기가 아닌 연주 방법(림샷, 열린 하이햇 등)을 치는 작은 고무 패드. */
void drawZonePad (juce::Graphics&, Point centre, float radius, juce::Colour colour, float level);

/** 치는 순간에 잠깐 보이는 스틱. tip은 닿는 곳, hand는 손이 있는 쪽(화면 아래)이다. */
void drawStick (juce::Graphics&, Point tip, Point hand, float length, float thickness, float alpha);

//==============================================================================
/** 키 이름을 자판의 키처럼 그린다. 가운데가 centre인 한 줄. */
void drawKeyCaps (juce::Graphics&, const juce::StringArray& keys, Point centre, float height);

/** drawKeyCaps가 그리는 키 캡 줄의 테두리. 키가 없으면 빈 네모. */
juce::Rectangle<float> keyCapsBounds (const juce::StringArray& keys, Point centre, float height);

/** 어두운 테두리를 두른 글자. 밝은 헤드와 황동 위에서도 읽힌다. area 안에 한 줄로 맞춘다. */
void drawName (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, float fontSize, juce::Colour colour, juce::Colour halo);

/** drawName의 글자가 실제로 차지하는 곳 (이름끼리 겹치는지 볼 때). */
juce::Rectangle<float> nameBounds (const juce::String& text, juce::Rectangle<float> area, float fontSize);

} // namespace kitart
