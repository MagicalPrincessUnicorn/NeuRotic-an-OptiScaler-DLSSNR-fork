"""Exercise the production primary navigation and its native hit targets in real ImGui."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1];out=root/'builds/ui-release/navigation-icons';out.mkdir(parents=True,exist_ok=True)
source=(root/'OptiScaler/menu/SleekShell.h').read_text(encoding='utf-8')
navigation=source[source.index('inline void Navigation('):source.index('struct PageReveal')]
navigation=navigation.replace('inline void Navigation(', 'inline void MeasuredNavigation(').replace('Icon(draw,','MeasuredIcon(draw,')
fixture=r'''
#include "menu/SleekShell.h"
#include <vector>
#include <cstdio>
#include <stdexcept>
using namespace Neurotic::Sleek;
struct Glyph{int glyph;ImVec2 origin;float size;ImU32 color;ImGuiID id;ImRect target;};
static std::vector<Glyph> glyphs;static int checks=0;
static void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
namespace Neurotic::Sleek {
static void MeasuredIcon(ImDrawList* draw,int glyph,ImVec2 origin,float size,ImU32 color){
 Require(ImGui::GetItemID()==ImGui::GetID("##Page"),"icon creates or replaces the native selection target");
 glyphs.push_back({glyph,origin,size,color,ImGui::GetItemID(),{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()}});
 const auto before=draw->VtxBuffer.Size;Icon(draw,glyph,origin,size,color);
 Require(draw->VtxBuffer.Size>before,"section icon draws no artwork");
}
// PRODUCTION_NAVIGATION
}
int main(){try{
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
 io.DisplaySize={2000,1300};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
 const ImU32 green=IM_COL32(51,204,89,255),gray=IM_COL32(122,128,138,255),orange=IM_COL32(255,148,31,255),red=IM_COL32(242,51,51,255);
 int selected=0;NavigationStatus statuses[PageCount]{};std::vector<Glyph> previous;
 const auto frame=[&](float width,float font,bool wide,int health){
  ImGui::NewFrame();Scope scope(true);ImGui::PushFont(nullptr,font);glyphs.clear();
  ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,1000});ImGui::Begin("Production section icons");
  statuses[1]={health==0,health!=0,health==0?PilotState::On:health==1?PilotState::Degraded:PilotState::Blocked};
  statuses[2].running=health==0;statuses[3]={health==0,health!=0,health==0?PilotState::On:health==1?PilotState::Off:PilotState::Degraded};
  MeasuredNavigation(selected,600,wide,statuses);
  Require(glyphs.size()==PageCount,"horizontal navigation omits one or more original section icons");
  const int expected[]{0,2,1,3,4,5,6};
  for(int page=0;page<PageCount;++page){const auto& g=glyphs[page];
   Require(g.glyph==expected[page],"section restored the wrong original glyph");
   Require(g.origin.x>=g.target.Min.x&&g.origin.x+g.size<=g.target.Min.x+g.target.GetWidth()*.4f,"section icon is not left of its label");
   Require(g.origin.y>=g.target.Min.y&&g.origin.y+g.size<=g.target.Max.y,"icon escapes its native navigation target");
   if(!previous.empty()){Require(previous[page].id==g.id,"status changes navigation identity");Require(previous[page].target.GetSize()==g.target.GetSize(),"status changes navigation dimensions");}
   if(page==1||page==2||page==3){const ImU32 color=page==2?(health==0?green:gray):health==0?green:page==1?(health==1?orange:red):health==1?gray:orange;
    Require(g.color==color,"navigation status does not tint its section artwork");
    int rightColorVertices=0;for(const auto& v:ImGui::GetWindowDrawList()->VtxBuffer)if(v.col==color&&v.pos.y>=g.target.Min.y&&v.pos.y<=g.target.Max.y&&v.pos.x>g.target.Min.x+g.target.GetWidth()*.55f&&v.pos.x<g.target.Max.x)++rightColorVertices;
    Require(rightColorVertices==0,"navigation still paints a separate right-side pilot");
   }
  }
  Require(GImGui->ErrorCountCurrentFrame==0,"navigation icon causes an ImGui error");
  previous=glyphs;ImGui::End();ImGui::PopFont();ImGui::Render();
 };
 for(const char* language:{"en","de","fr","es","pt"})for(float width:{640.f,940.f,1400.f})for(float font:{13.f,20.f})for(bool wide:{false,true}){
  Neurotic::SetLanguage(language);previous.clear();frame(width,font,wide,0);frame(width,font,wide,0);frame(width,font,wide,1);frame(width,font,wide,2);
 }
 previous.clear();Neurotic::SetLanguage("en");frame(940,16,false,0);const auto icon=glyphs[2].origin+ImVec2(5,5);io.AddMousePosEvent(icon.x,icon.y);frame(940,16,false,0);io.AddMouseButtonEvent(0,true);frame(940,16,false,0);io.AddMouseButtonEvent(0,false);frame(940,16,false,0);Require(selected==2,"clicking section artwork does not select its existing page");
 ImGui::DestroyContext();std::printf("PASS: %d checks; all seven original icons left aligned, status-colored, native targets and compact/localized geometry preserved\n",checks);
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''.replace('// PRODUCTION_NAVIGATION',navigation)
(out/'probe.cpp').write_text(fixture,encoding='utf-8')
batch='''@echo off
call "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I OptiScaler /I OptiScaler\\include /I external\\freetype builds\\ui-release\\navigation-icons\\probe.cpp OptiScaler\\menu\\Localization.cpp /I external\\nlohmann OptiScaler\\menu\\localization\\LanguageRuntime.cpp OptiScaler\\menu\\localization\\LanguagePack.cpp OptiScaler\\menu\\localization\\LanguageCatalog.cpp OptiScaler\\menu\\localization\\LanguageBundles.cpp OptiScaler\\include\\imgui\\imgui.cpp OptiScaler\\include\\imgui\\imgui_draw.cpp OptiScaler\\include\\imgui\\imgui_widgets.cpp OptiScaler\\include\\imgui\\imgui_tables.cpp OptiScaler\\include\\imgui\\misc\\freetype\\imgui_freetype.cpp /Fo:builds\\ui-release\\navigation-icons\\ /Fe:builds\\ui-release\\navigation-icons\\probe.exe /link user32.lib shell32.lib external\\freetype\\freetype.lib
'''
(out/'build.cmd').write_text(batch,encoding='utf-8')
with(out/'build.log').open('w',encoding='utf-8')as log:build=subprocess.run(['cmd','/c',str(out/'build.cmd')],cwd=root,stdout=log,stderr=subprocess.STDOUT)
if build.returncode:print((out/'build.log').read_text());raise SystemExit(build.returncode)
run=subprocess.run([str(out/'probe.exe')],cwd=root,capture_output=True,text=True,timeout=30);(out/'result.log').write_text(run.stdout+run.stderr,encoding='utf-8');print(run.stdout+run.stderr);raise SystemExit(run.returncode)
