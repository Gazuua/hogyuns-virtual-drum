#include "GamePage.h"

#include "core/PadKinds.h"

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

// 채보 메뉴의 항목
enum ChartMenuItem
{
    analyseThisItem = 1,
    analyseAllItem,
    separatorItem,
    cancelItem
};

// 드럼 세트에서의 자리 순서 (킥 제외)
constexpr hvd::Lane kitOrder[] = { hvd::Lane::crash, hvd::Lane::hihat, hvd::Lane::snare, hvd::Lane::tomHigh,
                                   hvd::Lane::tomMid, hvd::Lane::tomFloor, hvd::Lane::ride };

const char* const laneTitles[hvd::numLanes] = { "크래시", "하이햇", "스네어", "하이 탐", "미드 탐", "플로어 탐", "라이드", "킥" };
constexpr hvd::Instrument laneInstruments[hvd::numLanes] = { hvd::Instrument::crash, hvd::Instrument::hihat, hvd::Instrument::snare,
                                                             hvd::Instrument::tom, hvd::Instrument::tom, hvd::Instrument::tom,
                                                             hvd::Instrument::ride, hvd::Instrument::kick };

// 옆 칸의 항목 이름 폭
constexpr int sideCaptionWidth = 70;

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

} // namespace

GamePage::GamePage (Session& sessionToUse)
    : session (sessionToUse),
      library (sessionToUse.getCharts()),
      runner (sessionToUse)
{
    highway.setRunner (&runner);
    addChildComponent (highway);

    // 위 줄: 음악 폴더(이름만, 전체 경로는 툴팁) · [채보] [폴더 바꾸기] [새로 고침]
    ui::setupLabel (*this, folderLabel, {}, ui::Text::caption);
    ui::setupButton (*this, folderButton, ko ("폴더 바꾸기"), [this] { chooseFolder(); });
    folderButton.setTooltip (ko ("곡을 찾을 음악 폴더 바꾸기"));
    rescanButton.onClick = [this] { library.rescan(); };
    addAndMakeVisible (rescanButton);

    ui::setupButton (*this, chartButton, ko ("채보"), [this] { showChartMenu(); });
    chartButton.setTooltip (ko ("채보 만들기, 드럼 분리, 분석 멈춤"));

    // 분석하는 동안 목록 위에 진행 줄
    ui::setupLabel (*this, progressLabel, {}, ui::Text::caption);
    progressLabel.setMinimumHorizontalScale (0.7f);

    searchEditor.setTextToShowWhenEmpty (ko ("곡 이름이나 폴더 이름으로 찾기"), ui::theme().textMuted);
    searchEditor.onTextChange = [this] { rebuildRows(); };
    searchEditor.onReturnKey = [this] { searchEditor.giveAwayKeyboardFocus(); };
    searchEditor.onEscapeKey = [this] { searchEditor.giveAwayKeyboardFocus(); };
    addAndMakeVisible (searchEditor);

    ui::setupToggle (*this, chartedToggle, ko ("채보 있는 곡만"), [this]
    {
        session.setSetting ("gameChartedOnly", chartedToggle.getToggleState());
        rebuildRows();
    }, static_cast<bool> (session.getSetting ("gameChartedOnly", false)));

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

    // 옆 칸: 고른 곡, 단계, 곡 속도, 오토, [시작]
    ui::setupLabel (*this, songTitleLabel, {}, ui::Text::section);
    ui::setupLabel (*this, songInfoLabel, {}, ui::Text::caption);
    songTitleLabel.setJustificationType (juce::Justification::topLeft);
    songInfoLabel.setJustificationType (juce::Justification::topLeft);

    ui::setupLabel (*this, levelLabel, ko ("단계"), ui::Text::caption);

    juce::StringArray levelTexts;

    for (const auto* name : levelNames)
        levelTexts.add (ko (name));

    ui::setupSegmented (*this, levelButtons, levelTexts, 300, [this] (int level) { setLevel (level); });

    for (int level = 0; level < hvd::numChartLevels; ++level)
        levelButtons[static_cast<std::size_t> (level)].setTooltip (ko (levelHints[level]));

    // 슬라이더 값은 마우스를 놓은 뒤(값 상자에 적었으면 바로) 설정에 적는다.
    sliderSaver.save = [this]
    {
        session.setSetting ("gameSpeed", speedSlider.getValue());
        session.setSetting ("gameScroll", scrollSlider.getValue());
        session.setSetting ("gameOffsetMs", offsetSlider.getValue());
    };

    ui::setupLabel (*this, speedLabel, ko ("곡 속도"), ui::Text::caption);
    ui::setupSlider (*this, speedSlider, 50.0, 100.0, 5.0, 100.0, "%", 0);
    speedSlider.setValue (static_cast<double> (session.getSetting ("gameSpeed", 100.0)), juce::dontSendNotification);
    speedSlider.setTooltip (ko ("100% 아래로 친 판은 기록이 남지 않는다"));
    speedSlider.onValueChange = [this] { sliderSaver.mark(); };

    ui::setupToggle (*this, autoplayToggle, ko ("오토"), [] {});
    autoplayToggle.setTooltip (ko ("프로그램이 채보대로 대신 친다. 기록은 남지 않는다. 연주 도중에도 켜고 끌 수 있다."));

    ui::setupButton (*this, startButton, ko ("시작"), [this] { startSelected(); });
    ui::setButtonKind (startButton, ui::ButtonKind::primary);

    // 접어 두는 설정: 노트 속도, 판정 보정, 레인 순서, 드럼 빼고 틀기, 비슷한 악기 인정
    ui::setupButton (*this, settingsButton, ko ("설정"), [this] { setSettingsOpen (! settingsOpen); });
    ui::setButtonKind (settingsButton, ui::ButtonKind::quiet);

    ui::setupLabel (*this, scrollLabel, ko ("노트 속도"), ui::Text::caption);
    ui::setupSlider (*this, scrollSlider, 200.0, 1000.0, 20.0, 440.0, {}, 0);
    scrollSlider.setValue (static_cast<double> (session.getSetting ("gameScroll", 440.0)), juce::dontSendNotification);
    scrollSlider.onValueChange = [this]
    {
        highway.setScrollSpeed (static_cast<float> (scrollSlider.getValue()));
        sliderSaver.mark();
    };
    highway.setScrollSpeed (static_cast<float> (scrollSlider.getValue()));

    ui::setupLabel (*this, offsetLabel, ko ("판정 보정"), ui::Text::caption);
    ui::setupSlider (*this, offsetSlider, -150.0, 150.0, 1.0, 0.0, "ms", 0);
    offsetSlider.setValue (static_cast<double> (session.getSetting ("gameOffsetMs", 0.0)), juce::dontSendNotification);
    offsetSlider.onValueChange = [this] { sliderSaver.mark(); };
    offsetSlider.setTooltip (ko ("늘 늦다고 나오면 올리고, 늘 빠르다고 나오면 내린다. 한 판이 끝난 뒤 \"보정에 반영\"을 눌러도 된다."));

    ui::setupLabel (*this, orderLabel, ko ("레인 순서"), ui::Text::caption);
    orderCombo.addItem (ko ("키보드 자리 순"), 1);
    orderCombo.addItem (ko ("드럼 세트 자리 순"), 2);
    orderCombo.setSelectedId (session.getSetting ("gameLaneOrder", "keys").toString() == "kit" ? 2 : 1, juce::dontSendNotification);
    ui::setupCombo (*this, orderCombo, [this]
    {
        session.setSetting ("gameLaneOrder", orderCombo.getSelectedId() == 2 ? "kit" : "keys");
        updateLanes();
    });

    ui::setupToggle (*this, drumlessToggle, ko ("드럼 빼고 틀기"), [this] { session.setSetting ("gameDrumless", drumlessToggle.getToggleState()); },
                     static_cast<bool> (session.getSetting ("gameDrumless", true)));
    drumlessToggle.setTooltip (ko ("원곡의 드럼을 빼고 내가 친 드럼만 듣는다. 드럼을 분리한 곡에서만 된다."));

    ui::setupToggle (*this, relaxedToggle, ko ("비슷한 악기 인정"), [this] { session.setSetting ("gameRelaxed", relaxedToggle.getToggleState()); },
                     static_cast<bool> (session.getSetting ("gameRelaxed", false)));
    relaxedToggle.setTooltip (ko ("탐끼리, 심벌끼리는 바꿔 쳐도 맞은 것으로 본다"));

    // 드럼 분리: 채보 메뉴에서 켜고 끈다.
    useSeparator = library.isSeparatorInstalled() && static_cast<bool> (session.getSetting ("gameSeparator", true));
    library.setUseSeparator (useSeparator);

    // 연주 중: 아래 줄 [오토] ... [멈춤 (Esc)]   [그만하기]
    ui::setupButton (*this, pauseButton, {}, [this]
    {
        runner.setPaused (runner.getState() == GameRunner::State::playing);
        updatePauseText();
    });

    ui::setupButton (*this, stopButton, ko ("그만하기"), [this] { endPlay(); });
    stopButton.setTooltip (ko ("이 판을 끝내고 곡 고르기로"));

    // 연주 도중에 오토 모드를 켜고 끈다 (어려운 구간만 프로그램에 맡길 때).
    ui::setupToggle (*this, playAutoToggle, ko ("오토"), [this] { runner.setAutoplay (playAutoToggle.getToggleState()); });
    playAutoToggle.setTooltip (ko ("프로그램이 대신 친다. 켠 판은 기록이 남지 않는다."));

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
        applyOffsetButton.setButtonText (ko ("보정함"));
    });

    ui::setButtonKind (stopButton, ui::ButtonKind::danger);

    library.addChangeListener (this);
    session.addChangeListener (this);

    setLevel (juce::jlimit (0, hvd::numChartLevels - 1, static_cast<int> (session.getSetting ("gameLevel", 0))));
    setSettingsOpen (static_cast<bool> (session.getSetting ("gameSettingsOpen", false)));
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
    sliderSaver.flush();
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

void GamePage::setSettingsOpen (bool shouldOpen)
{
    settingsOpen = shouldOpen;
    settingsButton.setOpen (settingsOpen);
    session.setSetting ("gameSettingsOpen", settingsOpen);

    for (auto* component : std::initializer_list<juce::Component*> { &scrollLabel, &scrollSlider, &offsetLabel, &offsetSlider,
                                                                      &orderLabel, &orderCombo, &drumlessToggle, &relaxedToggle })
        component->setVisible (settingsOpen && ! playView);

    resized();
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

    missingCharts = 0;

    for (const auto songIndex : rows)
        missingCharts += library.getSong (songIndex).hasChart ? 0 : 1;

    // 폴더 이름만 적고 전체 경로는 툴팁으로 (화면을 찍어도 개인 경로가 남지 않게)
    const auto folder = library.getMusicFolder();
    juce::String text;
    text << ko ("음악 폴더 ") << folder.getFileName() << ui::separator() << ko ("곡 ");

    if (rows.size() != static_cast<std::size_t> (library.getNumSongs()))
        text << ui::withCommas (static_cast<int> (rows.size())) << " / ";

    text << ui::withCommas (library.getNumSongs());
    folderLabel.setText (text, juce::dontSendNotification);
    folderLabel.setTooltip (folder.getFullPathName());

    updateSelection();
}

void GamePage::updateSelection()
{
    const auto valid = selectedSong >= 0 && selectedSong < library.getNumSongs();

    if (! valid)
    {
        songTitleLabel.setText (ko ("곡을 고르세요"), juce::dontSendNotification);
        songInfoLabel.setText ({}, juce::dontSendNotification);
        songInfoLabel.setTooltip (ko ("두 번 누르면 바로 시작"));
    }

    for (int level = 0; level < hvd::numChartLevels; ++level)
    {
        juce::String text = ko (levelNames[level]);

        if (valid && library.getSong (selectedSong).hasChart)
            text << "\n" << ui::formatStars (library.getSong (selectedSong).stars[static_cast<std::size_t> (level)]);

        levelButtons[static_cast<std::size_t> (level)].setButtonText (text);
    }

    startButton.setEnabled (valid);
    drumlessToggle.setEnabled (valid && library.getSong (selectedSong).hasBacking);

    if (! valid)
    {
        startButton.setButtonText (ko ("시작"));
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
        info << ui::formatMinutes (song.duration) << ui::separator() << "BPM " << juce::roundToInt (song.tempo)
             << ui::separator() << ko ("노트 ") << ui::withCommas (song.noteCounts[level]) << "\n"
             << (song.precise ? ko ("드럼 분리 채보") : ko ("간이 채보"));

        if (song.bestScore[level] > 0)
            info << ui::separator() << ko ("최고 ") << song.bestGrade[level] << " " << ui::withCommas (song.bestScore[level]);
    }
    else if (song.error.isNotEmpty())
    {
        info << ko ("분석 실패: ") << song.error;
    }
    else
    {
        info << ko ("채보 없음. 시작하면 먼저 만든다");
    }

    songInfoLabel.setText (info, juce::dontSendNotification);
    songInfoLabel.setTooltip (song.hasChart && ! song.precise ? ko ("간이 채보: 드럼을 분리하지 않고 완성곡에서 받아 적었다") : juce::String());
    startButton.setButtonText (song.hasChart ? ko ("시작") : ko ("채보 만들고 시작"));
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
        info.colour = ui::instrumentColour (laneInstruments[lane]);
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
    const auto running = progress.running || progress.done < progress.total;
    juce::String text;

    if (running)
    {
        text << ko ("분석 중 ") << juce::jmin (progress.done + 1, progress.total) << " / " << progress.total;

        if (progress.title.isNotEmpty())
            text << ui::separator() << progress.title;

        if (progress.stage.isNotEmpty())
        {
            text << ui::separator() << progress.stage;

            if (progress.fraction > 0.0f)
                text << " " << ui::formatPercent (progress.fraction * 100.0f);
        }
    }

    const auto fraction = progress.total > 0 ? juce::jlimit (0.0f, 1.0f, (static_cast<float> (progress.done) + progress.fraction) / static_cast<float> (progress.total))
                                             : 0.0f;

    if (text != progressLabel.getText())
        progressLabel.setText (text, juce::dontSendNotification);

    // 진행 줄은 분석하는 동안에만 자리를 차지한다.
    if (running != progressLabel.isVisible() && ! playView)
    {
        progressLabel.setVisible (running);
        resized();
    }

    if (std::abs (fraction - progressFraction) > 0.001f)
    {
        progressFraction = fraction;
        repaint (progressArea);
    }
}

void GamePage::showChartMenu()
{
    const auto valid = selectedSong >= 0 && selectedSong < library.getNumSongs();
    const auto hasChart = valid && library.getSong (selectedSong).hasChart;
    const auto progress = library.getProgress();
    const auto installed = library.isSeparatorInstalled();

    juce::PopupMenu menu;
    menu.addItem (analyseThisItem, hasChart ? ko ("이 곡 채보 다시 만들기") : ko ("이 곡 채보 만들기"), valid);
    menu.addItem (analyseAllItem, ko ("모두 만들기 (") + ui::withCommas (missingCharts) + ")", missingCharts > 0);
    menu.addSeparator();
    menu.addItem (separatorItem, installed ? ko ("드럼 분리") : ko ("드럼 분리 (도구 없음)"), installed, useSeparator);
    menu.addSeparator();
    menu.addItem (cancelItem, ko ("분석 멈춤"), progress.running || progress.done < progress.total);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chartButton),
                        [safe = juce::Component::SafePointer<GamePage> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            auto& page = *safe;

                            switch (result)
                            {
                                case analyseThisItem:
                                    if (page.selectedSong >= 0)
                                        page.library.analyse ({ page.selectedSong }, true);
                                    break;

                                case analyseAllItem:
                                    page.analyseMissing();
                                    break;

                                case separatorItem:
                                    page.useSeparator = ! page.useSeparator;
                                    page.library.setUseSeparator (page.useSeparator);
                                    page.session.setSetting ("gameSeparator", page.useSeparator);
                                    break;

                                case cancelItem:
                                    page.pendingStartSong = -1;
                                    page.library.cancelAnalysis();
                                    break;

                                default:
                                    break;
                            }

                            page.updateProgress();
                        });
}

void GamePage::analyseMissing()
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
        ui::confirm (this, ko ("채보 모두 만들기"),
                     ui::withCommas (static_cast<int> (wanted.size())) + ko ("곡의 채보를 만듭니다. 곡당 30초~1분, 중간에 멈출 수 있습니다."),
                     ko ("만들기"), queue);
    else
        queue();
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
            text = song.hasChart ? ui::formatMinutes (song.duration) : ui::dash();
            colour = ui::theme().textMuted;
            justification = juce::Justification::centredRight;
            break;

        case tempoColumn:
            text = song.hasChart && song.tempo > 0.0f ? juce::String (juce::roundToInt (song.tempo)) : ui::dash();
            colour = ui::theme().textMuted;
            justification = juce::Justification::centredRight;
            break;

        case starsColumn:
            if (song.hasChart)
            {
                text = ui::formatStars (song.stars[level]);
                colour = song.precise ? ui::theme().accent : ui::theme().accent.withAlpha (0.6f);
            }
            else if (song.error.isNotEmpty())
            {
                text = ko ("실패");
                colour = ui::theme().danger;
            }
            else
            {
                text = ui::dash();
                colour = ui::theme().textFaint;
            }
            break;

        case bestColumn:
            text = song.bestScore[level] > 0 ? song.bestGrade[level] + "  " + ui::withCommas (song.bestScore[level]) : juce::String();
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
    detail << ko (levelNames[level]) << "  " << ui::formatStars (song.stars[static_cast<std::size_t> (level)])
           << ui::separator() << "BPM " << juce::roundToInt (chart.tempo);

    if (setup.speed < 0.999)
        detail << ui::separator() << ko ("속도 ") << ui::formatPercent (setup.speed * 100.0);

    detail << "\n" << (drumless ? ko ("드럼 뺀 반주") : ko ("원곡 그대로"));

    playingSong = songIndex;
    playingLevel = level;

    juce::String error;

    {
        const ui::WaitCursor wait;
        error = runner.start (drumless ? backing : song.file, std::move (setup));
    }

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

    highway.setInfo (ko ("연습 패턴"), ko ("8비트 록, 템포 110\n곡 없이 메트로놈처럼 진행"));
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
    applyOffsetButton.setButtonText (useful ? ko ("보정에 반영 (") + ui::formatSignedMs (static_cast<float> (mean)) + ")"
                                            : ko ("보정할 것 없음"));

    setPlayView (true);
}

void GamePage::updatePauseText()
{
    pauseButton.setButtonText (runner.getState() == GameRunner::State::paused ? ko ("계속 (Esc)") : ko ("멈춤 (Esc)"));
}

void GamePage::setPlayView (bool shouldShowPlay)
{
    playView = shouldShowPlay;
    const auto done = playView && runner.getState() == GameRunner::State::finished;

    juce::Component* const selecting[] = { &folderLabel, &folderButton, &rescanButton, &chartButton, &searchEditor, &chartedToggle, &songTable,
                                           &songTitleLabel, &songInfoLabel, &levelLabel, &speedLabel, &speedSlider,
                                           &autoplayToggle, &startButton, &settingsButton };

    for (auto* component : selecting)
        component->setVisible (! playView);

    for (auto& button : levelButtons)
        button.setVisible (! playView);

    for (auto* component : std::initializer_list<juce::Component*> { &scrollLabel, &scrollSlider, &offsetLabel, &offsetSlider,
                                                                      &orderLabel, &orderCombo, &drumlessToggle, &relaxedToggle })
        component->setVisible (! playView && settingsOpen);

    progressLabel.setVisible (! playView && progressLabel.getText().isNotEmpty());

    highway.setVisible (playView);
    pauseButton.setVisible (playView && ! done);
    stopButton.setVisible (playView && ! done);
    playAutoToggle.setVisible (playView && ! done);
    playAutoToggle.setToggleState (runner.isAutoplay(), juce::dontSendNotification);
    retryButton.setVisible (done);
    backButton.setVisible (done);
    applyOffsetButton.setVisible (done);
    updatePauseText();

    resized();
    repaint();
}

//==============================================================================
void GamePage::pageShown()
{
    if (! library.hasScanned())
    {
        // 음악 폴더를 처음 훑는다. 곡이 많으면 잠깐 걸린다.
        const ui::WaitCursor wait;
        library.rescan();
    }

    runner.refreshPads();
    updateLanes();
}

void GamePage::pageHidden()
{
    if (runner.getState() == GameRunner::State::playing)
    {
        runner.setPaused (true);
        updatePauseText();
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

    // 분석 진행 막대
    if (progressLabel.isVisible() && ! progressArea.isEmpty())
    {
        const auto& t = ui::theme();
        const auto bar = progressArea.withTop (progressArea.getBottom() - 3).toFloat();
        g.setColour (t.surfaceSunken);
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (t.accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progressFraction), 1.5f);
    }
}

void GamePage::resized()
{
    auto area = getLocalBounds();

    if (playView)
    {
        // 아래 줄: [오토] ......... [멈춤 (Esc)]   [그만하기]
        auto bottom = area.removeFromBottom (30);
        stopButton.setBounds (bottom.removeFromRight (96));
        bottom.removeFromRight (ui::space::large);
        pauseButton.setBounds (bottom.removeFromRight (110));
        playAutoToggle.setBounds (bottom.removeFromLeft (90));

        area.removeFromBottom (ui::space::small);
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

    // 위 줄: 음악 폴더 · [채보] [폴더 바꾸기] [새로 고침]
    auto top = area.removeFromTop (ui::space::control);
    rescanButton.setBounds (top.removeFromRight (ui::space::control));
    top.removeFromRight (ui::space::tight);
    folderButton.setBounds (top.removeFromRight (104));
    top.removeFromRight (ui::space::small);
    chartButton.setBounds (top.removeFromRight (84));
    top.removeFromRight (ui::space::medium);
    folderLabel.setBounds (top);
    area.removeFromTop (ui::space::small);

    // 오른쪽: 고른 곡과 한 판 설정
    sidePanel = area.removeFromRight (340);
    area.removeFromRight (ui::space::medium);
    auto side = sidePanel.reduced (ui::space::large, ui::space::medium);

    songTitleLabel.setBounds (side.removeFromTop (42));
    songInfoLabel.setBounds (side.removeFromTop (68));
    side.removeFromTop (ui::space::small);

    auto row = side.removeFromTop (42);
    levelLabel.setBounds (row.removeFromLeft (40));
    const auto levelWidth = row.getWidth() / hvd::numChartLevels;

    for (auto& button : levelButtons)
        button.setBounds (row.removeFromLeft (levelWidth));

    side.removeFromTop (ui::space::small);
    ui::placeLabelled (side, speedLabel, speedSlider, sideCaptionWidth);
    autoplayToggle.setBounds (side.removeFromTop (24));
    side.removeFromTop (ui::space::small);
    startButton.setBounds (side.removeFromTop (40));
    side.removeFromTop (ui::space::medium);

    settingsButton.setBounds (side.removeFromTop (ui::space::control).removeFromLeft (80));

    if (settingsOpen)
    {
        side.removeFromTop (ui::space::tight);
        ui::placeLabelled (side, scrollLabel, scrollSlider, sideCaptionWidth);
        ui::placeLabelled (side, offsetLabel, offsetSlider, sideCaptionWidth);
        ui::placeLabelled (side, orderLabel, orderCombo, sideCaptionWidth);
        side.removeFromTop (ui::space::tight);
        drumlessToggle.setBounds (side.removeFromTop (24));
        relaxedToggle.setBounds (side.removeFromTop (24));
    }

    // 왼쪽: (분석 중이면 진행 줄) 찾기와 곡 목록
    progressArea = {};

    if (progressLabel.isVisible())
    {
        progressArea = area.removeFromTop (24);
        progressLabel.setBounds (progressArea.withTrimmedBottom (5));
        area.removeFromTop (ui::space::small);
    }

    row = area.removeFromTop (ui::space::control);
    chartedToggle.setBounds (row.removeFromRight (130));
    row.removeFromRight (ui::space::small);
    searchEditor.setBounds (row);
    area.removeFromTop (ui::space::small);
    songTable.setBounds (area);
}
