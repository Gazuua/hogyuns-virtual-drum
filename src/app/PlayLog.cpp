#include "PlayLog.h"

#include <cmath>

//==============================================================================
PlayLog::Line::Line (double seconds, const char* event)
{
    body << "{\"t\":" << juce::String (std::isfinite (seconds) ? seconds : 0.0, 4) << ",\"e\":\"" << event << "\"";
}

PlayLog::Line& PlayLog::Line::text (const char* name, const juce::String& value)
{
    // JSON::toString이 따옴표를 붙이고 특수 문자를 바꿔 준다. 한글과 일본어는 그대로(UTF-8) 적힌다.
    body << ",\"" << name << "\":" << juce::JSON::toString (juce::var (value), true);
    return *this;
}

PlayLog::Line& PlayLog::Line::number (const char* name, double value, int decimals)
{
    body << ",\"" << name << "\":" << juce::String (std::isfinite (value) ? value : 0.0, decimals);
    return *this;
}

PlayLog::Line& PlayLog::Line::integer (const char* name, juce::int64 value)
{
    body << ",\"" << name << "\":" << juce::String (value);
    return *this;
}

PlayLog::Line& PlayLog::Line::flag (const char* name, bool value)
{
    body << ",\"" << name << "\":" << (value ? "true" : "false");
    return *this;
}

//==============================================================================
void PlayLog::start (const juce::File& folderToUse)
{
    stop();

    folder = folderToUse;
    file = juce::File();
    failed = false;
    active = folder != juce::File();
}

void PlayLog::stop()
{
    if (stream != nullptr)
    {
        flush();
        stream->flush();    // 닫을 때는 디스크에 닿을 때까지 기다린다
        stream.reset();
    }

    waiting.clear();
    kept.clear();
    active = false;
}

bool PlayLog::begin()
{
    if (stream != nullptr)
        return true;

    if (! active || failed)
        return false;

    failed = true;    // 아래에서 성공하면 되돌린다

    if (! folder.createDirectory())
        return false;

    const auto stem = "play-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    auto target = folder.getChildFile (stem + ".jsonl");

    // 같은 초에 프로그램 두 개가 치기 시작했을 때를 위해, 이름이 겹치면 번호를 붙인다.
    for (int number = 2; target.exists() && number < 100; ++number)
        target = folder.getChildFile (stem + "-" + juce::String (number) + ".jsonl");

    // 스트림의 버퍼를 가장 작게 둔다. 그러면 flush()에서 한 번에 넘기는 덩어리가 버퍼에 머물지 않고 바로 파일로 간다.
    auto opened = target.createOutputStream (16);

    if (opened == nullptr || opened->failedToOpen())
        return false;

    stream = std::move (opened);
    file = target;
    failed = false;
    return true;
}

void PlayLog::write (const Line& line)
{
    if (stream != nullptr)
        append (line.toString());
}

void PlayLog::writeOrKeep (const Line& line)
{
    if (stream != nullptr)
    {
        append (line.toString());
        return;
    }

    if (! active || failed)
        return;

    kept.add (line.toString());

    if (kept.size() > maxKeptLines)
        kept.remove (0);
}

void PlayLog::writeKept()
{
    if (stream != nullptr)
        for (const auto& line : kept)
            append (line);

    kept.clear();
}

void PlayLog::flush()
{
    if (stream != nullptr && ! waiting.empty())
        stream->write (waiting.data(), waiting.size());

    waiting.clear();
}

void PlayLog::append (const juce::String& line)
{
    waiting.append (line.toRawUTF8());
    waiting.push_back ('\n');

    if (waiting.size() > maxWaitingBytes)
        flush();
}
