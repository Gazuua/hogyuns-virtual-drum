#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** 화면 디자인의 기준값(테마 토큰): 색, 모서리, 간격, 글자 크기.
    화면과 그림은 색을 직접 적지 않고 ui::theme()에서 읽는다. 테마는 프로그램을 켤 때 한 번 고른다. */
namespace ui
{

/** 지금 화면을 알리는 탭의 모양. */
enum class TabStyle
{
    underline,    // 글자 아래 액센트 줄
    segment       // 알약 모양 묶음 안에서 고른 칸을 채움
};

struct Theme
{
    juce::String id;
    bool dark = true;

    //==============================================================================
    // 면
    juce::Colour background;        // 창 바탕
    juce::Colour surface;           // 큰 칸(패널, 카드)
    juce::Colour surfaceRaised;     // 칸 위의 조절 부품: 버튼, 콤보, 값 상자, 타일
    juce::Colour surfaceSunken;     // 오목한 자리: 미터, 진행 막대, 그래프 바탕
    juce::Colour track;             // 슬라이더와 손잡이의 홈
    juce::Colour border;            // 나누는 선
    juce::Colour controlOutline;    // 조절 부품의 테두리. 어두운 테마는 투명
    juce::Colour hover;             // 마우스를 올렸을 때 덧칠(반투명)
    juce::Colour pressed;           // 눌렀을 때 덧칠(반투명)
    juce::Colour selection;         // 고른 줄, 켜진 토글의 바탕
    juce::Colour shadow;            // 떠 있는 것(메뉴, 고른 탭)의 그림자
    juce::Colour menu;              // 팝업 메뉴, 툴팁의 바탕

    //==============================================================================
    // 글자
    juce::Colour text;              // 본문
    juce::Colour textMuted;         // 설명, 항목 이름
    juce::Colour textFaint;         // 상태 표시줄, 꺼진 것

    //==============================================================================
    // 강조
    juce::Colour accent;            // 주요 버튼, 슬라이더의 채운 부분, 지금 탭
    juce::Colour accentText;        // 액센트 위의 글자
    juce::Colour danger;            // 녹음 중, 뮤트, 실패
    juce::Colour good;              // 맞음, 정상
    juce::Colour warning;           // 경고 글
    juce::Colour info;              // 진행한 부분, 자료 점

    //==============================================================================
    // 악기 색 (padColour가 고른다)
    juce::Colour kick, snare, hihat, tom, ride, cymbal, otherPad;

    //==============================================================================
    // 드럼 그림의 무대. 북과 심벌의 색은 테마와 상관없이 그대로다.
    struct Stage
    {
        juce::Colour floor;                     // 널 사이 틈
        juce::Colour plankFar, plankNear;       // 나무 바닥 (먼 쪽, 가까운 쪽)
        juce::Colour plankLine;                 // 널 이음매
        juce::Colour rugFar, rugNear;           // 러그
        juce::Colour rugTrim;                   // 러그 테두리 무늬
        juce::Colour rugShadow;
        juce::Colour light, vignette;           // 가운데 조명, 가장자리 그늘
        juce::Colour nameText, nameHalo;        // 바닥 위에 적는 이름
        juce::Colour keycap, keycapEdge, keycapText;
        juce::Colour editOverlay, editText;     // 배치 편집 안내
    } stage;

    //==============================================================================
    // 리듬 게임 연주 화면의 바탕 (노트가 내려오는 판)
    juce::Colour field, fieldLine, fieldText;

    //==============================================================================
    // 탭
    juce::Colour tabTrack;          // 알약 탭 묶음의 바탕
    juce::Colour tabSelectedFill;   // 고른 알약 탭
    juce::Colour tabSelectedText;

    //==============================================================================
    // 모양
    float controlRadius = 6.0f;     // 버튼, 콤보, 값 상자
    float panelRadius = 10.0f;      // 큰 칸
    TabStyle tabs = TabStyle::segment;

    /** 악기 색을 칸 바탕으로 쓸 때의 옅은 색. 그 위에는 text 색 글자가 읽힌다. */
    juce::Colour padTint (juce::Colour padColour) const;

    /** fill 위에 올릴 글자색 (밝은 바탕이면 어두운 글자). */
    juce::Colour textOn (juce::Colour fill) const;
};

/** 지금 테마. */
const Theme& theme();

/** 테마를 고른다. 모르는 이름이면 기본 테마. 화면을 만들기 전에 부른다. */
void setTheme (const juce::String& id);

/** 고를 수 있는 테마 이름(id)과 화면에 보일 이름. */
juce::StringArray themeIds();
juce::String themeDisplayName (const juce::String& id);

inline constexpr const char* defaultThemeId = "graphite";

//==============================================================================
/** 간격: 8px 격자. 붙어 있는 부품 사이에만 반 칸(4)을 쓴다. */
namespace space
{
    inline constexpr int tight = 4;      // 한 묶음 안의 부품 사이
    inline constexpr int small = 8;      // 부품 사이
    inline constexpr int medium = 12;    // 줄 사이
    inline constexpr int large = 16;     // 칸 안쪽 여백, 묶음 사이
    inline constexpr int xlarge = 24;    // 큰 묶음 사이

    inline constexpr int control = 28;   // 버튼과 콤보의 높이
}

/** 글자 크기 단계 (맑은 고딕): 12 보조, 14 본문, 17 칸 제목, 24 큰 숫자. 11px 아래는 쓰지 않는다. */
namespace type
{
    inline constexpr float display = 24.0f;    // 큰 숫자 (점수 등)
    inline constexpr float title = 17.0f;      // 화면 제목
    inline constexpr float section = 17.0f;    // 칸 제목
    inline constexpr float heading = 14.0f;    // 칸 안의 작은 제목 (굵게)
    inline constexpr float body = 14.0f;       // 본문, 버튼
    inline constexpr float caption = 12.0f;    // 항목 이름, 설명, 상태 표시줄
    inline constexpr float small = 12.0f;      // 작은 꼬리표 (caption과 같은 크기, 더 옅은 쓰임)
}

/** 글 꼬리표의 쓰임. 크기, 굵기, 색이 정해진다. */
enum class Text
{
    title,      // 화면 제목
    section,    // 칸 제목
    heading,    // 칸 안의 작은 제목
    body,       // 본문
    detail,     // 작은 본문 (목록, 파일 이름)
    caption,    // 항목 이름, 설명
    hint,       // 화면 설명 (본문 크기, 옅은 색)
    small,      // 작은 꼬리표
    warning     // 경고
};

/** 패드 id로 악기 종류를 짐작해 색을 고른다. */
juce::Colour padColour (const juce::String& padId);

} // namespace ui
