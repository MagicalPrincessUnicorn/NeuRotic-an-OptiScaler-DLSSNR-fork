#pragma once
#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <cstdint>
namespace Neurotic::MenuLayout {
inline unsigned Corner(unsigned value) { return value < 4 ? value : 0; }
inline std::optional<float> ManualScale(std::optional<float> value) {
    if (!value || !std::isfinite(*value)) return std::nullopt;
    return std::clamp(*value, .5f, 2.f);
}
inline bool ValidExtent(ImVec2 size) {
    return std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0 && size.y > 0 &&
           size.x <= 65536 && size.y <= 65536;
}
struct Area { ImVec2 origin, size; };
// Display-only fit: preserve Config's requested scale and recover it as space
// grows. Reserve a compact 480x480 logical shell, including normal-size
// scrollable navigation. Scrollable pages retain their own content.
inline float DisplayScale(float requested, Area area) {
    requested=std::isfinite(requested)?std::clamp(requested,.5f,2.f):.5f;
    if(!ValidExtent(area.size))return requested;
    const float fitX=(std::max)(1.f,area.size.x-24.f)/480.f;
    const float fitY=(std::max)(1.f,area.size.y-24.f)/480.f;
    return (std::max)(.5f,(std::min)({requested,fitX,fitY}));
}
inline ImVec2 FitSize(ImVec2 requested, Area area) {
    const float x = (std::max)(1.f, area.size.x - 24.f);
    const float y = (std::max)(1.f, area.size.y - 24.f);
    return {std::clamp(requested.x, 1.f, x), std::clamp(requested.y, 1.f, y)};
}
inline ImVec2 Clamp(ImVec2 point, Area area, ImVec2 size) {
    const float marginX = (std::min)(12.f, (std::max)(0.f, (area.size.x-size.x)*.5f));
    const float marginY = (std::min)(12.f, (std::max)(0.f, (area.size.y-size.y)*.5f));
    const float left = area.origin.x + marginX, top = area.origin.y + marginY;
    return {std::clamp(point.x, left, (std::max)(left,area.origin.x+area.size.x-size.x-marginX)),
            std::clamp(point.y, top, (std::max)(top,area.origin.y+area.size.y-size.y-marginY))};
}
inline ImVec2 Anchor(unsigned corner, Area area, ImVec2 size) {
    corner = Corner(corner);
    return Clamp({area.origin.x + ((corner&1) ? area.size.x-size.x-12.f : 12.f),
                  area.origin.y + ((corner&2) ? area.size.y-size.y-12.f : 12.f)},area,size);
}
inline bool Same(ImVec2 a, ImVec2 b) { return std::abs(a.x-b.x)<.51f && std::abs(a.y-b.y)<.51f; }
// Anchor on first use/explicit selection. Once dragged, keep the user's position
// and only clamp when needed. Resizing an undragged corner follows that corner.
class Placement {
    bool initialized=false, dragged=false, reset=false;
    unsigned previousCorner=0;
    ImVec2 previousPosition{}, previousSize{};
    Area previousArea{};
public:
    void Reset() { reset=true; }
    ImVec2 Resolve(unsigned corner, Area area, ImVec2 size, const ImVec2* existing) {
        corner=Corner(corner);
        const bool newChoice=!initialized || reset || previousCorner!=corner || !existing;
        if (!newChoice && existing && !Same(*existing,previousPosition)) dragged=true;
        const bool geometry=!Same(area.origin,previousArea.origin) || !Same(area.size,previousArea.size) || !Same(size,previousSize);
        ImVec2 position;
        if (newChoice || (!dragged && geometry)) { position=Anchor(corner,area,size); if(newChoice)dragged=false; }
        else position=Clamp(existing ? *existing : previousPosition,area,size);
        initialized=true;reset=false;previousCorner=corner;previousArea=area;previousSize=size;previousPosition=position;
        return position;
    }
};
// Client coordinates and draw pixels use the same proportional projection.
// Preserve the unfocused/absent pointer sentinel rather than clamping it on-screen.
inline ImVec2 Mouse(ImVec2 clientPoint, ImVec2 clientSize, Area canvas) {
    if (!ValidExtent(clientSize) || !ValidExtent(canvas.size) || !std::isfinite(clientPoint.x) ||
        !std::isfinite(clientPoint.y) || clientPoint.x < -1.e30f || clientPoint.y < -1.e30f) return clientPoint;
    return {canvas.origin.x+clientPoint.x*canvas.size.x/clientSize.x,
            canvas.origin.y+clientPoint.y*canvas.size.y/clientSize.y};
}
}
