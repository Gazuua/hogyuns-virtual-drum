#pragma once

#include <juce_core/juce_core.h>

#include <string>

/** 소스에 적은 한글(UTF-8)을 juce::String으로 바꾼다. */
inline juce::String ko (const char* utf8)
{
    return juce::String::fromUTF8 (utf8);
}

/** 코어가 쓰는 UTF-8 std::string을 juce::String으로 바꾼다. */
inline juce::String fromUtf8 (const std::string& text)
{
    return juce::String::fromUTF8 (text.c_str());
}
