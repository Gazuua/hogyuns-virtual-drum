#include "PlayPage.h"

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

} // namespace

PlayPage::PlayPage (Session& sessionToUse)
    : session (sessionToUse)
{
    padGrid.onPadClicked = [this] (int pad) { session.previewPad (pad, 0.9f, hvd::HitSource::mouse); };

    // 그림의 두 번째 자리(닫힘 고정)는 페달을 따르지 않는다.
    kitView.onPadClicked = [this] (int index)
    {
        const auto& item = kitView.getItems()[static_cast<std::size_t> (index)];

        // 이 타격은 원래 자리 대신 두 번째 자리를 번쩍인다.
        if (item.fixed)
            fixedSpotPad = item.pad;

        session.previewPad (item.pad, 0.9f, hvd::HitSource::mouse, ! item.fixed);
    };
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

    ui::setupLabel (*this, functionKeyLabel, {}, 14.0f, ui::quietText);
    ui::setupLabel (*this, lastKeyLabel, ko ("마지막 키: 아직 없음"), 15.0f);
    ui::setupLabel (*this, timingLabel, {}, 14.0f, ui::quietText);
    timingLabel.setJustificationType (juce::Justification::centredRight);

    session.addChangeListener (this);
    setShowingSet (session.getSetting ("padView", "set").toString() != "tiles");
    rebuild();
}

PlayPage::~PlayPage()
{
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

    auto row = area.removeFromTop (28);
    tilesButton.setBounds (row.removeFromLeft (64));
    row.removeFromLeft (4);
    setButton.setBounds (row.removeFromLeft (92));
    row.removeFromLeft (16);
    editButton.setBounds (row.removeFromLeft (92));
    row.removeFromLeft (4);
    resetButton.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (4);
    labelsButton.setBounds (row.removeFromLeft (108));
    silenceButton.setBounds (row.removeFromRight (92));
    row.removeFromRight (12);
    timingLabel.setBounds (row);

    auto bottom = area.removeFromBottom (48);
    functionKeyLabel.setBounds (bottom.removeFromTop (22));
    lastKeyLabel.setBounds (bottom);

    area.removeFromTop (8);
    area.removeFromBottom (6);
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

    if (kit != nullptr)
    {
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

    for (std::size_t i = 0; i < seenHitCounts.size(); ++i)
        seenHitCounts[i] = session.getEngine().getHitCount (static_cast<int> (i));
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
        seenHitCounts[i] = session.getEngine().getHitCount (static_cast<int> (i));
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

    // 방금 친 패드를 밝힌다.
    for (std::size_t i = 0; i < seenHitCounts.size(); ++i)
    {
        const auto pad = static_cast<int> (i);
        const auto count = session.getEngine().getHitCount (pad);

        if (count != seenHitCounts[i])
        {
            seenHitCounts[i] = count;
            const auto strength = 0.35f + 0.65f * session.getEngine().getLastVelocity (pad);
            const auto fixedSpot = pad == fixedSpotPad;

            if (fixedSpot)
                fixedSpotPad = -1;

            padGrid.flash (pad, strength);
            kitView.flash (pad, strength, fixedSpot);
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
