#include "PracticeLog.h"

#include <cstring>
#include <map>

namespace
{

// 키를 누르고 있어서 엔진이 이어 치는 타격(롤, 스웰)은 같은 키의 타격이 이보다 촘촘하게 잇따른다.
// 사람이 키 하나를 이렇게 빨리 거듭 누를 수는 없으므로, 이런 타격은 박자를 잴 때 뺀다.
constexpr double rollGapSeconds = 0.09;

// 같은 곡을 이만큼 넘게 앞으로 되감아 다시 틀면 새 연주로 본다.
constexpr double restartSeconds = 5.0;

/** line에서 key(예: "\"song\":") 바로 뒤의 수를 읽는다. 없으면 false. 로그의 줄은 PlayLog가 쓴 납작한 JSON이다. */
bool numberIn (const juce::String& line, const char* key, double& value)
{
    const auto at = line.indexOf (key);

    if (at < 0)
        return false;

    value = line.substring (at + static_cast<int> (std::strlen (key))).getDoubleValue();
    return true;
}

/** line에서 key(예: "\"pad\":\"") 바로 뒤의, 따옴표로 닫히는 글자열을 읽는다. 패드 id나 키 이름처럼 바꿔 쓴 글자가 없는 값에만 쓴다. */
juce::String wordIn (const juce::String& line, const char* key)
{
    const auto at = line.indexOf (key);

    if (at < 0)
        return {};

    const auto start = at + static_cast<int> (std::strlen (key));
    const auto end = line.indexOfChar (start, '"');
    return end > start ? line.substring (start, end) : juce::String();
}

} // namespace

std::vector<PracticeTake> readPracticeTakes (const juce::File& logFile, int minimumHits)
{
    std::vector<PracticeTake> takes;

    juce::StringArray lines;
    lines.addLines (logFile.loadFileAsString());

    juce::Time logStart = logFile.getCreationTime();
    auto logStartSeconds = 0.0;

    juce::File trackFile;
    juce::var game;                  // 지금 하고 있는 리듬 게임을 시작한 줄. 게임이 아니면 비어 있다
    auto gameSeconds = -1.0e9;       // 그 줄의 시각
    auto speed = 1.0f;
    auto lastSong = -1.0e9;
    auto open = false;               // takes의 마지막 것이 지금 이어지고 있는지

    struct LastHit
    {
        double seconds = -1.0e9;
        juce::String key;
    };

    std::map<juce::String, LastHit> lastHits;    // 패드마다 직전 타격

    for (const auto& line : lines)
    {
        const auto event = wordIn (line, "\"e\":\"");
        auto seconds = 0.0;
        numberIn (line, "\"t\":", seconds);

        if (event == "hit")
        {
            auto song = 0.0;

            if (trackFile == juce::File() || ! numberIn (line, "\"song\":", song))
                continue;    // 곡 없이 친 것

            const auto source = wordIn (line, "\"src\":\"");

            if (source != "key" && source != "mouse" && source != "midi")
                continue;    // 프로그램이 대신 친 것, 다시 재생한 것, 편집 화면에서 들어 본 것

            if (! open)
            {
                PracticeTake take;
                take.logFile = logFile;
                take.track = trackFile;
                take.song = trackFile;
                take.time = logStart + juce::RelativeTime::seconds (seconds - logStartSeconds);
                take.firstSeconds = song;
                take.lastSeconds = song;

                // 리듬 게임에서 친 것이면 원래 곡과 단계를 그 줄에서 가져온다.
                if (game.isObject() && juce::File::isAbsolutePath (game["audio"].toString()) && juce::File (game["audio"].toString()) == trackFile)
                {
                    take.fromGame = true;
                    take.level = static_cast<int> (game["level"]);

                    if (juce::File::isAbsolutePath (game["song"].toString()))
                        take.song = juce::File (game["song"].toString());
                }

                takes.push_back (std::move (take));
                lastHits.clear();
                open = true;
            }

            auto& take = takes.back();
            const auto pad = wordIn (line, "\"pad\":\"");
            const auto key = wordIn (line, "\"key\":\"");

            ++take.numPlayed;
            take.numByKey += source == "key" ? 1 : 0;
            take.numByMouse += source == "mouse" ? 1 : 0;
            take.numByMidi += source == "midi" ? 1 : 0;
            take.firstSeconds = juce::jmin (take.firstSeconds, song);
            take.lastSeconds = juce::jmax (take.lastSeconds, song);
            take.slowestSpeed = juce::jmin (take.slowestSpeed, speed);
            lastSong = song;

            // 같은 키로 같은 패드를 아주 촘촘하게 잇따라 친 것은 누르고 있어서 이어진 타격이다.
            auto& last = lastHits[pad];
            const auto rolled = source == "key" && key.isNotEmpty() && key == last.key && seconds - last.seconds < rollGapSeconds;
            last.seconds = seconds;
            last.key = key;

            if (rolled)
            {
                ++take.numRolled;
                continue;
            }

            hvd::Lane lane;

            if (hvd::laneForPadId (pad.toStdString(), lane))
                take.hits.push_back ({ song, lane, speed });
        }
        else if (event == "play" || event == "speed")
        {
            auto value = 1.0, song = 0.0;

            if (numberIn (line, "\"speed\":", value))
                speed = static_cast<float> (juce::jlimit (0.25, 4.0, value));

            // 같은 곡을 앞으로 되감아 다시 틀었으면 새 연주다.
            if (event == "play" && numberIn (line, "\"song\":", song) && song < lastSong - restartSeconds)
                open = false;
        }
        else if (event == "track" || event == "game" || event == "start")
        {
            // 경로가 들어 있는 줄은 글자가 바꿔 적혀 있으므로(역슬래시 등) JSON으로 제대로 읽는다.
            juce::var parsed;

            if (juce::JSON::parse (line, parsed).failed() || ! parsed.isObject())
                continue;

            if (event == "track")
            {
                const auto path = parsed["file"].toString();
                trackFile = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
                open = false;
                lastSong = -1.0e9;

                // 리듬 게임은 시작하면서 곡을 연다(게임 줄 바로 뒤에 이 줄이 온다). 그게 아니면 게임과 상관없이 연 곡이다.
                if (seconds - gameSeconds > 2.0)
                    game = juce::var();
            }
            else if (event == "game")
            {
                const auto state = parsed["state"].toString();

                if (state == "start")
                {
                    game = parsed;
                    gameSeconds = seconds;
                    open = false;
                    lastSong = -1.0e9;
                }
                else if (state == "stop" || state == "finish")
                {
                    game = juce::var();
                    open = false;
                }
            }
            else
            {
                const auto started = juce::Time::fromISO8601 (parsed["time"].toString());

                if (started != juce::Time())
                {
                    logStart = started;
                    logStartSeconds = seconds;
                }
            }
        }
    }

    std::erase_if (takes, [minimumHits] (const PracticeTake& take) { return take.numPlayed < minimumHits; });
    return takes;
}
