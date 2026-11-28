#include "MouseRouter.h"

MouseRouter::MouseRouter (Session& sessionToUse, std::function<void (const Click&)> playClickToUse)
    : session (sessionToUse),
      playClick (std::move (playClickToUse))
{
}

MouseRouter::~MouseRouter()
{
    if (published != nullptr)
        session.setMouseTargets (nullptr);
}

std::shared_ptr<hvd::MouseTargets> MouseRouter::begin (const juce::Component& shown) const
{
    // 창이 최소화되면 isShowing()이 false다. 우리 팝업이나 대화 상자가 떠 있으면 그 창의 클릭이므로 받지 않는다.
    // 입력 스레드는 창 위에 무엇이 있는지 묻지 않는다.
    if (! shown.isShowing() || session.hasGivenUpMouseTakeover())
        return nullptr;

    const auto visible = visibleScreenArea (shown);

    if (visible.isEmpty() || isCoveredByOtherWindow (shown, visible))
        return nullptr;

    auto targets = std::make_shared<hvd::MouseTargets>();
    const auto frame = juce::Desktop::getInstance().getDisplays().logicalToPhysical (visible.toFloat());
    targets->left = frame.getX();
    targets->top = frame.getY();
    targets->right = frame.getRight();
    targets->bottom = frame.getBottom();
    return targets;
}

hvd::MouseTarget MouseRouter::makeTarget (const juce::Component& component, juce::Rectangle<float> area, hvd::MouseTarget::Shape shape,
                                          int pad, int spot)
{
    // 입력 스레드는 모니터의 실제 화소로 커서 자리를 받는다.
    const auto bounds = juce::Desktop::getInstance().getDisplays().logicalToPhysical (component.localAreaToGlobal (area));

    hvd::MouseTarget target;
    target.shape = shape;
    target.left = bounds.getX();
    target.top = bounds.getY();
    target.right = bounds.getRight();
    target.bottom = bounds.getBottom();
    target.pad = static_cast<std::uint16_t> (pad);
    target.spot = spot;
    return target;
}

void MouseRouter::publish (std::shared_ptr<hvd::MouseTargets> targets)
{
    // 바뀌었을 때만 넘긴다.
    const auto same = targets == nullptr ? published == nullptr
                                         : published != nullptr && *published == *targets;

    if (! same)
    {
        published = std::move (targets);
        session.setMouseTargets (published);
    }
}

void MouseRouter::handleClick (int view, int spot, bool right)
{
    // 화면 스레드가 바빴으면 클릭을 늦게 받는다. 누른 때는 지금 처리하는 마우스 메시지가 생긴 시각으로 어림한다.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const Click click { view, spot, right, now, now - hvd::RawKeyboard::currentMessageAgeMs() };

    if (! session.isMouseTakenByInput())
    {
        playClick (click);
        return;
    }

    uiClicks.push_back (click);
    reconcile();
}

void MouseRouter::reconcile()
{
    // 화면 스레드로 온 클릭마다 입력 스레드가 같은 단추로 친 기록을 찾는다. 입력 스레드는 먼저 받으므로 기록이 먼저 와 있다.
    // 40ms가 지나도 기록이 없으면 입력 스레드가 놓친 것이니 여기서 친다. 놓치거나 다른 자리를 친 일이 세 번 이어지면
    // (화면 배율 계산이 어긋난 경우 등) 입력 스레드에 맡기기를 그만둔다.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    Session::RawClick raw;

    while (session.popRawClick (raw))
        rawClicks.push_back (raw);

    for (auto click = uiClicks.begin(); click != uiClicks.end();)
    {
        const auto match = std::find_if (rawClicks.begin(), rawClicks.end(), [&click] (const Session::RawClick& candidate)
        {
            return candidate.right == click->right && hvd::isSameClick (candidate.timeMs, click->timeMs, click->pressedMs);
        });

        if (match != rawClicks.end())
        {
            failedClicks = match->spot == click->spot ? 0 : failedClicks + 1;
            rawClicks.erase (match);
            click = uiClicks.erase (click);
        }
        else if (now - click->timeMs >= 40.0)
        {
            playClick (*click);
            ++failedClicks;
            click = uiClicks.erase (click);
        }
        else
        {
            ++click;
        }
    }

    // 화면 쪽이 받지 못한 클릭(자리 가장자리, 다른 프로그램의 창)의 기록은 10초 뒤에 버린다. 화면 스레드가 오래 바빴으면
    // 밀린 클릭이 그 뒤의 tick보다 늦게 오므로 넉넉히 둔다. 오래된 기록은 시각이 맞지 않아 다른 클릭과 짝이 되지 않는다.
    rawClicks.erase (std::remove_if (rawClicks.begin(), rawClicks.end(),
                                     [now] (const Session::RawClick& old) { return old.timeMs < now - 10000.0; }),
                     rawClicks.end());

    if (failedClicks >= 3)
    {
        failedClicks = 0;
        session.giveUpMouseTakeover();
        published = nullptr;

        for (const auto& click : uiClicks)
            playClick (click);

        uiClicks.clear();
        rawClicks.clear();
    }
}

void MouseRouter::release()
{
    published = nullptr;
    session.setMouseTargets (nullptr);

    for (const auto& click : uiClicks)
        playClick (click);

    uiClicks.clear();
    rawClicks.clear();
}

bool MouseRouter::isCoveredByOtherWindow (const juce::Component& owner, juce::Rectangle<int> area)
{
    // 모달 창(대화 상자, 팝업 메뉴)이 떠 있으면 그 창 밖을 눌러도 화면은 클릭을 받지 않는다.
    if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
        return true;

    const auto* own = owner.getTopLevelComponent();
    const auto& desktop = juce::Desktop::getInstance();

    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        const auto* other = desktop.getComponent (i);

        if (other == nullptr || other == own || ! other->isVisible())
            continue;

        // 툴팁처럼 클릭을 받지 않는 창은 클릭이 아래로 지나간다.
        if (const auto* peer = other->getPeer(); peer != nullptr && (peer->getStyleFlags() & juce::ComponentPeer::windowIgnoresMouseClicks) != 0)
            continue;

        if (other->getScreenBounds().intersects (area))
            return true;
    }

    return false;
}

juce::Rectangle<int> MouseRouter::visibleScreenArea (const juce::Component& component)
{
    auto area = component.getScreenBounds();

    for (const auto* parent = component.getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
        area = area.getIntersection (parent->getScreenBounds());

    return area;
}
