#pragma once
#include <imgui/imgui.h>
#include <algorithm>
#include "Localization.h"
namespace Neurotic {
// Keep the established width unless the displayed preset needs more room.
// This changes presentation only; the caller still owns the label/value/commit.
inline void FitPresetComboWidth(const char* label,const char* preview) {
    const auto& style=ImGui::GetStyle();
    const auto text=Translate(preview);
    const float desired=ImGui::CalcTextSize(text.c_str()).x+ImGui::GetFrameHeight()+2*style.FramePadding.x;
    const float caption=ImGui::CalcTextSize(label,nullptr,true).x;
    const float available=ImGui::GetContentRegionAvail().x-(caption>0?caption+style.ItemInnerSpacing.x:0);
    ImGui::SetNextItemWidth((std::max)(1.f,(std::min)(available,(std::max)(ImGui::CalcItemWidth(),desired))));
}
}
