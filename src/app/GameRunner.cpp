#include "GameRunner.h"

#include "Text.h"

namespace
{

constexpr double leadInSeconds = 3.0;         // 첫 노트가 내려올 시간을 주려고 곡 앞에 두는 준비 시간
constexpr double inputDelaySeconds = 0.004;   // 키를 누른 뒤 엔진이 타격을 내기까지 걸리는 평균 시간
constexpr double tailSeconds = 3.0;           // 마지막 노트 뒤에 더 틀어 두는 시간
constexpr double fadeSeconds = 1.5;
constexpr double autoplayLookAhead = 0.3;

const char* const preferredPads[hvd::numLanes] = { "crash1", "hihat_closed", "snare", "tom_high", "tom_mid", "tom_floor", "ride", "kick" };

} // namespace

GameRunner::GameRunner (Session& sessionToUse)
    : session (sessionToUse)
{
    lanePads.fill (-1);
    refreshPads();
}

GameRunner::~GameRunner()
{
    stop();
}

void GameRunner::refreshPads()
{
    padLanes.clear();
    lanePads.fill (-1);
    openHatPad = -1;

    const auto* kit = session.getKit();

    if (kit == nullptr)
        return;

    for (std::size_t i = 0; i < kit->pads.size(); ++i)
    {
        const auto& pad = kit->pads[i];
        auto lane = hvd::Lane::snare;
        auto known = false;

        // 킷 파일의 패드에 "lane"을 적어 두면 그것을 따른다.
        if (const auto custom = session.getKitDocument().getPadProperty (juce::String (pad.id), "lane").toString(); custom.isNotEmpty())
            known = hvd::laneFromName (custom.toStdString(), lane);

        if (! known)
            known = hvd::laneForPadId (pad.id, lane);

        padLanes.push_back (known ? static_cast<int> (lane) : -1);

        if (! known || pad.layers.empty())
            continue;

        auto& chosen = lanePads[static_cast<std::size_t> (lane)];

        if (chosen < 0 || pad.id == preferredPads[static_cast<std::size_t> (lane)])
            chosen = static_cast<int> (i);

        if (pad.id == "hihat_open")
            openHatPad = static_cast<int> (i);
    }
}

int GameRunner::laneOfPad (int pad) const noexcept
{
    return pad >= 0 && pad < static_cast<int> (padLanes.size()) ? padLanes[static_cast<std::size_t> (pad)] : -1;
}

juce::String GameRunner::start (const juce::File& audio, Setup newSetup)
{
    stop();

    if (newSetup.notes.empty())
        return ko ("칠 노트가 없습니다");

    setup = std::move (newSetup);
    setup.speed = juce::jlimit (0.5, 1.0, setup.speed);
    usingTrack = audio != juce::File();

    auto& track = session.getTrack();

    if (usingTrack)
    {
        if (session.getTrackFile() != audio)
            if (const auto error = session.loadTrack (audio); error.isNotEmpty())
                return error;

        track.setPlaying (false);
        track.setLoop (0.0, 0.0, false);
        track.setSpeed (static_cast<float> (setup.speed));
        track.seekWithLeadIn (-leadInSeconds * setup.speed);
        track.setPlaying (true);
    }
    else
    {
        startClock = session.getEngine().getClock();
        pausedSeconds = 0.0;
    }

    refreshPads();

    const auto status = session.getAudio().getStatus();
    latency = status.sampleRate > 0.0 ? static_cast<double> (status.outputLatency) / status.sampleRate : 0.0;

    const auto lastNote = setup.notes.empty() ? 0.0 : setup.notes.back().seconds;
    endSeconds = setup.duration > 0.0 ? std::min (setup.duration, lastNote + tailSeconds) : lastNote + tailSeconds;

    game.start (setup.notes, setup.windows, setup.speed, setup.relaxedLanes);
    feedback.clear();
    assisted = setup.autoplay;
    nextAutoNote = 0;
    trackStarted = false;
    songSeconds = -leadInSeconds * setup.speed;
    savedTrackGain = -1.0f;
    state = State::playing;

    session.onHit = [this] (const hvd::HitEvent& event) { handleHit (event); };

    // 연주 로그: 무엇을 어떤 설정으로 시작했는지. 타격마다의 곡 위치는 Session이 적는다.
    session.writeLogPlay (session.beginLogLine ("game").text ("state", "start")
                                                       .text ("song", setup.song.getFullPathName())
                                                       .text ("audio", audio.getFullPathName())
                                                       .integer ("level", setup.level)
                                                       .number ("speed", setup.speed, 3)
                                                       .flag ("auto", setup.autoplay)
                                                       .integer ("notes", static_cast<juce::int64> (setup.notes.size()))
                                                       .number ("offsetMs", setup.offsetMs, 1)
                                                       .number ("leadIn", leadInSeconds, 2));
    return {};
}

void GameRunner::logResult (const char* stateName)
{
    session.writeLogNote (session.beginLogLine ("game").text ("state", stateName)
                                                       .number ("song", songSeconds, 3)
                                                       .integer ("score", game.getScore())
                                                       .number ("accuracy", game.getAccuracy(), 4)
                                                       .integer ("perfect", game.getCount (hvd::Judgement::perfect))
                                                       .integer ("great", game.getCount (hvd::Judgement::great))
                                                       .integer ("good", game.getCount (hvd::Judgement::good))
                                                       .integer ("miss", game.getCount (hvd::Judgement::miss))
                                                       .integer ("maxCombo", game.getMaxCombo())
                                                       .flag ("assisted", assisted));
}

void GameRunner::stop()
{
    if (state == State::idle)
        return;

    session.onHit = nullptr;

    if (usingTrack)
    {
        session.getTrack().setPlaying (false);
        session.getTrack().setSpeed (1.0f);

        if (savedTrackGain >= 0.0f)
            session.getTrack().setGain (savedTrackGain);
    }

    if (setup.autoplay)
        session.allSoundOff();    // 엔진에 미리 넘겨 둔 시범 타격을 지운다

    if (state != State::finished)
        logResult ("stop");       // 끝까지 간 판은 finish()에서 이미 적었다

    savedTrackGain = -1.0f;
    state = State::idle;
}

void GameRunner::finish()
{
    session.onHit = nullptr;

    if (usingTrack)
    {
        session.getTrack().setPlaying (false);
        session.getTrack().setSpeed (1.0f);

        if (savedTrackGain >= 0.0f)
            session.getTrack().setGain (savedTrackGain);
    }

    savedTrackGain = -1.0f;
    game.advance (1.0e9);    // 남은 노트는 놓친 것으로 친다
    state = State::finished;
    logResult ("finish");
}

void GameRunner::setPaused (bool shouldPause)
{
    if (shouldPause && state == State::playing)
    {
        state = State::paused;
        pauseClock = session.getEngine().getClock();

        if (usingTrack)
            session.getTrack().setPlaying (false);

        if (setup.autoplay)
            session.allSoundOff();

        session.writeLogNote (session.beginLogLine ("game").text ("state", "pause").number ("song", songSeconds, 3));
    }
    else if (! shouldPause && state == State::paused)
    {
        state = State::playing;
        session.writeLogNote (session.beginLogLine ("game").text ("state", "resume").number ("song", songSeconds, 3));

        if (usingTrack)
            session.getTrack().setPlaying (true);
        else
            pausedSeconds += static_cast<double> (session.getEngine().getClock() - pauseClock) / session.getEngine().getSampleRate();

        // 멈춘 동안 지나간 시범 노트는 다시 걸지 않는다.
        while (nextAutoNote < setup.notes.size() && setup.notes[nextAutoNote].seconds < songSeconds)
            ++nextAutoNote;
    }
}

void GameRunner::setAutoplay (bool shouldAutoplay)
{
    if ((state != State::playing && state != State::paused) || setup.autoplay == shouldAutoplay)
        return;

    setup.autoplay = shouldAutoplay;
    session.writeLogNote (session.beginLogLine ("game").text ("state", "auto").flag ("on", shouldAutoplay)
                                                       .number ("song", songSeconds, 3));

    if (shouldAutoplay)
    {
        assisted = true;

        // 지금 위치 다음의 노트부터 대신 친다.
        const auto now = renderedSecondsAt (session.getEngine().getClock());
        nextAutoNote = 0;

        while (nextAutoNote < setup.notes.size() && setup.notes[nextAutoNote].seconds < now + 0.02)
            ++nextAutoNote;
    }

    // 끌 때는 이미 엔진에 넘겨 둔 타격(0.3초 안쪽)까지만 울리고 멈춘다.
}

double GameRunner::renderedSecondsAt (std::uint64_t clock) const noexcept
{
    const auto sampleRate = session.getEngine().getSampleRate();

    if (usingTrack)
        return session.getAudio().getTrackSync().positionAt (clock, sampleRate);

    const auto elapsed = static_cast<double> (static_cast<std::int64_t> (clock - startClock)) / sampleRate - pausedSeconds;
    return (elapsed - leadInSeconds) * setup.speed;
}

void GameRunner::handleHit (const hvd::HitEvent& event)
{
    if (state != State::playing)
        return;

    const auto laneIndex = laneOfPad (event.pad);

    if (laneIndex < 0)
        return;

    // 치는 사람은 "들리는" 곡에 맞춰 친다. 들리는 소리는 출력 지연만큼 전에 만든 것이므로 그만큼을 뺀다.
    // 오토 모드에서는 엔진이 정확한 시각에 치므로 보정하지 않는다.
    const auto compensation = setup.autoplay ? 0.0 : (latency + inputDelaySeconds + setup.offsetMs / 1000.0) * setup.speed;
    const auto seconds = renderedSecondsAt (event.time) - compensation;

    const auto lane = static_cast<hvd::Lane> (laneIndex);
    const auto result = game.hit (lane, seconds);

    Feedback item;
    item.judgement = result.judgement;
    item.lane = result.note >= 0 ? game.getNotes()[static_cast<std::size_t> (result.note)].lane : lane;
    item.offsetMs = static_cast<float> (result.offsetSeconds * 1000.0);
    item.velocity = event.velocity;
    feedback.push_back (item);
}

void GameRunner::pumpAutoplay()
{
    auto& engine = session.getEngine();
    const auto sampleRate = engine.getSampleRate();
    const auto clock = engine.getClock();
    const auto now = renderedSecondsAt (clock);

    while (nextAutoNote < setup.notes.size())
    {
        const auto& note = setup.notes[nextAutoNote];

        if (note.seconds > now + autoplayLookAhead * setup.speed)
            break;

        auto pad = lanePads[static_cast<std::size_t> (note.lane)];

        if (note.lane == hvd::Lane::hihat && (note.flags & hvd::ChartNote::openHat) != 0 && openHatPad >= 0)
            pad = openHatPad;

        if (pad >= 0 && note.seconds >= now)
        {
            hvd::EngineCommand command;
            command.type = hvd::EngineCommand::Type::hitAt;
            command.pad = static_cast<std::uint16_t> (pad);
            command.velocity = juce::jlimit (0.2f, 1.0f, note.velocity);
            command.time = clock + static_cast<std::uint64_t> (std::llround ((note.seconds - now) / setup.speed * sampleRate));
            command.origin.source = hvd::HitSource::autoplay;

            if (! engine.postFromControl (command))
                break;    // 큐가 꽉 찼으면 다음 번에 이어서 보낸다
        }

        ++nextAutoNote;
    }
}

void GameRunner::tick()
{
    if (state != State::playing)
        return;

    auto& engine = session.getEngine();
    const auto rendered = renderedSecondsAt (engine.getClock());
    songSeconds = rendered - latency * setup.speed;

    if (usingTrack)
    {
        const auto sync = session.getAudio().getTrackSync();
        trackStarted = trackStarted || sync.active;

        // 곡이 끝까지 가서 재생기가 스스로 멈췄으면 끝낸다.
        if (trackStarted && ! sync.active)
        {
            finish();
            return;
        }
    }

    if (setup.autoplay)
        pumpAutoplay();

    for (auto missed = game.advance (songSeconds); missed > 0; --missed)
    {
        Feedback item;
        item.judgement = hvd::Judgement::miss;
        feedback.push_back (item);
    }

    // 마지막 노트가 지나가면 곡 소리를 서서히 줄이고 끝낸다.
    const auto remaining = (endSeconds - songSeconds) / setup.speed;

    if (usingTrack && remaining < fadeSeconds)
    {
        auto& track = session.getTrack();

        if (savedTrackGain < 0.0f)
            savedTrackGain = track.getGain();

        track.setGain (savedTrackGain * static_cast<float> (juce::jlimit (0.0, 1.0, remaining / fadeSeconds)));
    }

    if (remaining <= 0.0)
        finish();
}

std::vector<GameRunner::Feedback> GameRunner::takeFeedback()
{
    std::vector<Feedback> items;
    items.swap (feedback);
    return items;
}
