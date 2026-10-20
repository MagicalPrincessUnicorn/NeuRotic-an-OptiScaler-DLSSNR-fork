#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "menu/SleekContentCard.h"
#include <cmath>
#include <iostream>
int main(){
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
 int failed=0;auto check=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';failed+=!ok;};
 for(float scale:{1.f,1.5f,2.f}){io.FontGlobalScale=scale;ImGui::NewFrame();ImGui::Begin("test");const float before=ImGui::GetFontSize();const int fonts=GImGui->FontStack.Size,styles=GImGui->StyleVarStack.Size,colors=GImGui->ColorStack.Size;auto id=ImGui::GetID("##stable-card");
  Neurotic::Sleek::WindowSectionHeader("Rendering");auto title=ImGui::GetItemRectMax();check(ImGui::GetItemRectSize().y==ImGui::GetRoundedFontSize(before*1.25f),"boxed title uses the accepted 1.25 font factor with ImGui pixel rounding");
  check(ImGui::GetFontSize()==before&&GImGui->FontStack.Size==fonts&&GImGui->StyleVarStack.Size==styles&&GImGui->ColorStack.Size==colors,"header restores font and style stacks");
  ImGui::Button("content");check(ImGui::GetItemRectMin().y>=title.y,"header and following controls do not overlap");check(id==ImGui::GetID("##stable-card"),"header leaves control and card identity unchanged");ImGui::End();ImGui::Render();
 }
 ImGui::DestroyContext();return failed?1:0;
}
