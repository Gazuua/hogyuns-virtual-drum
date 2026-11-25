#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <deque>
#include <vector>

#include "core/Chart.h"

/** 음악 폴더의 곡 하나와, 그 곡에서 받아 적은 채보의 요약. */
struct Song
{
    juce::File file;
    juce::String title;       // 파일 이름에서 확장자를 뗀 것
    juce::String folder;      // 음악 폴더 아래의 폴더 경로
    juce::String id;          // 채보 파일 이름에 쓰는 글자열

    bool hasChart = false;
    bool precise = false;     // 드럼을 분리해서 받아 적었으면 true
    bool hasBacking = false;  // 드럼을 뺀 반주 파일이 있는지
    float tempo = 0.0f;
    double duration = 0.0;
    std::array<float, hvd::numChartLevels> stars { -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<int, hvd::numChartLevels> noteCounts {};
    std::array<int, hvd::numChartLevels> bestScore {};
    std::array<juce::String, hvd::numChartLevels> bestGrade;
    juce::String error;       // 마지막 분석이 실패한 이유
};

/** 음악 폴더의 곡 목록과 채보를 맡는다. 곡에서 드럼을 받아 적는 일은 따로 둔 스레드들이 한다.

    채보는 <데이터 폴더>/charts/ 에 곡마다 JSON 파일 하나로 저장한다. 음악 파일은 읽기만 한다.
    드럼 분리 도구(tools/demucs-env, tools/separate_drums.py)가 있으면 드럼만 분리한 소리에서 받아 적고,
    없으면 완성곡에서 바로 받아 적는다.

    여러 곡을 함께 처리할 수 있다(setNumWorkers). 드럼 분리는 도구 하나가 한 곡씩 차례로 하고,
    곡을 풀고, 받아 적고, 반주를 저장하는 일이 그 앞뒤에서 겹쳐 돈다. 분리가 빠를수록(GPU) 효과가 크다.

    공개 함수는 모두 화면 스레드에서 부른다.
*/
class ChartLibrary final : public juce::ChangeBroadcaster
{
public:
    /** chartsFolderToUse가 비어 있으면 <데이터 폴더>/charts 를 쓴다. */
    explicit ChartLibrary (const juce::File& dataRootToUse, const juce::File& chartsFolderToUse = {});
    ~ChartLibrary() override;

    juce::File getChartsFolder() const                { return chartsFolder; }
    juce::File getMusicFolder() const                 { return musicFolder; }

    /** 음악 폴더를 정하고 곡 목록을 다시 만든다. */
    void setMusicFolder (const juce::File& folder);

    /** 음악 폴더만 정해 둔다. 곡 목록은 rescan()을 부를 때 만든다. */
    void setMusicFolderWithoutScan (const juce::File& folder)    { musicFolder = folder; scanned = false; }
    void rescan();
    bool hasScanned() const noexcept                  { return scanned; }

    int getNumSongs() const noexcept                  { return static_cast<int> (songs.size()); }
    const Song& getSong (int songIndex) const         { return songs[static_cast<std::size_t> (songIndex)]; }
    int findSong (const juce::File& file) const;

    //==============================================================================
    /** 드럼 분리 도구가 설치돼 있는지. */
    bool isSeparatorInstalled() const;

    /** 분리 도구를 쓸지. 설치돼 있지 않으면 켜도 쓰지 않는다. */
    void setUseSeparator (bool shouldUse) noexcept    { useSeparator.store (shouldUse, std::memory_order_relaxed); }
    bool getUseSeparator() const noexcept             { return useSeparator.load (std::memory_order_relaxed); }

    /** 분리 도구가 계산에 쓰는 장치: "cuda"(그래픽 카드) 또는 "cpu". 아직 띄운 적이 없으면 비어 있다. */
    juce::String getSeparatorDevice() const;

    /** 함께 처리할 곡 수 (1 ~ 8). */
    void setNumWorkers (int count);
    int getNumWorkers() const noexcept                { return numWorkers.load (std::memory_order_relaxed); }

    /** 곡들의 채보를 만든다. evenIfDone이 false이면 이미 채보가 있는 곡은 건너뛴다. */
    void analyse (const std::vector<int>& songIndices, bool evenIfDone);
    void cancelAnalysis();

    struct Progress
    {
        bool running = false;
        int done = 0, total = 0, failed = 0;
        juce::String title, stage;
        float fraction = 0.0f;      // 지금 곡의 진행 정도 (한 곡씩 처리할 때만)
    };

    Progress getProgress() const;

    /** 화면 타이머가 부른다. 끝난 분석의 결과를 곡 목록에 반영한다. */
    void poll();

    //==============================================================================
    bool loadChart (int songIndex, hvd::Chart& chart) const;

    /** 곡 파일의 채보를 곡 목록 없이 바로 읽는다 (음악 폴더를 훑지 않았어도 된다).
        드럼을 뺀 반주 파일(<id>.backing.ogg)을 주면 그 곡의 채보를 읽는다. 채보가 없으면 false. */
    bool loadChartForFile (const juce::File& songFile, hvd::Chart& chart) const;

    /** 드럼을 뺀 반주 파일. 없으면 빈 File. */
    juce::File getBackingFile (int songIndex) const;

    /** 한 판의 결과를 적어 둔다. 지금까지의 최고 점수보다 높으면 바꾼다. */
    void recordResult (int songIndex, int level, int score, char grade);

private:
    struct Job
    {
        juce::File file;
        juce::String id, title;
    };

    struct Result
    {
        juce::String id, error;
        Song summary;
    };

    class Worker;

    void workerLoop (Worker& worker);
    void stopWorkers();
    Result process (const Job& job, Worker& worker);
    juce::String separate (const Job& job, const juce::File& work, Worker& worker, juce::File& drums, juce::File& backing);
    bool startSeparator (Worker& worker, juce::String& error);
    void stopSeparator();
    void stopSeparatorUnlocked();

    void setStage (Worker& worker, const juce::String& stage);
    void loadIndex();
    void saveIndex() const;
    void applySummary (Song& song, const juce::var& entry) const;
    juce::var makeEntry (const Song& song) const;
    static void summarise (const hvd::Chart& chart, Song& song);

    juce::File dataRoot, chartsFolder, musicFolder;
    std::vector<Song> songs;
    juce::var index;          // id → 요약. charts/index.json 에 저장한다
    bool scanned = false;

    // 분석 스레드들과 주고받는 것 (lock으로 지킨다)
    juce::CriticalSection lock;
    std::deque<Job> jobs;
    std::vector<Result> results;
    Progress progress;
    int activeJobs = 0;
    std::vector<std::unique_ptr<Worker>> workers;    // 화면 스레드만 늘리고 줄인다
    std::atomic<int> numWorkers { 2 };
    std::atomic<bool> useSeparator { true };

    // 드럼 분리 도구. 하나를 띄워 모든 스레드가 함께 쓴다 (separatorLock으로 지킨다).
    juce::CriticalSection separatorLock;
    juce::ChildProcess separator;
    juce::File separatorQueue;
    juce::String separatorDevice;
    std::atomic<int> separatorJobCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChartLibrary)
};
