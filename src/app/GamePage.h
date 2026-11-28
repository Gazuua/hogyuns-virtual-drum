#pragma once

#include <array>
#include <memory>
#include <vector>

#include "GameHighway.h"
#include "GameRunner.h"
#include "MouseRouter.h"
#include "Session.h"
#include "Ui.h"

/** 리듬 게임 화면. 음악 폴더의 곡을 고르고, 채보를 만들고, 한 판을 한다. */
class GamePage final : public ui::Page,
                       private juce::ChangeListener,
                       private juce::TableListBoxModel
{
public:
    explicit GamePage (Session& sessionToUse);
    ~GamePage() override;

    void tick() override;
    void pageShown() override;
    void pageHidden() override;
    bool escapePressed() override;
    bool hidesTransport() const override    { return playView; }
    void resized() override;
    void paint (juce::Graphics&) override;

    GameRunner& getRunner() noexcept    { return runner; }
    GameHighway& getHighway() noexcept  { return highway; }

    /** 곡 없이 연습 패턴으로 한 판을 시작한다 (화면 캡처와 자체 검사용). */
    void startDemo (bool autoplay);

    /** 목록의 곡으로 한 판을 시작한다. 실패하면 이유를 돌려준다. */
    juce::String startSong (int songIndex, bool autoplay);

    /** 한 판을 그만두고 곡 고르기로 돌아간다. */
    void endPlay();

private:
    // 곡 목록 표
    int getNumRows() override                         { return static_cast<int> (rows.size()); }
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int column, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void cellDoubleClicked (int row, int column, const juce::MouseEvent&) override;
    void sortOrderChanged (int newSortColumnId, bool isForwards) override;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    void rebuildRows();
    void updateSelection();
    void updateLanes();
    void updateProgress();
    void setPlayView (bool shouldShowPlay);
    void setSettingsOpen (bool shouldOpen);
    void showChartMenu();
    void analyseMissing();
    void updatePauseText();
    void beginPlay (const juce::File& audio, GameRunner::Setup setup, const juce::String& title, const juce::String& detail);
    void finished();
    void startSelected();
    void chooseFolder();
    int getLevel() const;
    void setLevel (int level);

    // 마우스로 레인 치기. 연주 화면의 드럼 그림처럼 입력 스레드가 받아 바로 친다.
    void publishLaneTargets();
    void playLaneClick (const MouseRouter::Click& click);
    const std::vector<std::uint16_t>& chokesOf (int pad) const;

    Session& session;
    ChartLibrary& library;
    GameRunner runner;
    GameHighway highway;

    // 곡 고르기
    juce::Label folderLabel, songTitleLabel, songInfoLabel, levelLabel, speedLabel, scrollLabel, offsetLabel, orderLabel, progressLabel;
    juce::TextButton folderButton, startButton;
    ui::IconButton rescanButton { ui::IconButton::Icon::refresh, ko ("새로 고침") };
    ui::ChevronButton chartButton { ui::ChevronButton::Style::menu };          // 채보 만들기 메뉴
    ui::ChevronButton settingsButton { ui::ChevronButton::Style::disclosure };  // 한 판 설정 펼치기
    std::array<juce::TextButton, hvd::numChartLevels> levelButtons;
    juce::TextEditor searchEditor;
    juce::ToggleButton chartedToggle, drumlessToggle, relaxedToggle, autoplayToggle;
    juce::Slider speedSlider, scrollSlider, offsetSlider;
    juce::ComboBox orderCombo;
    juce::TableListBox songTable;
    std::unique_ptr<juce::FileChooser> chooser;
    bool useSeparator = false;          // 드럼을 분리해서 받아 적기
    bool settingsOpen = false;
    int missingCharts = 0;              // 목록에서 채보 없는 곡 수
    juce::Rectangle<int> progressArea;  // 분석 중일 때 목록 위의 진행 줄
    float progressFraction = 0.0f;

    // 연주 중
    juce::TextButton pauseButton, stopButton, retryButton, backButton, applyOffsetButton;
    juce::ToggleButton playAutoToggle;

    std::vector<int> rows;              // 표의 줄 → 곡 번호
    int selectedSong = -1;
    int playingSong = -1;               // 지금 하는 곡. 연습 패턴이면 -1
    int playingLevel = 0;
    int pendingStartSong = -1;          // 분석이 끝나면 바로 시작할 곡
    int sortColumn = 0;
    bool sortForwards = true;
    bool playView = false;
    bool resultRecorded = false;
    juce::Rectangle<int> sidePanel;
    int tickCount = 0;

    MouseRouter mouse { session, [this] (const MouseRouter::Click& click) { playLaneClick (click); } };
    std::vector<std::vector<std::uint16_t>> chokeSets;     // 패드마다 오른쪽 단추로 함께 멈출 패드들 (updateLanes가 채운다)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GamePage)
};
