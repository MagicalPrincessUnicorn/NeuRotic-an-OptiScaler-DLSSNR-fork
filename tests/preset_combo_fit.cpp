#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "menu/Localization.h"
#include "menu/localization/LanguageRuntime.h"
#include "menu/localization/LanguageBundles.h"
#if __has_include("menu/PresetComboLayout.h")
#include "menu/PresetComboLayout.h"
#else
// Existing product behavior: the preset's pushed item width is used unchanged.
namespace Neurotic { inline void FitPresetComboWidth(const char*,const char*){} }
#endif
#include <stdexcept>
#include <cstdio>
int main(){try{
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1400,1000};io.Fonts->AddFontDefault();io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
 for(auto locale:{"en","de"})for(float scale:{1.f,1.5f})for(float width:{720.f,1100.f})for(auto id:{"ingame.menu-common.use_global_7994c5e6","ingame.menu-common.nvidia_default_4d29d728"}){
  if(std::string_view(locale)=="en")Neurotic::Localization::SetSharedCatalog({});else Neurotic::Localization::SetSharedCatalog(*Neurotic::Localization::IncludedPack("included-de"));
  auto preview=Neurotic::UiText(id);ImGui::GetStyle()=ImGuiStyle();ImGui::GetStyle().ScaleAllSizes(scale);io.FontGlobalScale=scale;
  for(int frame=0;frame<2;++frame){ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width*scale,500*scale});ImGui::Begin("Preset fit",nullptr,ImGuiWindowFlags_NoSavedSettings);ImGui::PushItemWidth(135*scale);
   const char* label="Preset";const auto identity=ImGui::GetID(label);const float available=ImGui::GetContentRegionAvail().x;
   Neurotic::FitPresetComboWidth(label,preview.c_str());if(ImGui::BeginCombo(label,preview.c_str()))ImGui::EndCombo();
   const auto rectangle=ImGui::GetCurrentContext()->LastItemData.Rect;const float textRoom=rectangle.GetWidth()-ImGui::CalcTextSize(label).x-ImGui::GetStyle().ItemInnerSpacing.x-ImGui::GetFrameHeight()-2*ImGui::GetStyle().FramePadding.x;
   if(textRoom+1<ImGui::CalcTextSize(preview.c_str()).x)throw std::runtime_error("Translated preset preview is clipped");
   if(rectangle.GetWidth()>available+1||ImGui::GetCurrentContext()->LastItemData.ID!=identity)throw std::runtime_error("Preset fit changed identity or escaped available space");
   if(std::string_view(locale)=="en"&&std::abs(rectangle.GetWidth()-ImGui::CalcTextSize(label).x-ImGui::GetStyle().ItemInnerSpacing.x-135*scale)>1)throw std::runtime_error("English preset geometry changed");
   ImGui::PopItemWidth();ImGui::End();ImGui::Render();
  }
 }
 ImGui::DestroyContext();puts("PASS 32 actual preset combos: translated fit, English width, identity and bounds");return 0;
}catch(const std::exception& e){fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
