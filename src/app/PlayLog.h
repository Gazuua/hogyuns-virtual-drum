#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <string>

/** 연주 로그의 파일. 프로그램이 켜져 있는 동안 친 것과 그때의 상황(곡, 템포, 킷 등)을 파일 하나에 차례로 적는다.

    한 줄이 JSON 객체 하나다 (JSON Lines, UTF-8). 모든 줄에 t(초)와 e(무슨 일인지)가 있다.
    무엇을 언제 적는지는 Session이 정한다 (Session.cpp의 "연주 로그"). 줄의 뜻은 CLAUDE.md의 "연주 로그"에 있다.

    파일은 begin()을 부를 때 만든다. Session은 처음으로 무언가를 쳤을 때 부른다.
    그래서 켰다가 치지 않고 끈 날에는 파일이 생기지 않는다.

    화면 스레드에서만 쓴다.
*/
class PlayLog
{
public:
    /** 줄 하나를 만들어 가는 도우미. 값은 붙인 순서대로 적힌다. */
    class Line
    {
    public:
        Line (double seconds, const char* event);

        Line& text (const char* name, const juce::String& value);
        Line& number (const char* name, double value, int decimals);
        Line& integer (const char* name, juce::int64 value);
        Line& flag (const char* name, bool value);

        /** 닫는 괄호까지 붙인 한 줄 (줄바꿈은 없다). */
        juce::String toString() const                { return body + "}"; }

    private:
        juce::String body;
    };

    PlayLog() = default;
    ~PlayLog()                                       { stop(); }

    /** folder 안에 적을 준비를 한다. 파일은 begin()에서 만든다. */
    void start (const juce::File& folder);

    /** 남은 것을 디스크에 넘기고 파일을 닫는다. */
    void stop();

    /** start()를 부른 뒤인지. */
    bool isActive() const noexcept                   { return active; }

    /** 파일을 만든다. 이미 있으면 그대로 둔다. 만들지 못하면 false를 돌려주고, 그 뒤로는 아무것도 적지 않는다. */
    bool begin();

    bool hasFile() const noexcept                    { return stream != nullptr; }

    /** 지금 적고 있는 파일. 아직 만들지 않았으면 빈 값이다. */
    juce::File getFile() const                       { return file; }

    /** 한 줄을 적는다. 파일이 아직 없으면 버린다. */
    void write (const Line& line);

    /** 한 줄을 적는다. 파일이 아직 없으면 최근 몇 줄만 간직했다가 writeKept()에서 적는다.
        (처음 친 타격보다 조금 앞서 들어오는 키 누름을 잃지 않기 위한 것이다.) */
    void writeOrKeep (const Line& line);
    void writeKept();

    /** 모아 둔 줄을 파일에 쓴다. Session이 1초에 한 번쯤 부른다.
        운영체제에 넘기기만 하고 디스크에 닿을 때까지 기다리지는 않는다 (기다리면 화면이 잠깐 멈출 수 있다).
        프로그램이 죽어도 넘긴 데까지는 남는다. */
    void flush();

private:
    void append (const juce::String& line);

    static constexpr int maxKeptLines = 16;
    static constexpr std::size_t maxWaitingBytes = 64 * 1024;

    juce::File folder, file;
    std::unique_ptr<juce::FileOutputStream> stream;
    std::string waiting;        // 아직 파일에 쓰지 않은 줄들
    juce::StringArray kept;
    bool active = false;
    bool failed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayLog)
};
