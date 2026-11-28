#include "TakeRecorder.h"

#include <algorithm>

#include "Text.h"

namespace
{

constexpr double replayLookAheadSeconds = 0.25;

} // namespace

TakeRecorder::TakeRecorder (hvd::Engine& engineToUse, hvd::TrackPlayer& trackToUse)
    : engine (engineToUse), track (trackToUse)
{
}

void TakeRecorder::start()
{
    stopPlayback();
    take.clear();
    length = 0.0;
    startClock = engine.getClock();
    trackPosition = track.isPlaying() ? track.getPosition() : -1.0;
    recording = true;
}

bool TakeRecorder::stop()
{
    if (! recording)
        return false;

    recording = false;
    length = static_cast<double> (engine.getClock() - startClock) / engine.getSampleRate();
    return true;
}

void TakeRecorder::add (const hvd::HitEvent& event, const hvd::Kit& kit)
{
    if (recording && ! replay.isActive() && event.time >= startClock && event.pad < kit.pads.size())
        take.push_back ({ static_cast<double> (event.time - startClock) / engine.getSampleRate(),
                          juce::String (kit.pads[event.pad].id),
                          event.velocity });
}

bool TakeRecorder::play (const hvd::Kit& kit)
{
    if (take.empty())
        return false;

    std::vector<hvd::ReplayEvent> events;
    events.reserve (take.size());

    for (const auto& hit : take)
        if (const auto pad = kit.findPad (hit.padId.toStdString()); pad >= 0)
            events.push_back ({ hit.seconds, static_cast<std::uint16_t> (pad), hit.velocity });

    // 기록할 때 반주가 나오고 있었으면 같은 자리에서 함께 재생한다.
    if (trackPosition >= 0.0 && track.getLength() > 0.0)
    {
        track.seek (trackPosition);
        track.setPlaying (true);
    }

    replay.start (std::move (events), engine.getClock(), engine.getSampleRate());
    return true;
}

bool TakeRecorder::stopPlayback()
{
    if (! replay.isActive())
        return false;

    replay.stop();

    // 엔진에 미리 넘겨 둔 타격도 지운다.
    hvd::EngineCommand command;
    command.type = hvd::EngineCommand::Type::allSoundOff;
    engine.postFromControl (command);

    if (trackPosition >= 0.0)
        track.setPlaying (false);

    return true;
}

bool TakeRecorder::pump()
{
    const auto lookAhead = static_cast<std::uint32_t> (engine.getSampleRate() * replayLookAheadSeconds);
    return replay.pump (engine, engine.getClock(), lookAhead);
}

juce::String TakeRecorder::exportMidi (const juce::File& file, const hvd::Kit& kit, float tempoToUse) const
{
    if (take.empty())
        return ko ("기록한 연주가 없습니다");

    constexpr int ticksPerQuarter = 480;
    const auto tempo = static_cast<double> (tempoToUse);
    const auto ticksPerSecond = tempo / 60.0 * ticksPerQuarter;

    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / tempo)), 0.0);

    for (const auto& hit : take)
    {
        const auto pad = kit.findPad (hit.padId.toStdString());
        const auto note = pad >= 0 ? kit.pads[static_cast<std::size_t> (pad)].midiNote : -1;

        if (note < 0)
            continue;

        const auto tick = hit.seconds * ticksPerSecond;
        const auto velocity = static_cast<juce::uint8> (juce::jlimit (1, 127, juce::roundToInt (hit.velocity * 127.0f)));

        // 드럼은 10번 채널에 적는 것이 관례다.
        sequence.addEvent (juce::MidiMessage::noteOn (10, note, velocity), tick);
        sequence.addEvent (juce::MidiMessage::noteOff (10, note), tick + 60.0);
    }

    sequence.updateMatchedPairs();

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticksPerQuarter);
    midi.addTrack (sequence);

    file.deleteFile();
    juce::FileOutputStream stream (file);

    if (! stream.openedOk() || ! midi.writeTo (stream))
        return ko ("파일을 쓰지 못했습니다: ") + file.getFullPathName();

    return {};
}

juce::String TakeRecorder::importMidi (const juce::File& file, const std::function<int (int note)>& padOfNote, const hvd::Kit& kit, bool& imported)
{
    imported = false;

    juce::FileInputStream stream (file);
    juce::MidiFile midi;

    if (! stream.openedOk() || ! midi.readFrom (stream))
        return ko ("MIDI 파일을 읽지 못했습니다: ") + file.getFileName();

    midi.convertTimestampTicksToSeconds();

    std::vector<Hit> hits;
    auto skipped = 0;

    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        const auto* sequence = midi.getTrack (trackIndex);

        for (int i = 0; i < sequence->getNumEvents(); ++i)
        {
            const auto& message = sequence->getEventPointer (i)->message;

            if (! message.isNoteOn())
                continue;

            const auto pad = padOfNote (message.getNoteNumber() & 127);

            if (! juce::isPositiveAndBelow (pad, static_cast<int> (kit.pads.size())))
            {
                ++skipped;
                continue;
            }

            hits.push_back ({ message.getTimeStamp(), juce::String (kit.pads[static_cast<std::size_t> (pad)].id), message.getFloatVelocity() });
        }
    }

    if (hits.empty())
        return ko ("이 킷의 패드에 해당하는 노트가 없습니다");

    std::stable_sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.seconds < b.seconds; });

    stopPlayback();
    recording = false;
    take = std::move (hits);
    length = take.back().seconds + 1.0;
    trackPosition = -1.0;
    imported = true;

    return skipped > 0 ? ko ("맞는 패드가 없어 건너뛴 노트: ") + juce::String (skipped) : juce::String();
}
