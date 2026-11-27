#include "ChartLibrary.h"

#include "ChartTools.h"
#include "MusicFile.h"
#include "Text.h"
#include "core/DrumTranscriber.h"

namespace
{

constexpr const char* songPatterns = "*.mp3;*.wav;*.flac;*.ogg;*.wma;*.aif;*.aiff";
constexpr juce::int64 smallestSongBytes = 300 * 1024;    // 이보다 작은 파일은 효과음으로 보고 건너뛴다
constexpr int separatorStartSeconds = 240;               // 분리 도구가 준비될 때까지 기다리는 시간 (처음에는 모델을 내려받는다)
constexpr int separatorJobSeconds = 1200;

juce::String makeId (const juce::File& file)
{
    return juce::String::toHexString (file.getFullPathName().toLowerCase().hashCode64());
}

} // namespace

ChartLibrary::ChartLibrary (const juce::File& dataRootToUse, const juce::File& chartsFolderToUse)
    : dataRoot (dataRootToUse),
      chartsFolder (chartsFolderToUse != juce::File() ? chartsFolderToUse : dataRootToUse.getChildFile ("charts")),
      musicFolder (juce::File::getSpecialLocation (juce::File::userMusicDirectory))
{
    loadIndex();
}

ChartLibrary::~ChartLibrary()
{
    stopWorkers();
}

//==============================================================================
void ChartLibrary::loadIndex()
{
    index = juce::var (new juce::DynamicObject());

    const auto file = getChartsFolder().getChildFile ("index.json");
    juce::var parsed;

    if (file.existsAsFile() && juce::JSON::parse (file.loadFileAsString(), parsed).wasOk() && parsed.isObject())
        index = parsed;
}

void ChartLibrary::saveIndex() const
{
    getChartsFolder().createDirectory();
    getChartsFolder().getChildFile ("index.json").replaceWithText (juce::JSON::toString (index));
}

juce::var ChartLibrary::makeEntry (const Song& song) const
{
    auto* entry = new juce::DynamicObject();
    entry->setProperty ("file", song.file.getFullPathName());
    entry->setProperty ("size", song.file.getSize());
    entry->setProperty ("precise", song.precise);
    entry->setProperty ("tempo", juce::roundToInt (song.tempo * 10.0f) / 10.0);
    entry->setProperty ("duration", juce::roundToInt (song.duration * 10.0) / 10.0);

    juce::Array<juce::var> stars, notes, best;

    for (int level = 0; level < hvd::numChartLevels; ++level)
    {
        const auto i = static_cast<std::size_t> (level);
        stars.add (juce::roundToInt (song.stars[i] * 10.0f) / 10.0);
        notes.add (song.noteCounts[i]);

        auto* record = new juce::DynamicObject();
        record->setProperty ("score", song.bestScore[i]);
        record->setProperty ("grade", song.bestGrade[i]);
        best.add (juce::var (record));
    }

    entry->setProperty ("stars", stars);
    entry->setProperty ("notes", notes);
    entry->setProperty ("best", best);
    return juce::var (entry);
}

void ChartLibrary::applySummary (Song& song, const juce::var& entry) const
{
    if (! entry.isObject())
        return;

    // 최고 기록은 채보를 다시 만들어도 남긴다.
    if (const auto* best = entry["best"].getArray())
    {
        for (int level = 0; level < juce::jmin (hvd::numChartLevels, best->size()); ++level)
        {
            song.bestScore[static_cast<std::size_t> (level)] = static_cast<int> (best->getReference (level)["score"]);
            song.bestGrade[static_cast<std::size_t> (level)] = best->getReference (level)["grade"].toString();
        }
    }

    // 음악 파일이 바뀌었거나 채보 파일이 없으면 채보가 없는 것으로 본다.
    if (static_cast<juce::int64> (entry["size"]) != song.file.getSize()
        || ! getChartsFolder().getChildFile (song.id + ".json").existsAsFile())
        return;

    song.hasChart = true;
    song.precise = static_cast<bool> (entry["precise"]);
    song.tempo = static_cast<float> (static_cast<double> (entry["tempo"]));
    song.duration = static_cast<double> (entry["duration"]);
    song.hasBacking = getChartsFolder().getChildFile (song.id + ".backing.ogg").existsAsFile();

    const auto* stars = entry["stars"].getArray();
    const auto* notes = entry["notes"].getArray();

    for (int level = 0; level < hvd::numChartLevels; ++level)
    {
        const auto i = static_cast<std::size_t> (level);

        if (stars != nullptr && level < stars->size())
            song.stars[i] = static_cast<float> (static_cast<double> (stars->getReference (level)));

        if (notes != nullptr && level < notes->size())
            song.noteCounts[i] = static_cast<int> (notes->getReference (level));
    }
}

void ChartLibrary::summarise (const hvd::Chart& chart, Song& song)
{
    song.tempo = chart.tempo;
    song.duration = chart.duration;

    for (int level = 0; level < hvd::numChartLevels; ++level)
    {
        const auto notes = hvd::makeChartVariant (chart, static_cast<hvd::ChartLevel> (level));
        song.noteCounts[static_cast<std::size_t> (level)] = static_cast<int> (notes.size());
        song.stars[static_cast<std::size_t> (level)] = hvd::rateChartDifficulty (notes);
    }
}

//==============================================================================
void ChartLibrary::setMusicFolder (const juce::File& folder)
{
    musicFolder = folder;
    rescan();
}

void ChartLibrary::rescan()
{
    songs.clear();

    juce::Array<juce::File> files;

    if (musicFolder.isDirectory())
        musicFolder.findChildFiles (files, juce::File::findFiles, true, songPatterns);

    for (const auto& file : files)
    {
        if (file.getSize() < smallestSongBytes)
            continue;

        Song song;
        song.file = file;
        song.title = file.getFileNameWithoutExtension();
        song.folder = file.getParentDirectory().getRelativePathFrom (musicFolder);
        song.id = makeId (file);

        if (song.folder == ".")
            song.folder = {};

        applySummary (song, index[juce::Identifier (song.id)]);
        songs.push_back (std::move (song));
    }

    std::sort (songs.begin(), songs.end(), [] (const Song& a, const Song& b)
    {
        const auto folders = a.folder.compareNatural (b.folder);
        return folders != 0 ? folders < 0 : a.title.compareNatural (b.title) < 0;
    });

    scanned = true;
    sendChangeMessage();
}

int ChartLibrary::findSong (const juce::File& file) const
{
    for (std::size_t i = 0; i < songs.size(); ++i)
        if (songs[i].file == file)
            return static_cast<int> (i);

    return -1;
}

bool ChartLibrary::loadChart (int songIndex, hvd::Chart& chart) const
{
    if (songIndex < 0 || songIndex >= getNumSongs())
        return false;

    const auto file = getChartsFolder().getChildFile (getSong (songIndex).id + ".json");
    return file.existsAsFile() && chartFromJson (file.loadFileAsString(), chart);
}

bool ChartLibrary::loadChartForFile (const juce::File& songFile, hvd::Chart& chart) const
{
    const auto name = songFile.getFileName();
    const auto id = (songFile.getParentDirectory() == getChartsFolder() && name.endsWith (".backing.ogg"))
                        ? name.upToFirstOccurrenceOf (".", false, false)
                        : makeId (songFile);

    const auto file = getChartsFolder().getChildFile (id + ".json");
    return file.existsAsFile() && chartFromJson (file.loadFileAsString(), chart);
}

juce::File ChartLibrary::getBackingFile (int songIndex) const
{
    if (songIndex < 0 || songIndex >= getNumSongs())
        return {};

    const auto file = getChartsFolder().getChildFile (getSong (songIndex).id + ".backing.ogg");
    return file.existsAsFile() ? file : juce::File();
}

void ChartLibrary::recordResult (int songIndex, int level, int score, char grade)
{
    if (songIndex < 0 || songIndex >= getNumSongs() || level < 0 || level >= hvd::numChartLevels)
        return;

    auto& song = songs[static_cast<std::size_t> (songIndex)];
    const auto i = static_cast<std::size_t> (level);

    if (score <= song.bestScore[i])
        return;

    song.bestScore[i] = score;
    song.bestGrade[i] = juce::String::charToString (static_cast<juce::juce_wchar> (grade));

    if (auto* object = index.getDynamicObject())
        object->setProperty (juce::Identifier (song.id), makeEntry (song));

    saveIndex();
    sendChangeMessage();
}

//==============================================================================
bool ChartLibrary::isSeparatorInstalled() const
{
    const auto tools = dataRoot.getChildFile ("tools");
    return tools.getChildFile ("demucs-env").getChildFile ("Scripts").getChildFile ("python.exe").existsAsFile()
        && tools.getChildFile ("separate_drums.py").existsAsFile();
}

juce::String ChartLibrary::getSeparatorDevice() const
{
    const juce::ScopedLock scope (separatorLock);
    return separatorDevice;
}

//==============================================================================
/** 곡을 하나씩 꺼내 채보를 만드는 스레드. 여러 개가 함께 돈다. */
class ChartLibrary::Worker final : public juce::Thread
{
public:
    Worker (ChartLibrary& ownerToUse, int indexToUse)
        : juce::Thread ("HVD chart analysis"), owner (ownerToUse), index (indexToUse)
    {
    }

    void run() override    { owner.workerLoop (*this); }

    bool shouldStop() const noexcept
    {
        return cancelled.load (std::memory_order_relaxed) || threadShouldExit();
    }

    ChartLibrary& owner;
    const int index;
    std::atomic<bool> cancelled { false };    // 지금 하는 곡을 그만둬야 하는지
    std::atomic<float> fraction { 0.0f };     // 지금 단계의 진행 정도
};

void ChartLibrary::setNumWorkers (int count)
{
    numWorkers.store (juce::jlimit (1, 8, count), std::memory_order_relaxed);
}

void ChartLibrary::stopWorkers()
{
    cancelAnalysis();

    for (auto& worker : workers)
        worker->signalThreadShouldExit();

    for (auto& worker : workers)
    {
        worker->notify();
        worker->stopThread (8000);
    }

    workers.clear();
    stopSeparator();
}

void ChartLibrary::analyse (const std::vector<int>& songIndices, bool evenIfDone)
{
    {
        const juce::ScopedLock scope (lock);

        if (activeJobs == 0 && jobs.empty())
        {
            progress.done = 0;
            progress.total = 0;
            progress.failed = 0;
        }

        for (const auto songIndex : songIndices)
        {
            if (songIndex < 0 || songIndex >= getNumSongs())
                continue;

            const auto& song = getSong (songIndex);

            if (song.hasChart && ! evenIfDone)
                continue;

            const auto queued = std::any_of (jobs.begin(), jobs.end(), [&song] (const Job& job) { return job.id == song.id; });

            if (! queued)
            {
                jobs.push_back ({ song.file, song.id, song.title });
                ++progress.total;
            }
        }
    }

    // 모자라는 만큼 스레드를 더 만든다. (줄였을 때 남는 스레드는 일을 받지 않고 쉰다)
    const auto wanted = static_cast<std::size_t> (numWorkers.load (std::memory_order_relaxed));

    while (workers.size() < wanted)
    {
        workers.push_back (std::make_unique<Worker> (*this, static_cast<int> (workers.size())));
        workers.back()->startThread();
    }

    for (auto& worker : workers)
        worker->notify();
}

void ChartLibrary::cancelAnalysis()
{
    const juce::ScopedLock scope (lock);
    jobs.clear();

    for (auto& worker : workers)
        worker->cancelled.store (true, std::memory_order_relaxed);

    progress.total = progress.done + activeJobs;
}

ChartLibrary::Progress ChartLibrary::getProgress() const
{
    const juce::ScopedLock scope (lock);
    auto copy = progress;
    copy.running = activeJobs > 0 || ! jobs.empty();
    copy.fraction = 0.0f;

    // 한 곡만 처리하고 있을 때는 그 곡의 진행 정도를 보여 준다. (쉬는 스레드의 값은 0이다)
    if (activeJobs == 1)
        for (const auto& worker : workers)
            copy.fraction = juce::jmax (copy.fraction, worker->fraction.load (std::memory_order_relaxed));

    return copy;
}

void ChartLibrary::setStage (Worker& worker, const juce::String& stage)
{
    const juce::ScopedLock scope (lock);

    // 여러 곡을 함께 처리할 때는 단계가 곡마다 달라서 하나로 보여 줄 수 없다.
    progress.stage = activeJobs <= 1 ? stage : ko ("여러 곡을 함께 처리하는 중");
    worker.fraction.store (0.0f, std::memory_order_relaxed);
}

void ChartLibrary::poll()
{
    std::vector<Result> finished;

    {
        const juce::ScopedLock scope (lock);
        finished.swap (results);
    }

    if (finished.empty())
        return;

    for (const auto& result : finished)
    {
        const auto found = std::find_if (songs.begin(), songs.end(), [&result] (const Song& song) { return song.id == result.id; });

        if (found == songs.end())
            continue;

        found->error = result.error;

        if (result.error.isEmpty())
        {
            found->hasChart = true;
            found->precise = result.summary.precise;
            found->tempo = result.summary.tempo;
            found->duration = result.summary.duration;
            found->stars = result.summary.stars;
            found->noteCounts = result.summary.noteCounts;
            found->hasBacking = getChartsFolder().getChildFile (found->id + ".backing.ogg").existsAsFile();

            if (auto* object = index.getDynamicObject())
                object->setProperty (juce::Identifier (found->id), makeEntry (*found));
        }
    }

    saveIndex();
    sendChangeMessage();
}

//==============================================================================
void ChartLibrary::workerLoop (Worker& worker)
{
    while (! worker.threadShouldExit())
    {
        Job job;
        auto haveJob = false;

        {
            const juce::ScopedLock scope (lock);

            if (worker.index < numWorkers.load (std::memory_order_relaxed) && ! jobs.empty())
            {
                job = jobs.front();
                jobs.pop_front();
                haveJob = true;
                ++activeJobs;
                progress.title = job.title;
                worker.cancelled.store (false, std::memory_order_relaxed);
            }
        }

        if (! haveJob)
        {
            worker.wait (300);
            continue;
        }

        auto result = process (job, worker);
        const auto cancelled = worker.shouldStop();

        const juce::ScopedLock scope (lock);
        --activeJobs;
        worker.fraction.store (0.0f, std::memory_order_relaxed);

        if (! cancelled)
        {
            progress.failed += result.error.isNotEmpty() ? 1 : 0;
            results.push_back (std::move (result));
            ++progress.done;
        }
    }
}

ChartLibrary::Result ChartLibrary::process (const Job& job, Worker& worker)
{
    Result result;
    result.id = job.id;

    const auto work = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hvd-chart-" + job.id);
    work.deleteRecursively();
    work.createDirectory();

    const auto cleanUp = [&work] { work.deleteRecursively(); };

    std::vector<float> left, right;
    auto sampleRate = 0.0;
    auto precise = false;
    juce::String note;

    // 1. 드럼 분리 도구가 있으면 드럼만 남긴 소리를 만든다.
    if (getUseSeparator() && isSeparatorInstalled())
    {
        juce::File drums, backing;
        note = separate (job, work, worker, drums, backing);

        if (worker.shouldStop())
        {
            stopSeparator();    // 하던 분리를 멈춘다
            cleanUp();
            return result;
        }

        if (note.isEmpty() && decodeStereo (drums, left, right, sampleRate).isEmpty())
        {
            precise = true;

            setStage (worker, ko ("반주 저장 중"));
            getChartsFolder().createDirectory();
            writeAsOgg (backing, getChartsFolder().getChildFile (job.id + ".backing.ogg"));
        }
    }

    // 2. 없거나 실패했으면 완성곡에서 바로 받아 적는다.
    if (! precise)
    {
        setStage (worker, ko ("곡 읽는 중"));

        if (const auto error = decodeStereo (job.file, left, right, sampleRate); error.isNotEmpty())
        {
            result.error = error;
            cleanUp();
            return result;
        }
    }

    setStage (worker, ko ("받아 적는 중"));

    hvd::TranscribeOptions options;
    options.isolatedDrums = precise;
    const auto chart = hvd::transcribeDrums (left.data(), right.data(), left.size(), sampleRate, options, &worker.fraction, &worker.cancelled);

    cleanUp();

    if (worker.shouldStop())
        return result;

    if (chart.notes.size() < 8)
    {
        result.error = ko ("드럼 소리를 거의 찾지 못했습니다");
        return result;
    }

    auto* header = new juce::DynamicObject();
    header->setProperty ("version", 1);
    header->setProperty ("file", job.file.getFullPathName());
    header->setProperty ("title", job.title);
    header->setProperty ("method", precise ? "stem" : "mix");

    if (note.isNotEmpty())
        header->setProperty ("note", note);

    getChartsFolder().createDirectory();

    if (! getChartsFolder().getChildFile (job.id + ".json").replaceWithText (chartToJson (chart, juce::var (header))))
    {
        result.error = ko ("채보 파일을 쓸 수 없습니다");
        return result;
    }

    summarise (chart, result.summary);
    result.summary.precise = precise;
    return result;
}

bool ChartLibrary::startSeparator (Worker& worker, juce::String& error)
{
    // 도구는 하나만 띄우고 여러 스레드가 함께 쓴다. 띄우는 동안 다른 스레드는 여기서 기다린다.
    const juce::ScopedLock scope (separatorLock);

    if (separatorQueue != juce::File() && separator.isRunning())
        return true;

    stopSeparatorUnlocked();

    const auto tools = dataRoot.getChildFile ("tools");
    separatorQueue = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("hvd-separator-" + juce::String::toHexString (juce::Time::currentTimeMillis()));
    separatorQueue.createDirectory();

    const juce::StringArray command { tools.getChildFile ("demucs-env").getChildFile ("Scripts").getChildFile ("python.exe").getFullPathName(),
                                      tools.getChildFile ("separate_drums.py").getFullPathName(),
                                      "--queue", separatorQueue.getFullPathName() };

    if (! separator.start (command, 0))
    {
        error = ko ("드럼 분리 도구를 실행하지 못했습니다");
        return false;
    }

    // 모델을 읽을 때까지 기다린다. 처음 쓸 때는 모델을 내려받느라 오래 걸릴 수 있다.
    const auto ready = separatorQueue.getChildFile ("ready");

    for (int i = 0; i < separatorStartSeconds * 10; ++i)
    {
        if (ready.existsAsFile())
        {
            separatorDevice = ready.loadFileAsString().trim();    // "cuda" 또는 "cpu"
            return true;
        }

        if (! separator.isRunning() || worker.shouldStop())
            break;

        worker.wait (100);
    }

    error = ko ("드럼 분리 도구가 준비되지 않았습니다");
    stopSeparatorUnlocked();
    return false;
}

void ChartLibrary::stopSeparator()
{
    const juce::ScopedLock scope (separatorLock);
    stopSeparatorUnlocked();
}

void ChartLibrary::stopSeparatorUnlocked()
{
    if (separatorQueue != juce::File())
    {
        separatorQueue.getChildFile ("quit").replaceWithText ("quit");

        if (separator.isRunning() && ! separator.waitForProcessToFinish (1500))
            separator.kill();

        separatorQueue.deleteRecursively();
        separatorQueue = juce::File();
    }
}

juce::String ChartLibrary::separate (const Job& job, const juce::File& work, Worker& worker, juce::File& drums, juce::File& backing)
{
    setStage (worker, ko ("드럼 분리 준비 중"));
    juce::String error;

    if (! startSeparator (worker, error))
        return error;

    setStage (worker, ko ("곡 읽는 중"));
    const auto input = work.getChildFile ("input.wav");

    if (error = writeAsWav (job.file, input); error.isNotEmpty())
        return error;

    drums = work.getChildFile ("drums.wav");
    backing = work.getChildFile ("backing.wav");

    auto* request = new juce::DynamicObject();
    request->setProperty ("input", input.getFullPathName());
    request->setProperty ("drums", drums.getFullPathName());
    request->setProperty ("backing", backing.getFullPathName());
    const auto requestText = juce::JSON::toString (juce::var (request));

    juce::File queue;

    {
        const juce::ScopedLock scope (separatorLock);
        queue = separatorQueue;
    }

    if (queue == juce::File())
        return ko ("드럼 분리 도구가 중간에 끝났습니다");

    // 다 쓴 뒤에 이름을 바꿔서, 도구가 쓰다 만 파일을 읽지 않게 한다. 도구는 이름 순서대로 하나씩 처리한다.
    const auto name = "job" + juce::String (separatorJobCount.fetch_add (1) + 1).paddedLeft ('0', 6);
    const auto pending = queue.getChildFile (name + ".tmp");
    pending.replaceWithText (requestText);
    pending.moveFileTo (queue.getChildFile (name + ".job"));

    setStage (worker, ko ("드럼 분리 중"));
    const auto done = queue.getChildFile (name + ".done");
    const auto progressFile = queue.getChildFile (name + ".progress");

    for (int i = 0; i < separatorJobSeconds * 10; ++i)
    {
        if (done.existsAsFile())
        {
            const auto text = done.loadFileAsString().trim();
            return text == "ok" ? juce::String() : ko ("드럼 분리 실패: ") + text;
        }

        if (worker.shouldStop())
            return "cancelled";

        if (i % 10 == 0)
        {
            const juce::ScopedLock scope (separatorLock);

            if (separatorQueue != queue || ! separator.isRunning())
                return ko ("드럼 분리 도구가 중간에 끝났습니다");
        }

        if (i % 5 == 0 && progressFile.existsAsFile())
            worker.fraction.store (progressFile.loadFileAsString().getFloatValue(), std::memory_order_relaxed);

        worker.wait (100);
    }

    return ko ("드럼 분리가 너무 오래 걸립니다");
}
