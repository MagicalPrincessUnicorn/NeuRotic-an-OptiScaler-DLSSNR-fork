#include <imgui/imgui_internal.h>
#ifndef NR_TOOLTIP_BASELINE
#include <menu/ItemHelp.h>
#endif
#include <iostream>
static ImVec2 target;
static bool Frame(ImVec2 mouse, bool down=false, bool keyboard=false) {
 auto& io=ImGui::GetIO();io.AddMousePosEvent(mouse.x,mouse.y);io.AddMouseButtonEvent(0,down);
 ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({400,250});
 ImGui::Begin("Help test",nullptr,ImGuiWindowFlags_NoSavedSettings);
 ImGui::Button("Pipeline stage",{160,60});
 auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();target={(lo.x+hi.x)/2,(lo.y+hi.y)/2};
 if(keyboard){auto& g=*ImGui::GetCurrentContext();ImGui::SetFocusID(ImGui::GetItemID(),ImGui::GetCurrentWindow());g.NavHighlightItemUnderNav=g.NavCursorVisible=true;}
#ifdef NR_TOOLTIP_BASELINE
 const bool help=ImGui::IsItemHovered()||ImGui::IsItemFocused();
#else
 const bool help=Neurotic::ItemHelpHovered();
#endif
 ImGui::End();ImGui::Render();return help;
}
int main(){
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
 io.DisplaySize={640,480};io.DeltaTime=.1f;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
 Frame({-100,-100});bool hover=false;for(int i=0;i<12;i++)hover=Frame(target);
 bool clicked=Frame(target,true);bool released=false;for(int i=0;i<12;i++)released|=Frame(target);
 bool away=Frame({500,400});bool returned=false;for(int i=0;i<12;i++)returned=Frame(target);
 Frame({500,400});bool keyboard=false;for(int i=0;i<12;i++)keyboard=Frame({500,400},false,true);
 const bool ok=hover&&!clicked&&!released&&!away&&returned&&keyboard;
 std::cout<<"hover="<<hover<<" click="<<clicked<<" held-after-click="<<released<<" away="<<away<<" return="<<returned<<" keyboard="<<keyboard<<'\n';
 ImGui::DestroyContext();return ok?0:1;
}
