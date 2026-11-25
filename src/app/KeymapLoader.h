#pragma once

#include <juce_core/juce_core.h>

#include "core/Mapping.h"

/** 키 배열 파일을 읽은 결과. */
struct KeymapLoadResult
{
    bool ok = false;
    hvd::MappingSpec spec;
    juce::String error;
    juce::StringArray warnings;
};

/** 키 배열 JSON을 읽는다. 패드 이름은 아직 풀지 않는다 (hvd::resolveMapping이 한다).

    형식:
    {
      "name": "기본",
      "tempo": 120,
      "bindings": [
        { "key": "KeyJ", "pad": "snare", "velocity": 1.0 },
        { "key": "Space", "pads": ["kick", "crash1"] },
        { "key": "KeyM", "action": "choke", "pads": ["crash1", "crash2"] },
        { "key": "Digit5", "pad": "snare", "stroke": "repeat", "rate": "1/16", "accentEvery": 4 },
        { "key": "Backquote", "action": "accent" },
        { "key": "Tab", "action": "pedal", "swap": { "hihat_closed": "hihat_open" }, "releasePad": "hihat_pedal" }
      ]
    }

    줄마다 쓸 수 있는 항목은 hvd::BindingSpec(src/core/Mapping.h)에 적혀 있다.
*/
KeymapLoadResult loadKeymap (const juce::File& keymapFile);
