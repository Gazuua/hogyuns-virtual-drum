#include "PlayPage.h"

#include <cstring>

namespace
{

/** 키 옆에 붙여 보여 줄 타격 방식 이름. 단타는 붙이지 않는다. */
juce::String strokeTag (const hvd::Stroke& stroke)
{
    switch (stroke.type)
    {
        case hvd::Stroke::Type::single:     return {};
        case hvd::Stroke::Type::downUp:     return ko ("(뗌)");
        case hvd::Stroke::Type::multi:      return stroke.firstScale < 1.0f ? ko ("(플램)") : ko ("(더블)");
        case hvd::Stroke::Type::repeat:     return ko ("(반복)");
        case hvd::Stroke::Type::buzz:       return ko ("(롤)");
        case hvd::Stroke::Type::pattern:    return ko ("(패턴)");
    }

    return {};
}

juce::String actionName (hvd::KeyAction::Type type)
{
    switch (type)
    {
        case hvd::KeyAction::Type::accent:      return ko ("악센트");
        case hvd::KeyAction::Type::ghost:       return ko ("고스트");
        case hvd::KeyAction::Type::pedal:       return ko ("페달");
        case hvd::KeyAction::Type::choke:       return ko ("멈춤");
        case hvd::KeyAction::Type::tapTempo:    return ko ("탭 템포");
        case hvd::KeyAction::Type::metronome:   return ko ("메트로놈");
        case hvd::KeyAction::Type::hit:         break;
    }

    return {};
}

/** 패드가 어느 악기의 것인지. 같은 악기의 패드는 오른쪽 단추로 잡을 때 함께 멈춘다 (라이드와 라이드 벨, 스네어와 림샷). */
juce::String instrumentOf (const juce::String& padId)
{
    const auto id = padId.toLowerCase();

    if (id == "sidestick")
        return "snare";

    for (const auto* suffix : { "_bell", "_edge", "_bow", "_rim", "_closed", "_half", "_open", "_pedal", "_splash", "_buzz" })
        if (id.endsWith (suffix))
            return id.dropLastCharacters (static_cast<int> (std::strlen (suffix)));

    return id;
}

} // namespace

PlayPage::PlayPage (Session& sessionToUse)
    : session (sessionToUse)
{
    // 클릭은 보통 입력 스레드가 받아 이미 쳤다 (Session::handleMouse). 화면 쪽은 그것과 맞춰 보기만 한다.
    padGrid.onPadClicked = [this] (int pad) { handleClick (false, pad, false); };
    padGrid.onPadChoked = [this] (int pad) { handleClick (false, pad, true); };
    kitView.onPadClicked = [this] (int index) { handleClick (true, index, false); };
    kitView.onPadChoked = [this] (int index) { handleClick (true, index, true); };
    addChildComponent (padGrid);
    addChildComponent (kitView);

    // 배치를 고치면 킷 문서에 적어 두고, 마우스를 놓은 뒤에 한 번만 저장한다.
    kitView.onItemChanged = [this] (int index)
    {
        const auto& item = kitView.getItems()[static_cast<std::size_t> (index)];

        auto* view = new juce::DynamicObject();
        view->setProperty ("x", juce::roundToInt (item.x * 1000.0f) / 1000.0);
        view->setProperty ("y", juce::roundToInt (item.y * 1000.0f) / 1000.0);
        view->setProperty ("size", juce::roundToInt (item.size * 1000.0f) / 1000.0);
        session.getKitDocument().setPadProperty (item.id, item.fixed ? "fixedView" : "view", juce::var (view));
        layoutDirty = true;
    };

    ui::setupButton (*this, tilesButton, ko ("타일"), [this] { setShowingSet (false); });
    ui::setupButton (*this, setButton, ko ("드럼 세트"), [this] { setShowingSet (true); });
    ui::setupButton (*this, editButton, ko ("배치 편집"), [this] { kitView.setEditing (editButton.getToggleState()); });
    ui::setupButton (*this, resetButton, ko ("기본 배치로"), [this] { resetLayout(); });
    ui::setupButton (*this, silenceButton, ko ("소리 끄기"), [this] { session.allSoundOff(); });

    ui::setupButton (*this, labelsButton, ko ("이름 · 키 표시"), [this]
    {
        kitView.setShowingLabels (labelsButton.getToggleState());
        session.setSetting ("kitLabels", labelsButton.getToggleState());
    });

    labelsButton.setClickingTogglesState (true);
    labelsButton.setToggleState (static_cast<bool> (session.getSetting ("kitLabels", true)), juce::dontSendNotification);
    kitView.setShowingLabels (labelsButton.getToggleState());

    tilesButton.setRadioGroupId (1);
    setButton.setRadioGroupId (1);
    tilesButton.setClickingTogglesState (true);
    setButton.setClickingTogglesState (true);
    editButton.setClickingTogglesState (true);

    // 보기 전환(타일 / 드럼 세트)은 붙은 세그먼트로 그린다.
    ui::setButtonKind (tilesButton, ui::ButtonKind::segment);
    ui::setButtonKind (setButton, ui::ButtonKind::segment);
    tilesButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    setButton.setConnectedEdges (juce::Button::ConnectedOnLeft);

    ui::setupLabel (*this, functionKeyLabel, {}, ui::Text::hint);
    ui::setupLabel (*this, lastKeyLabel, ko ("마지막 키: 아직 없음"));
    ui::setupLabel (*this, timingLabel, {}, ui::Text::hint);
    timingLabel.setJustificationType (juce::Justification::centredRight);

    // 사용자가 맞춘 배치를 지우는 버튼이라 위험 표시를 한다.
    ui::setButtonKind (resetButton, ui::ButtonKind::danger);

    session.addChangeListener (this);
    setShowingSet (session.getSetting ("padView", "set").toString() != "tiles");
    rebuild();
}

PlayPage::~PlayPage()
{
    session.setMouseTargets (nullptr);
    session.removeChangeListener (this);
}

void PlayPage::setShowingSet (bool shouldShowSet)
{
    showingSet = shouldShowSet;
    session.setSetting ("padView", showingSet ? "set" : "tiles");

    tilesButton.setToggleState (! showingSet, juce::dontSendNotification);
    setButton.setToggleState (showingSet, juce::dontSendNotification);

    if (! showingSet)
    {
        editButton.setToggleState (false, juce::dontSendNotification);
        kitView.setEditing (false);
    }

    padGrid.setVisible (! showingSet);
    kitView.setVisible (showingSet);
    editButton.setVisible (showingSet);
    resetButton.setVisible (showingSet);
    labelsButton.setVisible (showingSet);
}

void PlayPage::resized()
{
    auto area = getLocalBounds();

    auto row = area.removeFromTop (ui::space::control);
    tilesButton.setBounds (row.removeFromLeft (64));
    setButton.setBounds (row.removeFromLeft (92));
    row.removeFromLeft (ui::space::large);
    editButton.setBounds (row.removeFromLeft (92));
    row.removeFromLeft (ui::space::small);
    resetButton.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (ui::space::small);
    labelsButton.setBounds (row.removeFromLeft (112));
    silenceButton.setBounds (row.removeFromRight (92));
    row.removeFromRight (ui::space::medium);
    timingLabel.setBounds (row);

    auto bottom = area.removeFromBottom (48);
    functionKeyLabel.setBounds (bottom.removeFromTop (22));
    lastKeyLabel.setBounds (bottom);

    area.removeFromTop (ui::space::small);
    area.removeFromBottom (ui::space::small);
    padGrid.setBounds (area);
    kitView.setBounds (area);
}

void PlayPage::rebuild()
{
    const auto* kit = session.getKit();
    const auto mapping = session.getMapping();

    std::vector<PadGrid::Tile> tiles;
    std::vector<KitView::Item> items;
    juce::StringArray functionKeys;

    shownAs.clear();
    chokeSets.clear();

    if (kit != nullptr)
    {
        for (std::size_t i = 0; i < kit->pads.size(); ++i)
            shownAs.push_back (static_cast<int> (i));

        // 오른쪽 단추로 잡으면 함께 멈출 패드: 같은 악기의 패드와 같은 초크 그룹의 패드 (하이햇은 모두)
        for (const auto& pad : kit->pads)
        {
            const auto instrument = instrumentOf (juce::String (pad.id));
            std::vector<std::uint16_t> set;

            for (std::size_t j = 0; j < kit->pads.size(); ++j)
            {
                const auto& other = kit->pads[j];

                if (instrumentOf (juce::String (other.id)) == instrument || (pad.group >= 0 && other.group == pad.group))
                    set.push_back (static_cast<std::uint16_t> (j));
            }

            chokeSets.push_back (std::move (set));
        }

        for (std::size_t i = 0; i < kit->pads.size(); ++i)
        {
            const auto& pad = kit->pads[i];

            PadGrid::Tile tile;
            tile.name = fromUtf8 (pad.name);
            tile.hasSound = ! pad.layers.empty();

            KitView::Item item;
            item.id = juce::String (pad.id);
            item.name = tile.name;
            item.hasSound = tile.hasSound;
            item.pad = static_cast<int> (i);

            if (mapping != nullptr && i < mapping->keysForPad.size())
            {
                juce::StringArray otherKeys;

                for (const auto key : mapping->keysForPad[i])
                {
                    const auto plain = fromUtf8 (hvd::keyLabel (key));
                    auto label = plain;
                    auto single = true;

                    // 이 키가 이 패드를 단타가 아닌 방식으로 치면 방식 이름을 붙인다.
                    for (const auto& action : mapping->onPress[key])
                    {
                        if (action.type == hvd::KeyAction::Type::hit && action.pad == i)
                        {
                            label << strokeTag (action.stroke);
                            single = action.stroke.type == hvd::Stroke::Type::single;
                            break;
                        }
                    }

                    tile.keys.add (label);

                    // 그림에는 자리가 좁아 한 번 치는 키의 이름만 적는다. 롤이나 반복 키는 타일 화면에서 볼 수 있다.
                    (single ? item.keys : otherKeys).add (plain);
                }

                if (item.keys.isEmpty())
                    item.keys = otherKeys;

                while (item.keys.size() > 4)
                    item.keys.remove (item.keys.size() - 1);
            }

            // 그림 위의 자리: 킷 파일에 적어 둔 값이 있으면 그것을, 없으면 기본 자리를 쓴다.
            KitView::defaultPlacement (item.id, static_cast<int> (i), item.x, item.y, item.size);

            if (const auto view = session.getKitDocument().getPadProperty (item.id, "view"); view.isObject())
            {
                item.x = juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (view["x"])));
                item.y = juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (view["y"])));
                item.size = juce::jlimit (0.06f, 0.60f, static_cast<float> (static_cast<double> (view["size"])));
            }

            tiles.push_back (std::move (tile));

            // 다른 패드의 자리에 보이는 패드(하이햇 스플래시, 스네어 울림)는 그림에 따로 그리지 않는다. 타일에는 보인다.
            if (const auto showAs = session.getKitDocument().getPadProperty (item.id, "showAs").toString(); showAs.isNotEmpty())
            {
                if (const auto other = kit->findPad (showAs.toStdString()); other >= 0 && other != static_cast<int> (i))
                {
                    shownAs[i] = other;
                    continue;
                }
            }

            items.push_back (std::move (item));
        }

        // 하이햇 닫힘은 그림에 한 자리 더 둔다. 이 자리는 페달을 따르지 않아 언제나 닫힌 소리가 난다.
        // 자리는 킷 파일의 그 패드에 "fixedView"로 적는다. 없으면 하이햇의 왼쪽 위에 둔다.
        for (const auto& source : items)
        {
            if (source.id != "hihat_closed")
                continue;

            auto twin = source;
            twin.name = source.name + ko (" 고정");
            twin.keys.clear();
            twin.fixed = true;
            twin.x = juce::jlimit (0.04f, 0.96f, source.x - 0.11f);
            twin.y = juce::jlimit (0.06f, 0.94f, source.y - 0.09f);
            twin.size = 0.12f;

            if (const auto view = session.getKitDocument().getPadProperty (twin.id, "fixedView"); view.isObject())
            {
                twin.x = juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (view["x"])));
                twin.y = juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (view["y"])));
                twin.size = juce::jlimit (0.06f, 0.60f, static_cast<float> (static_cast<double> (view["size"])));
            }

            items.push_back (std::move (twin));
            break;
        }
    }

    // 패드를 치지 않는 키들(세기 보조, 페달, 멈춤, 템포)을 한 줄로 보여 준다.
    if (mapping != nullptr)
    {
        for (std::size_t code = 1; code < hvd::numKeyCodes; ++code)
        {
            for (const auto& action : mapping->onPress[code])
            {
                if (const auto role = actionName (action.type); role.isNotEmpty())
                {
                    functionKeys.add (fromUtf8 (hvd::keyLabel (static_cast<hvd::KeyCode> (code))) + " " + role);
                    break;
                }
            }
        }
    }

    padGrid.setTiles (std::move (tiles));
    kitView.setItems (std::move (items));
    kitView.setEditing (editButton.getToggleState());

    functionKeyLabel.setText (functionKeys.isEmpty() ? juce::String() : ko ("기능 키:  ") + functionKeys.joinIntoString ("  ·  "),
                              juce::dontSendNotification);

    // 지금까지의 타격 횟수를 본 것으로 쳐서, 다시 읽은 직후에 패드가 켜지지 않게 한다.
    seenHitCounts.assign (kit != nullptr ? kit->pads.size() : 0, 0);
    seenChokeCounts.assign (seenHitCounts.size(), 0);

    for (std::size_t i = 0; i < seenHitCounts.size(); ++i)
    {
        seenHitCounts[i] = session.getEngine().getHitCount (static_cast<int> (i));
        seenChokeCounts[i] = session.getEngine().getChokeCount (static_cast<int> (i));
    }

    publishMouseTargets();
}

void PlayPage::handleClick (bool inSet, int spot, bool right)
{
    const UiClick click { inSet, spot, right, juce::Time::getMillisecondCounterHiRes() };

    if (! session.isMouseTakenByInput())
    {
        playClick (click);
        return;
    }

    uiClicks.push_back (click);
    reconcileClicks();
}

void PlayPage::playClick (const UiClick& click)
{
    auto pad = click.spot;
    auto followPedals = true;

    if (click.inSet)
    {
        if (! juce::isPositiveAndBelow (click.spot, static_cast<int> (kitView.getItems().size())))
            return;

        // 그림의 두 번째 자리(닫힘 고정)는 페달을 따르지 않는다.
        const auto& item = kitView.getItems()[static_cast<std::size_t> (click.spot)];
        pad = item.pad;
        followPedals = ! item.fixed;
    }

    if (click.right)
    {
        if (juce::isPositiveAndBelow (pad, static_cast<int> (chokeSets.size())))
            session.chokePads (chokeSets[static_cast<std::size_t> (pad)]);

        return;
    }

    // 이 타격은 원래 자리 대신 두 번째 자리를 번쩍인다.
    if (! followPedals)
        session.noteMouseSpot (pad, click.spot);

    session.previewPad (pad, 0.9f, hvd::HitSource::mouse, followPedals);
}

void PlayPage::reconcileClicks()
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
            return candidate.right == click->right && candidate.timeMs <= click->timeMs + 5.0 && candidate.timeMs >= click->timeMs - 500.0;
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

    // 화면 쪽이 받지 못한 클릭(자리 가장자리)의 기록은 1초 뒤에 버린다.
    rawClicks.erase (std::remove_if (rawClicks.begin(), rawClicks.end(),
                                     [now] (const Session::RawClick& old) { return old.timeMs < now - 1000.0; }),
                     rawClicks.end());

    if (failedClicks >= 3)
    {
        failedClicks = 0;
        session.giveUpMouseTakeover();
        publishedTargets = nullptr;

        for (const auto& click : uiClicks)
            playClick (click);

        uiClicks.clear();
        rawClicks.clear();
    }
}

void PlayPage::publishMouseTargets()
{
    std::shared_ptr<hvd::MouseTargets> targets;

    // 이 화면이 보이고 배치를 고치는 중이 아닐 때만 입력 스레드가 클릭을 받는다.
    if (isShowing() && ! kitView.isEditing() && (kitView.isVisible() || padGrid.isVisible()) && ! session.hasGivenUpMouseTakeover())
    {
        targets = std::make_shared<hvd::MouseTargets>();
        const auto& displays = juce::Desktop::getInstance().getDisplays();

        // 입력 스레드는 모니터의 실제 화소로 커서 자리를 받는다.
        const auto toPhysical = [&displays] (const juce::Component& component, juce::Rectangle<float> area)
        {
            return displays.logicalToPhysical (component.localAreaToGlobal (area));
        };

        const auto chokesOf = [this] (int pad)
        {
            return juce::isPositiveAndBelow (pad, static_cast<int> (chokeSets.size())) ? chokeSets[static_cast<std::size_t> (pad)]
                                                                                      : std::vector<std::uint16_t>();
        };

        const juce::Component& shown = kitView.isVisible() ? static_cast<const juce::Component&> (kitView) : padGrid;
        const auto frame = toPhysical (shown, shown.getLocalBounds().toFloat());
        targets->left = frame.getX();
        targets->top = frame.getY();
        targets->right = frame.getRight();
        targets->bottom = frame.getBottom();

        if (kitView.isVisible())
        {
            for (const auto& area : kitView.getHitAreas())
            {
                const auto& item = kitView.getItems()[static_cast<std::size_t> (area.index)];
                const auto bounds = toPhysical (kitView, area.bounds);

                hvd::MouseTarget target;
                target.shape = hvd::MouseTarget::Shape::ellipse;
                target.left = bounds.getX();
                target.top = bounds.getY();
                target.right = bounds.getRight();
                target.bottom = bounds.getBottom();
                target.pad = static_cast<std::uint16_t> (item.pad);
                target.followPedals = ! item.fixed;
                target.spot = area.index;
                target.chokes = chokesOf (item.pad);
                targets->targets.push_back (std::move (target));
            }
        }
        else
        {
            for (int i = 0; i < padGrid.getNumTiles(); ++i)
            {
                const auto bounds = toPhysical (padGrid, padGrid.getTileBounds (i).toFloat());

                hvd::MouseTarget target;
                target.left = bounds.getX();
                target.top = bounds.getY();
                target.right = bounds.getRight();
                target.bottom = bounds.getBottom();
                target.pad = static_cast<std::uint16_t> (i);
                target.spot = i;
                target.chokes = chokesOf (i);
                targets->targets.push_back (std::move (target));
            }
        }
    }

    // 바뀌었을 때만 넘긴다.
    const auto same = targets == nullptr ? publishedTargets == nullptr
                                         : publishedTargets != nullptr && *publishedTargets == *targets;

    if (! same)
    {
        publishedTargets = targets;
        session.setMouseTargets (publishedTargets);
    }
}

void PlayPage::resetLayout()
{
    for (const auto& id : session.getKitDocument().getPadIds())
    {
        session.getKitDocument().setPadProperty (id, "view", {});
        session.getKitDocument().setPadProperty (id, "fixedView", {});
    }

    session.saveKit (false);
    rebuild();
}

void PlayPage::updateLastKeyLabel()
{
    const auto key = session.getLastKey();
    const auto* kit = session.getKit();
    const auto mapping = session.getMapping();
    juce::StringArray targets;

    if (mapping != nullptr && kit != nullptr && key < hvd::numKeyCodes)
    {
        for (const auto& action : mapping->onPress[key])
        {
            if (action.type == hvd::KeyAction::Type::hit || action.type == hvd::KeyAction::Type::choke)
            {
                if (action.pad >= kit->pads.size())
                    continue;

                auto name = fromUtf8 (kit->pads[action.pad].name);
                name << (action.type == hvd::KeyAction::Type::choke ? ko (" 멈춤") : strokeTag (action.stroke));
                targets.add (name);
            }
            else
            {
                targets.add (actionName (action.type));
            }
        }
    }

    juce::String text;
    text << ko ("마지막 키: ") << fromUtf8 (hvd::keyName (key)) << "  →  "
         << (targets.isEmpty() ? ko ("지정된 동작 없음") : targets.joinIntoString (", "));
    lastKeyLabel.setText (text, juce::dontSendNotification);
}

void PlayPage::updateTimingLabel()
{
    const auto& timing = session.getTiming();
    juce::String text;

    if (session.getEngine().isMetronomeOn() && timing.count > 0)
    {
        const auto ms = juce::roundToInt (timing.lastMs);
        text << ko ("박자: ") << (ms > 0 ? "+" : "") << ms << " ms "
             << (std::abs (ms) <= 8 ? ko ("(정확)") : (ms < 0 ? ko ("(빠름)") : ko ("(늦음)")))
             << "  ·  " << ko ("평균 오차 ") << juce::roundToInt (timing.averageAbsMs) << " ms";
    }

    if (text != timingLabel.getText())
        timingLabel.setText (text, juce::dontSendNotification);
}

void PlayPage::pageShown()
{
    // 다른 화면을 보는 동안 친 것까지 한꺼번에 번쩍이지 않게, 지금까지의 횟수를 본 것으로 친다.
    for (std::size_t i = 0; i < seenHitCounts.size(); ++i)
    {
        seenHitCounts[i] = session.getEngine().getHitCount (static_cast<int> (i));
        seenChokeCounts[i] = session.getEngine().getChokeCount (static_cast<int> (i));
    }

    publishMouseTargets();
}

void PlayPage::pageHidden()
{
    // 다른 화면의 같은 자리를 누를 때 드럼이 울리지 않게 한다.
    publishedTargets = nullptr;
    session.setMouseTargets (nullptr);

    for (const auto& click : uiClicks)
        playClick (click);

    uiClicks.clear();
    rawClicks.clear();
}

void PlayPage::tick()
{
    // 페달 상태에 따라 하이햇의 모양과 이름이 바뀐다 (페달을 떼면 반열림). 타격을 밝히기 전에 정해 둔다.
    if (kitView.isVisible())
    {
        actingPads.clear();

        for (const auto& item : kitView.getItems())
            actingPads.push_back (item.fixed ? item.pad : session.padAfterPedals (item.pad));

        kitView.setActingPads (actingPads);
    }

    publishMouseTargets();
    reconcileClicks();

    // 방금 친 패드를 밝힌다. 그림에 따로 없는 패드는 대신 보이는 자리를 밝힌다.
    for (std::size_t i = 0; i < seenHitCounts.size(); ++i)
    {
        const auto pad = static_cast<int> (i);
        const auto count = session.getEngine().getHitCount (pad);

        if (count != seenHitCounts[i])
        {
            seenHitCounts[i] = count;
            const auto strength = 0.35f + 0.65f * session.getEngine().getLastVelocity (pad);
            const auto spot = session.takeMouseSpot (pad);

            padGrid.flash (pad, strength);
            kitView.flash (i < shownAs.size() ? shownAs[i] : pad, strength, spot);
        }

        // 멈춘 패드(오른쪽 단추로 잡은 심벌, 멈춤 키)는 흔들림도 멈춘다.
        if (i < seenChokeCounts.size())
        {
            if (const auto chokes = session.getEngine().getChokeCount (pad); chokes != seenChokeCounts[i])
            {
                seenChokeCounts[i] = chokes;
                kitView.damp (pad);
            }
        }
    }

    if (padGrid.fade() && padGrid.isVisible())
        padGrid.repaint();

    if (kitView.fade() && kitView.isVisible())
        kitView.repaint();

    if (const auto count = session.getKeyPressCount(); count != seenKeyPressCount)
    {
        seenKeyPressCount = count;
        updateLastKeyLabel();
    }

    if (++tickCount % 6 == 0)
        updateTimingLabel();

    // 배치를 고쳤으면 마우스를 놓은 뒤에 저장한다.
    if (layoutDirty && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown() && tickCount % 30 == 0)
    {
        layoutDirty = false;
        session.saveKit (false);
    }
}
