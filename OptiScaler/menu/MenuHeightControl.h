#pragma once
#include "SleekShell.h"

namespace Neurotic::Sleek
{
// UI-session height only; this does not write game settings or change rendering.
class MenuHeightControl
{
    float logicalHeight = 0;
    float dragHeight = 0;
    float dragMouseY = 0;
public:
    void Reset() { logicalHeight=dragHeight=dragMouseY=0; }
    ImVec2 Size(ImVec2 viewport, float scale) const
    {
        auto size = WindowSize(viewport, scale);
        if (logicalHeight > 0)
        {
            const float maximum = (std::max)(32.0f, viewport.y - 24.0f);
            size.y = ImClamp(logicalHeight * scale, (std::min)(420.0f * scale, maximum), maximum);
        }
        return size;
    }
    float RowHeight() const
    {
        return ImGui::GetFontSize() * .65f + ImGui::GetStyle().FramePadding.y * 2;
    }
    void Draw(ImVec2 viewport, float scale, float footerHeight)
    {
        const auto position = ImGui::GetWindowPos();
        const auto size = ImGui::GetWindowSize();
        const auto padding = ImGui::GetStyle().WindowPadding;
        ImGui::SetCursorScreenPos({position.x + padding.x,
            position.y + size.y - FooterBottomInset() - footerHeight - RowHeight() - ImGui::GetStyle().ItemSpacing.y});
        ImGui::InvisibleButton("##MenuHeightDrag", {(std::max)(1.0f, size.x - padding.x * 2), RowHeight()});
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemActivated())
        {
            dragHeight = size.y;
            dragMouseY = ImGui::GetIO().MousePos.y;
        }
        if (active && ImGui::GetIO().MouseDown[0] && !ImGui::GetIO().AppFocusLost)
        {
            const float maximum = (std::max)(32.0f, viewport.y - 24.0f);
            logicalHeight = ImClamp(dragHeight + ImGui::GetIO().MousePos.y - dragMouseY,
                                   (std::min)(420.0f * scale, maximum), maximum) / scale;
        }
        if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        const auto low = ImGui::GetItemRectMin();
        const auto high = ImGui::GetItemRectMax();
        const float centerX = (low.x + high.x) * .5f;
        const float centerY = (low.y + high.y) * .5f;
        const float halfWidth = (std::min)((high.x - low.x) * .25f, ImGui::GetFontSize() * 3.0f);
        const auto color = ImGui::GetColorU32(active ? ImGuiCol_SliderGrabActive : hovered ? ImGuiCol_TextLink : ImGuiCol_TextDisabled);
        auto* draw = ImGui::GetWindowDrawList();
        for (float offset : {-2.0f, 2.0f})
            draw->AddLine({centerX - halfWidth, centerY + offset}, {centerX + halfWidth, centerY + offset}, color, 1.0f);
    }
};
}
