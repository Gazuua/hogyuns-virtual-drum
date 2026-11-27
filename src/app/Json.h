#pragma once

#include <juce_core/juce_core.h>

/** JSON 파일(킷, 키 배열, 설정)을 읽고 쓰는 작은 도우미. 화면 파일의 같은 이름 함수와 섞이지 않게 이름공간에 둔다. */
namespace json
{

/** 정수나 실수로 적힌 값인지. 문자열로 적힌 숫자는 아니다. */
inline bool isNumber (const juce::var& value)
{
    return value.isDouble() || value.isInt() || value.isInt64();
}

/** 숫자로 적혀 있으면 그 값, 아니면 fallback. */
inline double numberOr (const juce::var& value, double fallback)
{
    return isNumber (value) ? static_cast<double> (value) : fallback;
}

/** object의 key 항목이 숫자로 적혀 있으면 그 값, 아니면 fallback. */
inline float numberOr (const juce::var& object, const char* key, float fallback)
{
    const auto& value = object[key];
    return isNumber (value) ? static_cast<float> (static_cast<double> (value)) : fallback;
}

/** 파일을 읽어 JSON 객체 하나로 푼다. 파일이 없거나, 풀지 못하거나, 객체가 아니면 false이고 root는 그대로다. */
inline bool loadJsonObject (const juce::File& source, juce::var& root)
{
    juce::var parsed;

    if (! source.existsAsFile() || juce::JSON::parse (source.loadFileAsString(), parsed).failed() || ! parsed.isObject())
        return false;

    root = parsed;
    return true;
}

/** 저장하다 중간에 끊겨도 원래 파일이 망가지지 않도록 임시 파일에 쓴 뒤 바꿔치기한다. */
inline bool writeTextSafely (const juce::File& target, const juce::String& text)
{
    juce::TemporaryFile temporary (target);

    if (! temporary.getFile().replaceWithText (text))
        return false;

    return temporary.overwriteTargetFileWithTemporary();
}

} // namespace json
