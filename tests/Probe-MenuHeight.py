"""Real pointer-drag checks for the in-game menu's footer height grip."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1];out=root/'builds/ui-release/menu-height-probe';out.mkdir(parents=True,exist_ok=True)
fixture=r'''
#include "menu/SleekShell.h"
#if __has_include("menu/MenuHeightControl.h")
#include "menu/MenuHeightControl.h"
#else
namespace Neurotic::Sleek {struct MenuHeightControl {ImVec2 Size(ImVec2 viewport,float scale)const{return WindowSize(viewport,scale);}float RowHeight()const{return ImGui::GetFontSize()*.65f+ImGui::GetStyle().FramePadding.y*2;}void Draw(ImVec2,float,float footer){ImGui::SetCursorPosY(ImGui::GetWindowSize().y-footer-RowHeight()-ImGui::GetStyle().WindowPadding.y);ImGui::Dummy({ImGui::GetContentRegionAvail().x,RowHeight()});}};}
#endif
#include <cstdio>
#include <stdexcept>
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
 io.DisplaySize={1600,1200};io.DeltaTime=1.f/60.f;io.Fonts->AddFontDefault();io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
 Neurotic::Sleek::MenuHeightControl height;ImRect bar;ImVec2 size;float footerTop=0;int selected=2,neural=0,language=0,scaleIndex=0;
 const auto frame=[&](ImVec2 pointer,bool down){io.AddMousePosEvent(pointer.x,pointer.y);io.AddMouseButtonEvent(0,down);ImGui::NewFrame();Neurotic::Sleek::Scope scope(true);
  size=height.Size(io.DisplaySize,1);ImGui::SetNextWindowPos({12,12});ImGui::SetNextWindowSize(size);
  ImGui::Begin("Menu height test",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize|Neurotic::Sleek::FixedShellFlags);
  const float footer=Neurotic::Sleek::FooterHeight(true);
  const float reserve=footer+height.RowHeight()+ImGui::GetStyle().ItemSpacing.y*2;
  Neurotic::Sleek::Navigation(selected,(std::max)(1.f,ImGui::GetContentRegionAvail().y-reserve),false);
  Neurotic::Sleek::NeuralNavigation(neural,false,false,false,false);
  ImGui::BeginChild("##MainMenuPageContent",{0,(std::max)(1.f,ImGui::GetContentRegionAvail().y-reserve)});
  ImGui::Dummy({100,1500});ImGui::EndChild();const float pageBottom=ImGui::GetItemRectMax().y;
  height.Draw(io.DisplaySize,1,footer);bar={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
  Require(pageBottom<=bar.Min.y+.1f,"scrolling page overlaps resize grip");
  footerTop=ImGui::GetWindowPos().y+ImGui::GetWindowSize().y-Neurotic::Sleek::FooterBottomInset()-footer;
  Require(bar.Max.y<=footerTop,"resize grip overlaps support footer");
  ImGui::SetCursorScreenPos({ImGui::GetWindowPos().x+ImGui::GetStyle().WindowPadding.x,footerTop});
  Neurotic::Sleek::FooterControls(language,scaleIndex,"Auto");Neurotic::Sleek::FooterReadout("Offline fixture");
  Require(GImGui->ErrorCountCurrentFrame==0,"resize grip creates an ImGui error");ImGui::End();ImGui::Render();};
 frame({-100,-100},false);frame({-100,-100},false);
 const auto initial=size;const auto click=bar.GetCenter();frame(click,false);frame(click,true);
 frame({click.x,click.y+80},true);frame({click.x,click.y+80},false);frame({-100,-100},false);
 Require(size.y>initial.y+60,"dragging the grip does not extend menu height");Require(size.x==initial.x,"vertical grip changes menu width");
 const auto grown=size;const auto second=bar.GetCenter();frame(second,true);frame({second.x,second.y-150},true);frame({second.x,second.y-150},false);frame({-100,-100},false);
 Require(size.y<grown.y-100,"dragging upward does not shorten menu height");
 const auto focusPoint=bar.GetCenter();frame(focusPoint,true);const float focusHeight=size.y;
 io.AddFocusEvent(false);frame({focusPoint.x,focusPoint.y+100},true);frame({focusPoint.x,focusPoint.y+150},false);
 Require(size.y==focusHeight,"unfocused mouse movement resized the menu");io.AddFocusEvent(true);frame({-100,-100},false);
 const auto third=bar.GetCenter();frame(third,true);frame({third.x,third.y-2000},true);frame({third.x,third.y-2000},false);frame({-100,-100},false);
 Require(size.y>=420&&size.y<=io.DisplaySize.y-24,"height escapes useful minimum or viewport maximum");
 const auto small=height.Size({800,350},1);Require(small.y<=326,"short viewport leaves footer outside view");
 const auto scaled=height.Size({2400,1800},2);Require(scaled.x==Neurotic::Sleek::WindowSize({2400,1800},2).x,"scale changes width policy");Require(scaled.y>=840&&scaled.y<=1776,"scaled height fails bounds");
 ImGui::DestroyContext();std::puts("PASS: actual grip grows/shrinks menu, preserves width and footer, clamps at viewport and UI-scale bounds");
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
'''
(out/'probe.cpp').write_text(fixture,encoding='utf-8')
command=out/'build.cmd';command.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul\nif errorlevel 1 exit /b 1\n'
 'cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I OptiScaler /I OptiScaler\\include /I external\\freetype '
 'builds\\ui-release\\menu-height-probe\\probe.cpp OptiScaler\\menu\\Localization.cpp /I external\\nlohmann OptiScaler\\menu\\localization\\LanguageRuntime.cpp OptiScaler\\menu\\localization\\LanguagePack.cpp OptiScaler\\menu\\localization\\LanguageCatalog.cpp OptiScaler\\menu\\localization\\LanguageBundles.cpp OptiScaler\\include\\imgui\\imgui.cpp '
 'OptiScaler\\include\\imgui\\imgui_draw.cpp OptiScaler\\include\\imgui\\imgui_widgets.cpp OptiScaler\\include\\imgui\\imgui_tables.cpp '
 'OptiScaler\\include\\imgui\\misc\\freetype\\imgui_freetype.cpp /Fo:builds\\ui-release\\menu-height-probe\\ '
 '/Fe:builds\\ui-release\\menu-height-probe\\probe.exe /link user32.lib shell32.lib external\\freetype\\freetype.lib\n',encoding='utf-8')
with(out/'build.log').open('w',encoding='utf-8')as log:result=subprocess.run(['cmd','/c',str(command)],cwd=root,stdout=log,stderr=subprocess.STDOUT)
if result.returncode:print((out/'build.log').read_text());raise SystemExit(result.returncode)
result=subprocess.run([str(out/'probe.exe')],cwd=root,text=True,capture_output=True,timeout=30)
(out/'result.log').write_text(result.stdout+result.stderr,encoding='utf-8');print(result.stdout+result.stderr);raise SystemExit(result.returncode)
