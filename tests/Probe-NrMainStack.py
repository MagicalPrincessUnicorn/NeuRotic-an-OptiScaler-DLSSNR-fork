"""Exercise the production Main advanced disclosure's stack lifetime in real ImGui."""
from pathlib import Path
import re, subprocess
root=Path(__file__).resolve().parents[1]
out=root/'builds/ui-release/main-stack-probe'
out.mkdir(parents=True,exist_ok=True)
source=(root/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
advanced=source.index('auto advanced = ScopedCollapsingHeader("Advanced Neural Rendering Features')
start=source.rfind('ImGui::PushItemWidth(',0,advanced)
end=source.index('ImGui::PopTextWrapPos();',advanced)+len('ImGui::PopTextWrapPos();')
fragment=source[start:end]
opening=fragment.index('{',fragment.index('if (advanced.IsHeaderOpen())'))
masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m.group()),fragment,flags=re.S)
depth=1;closing=opening+1
while depth:
    depth+=(masked[closing]=='{')-(masked[closing]=='}');closing+=1
fragment=fragment[:opening+1]+'\nImGui::TextUnformatted("Actual advanced child body");\n'+fragment[closing-1:]
fixture=r'''
#include "menu/SleekSections.h"
#include <cstdio>
#include <string>
int main(){
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
 io.DisplaySize={1400,1000};io.DeltaTime=1.f/60.f;io.Fonts->AddFontDefault();
 io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
 io.ConfigErrorRecoveryEnableAssert=false;io.ConfigErrorRecoveryEnableDebugLog=false;io.ConfigErrorRecoveryEnableTooltip=false;
 int errors=0;GImGui->ErrorCallback=[](ImGuiContext*,void* data,const char* message){++*static_cast<int*>(data);std::fprintf(stderr,"ImGui error: %s\n",message);};GImGui->ErrorCallbackUserData=&errors;
 for(bool instant:{true,false})for(bool open:{false,true})for(int frame=0;frame<90;++frame){
  ImGui::NewFrame();Neurotic::Sleek::Scope scope(instant);
  ImGui::SetNextWindowSize({900,800});ImGui::Begin("Production Main stack");
  const auto* parent=ImGui::GetCurrentWindow();
  const float menuResScale=1;
  ImGui::PushTextWrapPos(0);
  ImGui::SetNextItemOpen(open,ImGuiCond_Always);
  // PRODUCTION_FRAGMENT
  if(ImGui::GetCurrentWindow()!=parent){std::fprintf(stderr,"FAIL: Main left a disclosure child active\n");return 1;}
  if(!ImGui::GetCurrentWindow()->DC.ItemWidthStack.empty()||!ImGui::GetCurrentWindow()->DC.TextWrapPosStack.empty()){std::fprintf(stderr,"FAIL: Main leaked parent width/wrap stacks\n");return 1;}
  ImGui::End();ImGui::Render();
 }
 ImGui::DestroyContext();if(errors){std::fprintf(stderr,"FAIL: %d production Main style stack errors\n",errors);return 1;}
 std::puts("PASS: production Main disclosure balances per-window width/wrap stacks when closed, open and animated");
}
'''.replace('// PRODUCTION_FRAGMENT',fragment)
(out/'probe.cpp').write_text(fixture,encoding='utf-8')
command=out/'build.cmd'
command.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul\nif errorlevel 1 exit /b 1\n'
 'cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I OptiScaler /I OptiScaler\\include /I external\\freetype '
 'builds\\ui-release\\main-stack-probe\\probe.cpp OptiScaler\\menu\\Localization.cpp /I external\\nlohmann OptiScaler\\menu\\localization\\LanguageRuntime.cpp OptiScaler\\menu\\localization\\LanguagePack.cpp OptiScaler\\menu\\localization\\LanguageCatalog.cpp OptiScaler\\menu\\localization\\LanguageBundles.cpp '
 'OptiScaler\\include\\imgui\\imgui.cpp OptiScaler\\include\\imgui\\imgui_draw.cpp '
 'OptiScaler\\include\\imgui\\imgui_widgets.cpp OptiScaler\\include\\imgui\\imgui_tables.cpp '
 'OptiScaler\\include\\imgui\\misc\\freetype\\imgui_freetype.cpp '
 '/Fo:builds\\ui-release\\main-stack-probe\\ /Fe:builds\\ui-release\\main-stack-probe\\probe.exe '
 '/link user32.lib shell32.lib external\\freetype\\freetype.lib\n',encoding='utf-8')
with(out/'build.log').open('w',encoding='utf-8')as log:result=subprocess.run(['cmd','/c',str(command)],cwd=root,stdout=log,stderr=subprocess.STDOUT)
if result.returncode:print((out/'build.log').read_text());raise SystemExit(result.returncode)
result=subprocess.run([str(out/'probe.exe')],cwd=root,text=True,capture_output=True,timeout=30)
(out/'result.log').write_text(result.stdout+result.stderr,encoding='utf-8');print((result.stdout+result.stderr)[-1800:]);raise SystemExit(result.returncode)
