"""Exercise production diagnostic readouts against real ImGui geometry across owner states."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'builds/ui-release/runtime-readout-probe'
out.mkdir(parents=True,exist_ok=True)
menu=(root/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
start=menu.index('    if (!enabled',menu.index('"Model sliders request settings on release; composition controls remain live."',menu.index('    renderReadouts = [&]()')))
end=menu.index('\n    };',start)
readouts=menu[start:end]
def declaration(path,signature):
    source=(root/path).read_text(encoding='utf-8');begin=source.index(signature);opening=source.index('{',begin)
    depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[begin:source.index(';',end)+1]
present=declaration('OptiScaler/dlssnr/DlssNr_Present.h','enum class PresentApi')+'\n'+declaration('OptiScaler/dlssnr/DlssNr_Present.h','struct PresentTelemetrySnapshot')
native=declaration('OptiScaler/dlssnr/DlssNrFeature_Dx12.h','struct TelemetrySnapshot')
fixture=r"""
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "dlssnr/NrStatusPanel.h"
#include "dlssnr/DlssNr_StageControls.h"
#include "dlssnr/DlssNr_PresentPacing.h"
#include "dlssnr/NrAdvisorSampling.h"
#include "dlssnr/NrPreflightSignals.h"
#include "dlssnr/DlssNr_BridgeTelemetry.h"
#include "menu/SleekContentCard.h"
#include <string>
#include <cstdio>
#include <cmath>
#include <stdexcept>
using DXGI_FORMAT=int;constexpr int DXGI_FORMAT_UNKNOWN=0;
using VkFormat=int;constexpr int VK_FORMAT_UNDEFINED=0;
using VkColorSpaceKHR=int;constexpr int VK_COLOR_SPACE_SRGB_NONLINEAR_KHR=0;
using DXGI_SWAP_EFFECT=int;constexpr int DXGI_SWAP_EFFECT_DISCARD=0;
using DXGI_COLOR_SPACE_TYPE=int;constexpr int DXGI_COLOR_SPACE_CUSTOM=0;
using HRESULT=int;constexpr int S_FALSE=1;
using DXGI_HDR_METADATA_TYPE=int;constexpr int DXGI_HDR_METADATA_TYPE_NONE=0;
namespace DlssNr {
namespace PresentInput{enum class Policy{AutoGuides};}
namespace PresentInputDecision{enum class InputClass{Refused};}
// OWNER_TYPES
static std::optional<double> vkTime;
static const char* FailureReasonVk(){return "";}
static std::optional<double> LastGpuTimeVk(){return vkTime;}
static unsigned long long FramesVk(){return 33;}
}
using namespace DlssNr;
struct BoolOption{bool value_or_default(){return true;}};
struct Config{BoolOption DlssNrApplyModel;};
static Config ownerConfig;
static Config* config=&ownerConfig;
static bool enabled=true,presentRoute=true,vulkan=false;
static int renderMode=0;
static const char* renderModeNames[]={"Quality","Performance"};
static PresentTelemetrySnapshot presentTelemetry;
static TelemetrySnapshot nrTelemetry;
static BridgeTelemetrySnapshot bridgeTelemetry;
static void DrawReadouts(){
// PRODUCTION_READOUTS
}
struct Geometry{float content=0,card=0,following=0;};
static Geometry Draw(bool summaryOnly=false,const std::optional<MenuStatus::RuntimeStatus>& status={}){
 ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({340,1800});
 ImGui::Begin("Diagnostic transition geometry",nullptr,ImGuiWindowFlags_NoSavedSettings);
 Geometry result;
 Neurotic::Sleek::BeginContentCard("Runtime","Runtime & completion");
 const float top=ImGui::GetCursorPosY();
 if(summaryOnly)StageUi::RenderRuntimeSummary(status);else DrawReadouts();
 result.content=ImGui::GetCursorPosY()-top;
 Neurotic::Sleek::EndContentCard();result.card=ImGui::GetItemRectSize().y;
 ImGui::Button("Check Automatic Setup");result.following=ImGui::GetItemRectMin().y;
 ImGui::End();ImGui::Render();return result;
}
static Geometry Settle(bool summaryOnly=false,const std::optional<MenuStatus::RuntimeStatus>& status={}){
 Geometry result;for(int i=0;i<4;++i)result=Draw(summaryOnly,status);return result;
}
static void Same(const Geometry& expected,const Geometry& got,const char* state){
 if(std::abs(expected.content-got.content)>.1f||std::abs(expected.card-got.card)>.1f||std::abs(expected.following-got.following)>.1f){
  std::fprintf(stderr,"FAIL %s: content %.1f/%.1f card %.1f/%.1f following %.1f/%.1f\n",state,expected.content,got.content,expected.card,got.card,expected.following,got.following);throw std::runtime_error("diagnostics moved during owner transition");
 }
}
int main(){try{
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={800,2400};io.DeltaTime=1.f/60;
 io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
 presentTelemetry.active=true;
 auto expected=Settle();
 presentTelemetry.hasPacingSummary=true;presentTelemetry.pacingSummary.route=PresentPacing::Route::NativeTemporal;
 Same(expected,Settle(),"Present native summary");
 presentTelemetry.pacingSummary.route=PresentPacing::Route::PresentEnhanced;
 Same(expected,Settle(),"Present enhanced summary");
 presentTelemetry.active=false;presentTelemetry.fallbackReason=std::string(500,'W')+"\nowner fallback";
 Same(expected,Settle(),"Present fallback");
 presentTelemetry.failure="Provider rejected this frame\n"+std::string(500,'F');
 Same(expected,Settle(),"Present failure");
 enabled=false;Same(expected,Settle(),"Present off");
 enabled=true;presentRoute=false;nrTelemetry.running=false;
 expected=Settle();nrTelemetry.failureReason="Confirmed model failure\nlong reason remains inspectable";
 Same(expected,Settle(),"Native failure");
 nrTelemetry.failureReason="";nrTelemetry.running=true;nrTelemetry.totalGpuMs=2.5;
 Same(expected,Settle(),"Native running");
 bridgeTelemetry.observed=true;bridgeTelemetry.reason=std::string(500,'B');
 Same(expected,Settle(),"Native bridge observed");
 enabled=false;Same(expected,Settle(),"Native off");
 enabled=true;vulkan=true;vkTime.reset();expected=Settle();vkTime=2.5;
 Same(expected,Settle(),"Vulkan GPU timing arrives");
 auto summaryExpected=Settle(true);
 MenuStatus::RuntimeStatus status;status.state=MenuStatus::State::Active;
 Same(summaryExpected,Settle(true,status),"Runtime summary active");
 status.state=MenuStatus::State::Recovering;std::snprintf(status.reason.data(),status.reason.size(),"%s",std::string(190,'R').c_str());
 Same(summaryExpected,Settle(true,status),"Runtime summary long waiting reason");
 status.reason[0]=0;Same(summaryExpected,Settle(true,status),"Runtime summary empty reason");
 ImGui::DestroyContext();std::puts("PASS: production Runtime card and setup actions keep fixed geometry across pacing, failure, bridge, off and optional runtime summary transitions");
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
"""
fixture=fixture.replace('// OWNER_TYPES',present+'\n'+native).replace('// PRODUCTION_READOUTS',readouts)
(out/'probe.cpp').write_text(fixture,encoding='utf-8')
command=out/'build.cmd'
command.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul\nif errorlevel 1 exit /b 1\n'
 'cl /nologo /std:c++20 /EHsc /utf-8 /W3 /I tests\\opticlip_stubs /I OptiScaler /I OptiScaler\\include /I external\\freetype '
 'builds\\ui-release\\runtime-readout-probe\\probe.cpp OptiScaler\\menu\\Localization.cpp /I external\\nlohmann OptiScaler\\menu\\localization\\LanguageRuntime.cpp OptiScaler\\menu\\localization\\LanguagePack.cpp OptiScaler\\menu\\localization\\LanguageCatalog.cpp OptiScaler\\menu\\localization\\LanguageBundles.cpp '
 'OptiScaler\\include\\imgui\\imgui.cpp OptiScaler\\include\\imgui\\imgui_draw.cpp '
 'OptiScaler\\include\\imgui\\imgui_widgets.cpp OptiScaler\\include\\imgui\\imgui_tables.cpp '
 'OptiScaler\\include\\imgui\\misc\\freetype\\imgui_freetype.cpp '
 '/Fe:builds\\ui-release\\runtime-readout-probe\\probe.exe /Fo:builds\\ui-release\\runtime-readout-probe\\ '
 '/link user32.lib shell32.lib external\\freetype\\freetype.lib\n',encoding='utf-8')
with(out/'build.txt').open('w',encoding='utf-8')as log:
 result=subprocess.run(['cmd','/c',str(command)],cwd=root,stdout=log,stderr=subprocess.STDOUT)
if result.returncode:print((out/'build.txt').read_text());raise SystemExit(result.returncode)
result=subprocess.run([str(out/'probe.exe')],cwd=root,text=True,capture_output=True,timeout=30)
print(result.stdout+result.stderr);(out/'result.txt').write_text(result.stdout+result.stderr,encoding='utf-8');raise SystemExit(result.returncode)
