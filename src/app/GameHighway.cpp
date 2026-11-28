#include "GameHighway.h"

#include "Ui.h"

namespace
{

/** 판정의 색. 연주 판(늘 어두움) 위에서는 밝은 색을, 칸(테마의 면) 위에서는 그 바탕에서 읽히는 색을 쓴다. */
juce::Colour judgementColour (hvd::Judgement judgement, bool onField = true)
{
    const auto& t = ui::theme();
    const auto bright = onField || t.dark;

    switch (judgement)
    {
        case hvd::Judgement::perfect:    return bright ? juce::Colour (0xffffd54f) : juce::Colour (0xffa35c00);
        case hvd::Judgement::great:      return bright ? juce::Colour (0xff81c784) : t.good;
        case hvd::Judgement::good:       return bright ? juce::Colour (0xff64b5f6) : t.info;
        case hvd::Judgement::miss:       return bright ? juce::Colour (0xffe57373) : t.danger;
        case hvd::Judgement::pending:    break;
    }

    return onField ? t.fieldText : t.text;
}

juce::String judgementName (hvd::Judgement judgement)
{
    switch (judgement)
    {
        case hvd::Judgement::perfect:    return ko ("퍼펙트");
        case hvd::Judgement::great:      return ko ("그레이트");
        case hvd::Judgement::good:       return ko ("굿");
        case hvd::Judgement::miss:       return ko ("미스");
        case hvd::Judgement::pending:    break;
    }

    return {};
}

} // namespace

GameHighway::GameHighway()
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    setOpaque (false);
}

void GameHighway::setLanes (std::vector<LaneInfo> columns, LaneInfo kick)
{
    lanes = std::move (columns);
    kickLane = std::move (kick);
    repaint();
}

void GameHighway::setInfo (const juce::String& title, const juce::String& detail)
{
    infoTitle = title;
    infoDetail = detail;
}

void GameHighway::reset()
{
    flashes.fill (0.0f);
    popups.fill ({});
    missPopup = {};
    comboPulse = 0.0f;
}

int GameHighway::columnOf (hvd::Lane lane) const noexcept
{
    for (std::size_t i = 0; i < lanes.size(); ++i)
        if (lanes[i].lane == lane)
            return static_cast<int> (i);

    return -1;
}

void GameHighway::addFeedback (const GameRunner::Feedback& item)
{
    if (item.judgement == hvd::Judgement::miss)
    {
        missPopup = { item.judgement, 0.0f, 0.0f };
        return;
    }

    const auto index = static_cast<std::size_t> (item.lane);
    flashes[index] = juce::jmax (flashes[index], 0.45f + 0.55f * item.velocity);

    if (item.judgement != hvd::Judgement::pending)
    {
        popups[index] = { item.judgement, item.offsetMs, 0.0f };
        comboPulse = 1.0f;
    }
}

void GameHighway::advance()
{
    for (auto& flash : flashes)
        flash = flash < 0.02f ? 0.0f : flash * 0.86f;

    for (auto& popup : popups)
        popup.age = juce::jmin (1.0f, popup.age + 1.0f / 30.0f);

    missPopup.age = juce::jmin (1.0f, missPopup.age + 1.0f / 30.0f);
    comboPulse *= 0.85f;
}

GameHighway::Layout GameHighway::getLayout() const
{
    Layout layout;
    auto area = getLocalBounds().toFloat();

    layout.hud = area.removeFromRight (juce::jmin (270.0f, area.getWidth() * 0.3f));
    area.removeFromRight (12.0f);
    layout.field = area;

    const auto numColumns = juce::jmax (1, static_cast<int> (lanes.size()));
    const auto width = juce::jmin (area.getWidth() - 36.0f, 104.0f * static_cast<float> (numColumns));
    constexpr float headerHeight = 62.0f;

    layout.top = area.getY() + 10.0f;
    layout.hitY = area.getBottom() - headerHeight - 16.0f;
    layout.lanes = juce::Rectangle<float> (area.getCentreX() - width * 0.5f, layout.top, width, area.getBottom() - layout.top - 8.0f);
    layout.laneWidth = width / static_cast<float> (numColumns);
    return layout;
}

juce::Rectangle<int> GameHighway::getResultButtonArea() const
{
    const auto layout = getLayout();
    return juce::Rectangle<float> (420.0f, 34.0f).withCentre ({ layout.field.getCentreX(), layout.field.getCentreY() + 150.0f }).toNearestInt();
}

void GameHighway::paint (juce::Graphics& g)
{
    const auto layout = getLayout();
    paintField (g, layout);
    paintHud (g, layout);

    if (runner != nullptr && runner->getState() == GameRunner::State::finished)
        paintResult (g, layout);
}

void GameHighway::paintField (juce::Graphics& g, const Layout& layout)
{
    const auto& t = ui::theme();
    const auto fieldText = t.fieldText;

    g.setColour (t.field);
    g.fillRoundedRectangle (layout.field, t.panelRadius);

    const auto lanesArea = layout.lanes;
    const auto numColumns = static_cast<int> (lanes.size());
    const auto hitY = layout.hitY;

    // 레인 바탕: 하나 건너 조금 밝게, 판정선 쪽으로 갈수록 밝아진다.
    for (int column = 0; column < numColumns; ++column)
    {
        const auto x = lanesArea.getX() + layout.laneWidth * static_cast<float> (column);
        const auto lane = juce::Rectangle<float> (x, layout.top, layout.laneWidth, hitY - layout.top + 8.0f);
        const auto base = lanes[static_cast<std::size_t> (column)].colour;

        g.setGradientFill (juce::ColourGradient (base.withAlpha (0.02f), x, layout.top, base.withAlpha (column % 2 == 0 ? 0.11f : 0.07f), x, hitY, false));
        g.fillRect (lane);

        // 방금 친 레인은 판정선에서 위로 빛이 올라온다.
        if (const auto flash = flashes[static_cast<std::size_t> (lanes[static_cast<std::size_t> (column)].lane)]; flash > 0.0f)
        {
            const auto glow = juce::Rectangle<float> (x, hitY - 150.0f, layout.laneWidth, 150.0f);
            g.setGradientFill (juce::ColourGradient (base.withAlpha (0.0f), x, glow.getY(), base.withAlpha (0.55f * flash), x, hitY, false));
            g.fillRect (glow);
        }
    }

    g.setColour (t.fieldLine);

    for (int column = 0; column <= numColumns; ++column)
        g.fillRect (lanesArea.getX() + layout.laneWidth * static_cast<float> (column) - 0.5f, layout.top, 1.0f, hitY - layout.top + 8.0f);

    if (runner == nullptr || runner->getState() == GameRunner::State::idle)
    {
        g.setColour (fieldText.withAlpha (0.6f));
        g.setFont (juce::FontOptions (16.0f));
        g.drawText (ko ("곡을 고르고 시작을 누르세요"), layout.field.toNearestInt(), juce::Justification::centred);
        return;
    }

    const auto& setup = runner->getSetup();
    const auto& game = runner->getGame();
    const auto now = runner->getSongSeconds();
    const auto speed = setup.speed;
    const auto yOf = [hitY, now, speed, this] (double seconds) { return hitY - static_cast<float> ((seconds - now) / speed) * scrollSpeed; };
    const auto visibleAhead = static_cast<double> ((hitY - layout.top) / scrollSpeed) * speed;
    const auto visibleBehind = 0.35 * speed;

    g.saveState();
    g.reduceClipRegion (juce::Rectangle<float> (lanesArea.getX() - 14.0f, layout.top, lanesArea.getWidth() + 28.0f, lanesArea.getBottom() - layout.top).toNearestInt());

    // 박 줄. 마디의 첫 박은 밝게 그린다.
    {
        const auto& beats = setup.beats;
        const auto first = std::lower_bound (beats.begin(), beats.end(), now - visibleBehind);

        for (auto beat = first; beat != beats.end() && *beat < now + visibleAhead; ++beat)
        {
            const auto number = static_cast<int> (beat - beats.begin()) - setup.firstDownbeat;
            const auto barStart = number % 4 == 0;
            const auto y = yOf (*beat);

            if (y > hitY + 4.0f)
                continue;

            g.setColour (fieldText.withAlpha (barStart ? 0.22f : 0.07f));
            g.fillRect (lanesArea.getX(), y - (barStart ? 1.0f : 0.5f), lanesArea.getWidth(), barStart ? 2.0f : 1.0f);
        }
    }

    // 노트. 킥은 레인 전체를 가로지르는 막대라서 먼저 그리고, 나머지를 그 위에 그린다.
    const auto& notes = game.getNotes();
    const auto firstVisible = static_cast<std::size_t> (std::lower_bound (notes.begin(), notes.end(), now - visibleBehind,
                                                                         [] (const hvd::ChartNote& note, double time) { return note.seconds < time; })
                                                        - notes.begin());

    for (int pass = 0; pass < 2; ++pass)
    {
        for (auto i = firstVisible; i < notes.size() && notes[i].seconds < now + visibleAhead; ++i)
        {
            const auto& note = notes[i];
            const auto isKick = note.lane == hvd::Lane::kick;

            if (isKick != (pass == 0))
                continue;

            const auto result = game.getResult (i);

            if (result != hvd::Judgement::pending && result != hvd::Judgement::miss)
                continue;    // 맞힌 노트는 사라진다

            const auto y = yOf (note.seconds);
            const auto missed = result == hvd::Judgement::miss;
            const auto alpha = missed ? 0.25f : 0.55f + 0.45f * note.velocity;

            if (isKick)
            {
                const auto bar = juce::Rectangle<float> (lanesArea.getX() - 8.0f, y - 4.0f, lanesArea.getWidth() + 16.0f, 8.0f);
                g.setColour (kickLane.colour.withAlpha (alpha));
                g.fillRoundedRectangle (bar, 4.0f);
                g.setColour (fieldText.withAlpha (missed ? 0.05f : 0.35f));
                g.fillRoundedRectangle (bar.withHeight (2.0f), 1.0f);
                continue;
            }

            const auto column = columnOf (note.lane);

            if (column < 0)
                continue;

            const auto colour = lanes[static_cast<std::size_t> (column)].colour;
            const auto box = juce::Rectangle<float> (lanesArea.getX() + layout.laneWidth * static_cast<float> (column) + 6.0f, y - 7.0f,
                                                     layout.laneWidth - 12.0f, 14.0f);

            if ((note.flags & hvd::ChartNote::openHat) != 0)
            {
                // 열린 하이햇: 속이 빈 노트
                g.setColour (colour.withAlpha (alpha));
                g.drawRoundedRectangle (box.reduced (1.5f), 5.0f, 3.0f);
            }
            else
            {
                g.setColour (colour.withAlpha (alpha));
                g.fillRoundedRectangle (box, 5.0f);
                g.setColour (fieldText.withAlpha (missed ? 0.05f : 0.4f));
                g.fillRoundedRectangle (box.reduced (4.0f, 0.0f).withHeight (3.0f).translated (0.0f, 2.0f), 1.5f);
            }
        }
    }

    g.restoreState();

    // 판정선과 레인마다의 과녁. 맞히면 과녁이 밝아지며 바깥으로 퍼진다.
    g.setColour (fieldText.withAlpha (0.85f));
    g.fillRoundedRectangle (lanesArea.getX() - 10.0f, hitY - 1.5f, lanesArea.getWidth() + 20.0f, 3.0f, 1.5f);

    for (int column = 0; column < numColumns; ++column)
    {
        const auto& lane = lanes[static_cast<std::size_t> (column)];
        const auto flash = flashes[static_cast<std::size_t> (lane.lane)];
        const auto& popup = popups[static_cast<std::size_t> (lane.lane)];
        const auto target = juce::Rectangle<float> (lanesArea.getX() + layout.laneWidth * static_cast<float> (column) + 6.0f, hitY - 8.0f,
                                                    layout.laneWidth - 12.0f, 16.0f);

        g.setColour (lane.colour.withAlpha (0.25f + 0.55f * flash));
        g.fillRoundedRectangle (target, 6.0f);
        g.setColour (lane.colour.brighter (0.4f).withAlpha (0.75f));
        g.drawRoundedRectangle (target, 6.0f, 1.5f);

        if (popup.age < 0.5f && popup.judgement != hvd::Judgement::pending)
        {
            const auto grow = 14.0f * popup.age;
            g.setColour (judgementColour (popup.judgement).withAlpha (0.9f * (1.0f - popup.age * 2.0f)));
            g.drawRoundedRectangle (target.expanded (grow, grow * 0.8f), 6.0f + grow * 0.5f, 2.0f);
        }
    }

    if (const auto kickFlash = flashes[static_cast<std::size_t> (hvd::Lane::kick)]; kickFlash > 0.0f)
    {
        g.setColour (kickLane.colour.withAlpha (0.8f * kickFlash));
        g.fillRoundedRectangle (lanesArea.getX() - 10.0f, hitY - 4.0f, lanesArea.getWidth() + 20.0f, 8.0f, 4.0f);
    }

    // 레인 이름과 키
    const auto headerTop = hitY + 10.0f;

    for (int column = 0; column < numColumns; ++column)
    {
        const auto& lane = lanes[static_cast<std::size_t> (column)];
        const auto flash = flashes[static_cast<std::size_t> (lane.lane)];
        auto box = juce::Rectangle<float> (lanesArea.getX() + layout.laneWidth * static_cast<float> (column) + 3.0f, headerTop,
                                           layout.laneWidth - 6.0f, 40.0f);

        g.setColour (lane.colour.withAlpha (0.16f + 0.5f * flash));
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (lane.colour.withAlpha (0.8f));
        g.drawRoundedRectangle (box.reduced (0.5f), 6.0f, 1.0f);

        g.setColour (fieldText);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (lane.name, box.removeFromTop (20.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

        g.setColour (t.dark ? t.accent : t.accent.brighter (0.6f));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (lane.keys.isEmpty() ? ko ("키 없음") : lane.keys.joinIntoString ("  "), box.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }

    // 킥은 레인 아래에 한 줄로 적는다.
    {
        const auto box = juce::Rectangle<float> (lanesArea.getX() + 3.0f, headerTop + 44.0f, lanesArea.getWidth() - 6.0f, 18.0f);
        const auto flash = flashes[static_cast<std::size_t> (hvd::Lane::kick)];

        g.setColour (kickLane.colour.withAlpha (0.16f + 0.5f * flash));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (fieldText);
        g.setFont (juce::FontOptions (ui::type::caption, juce::Font::bold));

        juce::String text;
        text << kickLane.name << "   " << (kickLane.keys.isEmpty() ? ko ("키 없음") : kickLane.keys.joinIntoString ("  "));
        g.drawFittedText (text, box.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }

    // 레인마다 방금 받은 판정
    for (int column = 0; column < numColumns; ++column)
    {
        const auto& popup = popups[static_cast<std::size_t> (lanes[static_cast<std::size_t> (column)].lane)];

        if (popup.age >= 1.0f || popup.judgement == hvd::Judgement::pending)
            continue;

        const auto x = lanesArea.getX() + layout.laneWidth * static_cast<float> (column);
        const auto box = juce::Rectangle<float> (x - 10.0f, hitY - 46.0f - 18.0f * popup.age, layout.laneWidth + 20.0f, 20.0f);

        g.setColour (judgementColour (popup.judgement).withAlpha (1.0f - popup.age * popup.age));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (judgementName (popup.judgement), box.toNearestInt(), juce::Justification::centred);

        if (popup.judgement != hvd::Judgement::perfect)
        {
            g.setFont (juce::FontOptions (ui::type::caption));
            g.drawText (popup.offsetMs < 0.0f ? ko ("빠름") : ko ("늦음"), box.translated (0.0f, 16.0f).toNearestInt(), juce::Justification::centred);
        }
    }

    // 킥의 판정과 놓친 표시는 가운데에
    const auto centre = juce::Rectangle<float> (lanesArea.getX(), hitY - 96.0f, lanesArea.getWidth(), 22.0f);

    if (const auto& popup = popups[static_cast<std::size_t> (hvd::Lane::kick)]; popup.age < 1.0f && popup.judgement != hvd::Judgement::pending)
    {
        g.setColour (judgementColour (popup.judgement).withAlpha (0.9f * (1.0f - popup.age * popup.age)));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (ko ("킥 ") + judgementName (popup.judgement), centre.translated (0.0f, -12.0f * popup.age).toNearestInt(), juce::Justification::centred);
    }

    if (missPopup.age < 1.0f)
    {
        g.setColour (judgementColour (hvd::Judgement::miss).withAlpha (1.0f - missPopup.age));
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (judgementName (hvd::Judgement::miss), centre.translated (0.0f, -26.0f).toNearestInt(), juce::Justification::centred);
    }

    // 콤보
    if (const auto combo = game.getCombo(); combo >= 5)
    {
        const auto size = 54.0f + 10.0f * comboPulse;
        const auto box = juce::Rectangle<float> (lanesArea.getX(), layout.top + 70.0f, lanesArea.getWidth(), 70.0f);

        g.setColour (fieldText.withAlpha (0.20f + 0.25f * comboPulse));
        g.setFont (juce::FontOptions (size, juce::Font::bold));
        g.drawText (juce::String (combo), box.toNearestInt(), juce::Justification::centred);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (ko ("콤보"), box.translated (0.0f, 52.0f).withHeight (20.0f).toNearestInt(), juce::Justification::centred);
    }

    // 준비 시간에는 남은 시간을 세고, 그동안에만 치는 법을 알린다.
    if (now < 0.0 && runner->getState() == GameRunner::State::playing)
    {
        const auto countdown = layout.field.withTrimmedBottom (160.0f);
        g.setColour (fieldText.withAlpha (0.85f));
        g.setFont (juce::FontOptions (64.0f, juce::Font::bold));
        g.drawText (juce::String (static_cast<int> (std::ceil (-now / speed))), countdown.toNearestInt(), juce::Justification::centred);

        g.setColour (fieldText.withAlpha (0.75f));
        g.setFont (juce::FontOptions (ui::type::body));
        g.drawText (ko ("노트가 흰 줄에 닿을 때 치기  ·  킥은 가로 막대  ·  Esc 멈춤"),
                    juce::Rectangle<float> (countdown.getX(), countdown.getCentreY() + 48.0f, countdown.getWidth(), 22.0f).toNearestInt(),
                    juce::Justification::centred);
    }

    if (runner->getState() == GameRunner::State::paused)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (layout.field, t.panelRadius);
        g.setColour (fieldText);
        g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
        g.drawText (ko ("멈춤"), layout.field.toNearestInt(), juce::Justification::centred);
        g.setFont (juce::FontOptions (14.0f));
        g.setColour (fieldText.withAlpha (0.7f));
        g.drawText (ko ("Esc 계속"), layout.field.translated (0.0f, 34.0f).toNearestInt(), juce::Justification::centred);
    }

    // 곡의 진행 정도
    if (setup.duration > 0.0)
    {
        const auto bar = juce::Rectangle<float> (layout.field.getX() + 10.0f, layout.field.getY() + 4.0f, layout.field.getWidth() - 20.0f, 3.0f);
        g.setColour (fieldText.withAlpha (0.12f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (t.dark ? t.accent : t.accent.brighter (0.6f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * static_cast<float> (juce::jlimit (0.0, 1.0, now / setup.duration))), 1.5f);
    }
}

void GameHighway::paintHud (juce::Graphics& g, const Layout& layout)
{
    g.setColour (ui::theme().surface);
    g.fillRoundedRectangle (layout.hud, ui::theme().panelRadius);

    auto area = layout.hud.reduced (16.0f, 14.0f).toNearestInt();

    g.setColour (ui::theme().text);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawFittedText (infoTitle, area.removeFromTop (44), juce::Justification::topLeft, 2, 0.8f);

    g.setColour (ui::theme().textMuted);
    g.setFont (juce::FontOptions (ui::type::caption));
    g.drawFittedText (infoDetail, area.removeFromTop (38), juce::Justification::topLeft, 2, 0.8f);
    area.removeFromTop (10);

    if (runner == nullptr || runner->getState() == GameRunner::State::idle)
        return;

    const auto& game = runner->getGame();
    const auto& setup = runner->getSetup();

    const auto label = [&g, &area] (const juce::String& name, const juce::String& value, float valueSize, juce::Colour colour)
    {
        g.setColour (ui::theme().textMuted);
        g.setFont (juce::FontOptions (ui::type::caption));
        g.drawText (name, area.removeFromTop (16), juce::Justification::left);
        g.setColour (colour);
        g.setFont (juce::FontOptions (valueSize, juce::Font::bold));
        g.drawText (value, area.removeFromTop (juce::roundToInt (valueSize * 1.25f)), juce::Justification::left);
        area.removeFromTop (8);
    };

    label (ko ("점수"), ui::withCommas (game.getScore()), 30.0f, ui::theme().text);
    label (ko ("콤보"), juce::String (game.getCombo()) + ko ("  (최고 ") + juce::String (game.getMaxCombo()) + ")", 20.0f, ui::theme().accent);
    label (ko ("정확도"), juce::String (game.getAccuracy() * 100.0, 1) + "%", 20.0f, ui::theme().text);

    // 판정 수
    const hvd::Judgement kinds[] = { hvd::Judgement::perfect, hvd::Judgement::great, hvd::Judgement::good, hvd::Judgement::miss };

    for (const auto kind : kinds)
    {
        auto row = area.removeFromTop (22);
        g.setColour (judgementColour (kind, false));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (judgementName (kind), row.removeFromLeft (96), juce::Justification::left);
        g.setColour (ui::theme().text);
        g.drawText (juce::String (game.getCount (kind)), row, juce::Justification::left);
    }

    area.removeFromTop (10);

    // 빠르게 치는지 늦게 치는지
    if (game.getJudgedCount() > 0 && ! runner->wasAssisted())
    {
        const auto mean = game.getMeanOffset() * 1000.0;
        const auto text = ko ("평균 ") + ui::formatLean (static_cast<float> (mean));

        g.setColour (ui::theme().textMuted);
        g.setFont (juce::FontOptions (ui::type::caption));
        g.drawText (text, area.removeFromTop (18), juce::Justification::left);
    }

    g.setColour (ui::theme().textMuted);
    g.setFont (juce::FontOptions (ui::type::caption));

    juce::String time;
    time << ui::formatTime (runner->getSongSeconds()) << " / " << ui::formatTime (setup.duration);
    g.drawText (time, area.removeFromTop (18), juce::Justification::left);

    if (runner->wasAssisted())
    {
        g.setColour (ui::theme().warning);
        g.drawFittedText (ko ("오토  ·  기록 안 남음"), area.removeFromTop (20), juce::Justification::topLeft, 1);
    }
}

void GameHighway::paintResult (juce::Graphics& g, const Layout& layout)
{
    const auto& game = runner->getGame();

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (layout.field, ui::theme().panelRadius);

    const auto panel = juce::Rectangle<float> (460.0f, 390.0f).withCentre (layout.field.getCentre());
    g.setColour (ui::theme().surface);
    g.fillRoundedRectangle (panel, ui::theme().panelRadius + 2.0f);
    g.setColour (ui::theme().border);
    g.drawRoundedRectangle (panel.reduced (0.5f), ui::theme().panelRadius + 2.0f, 1.0f);

    auto area = panel.reduced (24.0f, 18.0f).toNearestInt();

    g.setColour (ui::theme().textMuted);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (runner->wasAssisted() ? ko ("결과  ·  오토") : ko ("결과"), area.removeFromTop (20), juce::Justification::centred);

    const auto grade = game.getGrade();
    g.setColour (grade == 'S' ? judgementColour (hvd::Judgement::perfect, false)
                                : (grade == 'A' ? judgementColour (hvd::Judgement::great, false)
                                                : (grade == 'B' ? judgementColour (hvd::Judgement::good, false) : ui::theme().text)));
    g.setFont (juce::FontOptions (92.0f, juce::Font::bold));
    g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> (grade)), area.removeFromTop (104), juce::Justification::centred);

    g.setColour (ui::theme().text);
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText (ui::withCommas (game.getScore()), area.removeFromTop (38), juce::Justification::centred);

    const auto total = juce::jmax (1, static_cast<int> (game.getNotes().size()));
    const auto accuracy = (game.getCount (hvd::Judgement::perfect) + 0.7 * game.getCount (hvd::Judgement::great) + 0.4 * game.getCount (hvd::Judgement::good)) / total;

    juce::String line;
    line << ko ("정확도 ") << juce::String (accuracy * 100.0, 1) << "%" << ui::separator() << ko ("최고 콤보 ") << game.getMaxCombo() << " / " << total;
    g.setFont (juce::FontOptions (15.0f));
    g.drawText (line, area.removeFromTop (26), juce::Justification::centred);

    area.removeFromTop (6);
    auto counts = area.removeFromTop (24);
    const hvd::Judgement kinds[] = { hvd::Judgement::perfect, hvd::Judgement::great, hvd::Judgement::good, hvd::Judgement::miss };
    const auto cell = counts.getWidth() / 4;

    for (const auto kind : kinds)
    {
        g.setColour (judgementColour (kind, false));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (judgementName (kind) + " " + juce::String (game.getCount (kind)), counts.removeFromLeft (cell), juce::Justification::centred);
    }

    area.removeFromTop (8);

    if (! runner->wasAssisted() && game.getJudgedCount() > game.getCount (hvd::Judgement::miss))
    {
        const auto mean = game.getMeanOffset() * 1000.0;
        juce::String timing;
        timing << ko ("맞힌 타격 평균 ") << ui::formatLean (static_cast<float> (mean))
               << ui::separator() << ko ("빠름 ") << game.getEarlyCount() << ui::separator() << ko ("늦음 ") << game.getLateCount();

        g.setColour (ui::theme().textMuted);
        g.setFont (juce::FontOptions (ui::type::caption));
        g.drawText (timing, area.removeFromTop (20), juce::Justification::centred);
    }
}
