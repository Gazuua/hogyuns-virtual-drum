#include "SfzImport.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <vector>

#include "Text.h"

namespace
{

using Opcodes = std::map<juce::String, juce::String>;

struct Region
{
    juce::String sample;      // 절대 경로
    int lowKey = 0, highKey = 127;
    int lowVelocity = 0, highVelocity = 127;
    int sequencePosition = 1;
    double volumeDb = 0.0;
    double pan = 0.0;         // -100 ~ 100
    juce::String group, offBy;
};

struct StandardPad
{
    int note;
    const char* id;
    const char* name;
};

// General MIDI 드럼 번호와 기본 킷의 패드 id. 같은 id에 번호가 여럿이면 먼저 나온 것이 그 id를 갖는다.
constexpr StandardPad standardPads[] = {
    { 36, "kick", "킥" },               { 35, "kick", "킥 2" },
    { 38, "snare", "스네어" },          { 40, "snare_rim", "스네어 림" },
    { 37, "sidestick", "사이드 스틱" },
    { 42, "hihat_closed", "하이햇 닫힘" }, { 46, "hihat_open", "하이햇 열림" }, { 44, "hihat_pedal", "하이햇 페달" },
    { 48, "tom_high", "하이 탐" },      { 50, "tom_high", "하이 탐 2" },
    { 45, "tom_mid", "미드 탐" },       { 47, "tom_mid", "미드 탐 2" },
    { 43, "tom_floor", "플로어 탐" },   { 41, "tom_low", "로 탐" },
    { 49, "crash1", "크래시 1" },       { 57, "crash2", "크래시 2" },
    { 52, "china", "차이나" },          { 55, "crash_bright", "스플래시" },
    { 51, "ride", "라이드" },           { 53, "ride_bell", "라이드 벨" }, { 59, "ride_edge", "라이드 2" },
};

/** "36", "c2", "f#1" 같은 건반 표기를 노트 번호로 바꾼다. 읽지 못하면 -1. */
int parseKey (const juce::String& text)
{
    const auto value = text.trim().toLowerCase();

    if (value.isEmpty())
        return -1;

    if (value.containsOnly ("0123456789"))
        return juce::jlimit (0, 127, value.getIntValue());

    static const int offsets[] = { 9, 11, 0, 2, 4, 5, 7 };    // a b c d e f g
    const auto letter = value[0];

    if (letter < 'a' || letter > 'g')
        return -1;

    auto note = offsets[letter - 'a'];
    auto rest = value.substring (1);

    if (rest.startsWithChar ('#'))
    {
        ++note;
        rest = rest.substring (1);
    }
    else if (rest.startsWithChar ('b'))
    {
        --note;
        rest = rest.substring (1);
    }

    // SFZ에서는 c4가 60이다.
    return juce::jlimit (0, 127, note + (rest.getIntValue() + 1) * 12);
}

/** 주석을 지우고 #include와 #define을 풀어 한 덩어리의 글로 만든다. */
juce::String preprocess (const juce::File& file, std::map<juce::String, juce::String>& defines, int depth)
{
    juce::String text;

    for (auto line : juce::StringArray::fromLines (file.loadFileAsString()))
    {
        if (const auto comment = line.indexOf ("//"); comment >= 0)
            line = line.substring (0, comment);

        const auto trimmed = line.trim();

        if (trimmed.startsWith ("#include") && depth < 8)
        {
            const auto name = trimmed.fromFirstOccurrenceOf ("\"", false, false).upToFirstOccurrenceOf ("\"", false, false);
            const auto included = file.getParentDirectory().getChildFile (name.replaceCharacter ('\\', '/'));

            if (included.existsAsFile())
                text << preprocess (included, defines, depth + 1) << "\n";

            continue;
        }

        if (trimmed.startsWith ("#define"))
        {
            const auto parts = juce::StringArray::fromTokens (trimmed, " \t", {});

            if (parts.size() >= 3)
                defines[parts[1]] = parts[2];

            continue;
        }

        text << line << "\n";
    }

    return text;
}

/** SFZ 글을 머리말(<region> 등)과 그 아래의 "이름=값" 묶음으로 나눈다. */
std::vector<std::pair<juce::String, Opcodes>> parseSections (const juce::String& source)
{
    std::vector<std::pair<juce::String, Opcodes>> sections;
    const auto text = source.replaceCharacters ("\r\n\t", "   ");
    const auto length = text.length();
    auto position = 0;

    // 값은 다음 "이름=" 이나 "<머리말>" 앞에서 끝난다. 값 안에 빈칸이 들어갈 수 있다 (파일 이름).
    const auto findValueEnd = [&text, length] (int from)
    {
        for (auto i = from; i < length; ++i)
        {
            if (text[i] == '<')
                return i;

            if (text[i] == '=')
            {
                // 이 '='의 이름이 시작하는 곳(직전 빈칸 다음)이 값의 끝이다.
                auto start = i;

                while (start > from && text[start - 1] != ' ')
                    --start;

                return start;
            }
        }

        return length;
    };

    while (position < length)
    {
        const auto c = text[position];

        if (c == '<')
        {
            const auto close = text.indexOfChar (position, '>');

            if (close < 0)
                break;

            sections.push_back ({ text.substring (position + 1, close).trim().toLowerCase(), {} });
            position = close + 1;
        }
        else if (c == ' ')
        {
            ++position;
        }
        else
        {
            const auto equals = text.indexOfChar (position, '=');

            if (equals < 0)
                break;

            const auto name = text.substring (position, equals).trim().toLowerCase();
            const auto end = findValueEnd (equals + 1);

            if (! sections.empty() && name.isNotEmpty() && ! name.containsChar (' '))
                sections.back().second[name] = text.substring (equals + 1, end).trim();

            position = juce::jmax (end, equals + 1);
        }
    }

    return sections;
}

} // namespace

SfzImportResult importSfz (const juce::File& sfzFile, const juce::File& kitsFolder)
{
    SfzImportResult result;

    if (! sfzFile.existsAsFile())
    {
        result.error = ko ("파일이 없습니다: ") + sfzFile.getFullPathName();
        return result;
    }

    std::map<juce::String, juce::String> defines;
    auto text = preprocess (sfzFile, defines, 0);

    for (const auto& [name, value] : defines)
        text = text.replace (name, value);

    // 위쪽 머리말에 적힌 값은 아래쪽 머리말이 따로 적지 않는 한 그대로 이어받는다.
    Opcodes control, global, master, group;
    std::vector<Region> regions;

    for (const auto& [header, opcodes] : parseSections (text))
    {
        if (header == "control")       { control = opcodes; continue; }
        if (header == "global")        { global = opcodes; master.clear(); group.clear(); continue; }
        if (header == "master")        { master = opcodes; group.clear(); continue; }
        if (header == "group")         { group = opcodes; continue; }
        if (header != "region")        continue;

        auto merged = global;

        for (const auto* level : std::initializer_list<const Opcodes*> { &master, &group, &opcodes })
            for (const auto& [name, value] : *level)
                merged[name] = value;

        const auto get = [&merged] (const char* name, const juce::String& fallback = {})
        {
            const auto found = merged.find (name);
            return found != merged.end() ? found->second : fallback;
        };

        // 뗄 때 나는 소리(릴리스 샘플)와 샘플이 없는 영역은 건너뛴다.
        if (get ("sample").isEmpty() || get ("trigger", "attack") != "attack")
            continue;

        Region region;
        const auto path = (control["default_path"] + get ("sample")).replaceCharacter ('\\', '/');
        region.sample = sfzFile.getParentDirectory().getChildFile (path).getFullPathName().replaceCharacter ('\\', '/');

        if (const auto key = parseKey (get ("key")); key >= 0)
        {
            region.lowKey = region.highKey = key;
        }
        else
        {
            const auto centre = parseKey (get ("pitch_keycenter"));
            region.lowKey = juce::jmax (0, parseKey (get ("lokey", centre >= 0 ? juce::String (centre) : "0")));
            region.highKey = juce::jmax (region.lowKey, parseKey (get ("hikey", centre >= 0 ? juce::String (centre) : "127")));
        }

        region.lowVelocity = juce::jlimit (0, 127, get ("lovel", "0").getIntValue());
        region.highVelocity = juce::jlimit (region.lowVelocity, 127, get ("hivel", "127").getIntValue());
        region.sequencePosition = juce::jmax (1, get ("seq_position", "1").getIntValue());
        region.volumeDb = get ("volume", "0").getDoubleValue();
        region.pan = get ("pan", "0").getDoubleValue();
        region.group = get ("group");
        region.offBy = get ("off_by");
        regions.push_back (std::move (region));
    }

    if (regions.empty())
    {
        result.error = ko ("이 SFZ 파일에서 쓸 수 있는 영역(region)을 찾지 못했습니다");
        return result;
    }

    // 건반 범위가 같은 영역끼리 모아 패드 하나로 만든다. (노트 번호 순서)
    std::map<std::pair<int, int>, std::vector<const Region*>> byKey;

    for (const auto& region : regions)
        byKey[{ region.lowKey, region.highKey }].push_back (&region);

    juce::Array<juce::var> pads;
    juce::StringArray usedIds;
    juce::StringArray countedSamples;

    for (const auto& [keys, padRegions] : byKey)
    {
        if (pads.size() >= 128)
            break;

        const auto note = keys.first;
        auto id = "note_" + juce::String (note);
        auto name = ko ("노트 ") + juce::String (note);

        for (const auto& standard : standardPads)
        {
            if (standard.note == note)
            {
                name = ko (standard.name);

                if (! usedIds.contains (standard.id))
                    id = standard.id;

                break;
            }
        }

        usedIds.add (id);

        // 세기 구간마다 레이어 하나. 같은 구간의 샘플은 seq_position 순서로 번갈아 난다.
        std::map<std::pair<int, int>, std::vector<const Region*>> byVelocity;

        for (const auto* region : padRegions)
            byVelocity[{ region->highVelocity, region->lowVelocity }].push_back (region);

        juce::Array<juce::var> layers;

        for (auto& [range, layerRegions] : byVelocity)
        {
            std::stable_sort (layerRegions.begin(), layerRegions.end(),
                              [] (const Region* a, const Region* b) { return a->sequencePosition < b->sequencePosition; });

            juce::Array<juce::var> samples;

            for (const auto* region : layerRegions)
            {
                samples.add (region->sample);

                if (! countedSamples.contains (region->sample))
                {
                    countedSamples.add (region->sample);
                    ++result.numSamples;

                    if (! juce::File (region->sample).existsAsFile())
                        ++result.numMissing;
                }
            }

            auto* layer = new juce::DynamicObject();
            layer->setProperty ("upTo", juce::roundToInt (range.first / 127.0 * 1000.0) / 1000.0);
            layer->setProperty ("samples", samples);
            layers.add (juce::var (layer));
        }

        const auto& first = *padRegions.front();

        auto* pad = new juce::DynamicObject();
        pad->setProperty ("id", id);
        pad->setProperty ("name", name);
        pad->setProperty ("midiNote", note);
        pad->setProperty ("volume", juce::roundToInt (juce::jlimit (0.0, 4.0, std::pow (10.0, first.volumeDb / 20.0)) * 100.0) / 100.0);
        pad->setProperty ("pan", juce::jlimit (-1.0, 1.0, first.pan / 100.0));

        // SFZ: off_by=X인 소리는 group=X인 소리가 날 때 끊긴다.
        // 여기서는 "끊기는 쪽"이 off<X> 그룹에 속하고, "끊는 쪽"이 off<자기 group>을 끊는 것으로 옮긴다.
        if (first.offBy.isNotEmpty())
            pad->setProperty ("group", "off" + first.offBy);

        juce::Array<juce::var> cuts;

        if (first.group.isNotEmpty())
            cuts.add ("off" + first.group);

        pad->setProperty ("cuts", cuts);
        pad->setProperty ("layers", layers);
        pads.add (juce::var (pad));
    }

    // 새 킷 폴더를 만들고 kit.json을 쓴다.
    auto folderName = juce::File::createLegalFileName (sfzFile.getFileNameWithoutExtension()).trim();

    if (folderName.isEmpty())
        folderName = "sfz";

    const auto folder = kitsFolder.getNonexistentChildFile (folderName, {}, false);

    if (! folder.createDirectory())
    {
        result.error = ko ("킷 폴더를 만들지 못했습니다: ") + folder.getFullPathName();
        return result;
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("name", sfzFile.getFileNameWithoutExtension());
    root->setProperty ("source", ko ("SFZ에서 가져옴: ") + sfzFile.getFullPathName());
    root->setProperty ("license", ko ("원본 SFZ의 라이선스를 따릅니다"));
    root->setProperty ("pads", pads);

    if (! folder.getChildFile ("kit.json").replaceWithText (juce::JSON::toString (juce::var (root)) + "\n"))
    {
        result.error = ko ("kit.json을 쓰지 못했습니다");
        return result;
    }

    result.kitFolderName = folder.getFileName();
    result.numPads = pads.size();
    return result;
}
