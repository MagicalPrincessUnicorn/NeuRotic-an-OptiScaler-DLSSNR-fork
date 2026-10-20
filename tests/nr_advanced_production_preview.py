"""Link the existing product objects into an inert production-page renderer.

The test executable never runs DllMain, hooks a process, launches a worker or
starts NR/FG. Only controlled UI data and real production drawing are exercised.
One small fixture object borrows the checkout's stable runtime build objects.
"""
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
DIAGNOSTICS='--diagnostics-only' in sys.argv
TIMING='--timing-only' in sys.argv
OUT=ROOT/('builds/native-color/frame-cadence-preview' if TIMING else 'builds/native-color/diagnostics-preview' if DIAGNOSTICS else 'builds/universal-input/advanced-production');OUT.mkdir(parents=True,exist_ok=True)
cpp=r'''
#include "pch.h"
#include "menu/SleekSections.h"
#include "menu/SleekTheme.h"
#include "menu/localization/LanguageFonts.h"
#include "menu/localization/LanguageRuntime.h"
#include "menu/localization/LanguageBundles.h"
#include "nr/diagnostics/capability/ObservationRefresh.h"
#include "misc/IdentifyGpu.h"
// Access changes only in this fixture TU. Product object code is unchanged.
#define private public
#include "menu/menu_common.h"
#undef private
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_dx11.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <fstream>
#include <iostream>
#include "menu/menu_common.cpp"
using Microsoft::WRL::ComPtr;
// Hardware discovery is an input provider in this renderer. Never initialise
// process hooks to discover a GPU in an executable which intentionally has none.
GpuInformation IdentifyGpu::getPrimaryGpu(){GpuInformation gpu;gpu.name="Synthetic NVIDIA GPU";gpu.vendorId=VendorId::Nvidia;gpu.dlssCapable=true;gpu.nvidiaArchInfo.architecture_id=NV_GPU_ARCHITECTURE_AD100;return gpu;}
std::vector<GpuInformation> IdentifyGpu::getAllGpus(){return {getPrimaryGpu()};}
void IdentifyGpu::updateD3d12Capabilities(D3d12Proxy::PFN_D3D12CreateDevice){}
void IdentifyGpu::updateInt8Support(std::optional<bool>&,std::optional<bool>&){}
void IdentifyGpu::getHardwareAdapter(IDXGIFactory*,IDXGIAdapter** adapter,D3D_FEATURE_LEVEL){if(adapter)*adapter=nullptr;}
void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("UI fixture resource operation failed: "+std::to_string(hr));}
struct SyntheticFeature:IFeature {
 Upscaler type=Upscaler::DLSS;
 SyntheticFeature():IFeature(1,nullptr){_displayWidth=_targetWidth=1920;_displayHeight=_targetHeight=1080;_renderWidth=1280;_renderHeight=720;SetInit(true);_moduleLoaded=true;}
 NVSDK_NGX_PerfQuality_Value PerfQualityValue()override{return NVSDK_NGX_PerfQuality_Value_MaxQuality;}
 bool IsWithDx12()override{return false;}feature_version Version()override{return {3,8,0};}Upscaler GetUpscalerType()const override{return type;}API Api()const override{return API::DX12;}
};
int main(int argc,char** argv){if(argc<2)return 2;try{
 _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",(output/L"user-data").c_str());Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> renderer;Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&renderer));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=1800;desc.Height=1400;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target,staging;Check(device->CreateTexture2D(&desc,nullptr,&target));ComPtr<ID3D11RenderTargetView> view;Check(device->CreateRenderTargetView(target.Get(),nullptr,&view));desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(device->CreateTexture2D(&desc,nullptr,&staging));
 auto* gui=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1800,1400};io.DeltaTime=1.f/60.f;Check(ImGui_ImplDX11_Init(device.Get(),renderer.Get())?S_OK:E_FAIL);
 auto* config=Config::Instance();config->CheckForUpdate=false;config->CharacterInspectorEnabled=false;config->DLSSEnabled=true;config->DlssNrInputSource=0;config->SetDlssNrEnabled(false);currentBackend=Upscaler::DLSS;auto& state=State::Instance();state.api=state.swapchainApi=API::DX12;SyntheticFeature feature;MenuCommon::RenderMenuContext ctx{state,config,io};ctx.primaryGpu=std::make_unique<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>>();*ctx.primaryGpu=IdentifyGpu::getPrimaryGpu();ctx.frameRate=60;ctx.frameTime=16.667;
 Neurotic::Localization::Json receipts=Neurotic::Localization::Json::array();size_t frames=0;
 struct Page{const char* name;int surface,child,stage=-1,sub=0;};const Page pages[]={
{"nr-settings",2,0},{"render",2,6,0},{"inputs",2,6,1},{"inputs-menu",2,6,1},{"nr-quick",2,6,2},{"nr-quick-custom",2,6,2},
{"sr-backend",2,6,3,0},{"sr-rr",2,6,3,1},{"sr-fsr",2,6,3,2},{"sr-image",2,6,3,3},
{"present",2,6,4},{"fg-setup",2,6,5,0},{"fg-runtime",2,6,5,1},{"fg-game",2,6,5,2},{"fg-pacing",2,6,5,3},{"delivery",2,6,6}};
 auto save=[&](std::string name,int w,int h){renderer->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE pixels{};Check(renderer->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels));ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;Check(factory->CreateStream(&stream));Check(stream->InitializeFromFilename((output/(name+".png")).c_str(),GENERIC_WRITE));ComPtr<IWICBitmapEncoder> encoder;Check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;Check(encoder->CreateNewFrame(&frame,nullptr));Check(frame->Initialize(nullptr));Check(frame->SetSize(w,h));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Check(frame->SetPixelFormat(&format));Check(frame->WritePixels(h,pixels.RowPitch,h*pixels.RowPitch,static_cast<BYTE*>(pixels.pData)));Check(frame->Commit());Check(encoder->Commit());renderer->Unmap(staging.Get(),0);};
 for(const auto* locale:{"en"}){
  if(std::string_view(locale)=="en")Neurotic::Localization::SetSharedCatalog({});else Neurotic::Localization::SetSharedCatalog(*Neurotic::Localization::IncludedPack("included-"+std::string(locale)));
  io.Fonts->Clear();io.FontDefault=Neurotic::AddInterfaceFont(io.Fonts,18);if(!io.FontDefault)io.FontDefault=io.Fonts->AddFontDefault();Neurotic::Localization::AddFontFallbacks(io.Fonts,18,locale);
  for(bool light:{false,true})for(float scale:{1.f,1.25f})for(float logicalWidth:{420.f,736.f})for(const auto& page:pages){
   const std::string_view pageName=page.name;
   if((light||logicalWidth<736)&&pageName!="inputs"&&pageName!="inputs-menu"&&pageName!="nr-quick-custom"&&pageName!="fg-setup"&&pageName!="sr-backend")continue;
   const bool compact=logicalWidth<736;const float width=logicalWidth*scale;const float height=1000.f*scale;config->LightTheme=light;ImGui::GetStyle()=ImGuiStyle();Neurotic::Sleek::ApplyMetrics(ImGui::GetStyle());Neurotic::Sleek::ApplyTheme(config,light);ImGui::GetStyle().ScaleAllSizes(scale);io.FontGlobalScale=scale;state.currentFeature=(page.stage==3)?&feature:nullptr;ctx.currentFeature=state.currentFeature;ctx.menuResScale=scale;ctx.neuralPage=page.child;ctx.childPage=page.child;config->DlssNrStyle=std::string_view(page.name)=="nr-quick-custom"?2u:0u;config->DlssNrIntensity=std::string_view(page.name)=="nr-quick-custom"?.74f:1.f;const auto nrBefore=config->GetDlssNrConfigSnapshot();const auto persistenceBefore=config->GetPersistenceStatus().sequence;
   if(!gui->OpenPopupStack.empty())ImGui::ClosePopupToLevel(0,true);io.AddMousePosEvent(-100,-100);
   for(int pass=0;pass<7;++pass){if(pageName=="inputs-menu"){if(pass==2)io.AddMousePosEvent((compact?90.f:320.f)*scale,(compact?(argc>2?float(atof(argv[2])):536.f):(argc>3?float(atof(argv[3])):(scale>1.f?544.f:513.f)))*scale);if(pass==3)io.AddMouseButtonEvent(0,true);if(pass==4)io.AddMouseButtonEvent(0,false);}ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,height});Neurotic::Sleek::Scope sleek(true);ImGui::Begin("NeuRotic###AdvancedPreview",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoTitleBar|Neurotic::Sleek::FixedShellFlags);
 auto* rootWindow=ImGui::GetCurrentWindow();rootWindow->StateStorage.SetInt(ImGui::GetID("##SelectedMainPage"),2);rootWindow->StateStorage.SetInt(ImGui::GetID("##SelectedNeuralPage"),page.child);rootWindow->StateStorage.SetInt(ImGui::GetID("##RequestedNeuralPage"),page.child);
 for(auto* window:gui->Windows)if(std::string_view(window->Name).find("##MainMenuPageContent")!=std::string_view::npos){
   if(page.stage>=0)window->StateStorage.SetInt(ImHashStr("##PipelineInspectedStage",0,window->ID),page.stage);
   const auto barId=ImHashStr(page.stage==3?"##PipelineSrControls":"##PipelineFgControls",0,window->ID);
   if(auto* bar=gui->TabBars.GetByKey(barId);bar&&bar->Tabs.Size>page.sub)bar->SelectedTabId=bar->NextSelectedTabId=bar->Tabs[page.sub].ID;

 }
 MenuCommon::RenderMainMenuWindowActions(ctx);Neurotic::Sleek::Brand("Experimental Lab - inert preview",Neurotic::Sleek::HeaderActionsWidth(true,false)+ImGui::GetStyle().ItemSpacing.x);
 MenuCommon::RenderMainMenuBottomBar(ctx);MenuCommon::RenderMainMenuTabs(ctx);MenuCommon::RenderMainMenuSupportLink(ctx);
 ImGui::End();ImGui::Render();float clear[]={.07f,.08f,.10f,1};auto* rtv=view.Get();renderer->OMSetRenderTargets(1,&rtv,nullptr);renderer->ClearRenderTargetView(view.Get(),clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());++frames;}
   if(page.stage>=0&&(!nrBefore.SameConfiguration(config->GetDlssNrConfigSnapshot())||config->GetPersistenceStatus().sequence!=persistenceBefore))throw std::runtime_error("Page drawing changed shared NR configuration or persisted settings");
   if(ImGui::GetDrawData()->TotalVtxCount<180)throw std::runtime_error(std::string("Empty production render: ")+locale+"/"+page.name);
   bool capture=true;std::string name=std::string(locale)+(light?"-light":"-dark")+("-"+std::to_string(int(logicalWidth)))+"-"+std::to_string(int(scale*100))+"-"+page.name;if(capture)save(name,int(width),int(height));receipts.push_back({{"sourceMenuOpen",pageName=="inputs-menu"&&!gui->OpenPopupStack.empty()},{"locale",locale},{"theme",light?"light":"dark"},{"scale",scale},{"compact",compact},{"page",page.name},{"rendered",true},{"captured",capture},{"vertices",ImGui::GetDrawData()->TotalVtxCount}});
  }
 }
 state.currentFeature=nullptr;std::ofstream(output/L"rendered-pages.json")<<Neurotic::Localization::Json{{"frames",frames},{"productionDrawingSource",true},{"currentDependencyObjects",true},{"dllMainCalled",false},{"gameHooks",false},{"nrEvaluation",false},{"workerStarted",false},{"cases",receipts}}.dump(2);ImGui_ImplDX11_Shutdown();ImGui::DestroyContext(gui);CoUninitialize();std::cout<<"PASS "<<frames<<" production page frames across all pipeline destinations, themes, widths and scales\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
'''
if DIAGNOSTICS:
    start=cpp.index('const Page pages[]=')
    end=cpp.index(';',start)+1
    cpp=cpp[:start]+'''const Page pages[]={
{"diagnostics-off",2,3},{"diagnostics-native-pending",2,3},{"diagnostics-present-pending",2,3},{"diagnostics-color-none",2,3},{"diagnostics-hud-lower",2,3},{"diagnostics-present-inputs",2,3},{"diagnostics-native-inputs",2,3}};'''+cpp[end:]
    cpp=cpp.replace('for(float scale:{1.f,1.25f})','for(float scale:{1.f,1.25f})')
    cpp=cpp.replace('desc.Height=1400','desc.Height=1400').replace('io.DisplaySize={1800,1400}','io.DisplaySize={1800,1400}')
    cpp=cpp.replace('   if((light||logicalWidth<736)', '   if(false&&(light||logicalWidth<736)')
    cpp=cpp.replace('   const std::string_view pageName=page.name;', '''   const std::string_view pageName=page.name;
   config->DlssNrRoute=pageName.starts_with("diagnostics-present")?1u:0u;
   config->DlssNrColorSource=pageName=="diagnostics-color-none"?5:0;
   config->SetDlssNrEnabled(pageName!="diagnostics-off");
   DlssNr::Connections::RequestSavedSignalPreferences(0,0,false,config->DlssNrColorSource.value_or_default());''')
    cpp=cpp.replace('   if(page.stage>=0)window->StateStorage', '''   ImGui::SetScrollY(window,(pageName=="diagnostics-hud-lower"||pageName.ends_with("-inputs"))?window->ScrollMax.y:0.f);
   if(page.stage>=0)window->StateStorage''')
    cpp=cpp.replace('ImGui_ImplDX11_NewFrame();ImGui::NewFrame();', '\n'.join([
        'if(pageName.ends_with("-inputs")){for(int b=0;b<gui->TabBars.GetMapSize();++b){auto* bar=gui->TabBars.TryGetMapData(b);if(!bar)continue;',
        'for(auto& tab:bar->Tabs){const auto* label=ImGui::TabBarGetTabName(bar,&tab);if(label&&strstr(label,"###NrDiagInputs"))bar->NextSelectedTabId=tab.ID;}}}',
        'ImGui_ImplDX11_NewFrame();ImGui::NewFrame();']))
    # Initialize without collecting, then close the real observation service.
    # Rendering the board must never start a provider probe in this fixture.
    cpp=cpp.replace(' Neurotic::Localization::Json receipts=', ''' DlssNr::Capability::ObservationRefresh::Update({},false,false,false,false);
 DlssNr::Capability::ObservationRefresh::RequestClose();
 Neurotic::Localization::Json receipts=''')
if TIMING:
    cpp=cpp.replace('if(page.stage>=0&&(!nrBefore', 'if((!nrBefore')
    start=cpp.index('const Page pages[]=');end=cpp.index(';',start)+1
    cpp=cpp[:start]+'const Page pages[]={{"game",2,0},{"callbacks",2,0},{"stall",2,0},{"warming",2,0}};'+cpp[end:]
    cpp=cpp.replace('   if((light||logicalWidth<736)', '   if(false&&(light||logicalWidth<736)')
    cpp=cpp.replace('   const std::string_view pageName=page.name;', r'''
   const std::string_view pageName=page.name;
   auto& clock=Neurotic::FrameCadence::GameFrames();clock.Invalidate();
   for(int sample=0;sample<120;++sample)clock.Observe(1,1,100.+sample*22.);
   if(pageName=="stall")clock.Observe(1,1,2918.);
   if(pageName=="warming"){clock.Invalidate();clock.Observe(1,1,2718.);}
   ctx.gameTiming=clock.Read(pageName=="callbacks"?0:1,pageName=="stall"?2918.:2718.);
   state.frameTimes.assign(300,1.);state.upscaleTimes.assign(300,0.);
   gFrameTimes={};for(int sample=0;sample<360;++sample)gFrameTimes.Push(sample%2?1.f:21.f);
   ctx.frameTime=ctx.gameTiming.owner?ctx.gameTiming.Average(100):11.;ctx.frameRate=ctx.frameTime?1000./ctx.frameTime:0.;ctx.callbackFrameTime=11.;
   ctx.averageFrameTime=ctx.frameTime;ctx.averageUpscalerFT=0.;ctx.frameTimesCalculated=true;
   config->ShowFps=true;config->FpsOverlayType=FpsOverlay_DetailedGraph;config->FpsOverlayHorizontal=false;config->FpsOverlayPosition=FpsOverlayPos_TopLeft;config->OverlaysUseTheme=true;MenuCommon::_showMainMenuGraphs=true;
''')
    cpp=cpp.replace('ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,height});', 'io.DisplaySize={width,height};ctx.fpsScale=scale;MenuCommon::RenderPerformanceOverlay(ctx);ImGui::SetNextWindowPos({0,260.f*scale});ImGui::SetNextWindowSize({width,height-260.f*scale});')
    start=cpp.index(' MenuCommon::RenderMainMenuWindowActions(ctx);');end=cpp.index(' ImGui::End();ImGui::Render();',start)
    cpp=cpp[:start]+''' ImGui::TextUnformatted("Frame timing - production preview");MenuCommon::RenderMainMenuGraphs(ctx);
'''+cpp[end:]
(OUT/'preview.cpp').write_text(cpp,encoding='utf-8')
ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
targets=ET.parse(ROOT/'builds/universal-guides/dependencies.targets').getroot()
includes=targets.find('m:PropertyGroup/m:IncludePath',ns).text.split(';');libs=targets.find('m:PropertyGroup/m:LibraryPath',ns).text.split(';')
includes=[p for p in includes if '$(' not in p]+[str(ROOT/'builds/universal-guides/flow/generated'),str(Path('C:/NeuRotic/Optiscaler-Stuff/external/FidelityFX-SDK/sdk/include'))];libs=[p for p in libs if '$(' not in p]
project=ET.parse(ROOT/'OptiScaler/OptiScaler.vcxproj').getroot();group=next(g for g in project.findall('m:ItemDefinitionGroup',ns) if g.get('Condition')=="'$(Configuration)|$(Platform)'=='Release|x64'")
dependencies=group.find('m:Link/m:AdditionalDependencies',ns).text.split(';');dependencies=[p for p in dependencies if '$(' not in p and '%(' not in p]
definitions=group.find('m:ClCompile/m:PreprocessorDefinitions',ns).text.split(';');definitions=[p for p in definitions if '$(' not in p and '%(' not in p]
obj=sorted(p for p in (ROOT/'builds/universal-input/runtime/obj').rglob('*.obj') if p.name not in ('menu_common.obj','IdentifyGpu.obj') and '.tlog' not in str(p));assert obj,'Build current runtime objects first'
flowlibs=[ROOT/'builds/universal-guides/flow'/p for p in ('neurotic_prepared_flow.lib','lib/ffx_opticalflow_x64.lib','lib/ffx_backend_dx12_x64.lib')]
assert all(p.is_file() for p in flowlibs),'Build current isolated native flow dependencies first'
arguments=['/NOLOGO','/LTCG','/SUBSYSTEM:CONSOLE','/INCREMENTAL:NO','/IGNORE:4099','/OUT:'+str(OUT/'preview.exe'),str(OUT/'preview.obj')]+[str(p) for p in flowlibs]+[str(p) for p in obj]+[str(p) for p in (ROOT/'builds/universal-input/runtime/obj').glob('*.res')]+['/LIBPATH:'+p for p in libs]+dependencies+['ole32.lib','windowscodecs.lib','comdlg32.lib','shell32.lib','user32.lib','gdi32.lib','advapi32.lib','shlwapi.lib','delayimp.lib']
(OUT/'link.rsp').write_text('\n'.join('"'+p+'"' for p in arguments),encoding='utf-8')
command='cl /nologo /std:c++latest /MD /EHsc /O2 /utf-8 /DNOMINMAX /DUNICODE /D_UNICODE /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR '+ ' '.join('/I"'+p+'"' for p in includes)+' '+ ' '.join('/D'+d for d in definitions)+' /c "'+str(OUT/'preview.cpp')+'" /Fo:"'+str(OUT/'preview.obj')+'"'
batch='@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat" -vcvars_ver=14.44 >nul\n'+command+'\nif not "%errorlevel%"=="0" exit /b %errorlevel%\nlink @"'+str(OUT/'link.rsp')+'"\nexit /b %errorlevel%\n';(OUT/'build.cmd').write_text(batch)
r=subprocess.run(['cmd','/d','/c',str(OUT/'build.cmd')],cwd=ROOT,capture_output=True,text=True);print(r.stdout+r.stderr);sys.exit(r.returncode)
