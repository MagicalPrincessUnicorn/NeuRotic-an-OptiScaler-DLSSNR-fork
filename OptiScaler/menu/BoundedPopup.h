#pragma once
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <algorithm>

namespace Neurotic {
// Reapply the preferred font-relative size and work-area limit while open.
// A fixed modal body scrolls independently of its action row.
inline void SetBoundedPopupSize(float widthInFonts, float heightInFonts)
{
    const auto* viewport = ImGui::GetMainViewport();
    const float margin = (std::max)(16.0f, ImGui::GetStyle().WindowPadding.x * 2.0f);
    const ImVec2 limit{(std::max)(1.0f, viewport->WorkSize.x - margin),
                       (std::max)(1.0f, viewport->WorkSize.y - margin)};
    const ImVec2 size{(std::min)(limit.x, ImGui::GetFontSize() * widthInFonts),
                      (std::min)(limit.y, ImGui::GetFontSize() * heightInFonts)};
    ImGui::SetNextWindowSizeConstraints({1, 1}, limit);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * .5f,
                           viewport->WorkPos.y + viewport->WorkSize.y * .5f},
                          ImGuiCond_Always, {.5f, .5f});
}
inline bool WrappedPopupCheckbox(const char* label, bool* value)
{
    ImGui::BeginGroup();
    const auto id=ImGui::GetID(label);
    // An empty label hashes to the override seed, retaining the native widget ID.
    ImGui::PushOverrideID(id);
    bool changed=ImGui::Checkbox("",value);
    ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(label);
    ImGui::PopTextWrapPos();
    const auto minimum=ImGui::GetItemRectMin();
    const auto maximum=ImGui::GetItemRectMax();
    ImGui::SetCursorScreenPos(minimum);
    if(ImGui::InvisibleButton("##WrappedCaption",{(std::max)(1.f,maximum.x-minimum.x),(std::max)(1.f,maximum.y-minimum.y)})){
        *value=!*value;changed=true;ImGui::MarkItemEdited(id);
    }
    ImGui::PopID();
    ImGui::EndGroup();
    return changed;
}
}
