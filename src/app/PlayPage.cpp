#include "PlayPage.h"

#include "KitLayout.h"
#include "core/PadKinds.h"

namespace
{

/** 키 옆에 붙여 보여 줄 타격 방식 이름. 단타는 붙이지 않는다. */
juce::String strokeTag (const hvd::Stroke& stroke)
{
    switch (stroke.type)
    {
        case hvd::Stroke::Type::single:     return {};
        case hvd::Stroke::Type::downUp:     return ko ("(뗌)");
        case hvd::Stroke::Type::multi:      return stroke.firstScale >= 1.0f ? ko ("(더블)")
                                                 : stroke.numExtra >= 2 ? ko ("(드래그)") : ko ("(플램)");
        case hvd::Stroke::Type::repeat:     return ko ("(반복)");
        case hvd::Stroke::Type::buzz:       return ko ("(롤)");
        case hvd::Stroke::Type::pattern:    return ko ("(패턴)");
    }

    return {};
}

} // namespace

PlayPage::PlayPage (Session& sessionToUse)
    : session (sessionToUse)
{
    // 클릭은 보통 입력 스레드가 받아 이미 쳤다 (Session::handleMouse). 화면 쪽은 그것과 맞춰 보기만 한다.
    padGrid.onPadClicked = [this] (int pad) { mouse.handleClick (tilesView, pad, false); };
    padGrid.onPadChoked = [this] (int pad) { mouse.handleClick (tilesView, pad, true); };
    kitView.onPadClicked = [this] (int index) { mouse.handleClick (setView, index, false); };
    kitView.onPadChoked = [this] (int index) { mouse.handleClick (setView, index, true); };
    addChildComponent (padGrid);
    addChildComponent (kitView);

    // 배치를 고치면 킷 문서에 적어 두고, 마우스를 놓은 뒤에 한 번만 저장한다.
    kitView.onItemChanged = [this] (int index)
    {
        const auto& item = kitView.getItems()[static_cast<std::size_t> (index)];
        kitlayout::write (session.getKitDocument(), item.id, item.fixed ? kitlayout::fixedViewKey : kitlayout::viewKey,
                          { item.x, item.y, item.size });
        session.markKitEdited();
    };

    // 보기 전환: [타일 | 드럼 세트]
    ui::setupSegmented (*this, viewButtons, juce::StringArray (ko ("타일"), ko ("드럼 세트")), 1, [this] (int view) { setShowingSet (view == 1); });
    ui::setupButton (*this, editButton, ko ("배치 편집"), [this] { setEditing (true); });
    ui::setupButton (*this, silenceButton, ko ("소리 끄기"), [this] { session.allSoundOff(); });

    // 그림 위에 이름과 키를 적을지: 켜고 끄는 설정이라 체크로 둔다.
    ui::setupToggle (*this, labelsToggle, ko ("이름 · 키"), [this]
    {
        kitView.setShowingLabels (labelsToggle.getToggleState());
        session.setSetting ("kitLabels", labelsToggle.getToggleState());
    }, static_cast<bool> (session.getSetting ("kitLabels", true)));

    kitView.setShowingLabels (labelsToggle.getToggleState());

    // 배치 편집 막대. 기본 배치로는 사용자가 맞춘 배치를 지우므로 위험 버튼으로 두고 확인을 받는다.
    ui::setupLabel (*this, editTitle, ko ("배치 편집"), ui::Text::heading);
    editTitle.setColour (juce::Label::textColourId, ui::theme().accent);
    ui::setupLabel (*this, editHint, ko ("끌기 이동 · 휠 크기"), ui::Text::hint);
    ui::setupButton (*this, resetButton, ko ("기본 배치로"), [this] { confirmResetLayout(); });
    ui::setupButton (*this, doneButton, ko ("완료"), [this] { setEditing (false); });
    ui::setButtonKind (resetButton, ui::ButtonKind::danger);
    ui::setButtonKind (doneButton, ui::ButtonKind::primary);

    ui::setupLabel (*this, timingLabel, {}, ui::Text::hint);
    timingLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (trackStrip);

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

    viewButtons[0].setToggleState (! showingSet, juce::dontSendNotification);
    viewButtons[1].setToggleState (showingSet, juce::dontSendNotification);

    if (! showingSet)
        setEditing (false);

    padGrid.setVisible (! showingSet);
    kitView.setVisible (showingSet);
    updateToolbar();
}

void PlayPage::setEditing (bool shouldEdit)
{
    shouldEdit = shouldEdit && showingSet;

    if (shouldEdit == kitView.isEditing())
        return;

    kitView.setEditing (shouldEdit);
    updateToolbar();
}

void PlayPage::updateToolbar()
{
    const auto editing = kitView.isEditing();

    for (auto* component : std::initializer_list<juce::Component*> { &viewButtons[0], &viewButtons[1], &silenceButton })
        component->setVisible (! editing);

    labelsToggle.setVisible (showingSet && ! editing);
    editButton.setVisible (showingSet && ! editing);

    for (auto* component : std::initializer_list<juce::Component*> { &editTitle, &editHint, &resetButton, &doneButton })
        component->setVisible (editing);
}

void PlayPage::resized()
{
    auto area = getLocalBounds();

    // 툴바 한 줄. 평소의 부품과 편집 막대의 부품이 같은 줄을 나눠 쓰고, 한쪽만 보인다.
    const auto row = area.removeFromTop (ui::space::control);

    auto normal = row;
    viewButtons[0].setBounds (normal.removeFromLeft (64));
    viewButtons[1].setBounds (normal.removeFromLeft (92));
    normal.removeFromLeft (ui::space::large);
    labelsToggle.setBounds (normal.removeFromLeft (96));
    silenceButton.setBounds (normal.removeFromRight (92));
    normal.removeFromRight (ui::space::small);
    editButton.setBounds (normal.removeFromRight (92));

    auto edit = row;
    doneButton.setBounds (edit.removeFromRight (80));
    edit.removeFromRight (ui::space::small);
    resetButton.setBounds (edit.removeFromRight (100));
    edit.removeFromRight (ui::space::large);
    editTitle.setBounds (edit.removeFromLeft (ui::textWidth (editTitle) + editTitle.getBorderSize().getLeftAndRight() + ui::space::tight));
    editHint.setBounds (edit);

    // 그림 아래 줄: 왼쪽에 반주, 오른쪽에 박자(메트로놈을 켰을 때만 자리를 차지한다).
    // 그림의 크기가 바뀌지 않도록 줄의 높이는 그대로 둔다.
    auto bottom = area.removeFromBottom (48).removeFromTop (ui::space::control);

    if (timingLabel.getText().isNotEmpty())
    {
        timingLabel.setBounds (bottom.removeFromRight (juce::jmin (300, bottom.getWidth() / 3)));
        bottom.removeFromRight (ui::space::large);
    }

    trackStrip.setBounds (bottom);

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

    shownAs.clear();
    chokeSets.clear();

    if (kit != nullptr)
    {
        for (std::size_t i = 0; i < kit->pads.size(); ++i)
            shownAs.push_back (static_cast<int> (i));

        // 오른쪽 단추로 잡으면 함께 멈출 패드: 같은 악기의 패드와 같은 초크 그룹의 패드 (하이햇은 모두)
        chokeSets = hvd::chokeSetsOf (*kit);

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

            if (const auto view = kitlayout::read (session.getKitDocument(), item.id, kitlayout::viewKey))
            {
                item.x = view->x;
                item.y = view->y;
                item.size = view->size;
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

            if (const auto view = kitlayout::read (session.getKitDocument(), twin.id, kitlayout::fixedViewKey))
            {
                twin.x = view->x;
                twin.y = view->y;
                twin.size = view->size;
            }

            items.push_back (std::move (twin));
            break;
        }
    }

    padGrid.setTiles (std::move (tiles));
    kitView.setItems (std::move (items));

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

void PlayPage::playClick (const MouseRouter::Click& click)
{
    auto pad = click.spot;
    auto followPedals = true;

    if (click.view == setView)
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

void PlayPage::publishMouseTargets()
{
    // 이 화면이 보이고 배치를 고치는 중이 아닐 때만 입력 스레드가 클릭을 받는다.
    const juce::Component& shown = kitView.isVisible() ? static_cast<const juce::Component&> (kitView) : padGrid;
    auto targets = kitView.isEditing() ? nullptr : mouse.begin (shown);

    if (targets != nullptr)
    {
        const auto chokesOf = [this] (int pad)
        {
            return juce::isPositiveAndBelow (pad, static_cast<int> (chokeSets.size())) ? chokeSets[static_cast<std::size_t> (pad)]
                                                                                      : std::vector<std::uint16_t>();
        };

        if (kitView.isVisible())
        {
            for (const auto& area : kitView.getHitAreas())
            {
                const auto& item = kitView.getItems()[static_cast<std::size_t> (area.index)];
                auto target = MouseRouter::makeTarget (kitView, area.bounds, hvd::MouseTarget::Shape::ellipse, item.pad, area.index);
                target.followPedals = ! item.fixed;
                target.chokes = chokesOf (item.pad);
                targets->targets.push_back (std::move (target));
            }
        }
        else
        {
            for (int i = 0; i < padGrid.getNumTiles(); ++i)
            {
                auto target = MouseRouter::makeTarget (padGrid, padGrid.getTileBounds (i).toFloat(), hvd::MouseTarget::Shape::rectangle, i, i);
                target.chokes = chokesOf (i);
                targets->targets.push_back (std::move (target));
            }
        }
    }

    mouse.publish (std::move (targets));
}

void PlayPage::confirmResetLayout()
{
    ui::confirm (this, ko ("기본 배치로"),
                 ko ("모든 패드를 처음 자리로 옮깁니다.\n지금 배치는 킷 화면의 되돌리기로 되살릴 수 있습니다."),
                 ko ("기본 배치로"), [safe = juce::Component::SafePointer<PlayPage> (this)]
                 {
                     if (safe != nullptr)
                         safe->resetLayout();
                 },
                 true);
}

void PlayPage::resetLayout()
{
    kitlayout::clearAll (session.getKitDocument());
    session.saveKit (false);
    rebuild();
}

void PlayPage::updateTimingLabel()
{
    const auto& timing = session.getTiming();
    juce::String text;

    // 박에 맞았는지는 반주 화면의 박자 연습, 연습 기록과 같은 기준(ui::timing::onBeatMs)으로 본다.
    if (session.getEngine().isMetronomeOn() && timing.count > 0)
        text << ko ("박자: ") << ui::formatHitTiming (timing.lastMs) << ui::separator() << ko ("평균 오차 ") << ui::formatMs (timing.averageAbsMs);

    if (text == timingLabel.getText())
        return;

    const auto wasEmpty = timingLabel.getText().isEmpty();
    timingLabel.setText (text, juce::dontSendNotification);

    if (wasEmpty != text.isEmpty())
        resized();
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
    // 배치 편집은 이 화면을 떠나면 끝낸다 (켜 둔 것을 잊고 돌아와 클릭이 안 된다고 느끼지 않게).
    setEditing (false);

    // 다른 화면의 같은 자리를 누를 때 드럼이 울리지 않게 한다.
    mouse.release();
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
    mouse.reconcile();

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

    // 둘 다 움직인 자리만 스스로 다시 그린다 (가려져 있으면 그리지 않는다).
    padGrid.fade();
    kitView.fade();
    trackStrip.update();

    if (++tickCount % 6 == 0)
        updateTimingLabel();
}
