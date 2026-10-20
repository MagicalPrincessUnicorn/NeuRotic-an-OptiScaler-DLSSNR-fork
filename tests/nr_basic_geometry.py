from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]; out=root/'builds/ui-release/nr-input-layout'; out.mkdir(parents=True,exist_ok=True)
s=(root/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf8')
overview=(root/'OptiScaler/dlssnr/DlssNr_MultipassMenu.h').read_text(encoding='utf8')
assert 'true,&lo,&hi,title)' in overview
fn=s[s.index('static bool DeferredNrSlider('):s.index('static void DrawCumulativePassSegments(')]
fn+=s[s.index('static void RenderInputRestartWarning('):s.index('static bool UsesVulkanNrRoute(')]
cpp=r'''
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "dlssnr/NrPendingEdit.h"
#include "dlssnr/DlssNr_MenuControls.h"
#include "menu/SleekContentCard.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <cfloat>
#include <cmath>
#include <cassert>
#include <cstdio>
using namespace DlssNr;
static std::unordered_map<std::string,NrPendingEdit> pendingNrEdits;
static void CancelNrEdits(){for(auto& [k,e]:pendingNrEdits)e.Cancel();}
static void HelpMarker(const char*){}
@@FUNCTION@@
int main(){
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1600,1200};io.DeltaTime=1.f/60;
 io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
 for(float scale:{1.f,1.5f}) for(float width:{420.f,720.f,1040.f}) for(int frame=0;frame<3;++frame){
  ImGui::NewFrame();ImGui::PushFont(nullptr,16.f*scale);ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,1100});
  ImGui::Begin("fixture",nullptr,ImGuiWindowFlags_NoSavedSettings);
  Neurotic::Sleek::BeginContentCard("##NrBasicStrengths","Model / detail strength");
  const float available=ImGui::GetContentRegionAvail().x;
  const float reset=ImGui::CalcTextSize("Reset").x+ImGui::GetStyle().FramePadding.x*2;
  for(const char* title:{"Model Strength","Detail Strength"}){
   NrOptional<float> value{2.3f};ImVec2 lo,hi;
   std::string id=std::string(title)+"###"+title;
   DeferredNrSlider(id.c_str(),{&value},0,5,1,"%d%%",true,&lo,&hi,title);
   assert(hi.x-lo.x>=available-reset-ImGui::GetStyle().ItemSpacing.x-1);
   assert(hi.x<=ImGui::GetCurrentWindow()->WorkRect.Max.x);
   assert(ImGui::GetItemRectMax().x<=ImGui::GetCurrentWindow()->WorkRect.Max.x+1);
  }
  const float beforeWarning=ImGui::GetCursorPosY();RenderInputRestartWarning(false);
  const float afterBlank=ImGui::GetCursorPosY();RenderInputRestartWarning(true);
  assert(std::abs((afterBlank-beforeWarning)-(ImGui::GetCursorPosY()-afterBlank))<1.f);
  Neurotic::Sleek::EndContentCard();ImGui::End();ImGui::PopFont();ImGui::Render();
 }
 ImGui::DestroyContext();puts("PASS real ImGui Basic rails, Reset bounds and stable restart-warning height at 420/720/1040 px, 100/150% scale");
}
'''
(out/'geometry.cpp').write_text(cpp.replace('@@FUNCTION@@',fn),encoding='utf8')
batch=r'''@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /utf-8 /DNOMINMAX /I tests\opticlip_stubs /I OptiScaler /I OptiScaler\include /I external\freetype builds\ui-release\nr-input-layout\geometry.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:builds\ui-release\nr-input-layout\geometry.exe /Fo:builds\ui-release\nr-input-layout\ /link /INCREMENTAL:NO user32.lib shell32.lib external\freetype\freetype.lib
if errorlevel 1 exit /b 1
builds\ui-release\nr-input-layout\geometry.exe
exit /b %errorlevel%
'''
(out/'geometry.cmd').write_text(batch)
r=subprocess.run(['cmd','/c',str(out/'geometry.cmd')],cwd=root,capture_output=True,text=True)
(root/'docs/implementation-evidence/ingame-ui-20261006/nr-geometry-green.txt').write_text(r.stdout+r.stderr,encoding='utf8')
print(r.stdout+r.stderr);sys.exit(r.returncode)
