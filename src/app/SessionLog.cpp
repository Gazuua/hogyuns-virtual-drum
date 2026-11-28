#include "SessionLog.h"

#include <cmath>

#include "Session.h"
#include "Text.h"

// 줄마다 t(초)와 e(무슨 일인지)가 있다. t는 엔진 시계를 초로 바꾼 값이라 타격끼리는 샘플 단위로 정확하다.
//   hit     타격 하나: pad, v(세기), src(key / mouse / midi / screen / replay / auto), key(누른 키), song(곡 안의 위치)
//   down/up 키를 누르고 뗀 것: key
//   beat    메트로놈의 박: n
//   play / pause / seek / speed    반주의 진행: song, speed
//   start, audio, kit, keymap, tempo, track, game, end    그때의 상황
// song은 그 타격의 소리가 곡의 어느 자리에 놓였는지다 (녹음하면 그 자리에 들린다).

SessionLog::SessionLog (const Session& sessionToRead, const hvd::Engine& engineToRead,
                        const hvd::TrackPlayer& trackToRead, const AudioHost& audioToRead)
    : session (sessionToRead), engine (engineToRead), track (trackToRead), audio (audioToRead)
{
}

void SessionLog::start (const juce::File& folder)
{
    playLog.start (folder);
    keysWanted.store (playLog.isActive(), std::memory_order_relaxed);
}

void SessionLog::stop()
{
    keysWanted.store (false, std::memory_order_relaxed);
    writeNote (beginLine ("end"));
    playLog.stop();
}

double SessionLog::logSecondsAt (std::uint64_t clock) const noexcept
{
    const auto rate = logRate > 0.0 ? logRate : 48000.0;
    return logBaseSeconds + static_cast<double> (static_cast<std::int64_t> (clock - logBaseClock)) / rate;
}

PlayLog::Line SessionLog::beginLine (const char* event) const
{
    return PlayLog::Line (logSecondsAt (engine.getClock()), event);
}

void SessionLog::writePlay (const PlayLog::Line& line)
{
    if (! playLog.hasFile())
    {
        if (! playLog.begin())
            return;

        logWholeSituation();
        playLog.writeKept();
    }

    playLog.write (line);
}

void SessionLog::flushIfDue()
{
    if (logPollCount % 60 == 0)
        playLog.flush();
}

void SessionLog::logWholeSituation()
{
    playLog.write (beginLine ("start").integer ("version", 1)
                                      .text ("time", juce::Time::getCurrentTime().toISO8601 (true)));
    logAudio (audio.getStatus());
    logKit();
    logKeymap();
    logTempo();
    logTrackFile();

    loggedSync = audio.getTrackSync();
    loggedSync.active = loggedSync.active && session.getTrackFile() != juce::File();
    loggedBeatCount = engine.getBeatCount();

    if (loggedSync.active)
        playLog.write (PlayLog::Line (logSecondsAt (loggedSync.clock), "play").number ("song", loggedSync.position, 4)
                                                                                 .number ("speed", loggedSync.speed, 3));
}

void SessionLog::logAudio (const AudioHost::Status& status)
{
    loggedAudio = status;

    const auto latencyMs = status.sampleRate > 0.0 ? 1000.0 * status.outputLatency / status.sampleRate : 0.0;

    playLog.write (beginLine ("audio").flag ("open", status.open)
                                      .text ("type", status.typeName)
                                      .text ("device", status.deviceName)
                                      .number ("rate", status.sampleRate, 0)
                                      .integer ("buffer", status.bufferSize)
                                      .number ("latencyMs", latencyMs, 2));
}

void SessionLog::logKit()
{
    if (const auto* kit = session.getKit())
        playLog.write (beginLine ("kit").text ("folder", session.getKitFolderName())
                                        .text ("name", fromUtf8 (kit->name))
                                        .integer ("pads", static_cast<juce::int64> (kit->pads.size())));
}

void SessionLog::logKeymap()
{
    playLog.write (beginLine ("keymap").text ("name", session.getKeymapName()));
}

void SessionLog::logTempo()
{
    loggedTempo = engine.getTempo();
    loggedBeatsPerBar = engine.getBeatsPerBar();
    loggedMetronome = engine.isMetronomeOn();

    playLog.write (beginLine ("tempo").number ("bpm", loggedTempo, 2)
                                      .integer ("beatsPerBar", loggedBeatsPerBar)
                                      .flag ("metronome", loggedMetronome));
}

void SessionLog::logTrackFile()
{
    const auto trackFile = session.getTrackFile();
    loggedTrackFile = trackFile;

    playLog.write (beginLine ("track").text ("file", trackFile.getFullPathName())
                                      .number ("length", trackFile != juce::File() ? track.getLength() : 0.0, 3));
}

void SessionLog::logSituation()
{
    ++logPollCount;

    if (! playLog.isActive())
        return;

    // 장치의 샘플레이트가 바뀌면 그때부터 새 배율로 센다. 로그의 시각은 이어진다.
    if (const auto rate = engine.getSampleRate(); rate > 0.0 && rate != logRate)
    {
        const auto now = engine.getClock();
        logBaseSeconds = logSecondsAt (now);
        logBaseClock = now;
        logRate = rate;
    }

    if (! playLog.hasFile())
        return;    // 아직 친 것이 없다. 처음 칠 때 그때의 상황을 통째로 적는다

    // 장치와 템포는 0.5초에 한 번만 살핀다 (템포 슬라이더를 끄는 동안 줄이 쏟아지지 않게).
    if (logPollCount % 30 == 0)
    {
        const auto status = audio.getStatus();

        if (status.open != loggedAudio.open || status.typeName != loggedAudio.typeName || status.deviceName != loggedAudio.deviceName
            || status.sampleRate != loggedAudio.sampleRate || status.bufferSize != loggedAudio.bufferSize
            || status.outputLatency != loggedAudio.outputLatency)
            logAudio (status);

        if (engine.getTempo() != loggedTempo || engine.getBeatsPerBar() != loggedBeatsPerBar)
            logTempo();
    }

    if (engine.isMetronomeOn() != loggedMetronome)
        logTempo();

    // 메트로놈의 박. 타격이 박에서 얼마나 벗어났는지 나중에 잴 수 있게 한다.
    if (auto count = engine.getBeatCount(); count != loggedBeatCount)
    {
        auto time = engine.getLastBeatTime();

        // 읽는 사이에 다음 박이 쳐졌으면 다시 읽는다.
        for (int attempt = 0; attempt < 4 && engine.getBeatCount() != count; ++attempt)
        {
            count = engine.getBeatCount();
            time = engine.getLastBeatTime();
        }

        loggedBeatCount = count;

        if (loggedMetronome)
            playLog.write (PlayLog::Line (logSecondsAt (time), "beat").integer ("n", count));
    }

    // 반주. 곡을 연 것은 그 곡의 진행보다 먼저, 닫은 것은 멈춘 것 다음에 적는다.
    const auto trackFile = session.getTrackFile();
    const auto loaded = trackFile != juce::File();
    const auto trackChanged = trackFile != loggedTrackFile;
    auto sync = audio.getTrackSync();
    sync.active = sync.active && loaded;    // 곡을 닫은 직후에는 오디오 쪽의 값이 닫기 전 그대로일 수 있다

    const auto at = logSecondsAt (sync.clock);

    if (trackChanged && loaded)
        logTrackFile();

    if (sync.active != loggedSync.active || (trackChanged && sync.active))
    {
        playLog.write (PlayLog::Line (at, sync.active ? "play" : "pause").number ("song", sync.position, 4)
                                                                         .number ("speed", sync.speed, 3));
    }
    else if (sync.active)
    {
        const auto expected = loggedSync.positionAt (sync.clock, engine.getSampleRate());

        if (std::abs (sync.speed - loggedSync.speed) > 1.0e-4f)
            playLog.write (PlayLog::Line (at, "speed").number ("song", sync.position, 4).number ("speed", sync.speed, 3));
        else if (std::abs (sync.position - expected) > 0.03)
            playLog.write (PlayLog::Line (at, "seek").number ("song", sync.position, 4).number ("from", expected, 4));
    }
    else if (loaded && ! trackChanged && std::abs (sync.position - loggedSync.position) > 0.03)
    {
        playLog.write (PlayLog::Line (at, "seek").number ("song", sync.position, 4).number ("from", loggedSync.position, 4));
    }

    if (trackChanged && ! loaded)
        logTrackFile();

    loggedSync = sync;
}

void SessionLog::logKeys()
{
    LoggedKey entry;

    while (loggedKeys.pop (entry))
        playLog.writeOrKeep (PlayLog::Line (logSecondsAt (entry.clock), entry.down ? "down" : "up")
                                 .text ("key", fromUtf8 (hvd::keyName (entry.key))));
}

void SessionLog::logHit (const hvd::HitEvent& event, const hvd::Kit& kit, const AudioHost::TrackSync& sync)
{
    static constexpr const char* sources[] = { "screen", "key", "midi", "replay", "auto", "mouse" };    // HitSource의 순서
    const auto source = static_cast<std::size_t> (event.origin.source);

    PlayLog::Line line (logSecondsAt (event.time), "hit");
    line.text ("pad", juce::String (kit.pads[event.pad].id))
        .number ("v", event.velocity, 3)
        .text ("src", source < std::size (sources) ? sources[source] : "screen");

    if (event.origin.source == hvd::HitSource::key && event.origin.key != hvd::invalidKey)
        line.text ("key", fromUtf8 (hvd::keyName (event.origin.key)));

    // 반주가 진행 중이면 이 타격의 소리가 곡의 어느 자리에 놓였는지 적는다. 준비 시간 동안에는 음수다.
    if (sync.active && session.getTrackFile() != juce::File())
        line.number ("song", sync.positionAt (event.time, engine.getSampleRate()), 4);

    writePlay (line);
}
