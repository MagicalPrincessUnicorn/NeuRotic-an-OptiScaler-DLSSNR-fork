#pragma once
#include "SleekUi.h"
#include "Localization.h"
namespace Neurotic::Sleek {
// The accepted Upscalers card treatment. Call explicitly from scoped cards;
// protected DLSS and Frame Generation keep their existing heading call sites.
inline void WindowSectionHeader(const char* label){
 const auto origin=ImGui::GetCursorScreenPos();
 const auto* window=ImGui::GetCurrentWindow();
 const float scale=ImGui::GetIO().FontGlobalScale*window->FontWindowScale*window->FontDpiScale;
 ImGui::PushFont(ImGui::GetFont(),ImGui::GetFontSize()*1.25f/scale);
 ImGui::TextUnformatted(label);
 const auto title=Neurotic::Translate(label);
 ImGui::GetWindowDrawList()->AddText({origin.x+.45f,origin.y},ImGui::GetColorU32(ImGuiCol_Text),title.c_str());
 ImGui::PopFont();
}
}
