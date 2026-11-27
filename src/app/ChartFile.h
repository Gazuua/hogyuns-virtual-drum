#pragma once

#include <juce_core/juce_core.h>

#include "core/Chart.h"

/** 채보를 JSON 글로 적는다 / JSON 글에서 읽는다. header의 항목은 맨 앞에 그대로 적는다. */
juce::String chartToJson (const hvd::Chart& chart, const juce::var& header);
bool chartFromJson (const juce::String& text, hvd::Chart& chart, juce::var* header = nullptr);

/** 채보 파일을 읽는다. 파일이 없거나 채보가 아니면 false. */
bool readChartFile (const juce::File& file, hvd::Chart& chart, juce::var* header = nullptr);

/** 채보 파일을 쓴다. 쓰지 못하면 false. */
bool writeChartFile (const juce::File& file, const hvd::Chart& chart, const juce::var& header);
