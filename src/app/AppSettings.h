#pragma once

#include <juce_core/juce_core.h>

/** 프로그램 설정: %APPDATA%\HogyunsVirtualDrum\settings.json (고른 킷·키 배열, 전체 음량 등). 화면 스레드에서만 쓴다. */
class AppSettings
{
public:
    /** remember가 false이면 파일을 읽지도 쓰지도 않는다 (자체 검사와 도구 실행). */
    explicit AppSettings (bool remember);

    static juce::File getFile();

    juce::var get (const juce::Identifier& name, const juce::var& fallback) const;

    /** 값이 바뀌었으면 바로 파일에 쓴다. 미뤄 둔 값도 함께 적힌다. */
    void set (const juce::Identifier& name, const juce::var& value);

    /** 값을 바꾸기만 하고, 파일에는 saveIfPending()에서 쓴다 (슬라이더를 끄는 동안). */
    void setLater (const juce::Identifier& name, const juce::var& value);

    bool isPending() const noexcept       { return pending; }
    void saveIfPending();

private:
    void save();

    bool remember;
    bool pending = false;
    juce::var settings;
};
