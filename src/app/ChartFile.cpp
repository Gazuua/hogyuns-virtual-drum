#include "ChartFile.h"

juce::String chartToJson (const hvd::Chart& chart, const juce::var& header)
{
    // 노트가 수천 개라서, 한 줄에 하나씩 짧게 직접 적는다.
    juce::String text;
    text.preallocateBytes (chart.notes.size() * 40 + chart.beats.size() * 10 + 1024);
    text << "{\n";

    if (const auto* object = header.getDynamicObject())
        for (const auto& property : object->getProperties())
            text << "  " << juce::JSON::toString (juce::var (property.name.toString()), true) << ": "
                 << juce::JSON::toString (property.value, true) << ",\n";

    text << "  \"duration\": " << juce::String (chart.duration, 3) << ",\n"
         << "  \"tempo\": " << juce::String (chart.tempo, 2) << ",\n"
         << "  \"firstDownbeat\": " << chart.firstDownbeat << ",\n"
         << "  \"beats\": [";

    for (std::size_t i = 0; i < chart.beats.size(); ++i)
        text << (i > 0 ? "," : "") << juce::String (chart.beats[i], 3);

    // 노트 한 줄: [시각, 레인 이름, 세기, 박 격자 위의 자리, 표시]
    text << "],\n  \"notes\": [\n";

    for (std::size_t i = 0; i < chart.notes.size(); ++i)
    {
        const auto& note = chart.notes[i];
        text << "    [" << juce::String (note.seconds, 3) << ",\"" << hvd::laneName (note.lane) << "\","
             << juce::String (note.velocity, 2) << "," << juce::String (note.beat, 4) << "," << static_cast<int> (note.flags) << "]"
             << (i + 1 < chart.notes.size() ? ",\n" : "\n");
    }

    text << "  ]\n}\n";
    return text;
}

bool chartFromJson (const juce::String& text, hvd::Chart& chart, juce::var* header)
{
    juce::var parsed;

    if (juce::JSON::parse (text, parsed).failed() || ! parsed.isObject())
        return false;

    chart = {};
    chart.duration = static_cast<double> (parsed["duration"]);
    chart.tempo = static_cast<float> (static_cast<double> (parsed["tempo"]));
    chart.firstDownbeat = static_cast<int> (parsed["firstDownbeat"]);

    if (const auto* beats = parsed["beats"].getArray())
        for (const auto& beat : *beats)
            chart.beats.push_back (static_cast<double> (beat));

    if (const auto* notes = parsed["notes"].getArray())
    {
        chart.notes.reserve (static_cast<std::size_t> (notes->size()));

        for (const auto& item : *notes)
        {
            const auto* fields = item.getArray();
            hvd::ChartNote note;

            if (fields == nullptr || fields->size() < 3
                || ! hvd::laneFromName (fields->getReference (1).toString().toStdString(), note.lane))
                continue;

            note.seconds = static_cast<double> (fields->getReference (0));
            note.velocity = static_cast<float> (static_cast<double> (fields->getReference (2)));
            note.beat = fields->size() > 3 ? static_cast<double> (fields->getReference (3)) : -1.0;
            note.flags = fields->size() > 4 ? static_cast<std::uint8_t> (static_cast<int> (fields->getReference (4))) : std::uint8_t { 0 };
            chart.notes.push_back (note);
        }
    }

    if (header != nullptr)
        *header = parsed;

    return true;
}

bool readChartFile (const juce::File& file, hvd::Chart& chart, juce::var* header)
{
    return file.existsAsFile() && chartFromJson (file.loadFileAsString(), chart, header);
}

bool writeChartFile (const juce::File& file, const hvd::Chart& chart, const juce::var& header)
{
    return file.replaceWithText (chartToJson (chart, header));
}
