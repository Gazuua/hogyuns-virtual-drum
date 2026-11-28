#include "GamePage.h"

namespace
{

const char* const levelNames[hvd::numChartLevels] = { "가볍게", "보통", "어렵게", "원곡" };

const char* const levelHints[hvd::numChartLevels] = {
    "킥·스네어만, 4분음표",
    "킥·스네어 8분, 하이햇·라이드 4분음표",
    "킥·스네어 16분, 하이햇·라이드 8분음표",
    "받아 적은 그대로",
};

enum Column
{
    titleColumn = 1,
    folderColumn,
    lengthColumn,
    tempoColumn,
    starsColumn,
    bestColumn
};

// 드럼 세트에서의 자리 순서 (킥 제외)
constexpr hvd::Lane kitOrder[] = { hvd::Lane::crash, hvd::Lane::hihat, hvd::Lane::snare, hvd::Lane::tomHigh,
                                   hvd::Lane::tomMid, hvd::Lane::tomFloor, hvd::Lane::ride };

const char* const laneTitles[hvd::numLanes] = { "크래시", "하이햇", "스네어", "하이 탐", "미드 탐", "플로어 탐", "라이드", "킥" };
const char* const laneColourIds[hvd::numLanes] = { "crash", "hihat", "snare", "tom", "tom", "tom", "ride", "kick" };

/** 글자 키의 자판 위 자리: 왼쪽에서 몇 번째인지와 줄의 우선순위(가운데 줄이 먼저). 그 밖의 키는 false. */
bool keyGridPosition (hvd::KeyCode key, float& column, int& rowRank)
{
    struct Row
    {
        hvd::KeyCode first, last;
        int rank;
    };

    constexpr Row keyRows[] = { { 0x1e, 0x28, 0 }, { 0x10, 0x1b, 1 }, { 0x2c, 0x35, 2 }, { 0x02, 0x0d, 3 } };

    for (const auto& row : keyRows)
    {
        if (key >= row.first && key <= row.last)
        {
            column = static_cast<float> (key - row.first);
            rowRank = row.rank;
            return true;
        }
    }

    return false;
}

juce::String starsText (float stars)
{
    return stars < 0.0f ? juce::String::fromUTF8 ("\xe2\x80\x94") : juce::String::fromUTF8 ("\xe2\x98\x85 ") + juce::String (stars, 1);
}

} // namespace

GamePage::GamePage (Session& sessionToUse)
    : session (sessionToUse),
      library (sessionToUse.getCharts()),
      runner (sessionToUse)
{
    highway.setRunner (&runner);
    addChildComponent (highway);

    // 곡 고르기
    ui::setupLabel (*this, folderLabel, {}, ui::Text::caption);
    ui::setupButton (*this, folderButton, ko ("음악 폴더 바꾸기"), [this] { chooseFolder(); });
    ui::setupButton (*this, rescanButton, ko ("새로 고침"), [this] { library.rescan(); });

    searchEditor.setTextToShowWhenEmpty (ko ("곡 이름이나 폴더 이름으로 찾기"), ui::theme().textMuted);
    searchEditor.onTextChange = [this] { rebuildRows(); };
    searchEditor.onReturnKey = [this] { searchEditor.giveAwayKeyboardFocus(); };
    searchEditor.onEscapeKey = [this] { searchEditor.giveAwayKeyboardFocus(); };
    addAndMakeVisible (searchEditor);

    ui::setupToggle (*this, chartedToggle, ko ("채보 있는 곡만"), [this]
    {
        session.setSetting ("gameChartedOnly", chartedToggle.getToggleState());
        rebuildRows();
    });

    chartedToggle.setToggleState (static_cast<bool> (session.getSetting ("gameChartedOnly", false)), juce::dontSendNotification);

    songTable.setModel (this);
    songTable.setRowHeight (24);
    songTable.setWantsKeyboardFocus (false);
    songTable.setMouseClickGrabsKeyboardFocus (false);
    songTable.getHeader().setStretchToFitActive (true);
    songTable.getViewport()->setScrollBarsShown (true, false);

    const auto sortable = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::sortable | juce::TableHeaderComponent::resizable;
    songTable.getHeader().addColumn (ko ("곡"), titleColumn, 300, 120, -1, sortable);
    songTable.getHeader().addColumn (ko ("폴더"), folderColumn, 170, 60, -1, sortable);
    songTable.getHeader().addColumn (ko ("길이"), lengthColumn, 56, 50, 70, sortable);
    songTable.getHeader().addColumn ("BPM", tempoColumn, 52, 46, 70, sortable);
    songTable.getHeader().addColumn (ko ("난이도"), starsColumn, 66, 60, 90, sortable);
    songTable.getHeader().addColumn (ko ("최고 기록"), bestColumn, 96, 80, 130, sortable);
    addAndMakeVisible (songTable);

    ui::setupLabel (*this, songTitleLabel, {}, ui::Text::section);
    ui::setupLabel (*this, songInfoLabel, {}, ui::Text::caption);
    songTitleLabel.setJustificationType (juce::Justification::topLeft);
    songInfoLabel.setJustificationType (juce::Justification::topLeft);

    ui::setupLabel (*this, levelLabel, ko ("단계"), ui::Text::caption);

    for (int level = 0; level < hvd::numChartLevels; ++level)
    {
        auto& button = levelButtons[static_cast<std::size_t> (level)];
        ui::setupButton (*this, button, ko (levelNames[level]), [this, level] { setLevel (level); });
        button.setClickingTogglesState (true);
        button.setRadioGroupId (300);
        ui::setButtonKind (button, ui::ButtonKind::segment);
        button.setConnectedEdges ((level > 0 ? juce::Button::ConnectedOnLeft : 0) | (level < hvd::numChartLevels - 1 ? juce::Button::ConnectedOnRight : 0));
        button.setTooltip (ko (levelHints[level]));
    }

    ui::setupLabel (*this, speedLabel, ko ("곡 속도"), ui::Text::caption);
    ui::setupSlider (*this, speedSlider, 50.0, 100.0, 5.0, 100.0, "%", 0);
    speedSlider.setValue (static_cast<double> (session.getSetting ("gameSpeed", 100.0)), juce::dontSendNotification);
    speedSlider.onDragEnd = [this] { session.setSetting ("gameSpeed", speedSlider.getValue()); };

    ui::setupLabel (*this, scrollLabel, ko ("노트 속도"), ui::Text::caption);
    ui::setupSlider (*this, scrollSlider, 200.0, 1000.0, 20.0, 440.0, {}, 0);
    scrollSlider.setValue (static_cast<double> (session.getSetting ("gameScroll", 440.0)), juce::dontSendNotification);
    scrollSlider.onValueChange = [this] { highway.setScrollSpeed (static_cast<float> (scrollSlider.getValue())); };
    scrollSlider.onDragEnd = [this] { session.setSetting ("gameScroll", scrollSlider.getValue()); };
    highway.setScrollSpeed (static_cast<float> (scrollSlider.getValue()));

    ui::setupLabel (*this, offsetLabel, ko ("판정 보정"), ui::Text::caption);
    ui::setupSlider (*this, offsetSlider, -150.0, 150.0, 1.0, 0.0, " ms", 0);
    offsetSlider.setValue (static_cast<double> (session.getSetting ("gameOffsetMs", 0.0)), juce::dontSendNotification);
    offsetSlider.onDragEnd = [this] { session.setSetting ("gameOffsetMs", offsetSlider.getValue()); };
    offsetSlider.setTooltip (ko ("늘 늦다고 나오면 값을 올리고, 늘 빠르다고 나오면 내립니다. 한 판이 끝난 뒤 \"보정에 반영\"을 눌러도 됩니다."));

    ui::setupToggle (*this, drumlessToggle, ko ("원곡의 드럼을 빼고 틀기"), [this] { session.setSetting ("gameDrumless", drumlessToggle.getToggleState()); });
    drumlessToggle.setToggleState (static_cast<bool> (session.getSetting ("gameDrumless", true)), juce::dontSendNotification);
    drumlessToggle.setTooltip (ko ("드럼을 분리한 곡에서만 됩니다. 내가 친 드럼만 들립니다."));

    ui::setupToggle (*this, relaxedToggle, ko ("비슷한 악기는 바꿔 쳐도 인정"), [this] { session.setSetting ("gameRelaxed", relaxedToggle.getToggleState()); });
    relaxedToggle.setToggleState (static_cast<bool> (session.getSetting ("gameRelaxed", false)), juce::dontSendNotification);
    relaxedToggle.setTooltip (ko ("탐끼리, 심벌끼리는 바꿔 쳐도 맞은 것으로 봅니다."));

    ui::setupToggle (*this, autoplayToggle, ko ("오토 모드 (대신 연주)"), [] {});
    autoplayToggle.setTooltip (ko ("프로그램이 채보대로 정확한 박자에 대신 칩니다. 기록은 남지 않습니다. 연주 도중에도 아래의 \"오토\" 버튼으로 켜고 끌 수 있습니다."));

    ui::setupLabel (*this, orderLabel, ko ("레인 순서"), ui::Text::caption);
    orderCombo.addItem (ko ("키보드에서의 자리 순"), 1);
    orderCombo.addItem (ko ("드럼 세트에서의 자리 순"), 2);
    orderCombo.setSelectedId (session.getSetting ("gameLaneOrder", "keys").toString() == "kit" ? 2 : 1, juce::dontSendNotification);
    ui::setupCombo (*this, orderCombo, [this]
    {
        session.setSetting ("gameLaneOrder", orderCombo.getSelectedId() == 2 ? "kit" : "keys");
        updateLanes();
    });

    ui::setupButton (*this, startButton, ko ("시작"), [this] { startSelected(); });
    ui::setButtonKind (startButton, ui::ButtonKind::primary);

    ui::setupButton (*this, analyseButton, ko ("채보 만들기"), [this]
    {
        if (selectedSong >= 0)
            library.analyse ({ selectedSong }, true);
    });

    ui::setupButton (*this, analyseAllButton, {}, [this]
    {
        std::vector<int> wanted;

        for (const auto songIndex : rows)
            if (! library.getSong (songIndex).hasChart)
                wanted.push_back (songIndex);

        if (wanted.empty())
            return;

        const auto queue = [this, wanted] { library.analyse (wanted, false); };

        // 많은 곡은 오래 걸리므로 한 번 물어본다.
        if (wanted.size() > 20)
            ui::confirm (this, ko ("여러 곡 분석"),
                         juce::String (static_cast<int> (wanted.size())) + ko ("곡의 채보를 만듭니다. 곡당 30초~1분, 중간에 멈출 수 있습니다. 시작할까요?"),
                         queue);
        else
            queue();
    });

    ui::setupButton (*this, cancelButton, ko ("분석 멈춤"), [this]
    {
        pendingStartSong = -1;
        library.cancelAnalysis();
    });

    ui::setupToggle (*this, separatorToggle, ko ("드럼을 분리해서 정밀하게 받아 적기"), [this]
    {
        library.setUseSeparator (separatorToggle.getToggleState());
        session.setSetting ("gameSeparator", separatorToggle.getToggleState());
    });

    separatorToggle.setEnabled (library.isSeparatorInstalled());
    separatorToggle.setToggleState (library.isSeparatorInstalled() && static_cast<bool> (session.getSetting ("gameSeparator", true)), juce::dontSendNotification);
    library.setUseSeparator (separatorToggle.getToggleState());

    if (! library.isSeparatorInstalled())
        separatorToggle.setTooltip (ko ("드럼 분리 도구가 없어 완성곡에서 바로 받아 적습니다 (tools/setup-demucs.ps1)."));

    ui::setupLabel (*this, progressLabel, {}, ui::Text::warning);
    progressLabel.setJustificationType (juce::Justification::topLeft);
    progressLabel.setMinimumHorizontalScale (0.7f);

    // 연주 중
    ui::setupButton (*this, pauseButton, ko ("잠깐 멈춤"), [this]
    {
        runner.setPaused (runner.getState() == GameRunner::State::playing);
        pauseButton.setButtonText (runner.getState() == GameRunner::State::paused ? ko ("계속") : ko ("잠깐 멈춤"));
    });

    ui::setupButton (*this, stopButton, ko ("그만하기"), [this] { endPlay(); });

    // 연주 도중에 오토 모드를 켜고 끈다 (어려운 구간만 프로그램에 맡길 때).
    ui::setupButton (*this, autoButton, ko ("오토"), [this] { runner.setAutoplay (autoButton.getToggleState()); });
    autoButton.setClickingTogglesState (true);
    autoButton.setTooltip (ko ("프로그램이 대신 칩니다. 켠 판은 기록이 남지 않습니다."));
    ui::setupButton (*this, retryButton, ko ("다시 하기"), [this]
    {
        if (playingSong >= 0)
            startSong (playingSong, runner.getSetup().autoplay);
        else
            startDemo (runner.getSetup().autoplay);
    });

    ui::setupButton (*this, backButton, ko ("곡 고르기"), [this] { endPlay(); });
    ui::setupButton (*this, applyOffsetButton, {}, [this]
    {
        const auto mean = runner.getGame().getMeanOffset() * 1000.0;
        offsetSlider.setValue (std::round (offsetSlider.getValue() + mean), juce::dontSendNotification);
        session.setSetting ("gameOffsetMs", offsetSlider.getValue());
        applyOffsetButton.setEnabled (false);
        applyOffsetButton.setButtonText (ko ("보정했습니다"));
    });

    ui::setupLabel (*this, playHintLabel, ko ("Esc: 잠깐 멈춤  ·  노트가 흰 줄에 닿을 때 치기 (킥은 가로 막대)  ·  오토: 대신 연주"), ui::Text::caption);

    ui::setButtonKind (rescanButton, ui::ButtonKind::quiet);
    ui::setButtonKind (stopButton, ui::ButtonKind::danger);

    library.addChangeListener (this);
    session.addChangeListener (this);

    setLevel (juce::jlimit (0, hvd::numChartLevels - 1, static_cast<int> (session.getSetting ("gameLevel", 0))));
    setPlayView (false);
    rebuildRows();
    updateLanes();
}

GamePage::~GamePage()
{
    runner.stop();
    session.removeChangeListener (this);
    library.removeChangeListener (this);
    songTable.setModel (nullptr);
}

//==============================================================================
int GamePage::getLevel() const
{
    for (int level = 0; level < hvd::numChartLevels; ++level)
        if (levelButtons[static_cast<std::size_t> (level)].getToggleState())
            return level;

    return 1;
}

void GamePage::setLevel (int level)
{
    levelButtons[static_cast<std::size_t> (level)].setToggleState (true, juce::dontSendNotification);
    session.setSetting ("gameLevel", level);

    if (sortColumn == starsColumn || sortColumn == bestColumn)
        rebuildRows();

    songTable.repaint();
    updateSelection();
}

void GamePage::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &library)
    {
        rebuildRows();

        // 분석이 끝나기를 기다리던 곡이 있으면 바로 시작한다.
        if (pendingStartSong >= 0 && pendingStartSong < library.getNumSongs())
        {
            const auto& song = library.getSong (pendingStartSong);

            if (song.hasChart)
            {
                const auto songIndex = std::exchange (pendingStartSong, -1);

                if (isShowing() && ! playView)
                    startSong (songIndex, autoplayToggle.getToggleState());
            }
            else if (song.error.isNotEmpty())
            {
                pendingStartSong = -1;
            }
        }
    }
    else
    {
        // 킷이나 키 배열이 바뀌었다: 레인의 키 표시를 다시 만든다.
        runner.refreshPads();
        updateLanes();
    }
}

void GamePage::rebuildRows()
{
    const auto search = searchEditor.getText().trim();
    const auto chartedOnly = chartedToggle.getToggleState();
    const auto level = static_cast<std::size_t> (getLevel());

    rows.clear();

    for (int i = 0; i < library.getNumSongs(); ++i)
    {
        const auto& song = library.getSong (i);

        if (chartedOnly && ! song.hasChart)
            continue;

        if (search.isNotEmpty() && ! song.title.containsIgnoreCase (search) && ! song.folder.containsIgnoreCase (search))
            continue;

        rows.push_back (i);
    }

    if (sortColumn != 0)
    {
        const auto compare = [this, level] (int a, int b)
        {
            const auto& first = library.getSong (a);
            const auto& second = library.getSong (b);

            switch (sortColumn)
            {
                case titleColumn:     return first.title.compareNatural (second.title) < 0;
                case folderColumn:    return first.folder.compareNatural (second.folder) < 0;
                case lengthColumn:    return first.duration < second.duration;
                case tempoColumn:     return first.tempo < second.tempo;
                case starsColumn:     return first.stars[level] < second.stars[level];
                case bestColumn:      return first.bestScore[level] < second.bestScore[level];
                default:              break;
            }

            return false;
        };

        if (sortForwards)
            std::stable_sort (rows.begin(), rows.end(), compare);
        else
            std::stable_sort (rows.begin(), rows.end(), [&compare] (int a, int b) { return compare (b, a); });
    }

    songTable.updateContent();
    songTable.repaint();

    // 고른 곡이 목록에 남아 있으면 그 줄을 다시 고른다.
    const auto found = std::find (rows.begin(), rows.end(), selectedSong);

    if (found != rows.end())
        songTable.selectRow (static_cast<int> (found - rows.begin()), true);
    else
        songTable.deselectAllRows();

    auto missing = 0;

    for (const auto songIndex : rows)
        missing += library.getSong (songIndex).hasChart ? 0 : 1;

    analyseAllButton.setButtonText (ko ("목록에서 채보 없는 ") + juce::String (missing) + ko ("곡 분석"));
    analyseAllButton.setEnabled (missing > 0);

    juce::String folder;
    folder << ko ("음악 폴더: ") << library.getMusicFolder().getFullPathName() << "   ·   " << ko ("곡 ") << library.getNumSongs()
           << (rows.size() != static_cast<std::size_t> (library.getNumSongs()) ? ko (" 가운데 ") + juce::String (static_cast<int> (rows.size())) + ko ("곡 표시") : juce::String());
    folderLabel.setText (folder, juce::dontSendNotification);

    updateSelection();
}

void GamePage::updateSelection()
{
    const auto valid = selectedSong >= 0 && selectedSong < library.getNumSongs();

    if (! valid)
    {
        songTitleLabel.setText (ko ("곡을 고르세요"), juce::dontSendNotification);
        songInfoLabel.setText (ko ("왼쪽에서 곡을 고르세요. 두 번 누르면 바로 시작합니다."), juce::dontSendNotification);
    }

    for (int level = 0; level < hvd::numChartLevels; ++level)
    {
        juce::String text = ko (levelNames[level]);

        if (valid && library.getSong (selectedSong).hasChart)
            text << "\n" << starsText (library.getSong (selectedSong).stars[static_cast<std::size_t> (level)]);

        levelButtons[static_cast<std::size_t> (level)].setButtonText (text);
    }

    startButton.setEnabled (valid);
    analyseButton.setEnabled (valid);
    drumlessToggle.setEnabled (valid && library.getSong (selectedSong).hasBacking);

    if (! valid)
    {
        startButton.setButtonText (ko ("시작"));
        analyseButton.setButtonText (ko ("채보 만들기"));
        return;
    }

    const auto& song = library.getSong (selectedSong);
    const auto level = static_cast<std::size_t> (getLevel());

    songTitleLabel.setText (song.title, juce::dontSendNotification);

    juce::String info;

    if (song.folder.isNotEmpty())
        info << song.folder << "\n";

    if (song.hasChart)
    {
        info << ui::formatTime (song.duration).upToFirstOccurrenceOf (".", false, false) << "  ·  BPM " << juce::roundToInt (song.tempo)
             << "  ·  " << ko ("노트 ") << song.noteCounts[level] << ko ("개") << "\n"
             << (song.precise ? ko ("드럼을 분리해서 받아 적은 채보") : ko ("완성곡에서 바로 받아 적은 채보 (간이)"));

        if (song.bestScore[level] > 0)
            info << "\n" << ko ("최고 기록 ") << song.bestGrade[level] << "  " << song.bestScore[level];
    }
    else if (song.error.isNotEmpty())
    {
        info << ko ("분석 실패: ") << song.error;
    }
    else
    {
        info << ko ("채보가 없습니다. 시작하면 먼저 만듭니다.");
    }

    songInfoLabel.setText (info, juce::dontSendNotification);
    startButton.setButtonText (song.hasChart ? ko ("시작") : ko ("채보 만들고 시작"));
    analyseButton.setButtonText (song.hasChart ? ko ("채보 다시 만들기") : ko ("채보 만들기"));
}

void GamePage::updateLanes()
{
    const auto* kit = session.getKit();
    const auto mapping = session.getMapping();

    std::array<GameHighway::LaneInfo, hvd::numLanes> infos;
    std::array<float, hvd::numLanes> positions {};
    std::array<int, hvd::numLanes> rowRanks {};
    std::array<int, hvd::numLanes> positionCounts {};

    for (int lane = 0; lane < hvd::numLanes; ++lane)
    {
        auto& info = infos[static_cast<std::size_t> (lane)];
        info.lane = static_cast<hvd::Lane> (lane);
        info.name = ko (laneTitles[lane]);
        info.colour = ui::padColour (laneColourIds[lane]);
        rowRanks[static_cast<std::size_t> (lane)] = 9;
    }

    // 탐 셋은 같은 색이라 밝기로 구분한다.
    infos[static_cast<std::size_t> (hvd::Lane::tomHigh)].colour = infos[static_cast<std::size_t> (hvd::Lane::tomHigh)].colour.brighter (0.35f);
    infos[static_cast<std::size_t> (hvd::Lane::tomFloor)].colour = infos[static_cast<std::size_t> (hvd::Lane::tomFloor)].colour.darker (0.25f);

    if (kit != nullptr && mapping != nullptr)
    {
        // 레인마다 그 레인의 패드를 한 번에 치는 키(단타)를 모은다. 대표 패드의 키를 앞에 둔다.
        for (int pass = 0; pass < 2; ++pass)
        {
            for (std::size_t code = 1; code < hvd::numKeyCodes; ++code)
            {
                for (const auto& action : mapping->onPress[code])
                {
                    if (action.type != hvd::KeyAction::Type::hit || action.stroke.type != hvd::Stroke::Type::single)
                        continue;

                    const auto lane = runner.laneOfPad (action.pad);

                    if (lane < 0)
                        continue;

                    const auto preferred = runner.getPreferredPad (static_cast<hvd::Lane> (lane)) == static_cast<int> (action.pad);

                    if (preferred != (pass == 0))
                        continue;

                    const auto index = static_cast<std::size_t> (lane);
                    const auto label = fromUtf8 (hvd::keyLabel (static_cast<hvd::KeyCode> (code)));

                    if (infos[index].keys.size() < 4)
                        infos[index].keys.addIfNotAlreadyThere (label);

                    // 레인의 자리는 대표 패드를 치는 글자 키들의 평균 위치로 정한다.
                    auto column = 0.0f;
                    auto rank = 0;

                    if (pass == 0 && keyGridPosition (static_cast<hvd::KeyCode> (code), column, rank))
                    {
                        positions[index] += column;
                        ++positionCounts[index];
                        rowRanks[index] = juce::jmin (rowRanks[index], rank);
                    }
                }
            }
        }
    }

    std::vector<hvd::Lane> order (std::begin (kitOrder), std::end (kitOrder));

    if (orderCombo.getSelectedId() != 2)
    {
        // 키가 없는 레인은 드럼 세트 순서에서의 자리를 쓴다.
        std::array<float, hvd::numLanes> where {};

        for (std::size_t i = 0; i < order.size(); ++i)
        {
            const auto index = static_cast<std::size_t> (order[i]);
            where[index] = positionCounts[index] > 0 ? positions[index] / static_cast<float> (positionCounts[index])
                                                     : 1.6f * static_cast<float> (i);
        }

        std::stable_sort (order.begin(), order.end(), [&where, &rowRanks] (hvd::Lane a, hvd::Lane b)
        {
            const auto first = static_cast<std::size_t> (a), second = static_cast<std::size_t> (b);
            return where[first] != where[second] ? where[first] < where[second] : rowRanks[first] < rowRanks[second];
        });
    }

    std::vector<GameHighway::LaneInfo> columns;

    for (const auto lane : order)
        columns.push_back (infos[static_cast<std::size_t> (lane)]);

    highway.setLanes (std::move (columns), infos[static_cast<std::size_t> (hvd::Lane::kick)]);
}

void GamePage::updateProgress()
{
    const auto progress = library.getProgress();
    juce::String text;

    if (progress.running || progress.done < progress.total)
    {
        text << ko ("분석 중 ") << juce::jmin (progress.done + 1, progress.total) << " / " << progress.total;

        if (progress.title.isNotEmpty())
            text << "  ·  " << progress.title;

        if (progress.stage.isNotEmpty())
        {
            text << "\n" << progress.stage;

            if (progress.fraction > 0.0f)
                text << " " << juce::roundToInt (progress.fraction * 100.0f) << "%";
        }
    }
    else if (selectedSong >= 0 && selectedSong < library.getNumSongs() && library.getSong (selectedSong).error.isNotEmpty())
    {
        text << library.getSong (selectedSong).error;
    }

    if (text != progressLabel.getText())
        progressLabel.setText (text, juce::dontSendNotification);

    cancelButton.setEnabled (progress.running || progress.done < progress.total);
}

//==============================================================================
void GamePage::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (ui::theme().selection);
    else if (row % 2 == 1)
        g.fillAll (ui::theme().text.withAlpha (0.03f));
}

void GamePage::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (row < 0 || row >= static_cast<int> (rows.size()))
        return;

    const auto& song = library.getSong (rows[static_cast<std::size_t> (row)]);
    const auto level = static_cast<std::size_t> (getLevel());
    juce::String text;
    auto colour = ui::theme().text;
    auto justification = juce::Justification::centredLeft;

    switch (column)
    {
        case titleColumn:
            text = song.title;
            break;

        case folderColumn:
            text = song.folder;
            colour = ui::theme().textMuted;
            break;

        case lengthColumn:
            text = song.hasChart ? ui::formatTime (song.duration).upToFirstOccurrenceOf (".", false, false) : juce::String();
            colour = ui::theme().textMuted;
            justification = juce::Justification::centredRight;
            break;

        case tempoColumn:
            text = song.hasChart && song.tempo > 0.0f ? juce::String (juce::roundToInt (song.tempo)) : juce::String();
            colour = ui::theme().textMuted;
            justification = juce::Justification::centredRight;
            break;

        case starsColumn:
            text = song.hasChart ? starsText (song.stars[level]) : (song.error.isNotEmpty() ? ko ("실패") : juce::String());
            colour = song.hasChart ? (song.precise ? ui::theme().accent : ui::theme().accent.withAlpha (0.6f)) : ui::theme().danger;
            break;

        case bestColumn:
            text = song.bestScore[level] > 0 ? song.bestGrade[level] + "  " + juce::String (song.bestScore[level]) : juce::String();
            colour = ui::theme().good;
            break;

        default:
            break;
    }

    g.setColour (colour);
    g.setFont (juce::FontOptions (ui::type::body - 0.5f));
    g.drawText (text, 6, 0, width - 10, height, justification, true);
}

void GamePage::selectedRowsChanged (int lastRowSelected)
{
    if (lastRowSelected >= 0 && lastRowSelected < static_cast<int> (rows.size()))
        selectedSong = rows[static_cast<std::size_t> (lastRowSelected)];

    updateSelection();
}

void GamePage::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (row >= 0 && row < static_cast<int> (rows.size()))
    {
        selectedSong = rows[static_cast<std::size_t> (row)];
        startSelected();
    }
}

void GamePage::sortOrderChanged (int newSortColumnId, bool isForwards)
{
    sortColumn = newSortColumnId;
    sortForwards = isForwards;
    rebuildRows();
}

void GamePage::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> (ko ("음악 폴더 고르기"), library.getMusicFolder());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fileChooser)
                          {
                              if (const auto folder = fileChooser.getResult(); folder.isDirectory())
                              {
                                  session.setSetting ("musicFolder", folder.getFullPathName());
                                  selectedSong = -1;
                                  library.setMusicFolder (folder);
                              }
                          });
}

//==============================================================================
void GamePage::startSelected()
{
    if (selectedSong < 0 || selectedSong >= library.getNumSongs())
        return;

    if (! library.getSong (selectedSong).hasChart)
    {
        // 채보부터 만든다. 끝나면 changeListenerCallback()이 시작한다.
        pendingStartSong = selectedSong;
        library.analyse ({ selectedSong }, true);
        updateProgress();
        return;
    }

    if (const auto error = startSong (selectedSong, autoplayToggle.getToggleState()); error.isNotEmpty())
        ui::showMessage (ko ("시작하지 못했습니다"), error);
}

juce::String GamePage::startSong (int songIndex, bool autoplay)
{
    if (songIndex < 0 || songIndex >= library.getNumSongs())
        return ko ("곡이 없습니다");

    hvd::Chart chart;

    if (! library.loadChart (songIndex, chart))
        return ko ("채보 파일을 읽지 못했습니다");

    const auto& song = library.getSong (songIndex);
    const auto level = getLevel();

    GameRunner::Setup setup;
    setup.notes = hvd::makeChartVariant (chart, static_cast<hvd::ChartLevel> (level));
    setup.beats = chart.beats;
    setup.firstDownbeat = chart.firstDownbeat;
    setup.duration = chart.duration;
    setup.speed = speedSlider.getValue() / 100.0;
    setup.relaxedLanes = relaxedToggle.getToggleState();
    setup.autoplay = autoplay;
    setup.offsetMs = offsetSlider.getValue();
    setup.song = song.file;
    setup.level = level;

    const auto backing = library.getBackingFile (songIndex);
    const auto drumless = drumlessToggle.getToggleState() && backing != juce::File();

    juce::String detail;
    detail << ko (levelNames[level]) << "  " << starsText (song.stars[static_cast<std::size_t> (level)])
           << "  ·  BPM " << juce::roundToInt (chart.tempo);

    if (setup.speed < 0.999)
        detail << "  ·  " << ko ("속도 ") << juce::roundToInt (setup.speed * 100.0) << "%";

    detail << "\n" << (drumless ? ko ("원곡의 드럼을 뺀 반주") : ko ("원곡 그대로"));

    playingSong = songIndex;
    playingLevel = level;

    juce::MouseCursor::showWaitCursor();
    const auto error = runner.start (drumless ? backing : song.file, std::move (setup));
    juce::MouseCursor::hideWaitCursor();

    if (error.isNotEmpty())
        return error;

    highway.setInfo (song.title, detail);
    highway.reset();
    resultRecorded = false;
    setPlayView (true);
    return {};
}

void GamePage::startDemo (bool autoplay)
{
    // 템포 110, 여덟 마디: 하이햇 8분음표, 킥 1박과 3박, 스네어 2박과 4박, 넷째와 여덟째 마디 끝에 탐 필인.
    GameRunner::Setup setup;
    constexpr double beat = 60.0 / 110.0;

    for (int i = 0; i <= 34; ++i)
        setup.beats.push_back (beat * i);

    const auto add = [&setup] (double seconds, hvd::Lane lane, float velocity, double beatPosition)
    {
        hvd::ChartNote note;
        note.seconds = seconds;
        note.lane = lane;
        note.velocity = velocity;
        note.beat = beatPosition;
        setup.notes.push_back (note);
    };

    for (int bar = 0; bar < 8; ++bar)
    {
        const auto fill = bar == 3 || bar == 7;

        for (int eighth = 0; eighth < 8; ++eighth)
        {
            const auto position = bar * 4.0 + eighth * 0.5;
            const auto time = position * beat;

            if (fill && eighth >= 4)
            {
                const hvd::Lane toms[] = { hvd::Lane::tomHigh, hvd::Lane::tomHigh, hvd::Lane::tomMid, hvd::Lane::tomFloor };
                add (time, toms[eighth - 4], 0.9f, position);
                continue;
            }

            if (eighth == 0 && bar % 4 == 0)
                add (time, hvd::Lane::crash, 0.95f, position);
            else
                add (time, bar >= 4 ? hvd::Lane::ride : hvd::Lane::hihat, eighth % 2 == 0 ? 0.85f : 0.6f, position);

            if (eighth == 0 || eighth == 4 || (eighth == 3 && bar % 2 == 1))
                add (time, hvd::Lane::kick, 0.9f, position);

            if (eighth == 2 || eighth == 6)
                add (time, hvd::Lane::snare, 0.9f, position);
        }
    }

    setup.duration = 34.0 * beat;
    setup.speed = 1.0;
    setup.autoplay = autoplay;
    setup.offsetMs = offsetSlider.getValue();

    playingSong = -1;
    playingLevel = 1;

    if (runner.start (juce::File(), std::move (setup)).isNotEmpty())
        return;

    highway.setInfo (ko ("연습 패턴"), ko ("8비트 록, 템포 110\n곡 없이 메트로놈처럼 진행합니다"));
    highway.reset();
    resultRecorded = false;
    setPlayView (true);
}

void GamePage::endPlay()
{
    runner.stop();
    setPlayView (false);
    rebuildRows();
}

void GamePage::finished()
{
    const auto& game = runner.getGame();
    const auto& setup = runner.getSetup();

    // 오토 모드를 쓴 판과 느리게 튼 판은 기록으로 남기지 않는다.
    if (! resultRecorded && ! runner.wasAssisted() && setup.speed >= 0.999 && playingSong >= 0)
        library.recordResult (playingSong, playingLevel, game.getScore(), game.getGrade());

    resultRecorded = true;

    const auto mean = game.getMeanOffset() * 1000.0;
    const auto useful = ! runner.wasAssisted() && std::abs (mean) >= 8.0 && game.getJudgedCount() - game.getCount (hvd::Judgement::miss) >= 20;

    applyOffsetButton.setEnabled (useful);
    applyOffsetButton.setButtonText (useful ? ko ("보정에 반영 (") + (mean > 0.0 ? "+" : "") + juce::String (juce::roundToInt (mean)) + " ms)"
                                            : ko ("보정할 것 없음"));

    setPlayView (true);
}

void GamePage::setPlayView (bool shouldShowPlay)
{
    playView = shouldShowPlay;
    const auto done = playView && runner.getState() == GameRunner::State::finished;

    juce::Component* const selecting[] = { &folderLabel, &folderButton, &rescanButton, &searchEditor, &chartedToggle, &songTable,
                                           &songTitleLabel, &songInfoLabel, &levelLabel,
                                           &speedLabel, &speedSlider, &scrollLabel, &scrollSlider, &offsetLabel, &offsetSlider,
                                           &drumlessToggle, &relaxedToggle, &autoplayToggle, &orderLabel, &orderCombo, &startButton,
                                           &analyseButton, &analyseAllButton, &cancelButton, &separatorToggle, &progressLabel };

    for (auto* component : selecting)
        component->setVisible (! playView);

    for (auto& button : levelButtons)
        button.setVisible (! playView);

    highway.setVisible (playView);
    playHintLabel.setVisible (playView && ! done);
    pauseButton.setVisible (playView && ! done);
    stopButton.setVisible (playView && ! done);
    autoButton.setVisible (playView && ! done);
    autoButton.setToggleState (runner.isAutoplay(), juce::dontSendNotification);
    retryButton.setVisible (done);
    backButton.setVisible (done);
    applyOffsetButton.setVisible (done);
    pauseButton.setButtonText (ko ("잠깐 멈춤"));

    resized();
    repaint();
}

//==============================================================================
void GamePage::pageShown()
{
    if (! library.hasScanned())
    {
        // 음악 폴더를 처음 훑는다. 곡이 많으면 잠깐 걸린다.
        juce::MouseCursor::showWaitCursor();
        library.rescan();
        juce::MouseCursor::hideWaitCursor();
    }

    runner.refreshPads();
    updateLanes();
}

void GamePage::pageHidden()
{
    if (runner.getState() == GameRunner::State::playing)
    {
        runner.setPaused (true);
        pauseButton.setButtonText (ko ("계속"));
    }
}

bool GamePage::escapePressed()
{
    if (! playView)
        return false;

    switch (runner.getState())
    {
        case GameRunner::State::playing:
        case GameRunner::State::paused:
            pauseButton.triggerClick();
            break;

        case GameRunner::State::finished:
        case GameRunner::State::idle:
            endPlay();
            break;
    }

    return true;
}

void GamePage::tick()
{
    ++tickCount;

    if (! playView)
    {
        if (tickCount % 6 == 0)
            updateProgress();

        return;
    }

    const auto before = runner.getState();
    runner.tick();

    for (const auto& item : runner.takeFeedback())
        highway.addFeedback (item);

    highway.advance();
    highway.repaint();

    if (before != GameRunner::State::finished && runner.getState() == GameRunner::State::finished)
        finished();
}

void GamePage::paint (juce::Graphics& g)
{
    if (playView)
        return;

    ui::fillPanel (g, sidePanel);
}

void GamePage::resized()
{
    auto area = getLocalBounds();

    if (playView)
    {
        auto bottom = area.removeFromBottom (30);
        stopButton.setBounds (bottom.removeFromRight (92));
        bottom.removeFromRight (6);
        pauseButton.setBounds (bottom.removeFromRight (92));
        bottom.removeFromRight (6);
        autoButton.setBounds (bottom.removeFromRight (64));
        bottom.removeFromRight (12);
        playHintLabel.setBounds (bottom);

        area.removeFromBottom (6);
        highway.setBounds (area);

        auto buttons = highway.getResultButtonArea().translated (highway.getX(), highway.getY());
        const auto width = (buttons.getWidth() - 16) / 3;
        retryButton.setBounds (buttons.removeFromLeft (width));
        buttons.removeFromLeft (8);
        backButton.setBounds (buttons.removeFromLeft (width));
        buttons.removeFromLeft (8);
        applyOffsetButton.setBounds (buttons);
        return;
    }

    auto top = area.removeFromTop (28);
    rescanButton.setBounds (top.removeFromRight (84));
    top.removeFromRight (6);
    folderButton.setBounds (top.removeFromRight (128));
    top.removeFromRight (12);
    folderLabel.setBounds (top);
    area.removeFromTop (8);

    // 오른쪽: 고른 곡과 설정
    sidePanel = area.removeFromRight (340);
    area.removeFromRight (12);
    auto side = sidePanel.reduced (14, 12);

    songTitleLabel.setBounds (side.removeFromTop (42));
    songInfoLabel.setBounds (side.removeFromTop (68));
    side.removeFromTop (6);

    auto row = side.removeFromTop (42);
    levelLabel.setBounds (row.removeFromLeft (40));
    const auto levelWidth = row.getWidth() / hvd::numChartLevels;

    for (auto& button : levelButtons)
        button.setBounds (row.removeFromLeft (levelWidth));

    side.removeFromTop (8);

    const auto sliderRow = [&side] (juce::Label& label, juce::Slider& slider)
    {
        auto line = side.removeFromTop (26);
        label.setBounds (line.removeFromLeft (70));
        slider.setBounds (line);
        side.removeFromTop (2);
    };

    sliderRow (speedLabel, speedSlider);
    sliderRow (scrollLabel, scrollSlider);
    sliderRow (offsetLabel, offsetSlider);

    row = side.removeFromTop (26);
    orderLabel.setBounds (row.removeFromLeft (70));
    orderCombo.setBounds (row);
    side.removeFromTop (6);

    drumlessToggle.setBounds (side.removeFromTop (24));
    relaxedToggle.setBounds (side.removeFromTop (24));
    autoplayToggle.setBounds (side.removeFromTop (24));
    side.removeFromTop (8);

    startButton.setBounds (side.removeFromTop (44));
    side.removeFromTop (14);

    row = side.removeFromTop (28);
    analyseButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (6);
    cancelButton.setBounds (row);
    side.removeFromTop (6);
    analyseAllButton.setBounds (side.removeFromTop (28));
    side.removeFromTop (4);
    separatorToggle.setBounds (side.removeFromTop (24));
    side.removeFromTop (4);
    progressLabel.setBounds (side);

    // 왼쪽: 찾기와 곡 목록
    row = area.removeFromTop (26);
    chartedToggle.setBounds (row.removeFromRight (130));
    row.removeFromRight (8);
    searchEditor.setBounds (row);
    area.removeFromTop (6);
    songTable.setBounds (area);
}
