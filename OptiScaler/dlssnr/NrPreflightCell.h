#pragma once

#include <imgui/imgui.h>
#include <menu/Localization.h>
#include <algorithm>
#include <string>
#include <string_view>

namespace NrPreflightCell
{
// Live observations keep one row at the allotted width, including multiline owner messages.
inline void Draw(std::string_view value)
{
    const auto full = Neurotic::Translate(value);
    std::string visible(full);
    for (auto& c : visible) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    const auto pos = ImGui::GetCursorScreenPos();
    const ImVec2 size((std::max)(1.0f, ImGui::GetContentRegionAvail().x), ImGui::GetTextLineHeight());
    const bool clipped = visible != full || ImGui::CalcTextSize(visible.c_str()).x > size.x;
    ImGui::Dummy(size);
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(pos, {pos.x + size.x, pos.y + size.y}, true);
    draw->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), visible.c_str());
    draw->PopClipRect();
    if (clipped && ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
        ImGui::TextUnformatted(full.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
} // namespace NrPreflightCell
