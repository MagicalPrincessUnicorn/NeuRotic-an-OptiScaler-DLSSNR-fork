"""Run the production deferred Basic sliders in the real ImGui CPU backend.

Extract the label construction and control IDs as well as DeferredNrSlider, so
the regression catches slider/Reset ID aliasing at one and multiple passes.
"""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'builds/ui-release/basic-slider-probe'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')

def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

label = re.search(r'const std::string sliderLabel\s*=.*?;', source).group()
controls = re.findall(r'slider\("(Model Strength|Detail Strength)", "([^"]+)"', source)
assert len(controls) == 2
fixture = r'''
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "dlssnr/DlssNr_MenuControls.h"
#include "dlssnr/NrPendingEdit.h"
#include "menu/SleekContentCard.h"
#include <unordered_map>
#include <string>
#include <cstdio>
#include <stdexcept>
using namespace DlssNr;
// PRODUCTION_HELPERS
struct Row { ImGuiID slider=0,reset=0; ImVec2 min,max,resetMin,resetMax; };
static NrOptional<float> model{1.f},detail{1.f};
static Row rows[2];
static unsigned maximum=1;
static void Require(bool ok,const char* error) { if(!ok) throw std::runtime_error(error); }
static void Draw(const char* title,const char* id,NrOptional<float>& value,int index) {
    // PRODUCTION_LABEL
    ImGui::SetNextItemWidth(280);
    rows[index].slider=ImGui::GetID(sliderLabel.c_str());
    DeferredNrSlider(sliderLabel.c_str(),{&value},0.f,float(maximum),1.f,"%d%%",true,
                     &rows[index].min,&rows[index].max);
    rows[index].reset=ImGui::GetItemID();
    rows[index].resetMin=ImGui::GetItemRectMin();rows[index].resetMax=ImGui::GetItemRectMax();
    Require(rows[index].slider!=rows[index].reset,"Basic slider aliases its Reset button ID");
}
static void Frame(ImVec2 mouse={-100,-100},bool down=false) {
    auto& io=ImGui::GetIO();io.AddMousePosEvent(mouse.x,mouse.y);io.AddMouseButtonEvent(0,down);
    ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({800,400});
    ImGui::Begin("Basic slider regression",nullptr,ImGuiWindowFlags_NoSavedSettings);
    // PRODUCTION_CONTROLS
    Require(rows[0].slider!=rows[1].slider && rows[0].reset!=rows[1].reset,"Basic controls share IDs");
    ImGui::End();ImGui::Render();
}
int main() {
 try {
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
    io.DisplaySize={1000,700};io.DeltaTime=1.f/60.f;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
    for(unsigned passes:{1u,3u,10u}) {
      maximum=passes;model=1.f;detail=1.f;pendingNrEdits.clear();Frame();Frame();
      for(int index:{0,1}) {
        auto& target=index==0?model:detail;auto& other=index==0?detail:model;
        const float before=target.value_or_default(),otherBefore=other.value_or_default();
        const auto row=rows[index];
        const ImVec2 start((row.min.x+row.max.x)*.5f,(row.min.y+row.max.y)*.5f);
        const ImVec2 finish(row.min.x+(row.max.x-row.min.x)*.78f,start.y);
        Frame(start,true);Frame(finish,true);
        Require(target.value_or_default()==before,"Slider wrote config before release");
        Frame(finish,false);Frame();
        const float after=target.value_or_default();
        Require(after>0.f && after<=float(passes) && after!=before,"Drag did not commit within cumulative range");
        if(passes>1) Require(after>1.f,"Cumulative strength cannot exceed one pass");
        Require(other.value_or_default()==otherBefore,"Dragging one strength changed the other");
        const auto reset=rows[index];
        const ImVec2 click((reset.resetMin.x+reset.resetMax.x)*.5f,(reset.resetMin.y+reset.resetMax.y)*.5f);
        Frame(click,true);Frame(click,false);Frame();
        Require(target.value_or_default()==1.f,"Reset did not restore one-pass strength");
        Require(other.value_or_default()==otherBefore,"Reset changed the other strength");
      }
    }
    ImGui::DestroyContext();std::puts("PASS: real ImGui Basic strength IDs, drag/release, independent edits and reset at maximum 1/3/10");
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
'''
helpers = 'static std::vector<std::string> nrControlNotes;\n' + function('static void HelpMarker(') + '\nstatic std::unordered_map<std::string, NrPendingEdit> pendingNrEdits;\n'
helpers += function('static void CancelNrEdits(') + '\n' + function('static bool DeferredNrSlider(')
fixture = fixture.replace('// PRODUCTION_HELPERS', helpers).replace('// PRODUCTION_LABEL', label)
fixture = fixture.replace('// PRODUCTION_CONTROLS', '\n'.join(
    f'Draw("{title}","{ident}",{value},{index});'
    for index, ((title, ident), value) in enumerate(zip(controls, ['model', 'detail']))))
(out / 'probe.cpp').write_text(fixture, encoding='utf-8')
command = out / 'build.cmd'
command.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul\nif errorlevel 1 exit /b 1\n'
    'cl /nologo /std:c++20 /EHsc /utf-8 /W3 /I OptiScaler /I OptiScaler\\include /I external\\freetype '
    'builds\\ui-release\\basic-slider-probe\\probe.cpp OptiScaler\\menu\\Localization.cpp /I external\\nlohmann OptiScaler\\menu\\localization\\LanguageRuntime.cpp OptiScaler\\menu\\localization\\LanguagePack.cpp OptiScaler\\menu\\localization\\LanguageCatalog.cpp OptiScaler\\menu\\localization\\LanguageBundles.cpp '
    'OptiScaler\\include\\imgui\\imgui.cpp OptiScaler\\include\\imgui\\imgui_draw.cpp '
    'OptiScaler\\include\\imgui\\imgui_widgets.cpp OptiScaler\\include\\imgui\\imgui_tables.cpp '
    'OptiScaler\\include\\imgui\\misc\\freetype\\imgui_freetype.cpp '
    '/Fe:builds\\ui-release\\basic-slider-probe\\probe.exe /Fo:builds\\ui-release\\basic-slider-probe\\ '
    '/link external\\freetype\\freetype.lib\n', encoding='utf-8')
with (out / 'build.txt').open('w', encoding='utf-8') as log:
    result = subprocess.run(['cmd', '/c', str(command)], cwd=root, stdout=log, stderr=subprocess.STDOUT)
if result.returncode:
    print((out / 'build.txt').read_text());raise SystemExit(result.returncode)
result = subprocess.run([str(out / 'probe.exe')], cwd=root, text=True, capture_output=True, timeout=30)
print(result.stdout + result.stderr)
(out / 'result.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
raise SystemExit(result.returncode)
