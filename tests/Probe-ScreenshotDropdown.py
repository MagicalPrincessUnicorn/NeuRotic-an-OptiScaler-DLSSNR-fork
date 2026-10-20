"""Exercise the real screenshot panel and keybind, with inert runtime/status queries."""
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(__file__).resolve().parents[1]
out=root/'builds/ui-release/sleek'
evidence=root/'docs/implementation-evidence/ui-hdr-freeze-20261002'
if len(sys.argv)>1:
    evidence=evidence/sys.argv[1]
out.mkdir(parents=True,exist_ok=True);evidence.mkdir(parents=True,exist_ok=True)
source=(root/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
start=source.index('void RenderScreenshotMenu(Config* config)')
opening=source.index('{',start);depth=1;end=opening+1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
body=source[start:end]
common=(root/'OptiScaler/menu/menu_common.cpp').read_text(encoding='utf-8')
def function(text, signature):
    start=text.index(signature);opening=text.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
keybind=function(common,'class Keybind')+';'
helper=function(common,'void MenuCommon::RenderScreenshotKeybind(Config* config)')
# The emitted function is copied exactly, never rewritten. External queries are
# deterministic fixtures; this does not execute the game, capture, or provider.
fixture=r'''
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "menu/SleekSections.h"
#include "menu/SleekTheme.h"
#include "menu/UiBrightness.h"
#include "dlssnr/NrScreenshotContract.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <crtdbg.h>
#include <Windows.h>
#include <concepts>
#include "SynchronizedOptional.h"
template<class T> struct Field {T v{};T value_or_default() const{return v;}bool has_value()const{return false;}void reset(){v={};} Field& operator=(T x){v=x;return *this;}};
constexpr int UnboundKey=-1;
struct Config {
 Field<unsigned> DlssNrRoute;Field<bool> ScreenshotNrOff{true},ScreenshotNativeNr{true},ScreenshotPresentNr{true},DlssNrRunBeforeSr;
 NrOptional<int> ScreenshotKey{UnboundKey};
 bool enabled=false;struct Snapshot {bool enabled;};Snapshot GetDlssNrRuntimeSnapshot()const{return {enabled};}
};
enum class API {DX12,Vulkan};
struct State {API api=API::DX12;static State& Instance(){static State s;return s;}};
class ScopedIndent {float n;public:explicit ScopedIndent(float v=16):n(v){ImGui::Indent(n);}~ScopedIndent(){ImGui::Unindent(n);}};
class ScopedNestedTextWrap {public:ScopedNestedTextWrap(){ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x-16);}~ScopedNestedTextWrap(){ImGui::PopTextWrapPos();}};
int lastKey=0;bool capturingKey=false;
namespace OptiInput {void SetGameplayPolicy(bool,bool,bool,bool){}}
std::string wstring_to_string(const wchar_t* s){char b[256]{};WideCharToMultiByte(CP_UTF8,0,s,-1,b,sizeof(b),nullptr,nullptr);return b;}
struct MenuCommon {static void RenderScreenshotKeybind(Config*);};
// EXACT_KEYBIND_IMPLEMENTATION
bool DrawScreenshotPanel(Config*);
namespace DlssNr {
 bool busy=false,nativePair=false,analysis=false,requested=false;
 struct AdvisorState {bool running;};AdvisorState Advisor(){return {analysis};}
 bool ComparisonScreenshotBusy(){return busy;}
 bool NativeComparisonScreenshotAvailable(){return nativePair;}
 struct Capture {bool available=false;std::string reason="No Vulkan adapter";};
 struct Caps {Capture capture;};Caps CurrentVulkanNrCapabilities(){return {};}
 struct Telemetry {bool nativeRayReconstructionActive=false;};Telemetry SelectedNativeTelemetry(){return {};}
 void RequestComparisonScreenshot(){requested=true;}void CancelComparisonScreenshot(){busy=false;}
 std::string ComparisonScreenshotStatus(){return "Ready";}
}
struct Theme {
 Field<bool> LightTheme{true};Field<float> MenuAccentColorR,MenuAccentColorG,MenuAccentColorB,MenuBGColorR,MenuBGColorG,MenuBGColorB,MenuBGColorA{.95f};
};
int main() {
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
 try {
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1280,900};io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
 Theme theme;Config config;int frames=0;float extent=0;
 for(bool light:{false,true})for(bool hdrPalette:{false,true})for(float gain:{.25f,1.f,3.f})for(float rate:{30.f,60.f,120.f}) {
   theme.LightTheme.v=light;Neurotic::Sleek::ApplyTheme(&theme);Neurotic::Sleek::ApplyMetrics(ImGui::GetStyle());
   ImGui::GetStyle().ScaleAllSizes(1.2f);io.DeltaTime=1.f/rate;
   if(hdrPalette)for(auto& c:ImGui::GetStyle().Colors){const float peak=(std::max)(c.x,(std::max)(c.y,c.z));if(peak>0){c.x/=1+peak;c.y/=1+peak;c.z/=1+peak;}}
   for(int frame=0;frame<150;++frame){
    DlssNr::busy=frame>=80&&frame<90;DlssNr::nativePair=frame>=90;config.enabled=frame>=90;
    ImGui::NewFrame();Neurotic::Sleek::Scope scope;
    ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1100,800});ImGui::Begin("Screenshot probe");
    ImGui::BeginChild("Page",{0,700});bool open=(frame>=10&&frame<55)||(frame>=65&&frame<135);
    ImGui::SetNextItemOpen(open,ImGuiCond_Always);
    if(!DrawScreenshotPanel(&config))return 1;
    if(GImGui->CurrentTable)throw std::runtime_error("Screenshot panel leaked a table scope");
    extent=ImGui::GetItemRectMax().y;
    {ScopedCollapsingHeader next("Interface brightness");}
    if(ImGui::GetItemRectMin().y<extent-1)throw std::runtime_error("Screenshot panel overlaps next card");
    if(frame>=140 && ImGui::BeginTable("##GeneralKeybindFixture",3,ImGuiTableFlags_SizingStretchProp)){
     ImGui::TableNextRow();ImGui::TableSetColumnIndex(2);ImGui::TextUnformatted("Previous Reset");
     auto* table=ImGui::GetCurrentTable();const int previousRow=table->CurrentRow;
     MenuCommon::RenderScreenshotKeybind(&config);
     if(ImGui::GetCurrentTable()!=table || table->CurrentRow!=previousRow+1)
      throw std::runtime_error("Screenshot binding must remain a full row in General's existing keybind table");
     ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextUnformatted("Following binding");ImGui::EndTable();
    }
    ImGui::EndChild();ImGui::End();ImGui::Render();++frames;
    for(const auto* list:ImGui::GetDrawData()->CmdLists) {
     for(const auto& vertex:list->VtxBuffer){if(!std::isfinite(vertex.pos.x)||!std::isfinite(vertex.pos.y))throw std::runtime_error("Invalid geometry");(void)Neurotic::UiBrightness::Apply(vertex.col,gain);}
     for(const auto& cmd:list->CmdBuffer)if(!cmd.UserCallback && cmd.IdxOffset+cmd.ElemCount>static_cast<unsigned>(list->IdxBuffer.Size))throw std::runtime_error("Invalid draw range");
    }
   }
 }
 if(DlssNr::requested)throw std::runtime_error("Opening a panel unexpectedly requested a capture");
 ImGui::DestroyContext();std::printf("PASS: %d frames of exact screenshot-panel layout; 30/60/120Hz, dark/light, SDR/HDR palette transform, brightness .25/1/3; no capture requested\n",frames);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
'''
fixture=fixture.replace('// EXACT_KEYBIND_IMPLEMENTATION',keybind+'\n'+helper)
fixture=fixture.replace('struct Theme {','namespace DlssNr {\n'+body+'\n}\n'+r'''
bool DrawScreenshotPanel(Config* config){
 __try{DlssNr::RenderScreenshotMenu(config);return true;}
 __except(EXCEPTION_EXECUTE_HANDLER){std::fprintf(stderr,"FAIL: opening the real screenshot panel raised 0x%08lx\n",GetExceptionCode());return false;}
}
'''+'\nstruct Theme {')
generated=out/'screenshot-dropdown-probe.cpp';generated.write_text(fixture,encoding='utf-8')
command=out/'screenshot-dropdown-probe.cmd'
command.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul\nif errorlevel 1 exit /b 1\n'+
 'cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W3 /I OptiScaler /I OptiScaler\\include /I external\\freetype builds\\ui-release\\sleek\\screenshot-dropdown-probe.cpp OptiScaler\\menu\\Localization.cpp /I external\\nlohmann OptiScaler\\menu\\localization\\LanguageRuntime.cpp OptiScaler\\menu\\localization\\LanguagePack.cpp OptiScaler\\menu\\localization\\LanguageCatalog.cpp OptiScaler\\menu\\localization\\LanguageBundles.cpp OptiScaler\\include\\imgui\\imgui.cpp OptiScaler\\include\\imgui\\imgui_draw.cpp OptiScaler\\include\\imgui\\imgui_widgets.cpp OptiScaler\\include\\imgui\\imgui_tables.cpp OptiScaler\\include\\imgui\\misc\\freetype\\imgui_freetype.cpp /Fe:"builds\\ui-release\\sleek\\screenshot-dropdown-probe.exe" /Fo:builds\\ui-release\\sleek\\ /link external\\freetype\\freetype.lib\n',encoding='utf-8')
with (evidence/'probe-build.txt').open('w',encoding='utf-8') as log:
    compiled=subprocess.run(['cmd','/c',str(command)],cwd=root,stdout=log,stderr=subprocess.STDOUT)
if compiled.returncode: print((evidence/'probe-build.txt').read_text());raise SystemExit(compiled.returncode)
with (evidence/'probe-checks.txt').open('w',encoding='utf-8') as log:
    try: result=subprocess.run([str(out/'screenshot-dropdown-probe.exe')],cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=60)
    except subprocess.TimeoutExpired: print('FAIL: probe timed out');raise
print((evidence/'probe-checks.txt').read_text())
(evidence/'probe-source.json').write_text(json.dumps({'production_source':'OptiScaler/dlssnr/DlssNr_Menu.cpp','function':'RenderScreenshotMenu','function_sha256':hashlib.sha256(body.encode()).hexdigest(),'function_copied_without_changes':True,'runtime_status_queries':'inert fixtures','keybind':'exact production Keybind class and RenderScreenshotKeybind helper; Win32 key names; inert gameplay policy','keybind_sha256':hashlib.sha256(keybind.encode()).hexdigest(),'helper_sha256':hashlib.sha256(helper.encode()).hexdigest(),'render_backend':'CPU draw geometry only','hdr_scope':'production-equivalent style color transform, not GPU/HDR display','exit_code':result.returncode},indent=2)+'\n')
raise SystemExit(result.returncode)
