from pathlib import Path
import re, subprocess, os
r=Path(__file__).resolve().parents[1]
source=(r/'OptiScaler/menu/menu_common.cpp').read_text(encoding='utf-8')
out=r/'builds/ui-release/tool-reset-probe';out.mkdir(parents=True,exist_ok=True)
def body(name):
 a=source.index('void MenuCommon::'+name);b=source.index('{',a);depth=1;e=b+1
 while depth:
  depth+=(source[e]=='{')-(source[e]=='}');e+=1
 return source[a:e].replace('ImGui::Button(', 'ProbeButton(')
header=(r/'OptiScaler/Config.h').read_text(encoding='utf-8')
fields='\n'.join(line for line in header.splitlines() if re.match(r'\s*CustomOptional<',line) and (' Magnifier' in line or ' MipmapBias' in line))
cpp='''#define NOMINMAX
#include <windows.h>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <menu/SleekShell.h>
#include "OptiScaler/CustomOptional.h"
#include <map>
#include <string>
#include <cstdio>
struct Config {FIELDS};
enum API { DX12, Vulkan };
struct State { API swapchainApi=DX12; float lastMipBias=12, lastMipBiasMax=15; };
struct Feature { bool IsFrozen() const { return false; } };
struct RenderMenuContext { State state; Config* config; Feature* currentFeature=nullptr; };
std::map<std::string,ImRect> buttons;
bool ProbeButton(const char* label) { const bool hit=ImGui::Button(label); buttons[label]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return hit; }
void ShowHelpMarker(const char*) {}
class MenuCommon { public: static float _mipBias; static bool _showMipmapCalcWindow; static void RenderMagnifierSettings(RenderMenuContext&);static void RenderMipmapBiasSettings(RenderMenuContext&); };
float MenuCommon::_mipBias=0;bool MenuCommon::_showMipmapCalcWindow=false;
FUNCTIONS
int main() {
 auto* context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1000,900};io.DeltaTime=1.f/60.f;
 io.Fonts->AddFontDefault();io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures|ImGuiBackendFlags_RendererHasVtxOffset;
 Config config;RenderMenuContext ctx{{},&config};int fails=0;
 auto check=[&](bool ok,const char* text){std::printf("%s %s\\n",ok?"PASS":"FAIL",text);fails+=!ok;};
 auto frame=[&](bool mip,const char* activate,float width=600.f) {
  ImGui::NewFrame();ImGui::SetNextWindowPos({10,10});ImGui::SetNextWindowSize({width,850});ImGui::Begin("Tools probe");buttons.clear();
  if(activate){const auto id=ImGui::GetID(activate);GImGui->NavActivateId=id;GImGui->NavActivateDownId=id;GImGui->NavActivateFlags=ImGuiActivateFlags_None;}
  if(mip)MenuCommon::RenderMipmapBiasSettings(ctx);else MenuCommon::RenderMagnifierSettings(ctx);
  const float edge=ImGui::GetWindowPos().x+ImGui::GetWindowSize().x-ImGui::GetStyle().WindowPadding.x;
  for(auto& [label,rect]:buttons)if(label.starts_with("Reset##"))check(rect.Max.x<=edge+.5f,"Reset fits inside compact Tools content");
  ImGui::End();ImGui::Render();
 };
 config.MagnifierEnabled=true;config.MagnifierSize=42;config.MagnifierZoomFactor=12;config.MagnifierBorderSize=1.7f;
 config.MagnifierCursorOffsetX=170;config.MagnifierCursorOffsetY=-90;frame(false,nullptr);frame(false,nullptr);
 frame(false,"Reset##MagnifierSize");check(config.MagnifierSize.value_or_default()==15&&!config.MagnifierSize.has_value(),"Size restores default 15 and automatic preference");
 check(config.MagnifierZoomFactor.value_or_default()==12&&config.MagnifierCursorOffsetX.value_or_default()==170,"Size reset preserves unrelated controls");
 frame(false,"Reset##MagnifierZoomFactor");check(config.MagnifierZoomFactor.value_or_default()==4,"Zoom reset restores 4");
 frame(false,"Reset##MagnifierBorderSize");check(config.MagnifierBorderSize.value_or_default()==.3f,"Border reset restores .3");
 frame(false,"Reset##MagnifierCursorOffsetX");frame(false,"Reset##MagnifierCursorOffsetY");check(config.MagnifierCursorOffsetX.value_or_default()==0&&config.MagnifierCursorOffsetY.value_or_default()==0,"Cursor resets restore zero independently");
 config.MagnifierStaticPosX=90;config.MagnifierStaticPosY=10;frame(false,"Reset##MagnifierStaticPosX");check(config.MagnifierStaticPosX.value()==50&&config.MagnifierStaticPosY.value()==10,"Static X reset centers X and keeps fixed mode/Y");
 frame(false,"Reset##MagnifierStaticPosY");check(config.MagnifierStaticPosY.value()==50&&config.MagnifierStaticPosX.has_value(),"Static Y reset centers Y and keeps fixed mode");
 config.MagnifierEnabled=false;config.MagnifierSize=25;frame(false,nullptr);const auto disabledReset=buttons["Reset##MagnifierSize"].GetCenter();io.AddMousePosEvent(disabledReset.x,disabledReset.y);frame(false,nullptr);io.AddMouseButtonEvent(0,true);frame(false,nullptr);io.AddMouseButtonEvent(0,false);frame(false,nullptr);check(config.MagnifierSize.value_or_default()==25,"Disabled magnifier prevents reset edits");
 config.MipmapBiasOverride=1.25f;MenuCommon::_mipBias=2.25f;frame(true,"Reset##MipmapBias");check(!config.MipmapBiasOverride.has_value()&&MenuCommon::_mipBias==0&&ctx.state.lastMipBias==100&&ctx.state.lastMipBiasMax==-100,"Bias reset clears saved override, staged edit and cached observations");
 MenuCommon::_mipBias=.8f;frame(true,"Reset##MipmapBias");check(MenuCommon::_mipBias==0&&!config.MipmapBiasOverride.has_value(),"Bias reset also clears an unapplied edit without creating an override");
 ctx.state.swapchainApi=Vulkan;config.MipmapBiasOverride=1.25f;MenuCommon::_mipBias=2.25f;frame(true,"Reset##MipmapBias");check(MenuCommon::_mipBias==2.25f&&config.MipmapBiasOverride.value()==1.25f,"Vulkan unavailable page preserves bias settings");
 ImGui::DestroyContext(context);return fails?1:0;
}
'''.replace('FIELDS',fields).replace('FUNCTIONS',body('RenderMagnifierSettings')+'\n'+body('RenderMipmapBiasSettings'))
path=out/'probe.cpp';path.write_text(cpp,encoding='utf-8')
# Reuse this probe's stable compiler cache, rebuilding its library objects when absent.
objs=out
library=['imgui','imgui_draw','imgui_widgets','imgui_tables','imgui_freetype','Localization','LanguageRuntime','LanguagePack','LanguageCatalog','LanguageBundles']
if any(not (objs/(name+'.obj')).is_file() for name in library):
 sources=[r/'OptiScaler/include/imgui'/(name+'.cpp') for name in library[:4]]+[
  r/'OptiScaler/include/imgui/misc/freetype/imgui_freetype.cpp',r/'OptiScaler/menu/Localization.cpp',r/'OptiScaler/menu/localization/LanguageRuntime.cpp',r/'OptiScaler/menu/localization/LanguagePack.cpp',r/'OptiScaler/menu/localization/LanguageCatalog.cpp',r/'OptiScaler/menu/localization/LanguageBundles.cpp']
 subprocess.run(['cl','/nologo','/c','/std:c++20','/EHsc','/utf-8','/O2','/W3',
  '/I'+str(r/'OptiScaler/include'),'/I'+str(r/'external/freetype'),'/I'+str(r/'external/nlohmann'),
  '/Fo:'+str(out)+'\\']+[str(path) for path in sources],cwd=r,check=True)
args=['cl','/nologo','/std:c++20','/EHsc','/utf-8','/O2','/W3','/I'+str(r),'/I'+str(r/'OptiScaler'),'/I'+str(r/'OptiScaler/include'),str(path),'/Fo:'+str(out/'probe.obj'),'/Fe:'+str(out/'probe.exe')]+[str(objs/(n+'.obj')) for n in ['imgui','imgui_draw','imgui_widgets','imgui_tables','imgui_freetype','Localization','LanguageRuntime','LanguagePack','LanguageCatalog','LanguageBundles']]+['/link',str(r/'external/freetype/freetype.lib'),'user32.lib']
subprocess.run(args,cwd=r,check=True)
subprocess.run([str(out/'probe.exe')],cwd=r,check=True)


