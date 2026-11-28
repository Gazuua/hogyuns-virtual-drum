#pragma once

#include <functional>
#include <memory>

#include "Ui.h"

/** 화면 도우미 가운데 아직 Ui.h에 없는 것 (점검 보고서 modules.md 제안 8의 모양 그대로).
    Ui.h를 고치는 작업에서 그쪽으로 옮기고 이 파일은 지운다. */
namespace ui
{

/** 켜고 끄는 체크. 포커스를 갖지 않는다. */
inline void setupToggle (juce::Component& parent, juce::ToggleButton& toggle, const juce::String& text,
                         std::function<void()> onClick, bool initial = false)
{
    toggle.setButtonText (text);
    toggle.setToggleState (initial, juce::dontSendNotification);
    toggle.onClick = std::move (onClick);
    noFocus (toggle);
    parent.addAndMakeVisible (toggle);
}

/** 파일 고르기 창을 띄운다. 하나라도 고르면 onChosen이 불린다 (취소하면 불리지 않는다).
    창은 holder가 들고 있는 동안 산다. */
inline void chooseFile (std::unique_ptr<juce::FileChooser>& holder, const juce::String& title, const juce::File& start,
                        const juce::String& patterns, int flags, std::function<void (const juce::Array<juce::File>&)> onChosen)
{
    holder = std::make_unique<juce::FileChooser> (title, start, patterns);
    holder->launchAsync (flags, [onChosen = std::move (onChosen)] (const juce::FileChooser& chooser)
    {
        if (const auto files = chooser.getResults(); ! files.isEmpty() && files.getFirst() != juce::File() && onChosen != nullptr)
            onChosen (files);
    });
}

} // namespace ui
