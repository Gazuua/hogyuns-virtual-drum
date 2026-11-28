#pragma once

#include <optional>

#include "Documents.h"
#include "KitView.h"

/** 드럼 그림 위의 자리를 킷 파일에 읽고 쓴다. 패드마다 "view"(원래 자리)와 "fixedView"(하이햇 닫힘 고정 자리)에
    { "x", "y", "size" }로 적는다. x, y는 0 ~ 1, size는 그림 높이에 대한 지름의 비율이다. */
namespace kitlayout
{

inline constexpr const char* viewKey = "view";
inline constexpr const char* fixedViewKey = "fixedView";

struct Placement
{
    float x = 0.5f, y = 0.5f, size = 0.2f;
};

/** 적어 둔 자리. 없으면 빈 값. */
inline std::optional<Placement> read (const KitDocument& document, const juce::String& padId, const char* key)
{
    const auto view = document.getPadProperty (padId, key);

    if (! view.isObject())
        return std::nullopt;

    return Placement { juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (view["x"]))),
                       juce::jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (view["y"]))),
                       KitView::clampSize (static_cast<float> (static_cast<double> (view["size"]))) };
}

/** 자리를 소수 셋째 자리까지 적는다. 저장은 하지 않는다. */
inline void write (KitDocument& document, const juce::String& padId, const char* key, Placement placement)
{
    auto* view = new juce::DynamicObject();
    view->setProperty ("x", juce::roundToInt (placement.x * 1000.0f) / 1000.0);
    view->setProperty ("y", juce::roundToInt (placement.y * 1000.0f) / 1000.0);
    view->setProperty ("size", juce::roundToInt (placement.size * 1000.0f) / 1000.0);
    document.setPadProperty (padId, key, juce::var (view));
}

/** 모든 패드의 자리를 지워 기본 배치로 돌린다. 저장은 하지 않는다. */
inline void clearAll (KitDocument& document)
{
    for (const auto& id : document.getPadIds())
    {
        document.setPadProperty (id, viewKey, {});
        document.setPadProperty (id, fixedViewKey, {});
    }
}

} // namespace kitlayout
