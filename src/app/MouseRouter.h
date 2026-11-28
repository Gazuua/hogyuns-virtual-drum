#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "Session.h"

/** 화면의 클릭할 자리를 입력 스레드에 넘기고, 화면 스레드로 온 클릭을 입력 스레드가 친 기록과 맞춰 본다.
    클릭은 보통 입력 스레드가 Raw Input으로 받아 바로 친다(Session::handleMouse). 화면 스레드가 바빠도 밀리지 않는다.
    연주 화면(드럼 그림, 타일)과 리듬 게임(레인)이 쓴다. 보이는 화면 하나만 자리를 넘긴다. */
class MouseRouter
{
public:
    /** 화면 스레드로 온 클릭. view는 화면이 정하는 구분(그림과 타일 등), spot은 MouseTarget::spot이다. */
    struct Click
    {
        int view = 0;
        int spot = -1;
        bool right = false;
        double timeMs = 0.0;       // 화면 스레드가 받은 때 (juce::Time::getMillisecondCounterHiRes)
        double pressedMs = 0.0;    // 누른 때의 어림. 화면 스레드가 바빴으면 timeMs보다 한참 이르다
    };

    /** playClick은 화면 쪽이 클릭을 직접 칠 때 부른다 (입력 스레드가 놓쳤거나 맡지 않을 때). */
    MouseRouter (Session& sessionToUse, std::function<void (const Click&)> playClick);
    ~MouseRouter();

    /** 자리 목록을 시작한다. shown이 화면에 보이고 다른 창에 가리지 않았으면 그 보이는 부분을 테두리로 한 빈 목록을,
        아니면(최소화, 우리 팝업·대화 상자, 입력 스레드에 맡기기를 그만둔 뒤) nullptr을 준다. 창 밖으로 나간 부분은 자른다. */
    std::shared_ptr<hvd::MouseTargets> begin (const juce::Component& shown) const;

    /** 부품 안의 자리 하나. 좌표는 입력 스레드가 쓰는 모니터의 실제 화소로 바꾼다. */
    static hvd::MouseTarget makeTarget (const juce::Component& component, juce::Rectangle<float> area, hvd::MouseTarget::Shape shape,
                                        int pad, int spot);

    /** 자리 목록을 넘긴다. 바뀌었을 때만 Session에 준다. nullptr이면 입력 스레드가 클릭을 받지 않는다. */
    void publish (std::shared_ptr<hvd::MouseTargets> targets);

    /** 화면 스레드로 온 클릭. 입력 스레드가 맡고 있으면 맞춰 보고, 아니면 바로 친다. */
    void handleClick (int view, int spot, bool right);

    /** tick마다 부른다. 40ms 안에 입력 스레드의 기록이 없는 클릭은 대신 치고, 어긋남이 세 번 이어지면 맡기기를 그만둔다. */
    void reconcile();

    /** 화면을 떠날 때: 자리를 거두고, 아직 맞춰 보지 못한 클릭은 친다. 다른 화면의 같은 자리를 눌러도 드럼이 울리지 않게. */
    void release();

    /** 다른 창(팝업 메뉴, 대화 상자 등)이 화면 좌표의 area를 가리거나 입력을 붙잡고 있는지. owner는 area를 그린 부품이다.
        툴팁처럼 클릭을 받지 않는 창은 셈하지 않는다. */
    static bool isCoveredByOtherWindow (const juce::Component& owner, juce::Rectangle<int> area);

    /** 부품에서 화면에 보이는 부분(화면 좌표). 부모와 창의 클라이언트 영역 밖으로 나간 부분은 뺀다. */
    static juce::Rectangle<int> visibleScreenArea (const juce::Component& component);

private:
    Session& session;
    std::function<void (const Click&)> playClick;
    std::shared_ptr<const hvd::MouseTargets> published;
    std::vector<Click> uiClicks;                    // 입력 스레드가 친 것과 아직 맞춰 보지 않은 클릭
    std::vector<Session::RawClick> rawClicks;       // 입력 스레드가 친 클릭 가운데 아직 짝을 찾지 못한 것
    int failedClicks = 0;                           // 입력 스레드가 놓쳤거나 다른 자리를 친 클릭이 이어진 수

    JUCE_DECLARE_NON_COPYABLE (MouseRouter)
};
