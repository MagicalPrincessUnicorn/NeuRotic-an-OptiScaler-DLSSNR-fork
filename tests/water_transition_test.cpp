#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <chrono>
#if __has_include("menu/SleekWater.h")
#include "menu/SleekWater.h"
#define HAS_WATER 1
#else
#define HAS_WATER 0
#endif
#if HAS_WATER
template<class W> void RenderReduced(W& water,ImDrawData* data,ImGuiWindow* root,const Neurotic::Sleek::WaterColors& colors) {
 if constexpr(requires{water.Render(data,root,colors,true);}) water.Render(data,root,colors,true);
 else water.Render(data,root,colors);
}
#endif
static void Check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main() {
 try {
#if HAS_WATER
 using namespace Neurotic::Sleek;
 WaterTransition water;
 water.Update(false,false,0); Check(water.Level()==0,"first frame must settle at saved dark theme");
 water.Update(true,false,.01); water.Update(true,false,.585); Check(std::abs(water.Level()-.5f)<.001f,"water must reach half height after half duration");
 const float before=water.Level(); water.Update(false,false,.585); Check(water.Level()==before && !water.DrawingLight(),"rapid reversal must not jump or change drawing palette");
 water.Update(false,false,.7); Check(water.Level()<before,"dark transition must drain downward");
 water.Update(true,true,.7); Check(water.Level()==1&&!water.Active(),"reduced motion must settle immediately");
 water.Update(false,false,.8);water.Update(false,false,4);Check(water.Level()==0&&!water.Active(),"long frame gap must settle");
 water.Reset();water.Update(true,false,5);Check(water.Level()==1,"reopening/context reset must settle selected theme");
 for(int x=0;x<=100;++x) {
  Check(WaterSurface(x/100.f,0,0)>1,"dark endpoint must put entire surface below menu");
  Check(WaterSurface(x/100.f,1,0)<0,"light endpoint must put entire surface above menu");
 }
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={600,500};io.DeltaTime=.016f;
 io.IniFilename=io.LogFilename=nullptr;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;io.Fonts->AddFontDefault();
 ImVec4 dark[ImGuiCol_COUNT],light[ImGuiCol_COUNT];for(int i=0;i<ImGuiCol_COUNT;++i){dark[i]={.1f,.1f,.1f,1};light[i]={.9f,.9f,.9f,1};}
 WaterColors colors;colors.Set(dark,light,false);
 water.Reset();water.Update(false,false,0);water.Update(true,false,0);water.Update(true,false,.575);
 { ImDrawList draw(ImGui::GetDrawListSharedData());draw._ResetForNewFrame();draw.PushClipRect({100,100},{400,400});draw.PushTexture(ImTextureRef((ImTextureID)123));
 draw.AddRectFilled({80,80},{420,420},IM_COL32(26,26,26,255));
 const auto texture=draw.CmdBuffer[0].TexRef.GetTexID();water.Transform(draw,{{100,100},{400,400}},colors);
 bool darkFound=false,lightFound=false,foamFound=false;
 for(const auto& v:draw.VtxBuffer){Check(v.pos.x>=100&&v.pos.x<=400&&v.pos.y>=100&&v.pos.y<=400,"refracted vertices must remain clipped to menu");darkFound|=(v.col&0xffffff)==(IM_COL32(26,26,26,0)&0xffffff);lightFound|=(v.col&0xffffff)==(IM_COL32(230,230,230,0)&0xffffff);foamFound|=(v.col&0xffffff)!=(IM_COL32(26,26,26,0)&0xffffff)&&(v.col&0xffffff)!=(IM_COL32(230,230,230,0)&0xffffff);}
 Check(darkFound&&lightFound&&foamFound,"midpoint must contain two palettes and foam, not a flat fade");
 for(const auto& cmd:draw.CmdBuffer){Check(cmd.TexRef.GetTexID()==texture,"texture references must remain live/current");for(unsigned i=0;i<cmd.ElemCount;++i)Check(cmd.VtxOffset+draw.IdxBuffer[cmd.IdxOffset+i]<(unsigned)draw.VtxBuffer.Size,"split triangles must have valid indices");}
 } { ImDrawList large(ImGui::GetDrawListSharedData());large._ResetForNewFrame();large.Flags|=ImDrawListFlags_AllowVtxOffset;
 large.PushClipRect({100,100},{400,400});large.PushTexture(ImTextureRef((ImTextureID)123));
 for(int i=0;i<18000;++i){float x=110.f+(i%80)*3.f,y=110.f+((i/80)%80)*3.f;large.AddTriangleFilled({x,y},{x+2,y},{x,y+2},IM_COL32(26,26,26,255));}
 const auto start=std::chrono::steady_clock::now();water.Transform(large,{{100,100},{400,400}},colors);
 Check(large.VtxBuffer.Size>65535,"large fixture must exercise 16-bit index boundary");
 unsigned elements=0;for(const auto& cmd:large.CmdBuffer){elements+=cmd.ElemCount;for(unsigned i=0;i<cmd.ElemCount;++i)Check(cmd.VtxOffset+large.IdxBuffer[cmd.IdxOffset+i]<(unsigned)large.VtxBuffer.Size,"large mesh index must remain within output vertices");}
 Check(elements==(unsigned)large.IdxBuffer.Size,"chunk commands must cover every triangle");
 std::printf("Large mesh: %d vertices, %d commands, %.2f ms CPU transform\n",large.VtxBuffer.Size,large.CmdBuffer.Size,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
 }
 ImGui::NewFrame();ImGui::SetNextWindowSize({300,300});ImGui::Begin("main");auto* root=ImGui::GetCurrentWindow();ImGui::BeginChild("child");auto* child=ImGui::GetCurrentWindow();ImGui::Text("Live child");ImGui::EndChild();ImGuiWindow* popup=nullptr;ImGui::OpenPopup("outside");ImGui::SetNextWindowPos({450,50});ImGui::SetNextWindowSize({130,120});if(ImGui::BeginPopup("outside")){popup=ImGui::GetCurrentWindow();ImGui::Text("Popup beyond menu");ImGui::EndPopup();}ImGui::End();ImGui::Begin("other");auto* other=ImGui::GetCurrentWindow();ImGui::Text("Unrelated overlay");ImGui::End();ImGui::Render();
 Check(WaterTransition::Owns(child,root)&&!WaterTransition::Owns(other,root),"effect must own main menu children only");
 const std::vector<ImDrawVert> unrelated(other->DrawList->VtxBuffer.begin(),other->DrawList->VtxBuffer.end());
 Check(popup && popup->DrawList->VtxBuffer.Size>0,"popup fixture must have visible geometry before transform");
 water.Render(ImGui::GetDrawData(),root,colors);
 Check(other->DrawList->VtxBuffer.Size==(int)unrelated.size() && !std::memcmp(other->DrawList->VtxBuffer.Data,unrelated.data(),unrelated.size()*sizeof(ImDrawVert)),"other overlays must remain byte-identical");
 Check(popup && popup->DrawList->VtxBuffer.Size>0,"popup outside main bounds must remain visible during transition");
 int indices=0,vertices=0;for(auto* list:ImGui::GetDrawData()->CmdLists){indices+=list->IdxBuffer.Size;vertices+=list->VtxBuffer.Size;}
 Check(indices==ImGui::GetDrawData()->TotalIdxCount && vertices==ImGui::GetDrawData()->TotalVtxCount,"render backend upload sizes must track transformed data");
 // A checkbox can turn reduced motion on AFTER this frame was drawn with the
 // transition's fixed source palette. Settle those live vertices in this frame.
 ImGui::NewFrame();ImGui::SetNextWindowPos({100,100});ImGui::SetNextWindowSize({300,300});ImGui::Begin("reduced");
 auto* reducedRoot=ImGui::GetCurrentWindow();auto* reducedDraw=ImGui::GetWindowDrawList();
 reducedDraw->AddRectFilled({120,120},{140,140},IM_COL32(26,26,26,255));ImGui::End();ImGui::Render();
 const std::vector<ImDrawVert> beforeReduce(reducedDraw->VtxBuffer.begin(),reducedDraw->VtxBuffer.end());
 RenderReduced(water,ImGui::GetDrawData(),reducedRoot,colors);
 Check(reducedDraw->VtxBuffer.Size==(int)beforeReduce.size(),"reduce-motion click must settle without tessellation or geometry animation");
 bool settledMarker=false;
 for(int i=0;i<reducedDraw->VtxBuffer.Size;++i){const auto& v=reducedDraw->VtxBuffer[i];Check(v.pos.x==beforeReduce[i].pos.x && v.pos.y==beforeReduce[i].pos.y,"reduce-motion click must preserve geometry in same frame");if(beforeReduce[i].col==IM_COL32(26,26,26,255)){Check(v.col==IM_COL32(230,230,230,255),"reduce-motion click must recolor to selected endpoint in same frame");settledMarker=true;}}
 Check(settledMarker,"reduced-motion fixture must contain source-palette geometry");
 ImGui::DestroyContext();
#else
 Check(false,"missing in-game water transition");
#endif
 std::puts("PASS: water endpoints, reversals, reduced motion, reset, bounds, palettes, foam, indices, live texture and ownership");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
