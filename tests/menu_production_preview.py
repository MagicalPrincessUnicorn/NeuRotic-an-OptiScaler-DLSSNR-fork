"""Link the existing product objects into an inert production-page renderer.

The test executable never runs DllMain, hooks a process, launches a worker or
starts NR/FG. Only controlled UI data and real production drawing are exercised.
One small fixture object borrows the checkout's stable runtime build objects.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/build'));sys.path.insert(0,str(ROOT/'tools/packaging/customer'))
from BuildEvidence import validate_receipt
from OfflineDependencies import require,owned_output,write_owned
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--object-receipt',type=Path,required=True)
parser.add_argument('--receipt-sha256',required=True)
parser.add_argument('--output-root',type=Path,default=ROOT/'builds/tests/menu-production')
mode=parser.add_mutually_exclusive_group(required=True);mode.add_argument('--plan-only',action='store_true');mode.add_argument('--compile',action='store_true')
args=parser.parse_args()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def validate():
 require(sha(args.object_receipt)==args.receipt_sha256,'Object receipt changed')
 receipt=json.loads(args.object_receipt.read_bytes())
 require(receipt['qualification']=='INTERNAL_DEVELOPMENT_OBJECTS_ONLY','Object receipt qualification differs')
 require(Path(receipt['repository']).resolve()==ROOT.resolve(),'Object repository differs')
 require(sha(receipt['source_manifest_path'])==receipt['source_manifest_sha256'],'Renderer manifest changed')
 manifest=json.loads(Path(receipt['source_manifest_path']).read_bytes())
 known={str(Path(row['path']).resolve()).casefold():row for row in manifest['files']}
 required=[Path(__file__),ROOT/'tools/build/BuildEvidence.py',ROOT/'tools/packaging/customer/OfflineDependencies.py',ROOT/'OptiScaler/menu/menu_common.cpp',ROOT/'OptiScaler/dlssnr/DlssNr_StageControls.h']
 for path in required:require(str(path.resolve()).casefold() in known,'Production renderer input absent: '+str(path))
 require(receipt.get('core_receipt_path') and receipt.get('core_receipt_sha256'),'Matching observed core producer required')
 require(sha(receipt['core_receipt_path'])==receipt['core_receipt_sha256'],'Core producer receipt changed')
 core=json.loads(Path(receipt['core_receipt_path']).read_bytes());validate_receipt(core)
 require(core['target']=='core' and core['repository']==str(ROOT),'Selected core producer context differs')
 require(receipt.get('core_source_manifest_sha256')==core['source_manifest_sha256'],'Object/source producer identity differs')
 for r in core['input_pins']:
  require(str(Path(r['path']).resolve()).casefold() in known and known[str(Path(r['path']).resolve()).casefold()]['sha256']==r['sha256'],'Core/renderer frozen input differs: '+r['path'])
 products={str(Path(r['path']).resolve()).casefold():r for p in core['evidence']['producers'] for r in p['outputs']}
 for r in receipt['objects']+receipt['flow_libraries']+receipt['resources']:
  require(str(Path(r['path']).resolve()).casefold() in products and products[str(Path(r['path']).resolve()).casefold()]['sha256']==r['sha256'],'Object lacks matching completed core producer: '+r['path'])
 toolrows={str(Path(r['path']).resolve()).casefold():r for r in receipt['tools']}
 for name in ['compiler','linker','vcvars']:
  require(receipt.get(name) and str(Path(receipt[name]).resolve()).casefold() in toolrows,'Actual renderer tool absent: '+name)
 for r in receipt['tools']:require(str(Path(r['path']).resolve()).casefold() in known,'Renderer tool not frozen: '+r['path'])
 require(receipt.get('configuration')==core['evidence']['configuration'],'Renderer/core configuration differs')
 require(receipt.get('definitions')==receipt.get('core_definitions') and receipt.get('include_dirs')==receipt.get('core_include_dirs'),'Renderer/core effective options differ')
 for row in manifest['files']+receipt['objects']+receipt['flow_libraries']+receipt['resources']+receipt['tools']:
  path=Path(row['path']);require(path.stat().st_size==row['bytes'] and sha(path)==row['sha256'],'Renderer frozen input changed: '+str(path))
 for row in receipt['objects']+receipt['flow_libraries']+receipt['resources']:
  owned_output(ROOT,row['path']);require(Path(row['path']).resolve().is_relative_to((ROOT/'builds/source/core').resolve()),'Use matching 0.9.9 core artifacts only')
 require(receipt['objects'] and len(receipt['flow_libraries'])==3,'Complete matching objects and isolated flow libraries required')
 return receipt
receipt=validate();OUT=owned_output(ROOT,args.output_root,tree=True)
if args.plan_only:
 print(json.dumps({'qualification':'INTERNAL_DEVELOPMENT_PLAN_ONLY','objects':len(receipt['objects']),'receipt_sha256':args.receipt_sha256,'output':str(OUT),'execution':'none'}));sys.exit(0)
for name in ['preview.cpp','preview.obj','preview.exe','preview.pdb','link.rsp','build.cmd','compile.log']:owned_output(ROOT,OUT/name)
OUT.mkdir(parents=True,exist_ok=True)
cpp=r'''
#include "pch.h"
#include "menu/SleekSections.h"
#include "dlssnr/DlssNr_StageControls.h"
#include "menu/SleekTheme.h"
#include "menu/localization/LanguageFonts.h"
#include "menu/localization/LanguageRuntime.h"
#include "menu/localization/LanguageBundles.h"
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
 bool IsWithDx12()override{return false;}feature_version Version()override{return {3,8,0};}Upscaler GetUpscalerType()const override{return type;}API Api()const override{return API::DX12;}
};
int main(int argc,char** argv){if(argc!=2)return 2;try{
 _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",(output/L"user-data").c_str());Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> renderer;Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&renderer));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=1800;desc.Height=1200;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> target,staging;Check(device->CreateTexture2D(&desc,nullptr,&target));ComPtr<ID3D11RenderTargetView> view;Check(device->CreateRenderTargetView(target.Get(),nullptr,&view));desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Check(device->CreateTexture2D(&desc,nullptr,&staging));
 auto* gui=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1800,1200};io.DeltaTime=1.f/60.f;Check(ImGui_ImplDX11_Init(device.Get(),renderer.Get())?S_OK:E_FAIL);
 auto* config=Config::Instance();config->CheckForUpdate=false;config->CharacterInspectorEnabled=false;config->DLSSEnabled=true;config->DlssNrInputSource=0;config->SetDlssNrEnabled(false);currentBackend=Upscaler::DLSS;auto& state=State::Instance();state.api=state.swapchainApi=API::DX12;SyntheticFeature feature;MenuCommon::RenderMenuContext ctx{state,config,io};ctx.primaryGpu=std::make_unique<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>>();ctx.primaryGpu->name="Synthetic NVIDIA GPU";ctx.frameRate=60;ctx.frameTime=16.667;

 Neurotic::Localization::Json stageChecks=Neurotic::Localization::Json::array();
 for(bool stacked:{false,true})for(uint32_t saved:{3u,99u}){
  config->DlssNrRoute=saved;config->DlssNrPresentInputPolicy=1u;
  const auto policy=config->DlssNrPresentInputPolicy.value_or_default();
  std::string text;
  auto draw=[&](const char* activate=nullptr){
   ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowSize({720,650});ImGui::Begin("StageControlsFixture");
   if(activate){ImGuiID id=ImGui::GetCurrentWindow()->GetID(activate);
    if(strcmp(activate,"##NrMethod")&&GImGui->OpenPopupStack.Size&&GImGui->OpenPopupStack.back().Window)id=GImGui->OpenPopupStack.back().Window->GetID(activate);
    GImGui->NavActivateId=GImGui->NavActivatePressedId=GImGui->NavActivateDownId=id;
   }
   ImGui::LogToBuffer();DlssNr::StageUi::RenderControls(*config,false,false,true,stacked);text=GImGui->LogBuffer.c_str();ImGui::LogFinish();ImGui::End();ImGui::Render();
  };
  draw();draw();
  if(config->DlssNrRoute.value_or_default()!=saved||text.find("Unavailable")==std::string::npos)throw std::runtime_error("StageControls changed/untruthfully previewed unavailable saved route");
  draw("##NrMethod");draw();
  if(text.find("Present")==std::string::npos||text.find("Native")==std::string::npos||text.find("NR Anything")!=std::string::npos)throw std::runtime_error("StageControls selector membership differs");
  draw("Present");draw();
  if(DlssNr::StageUi::UnifiedMethodSelection(config->GetDlssNrConfigSnapshot())!=0||config->DlssNrPresentInputPolicy.value_or_default()!=policy)throw std::runtime_error("StageControls explicit Present choice/policy preservation failed");
  draw("##NrMethod");draw();draw("Native");draw();
  if(DlssNr::StageUi::UnifiedMethodSelection(config->GetDlssNrConfigSnapshot())!=1||config->DlssNrPresentInputPolicy.value_or_default()!=policy)throw std::runtime_error("StageControls explicit Native choice/policy preservation failed");
  stageChecks.push_back({{"stacked",stacked},{"savedRoute",saved},{"drawPreserved",true},{"unavailablePreview",true},{"choices","Present/Native"},{"explicitChoicePreservesPolicy",true}});
 }
 Neurotic::Localization::Json receipts=Neurotic::Localization::Json::array();size_t frames=0;
 struct Page{const char* name;int surface,child;};const Page pages[]={{"general",0,0},{"color",0,3},{"sr-idle",1,0},{"sr-active",1,0},{"sr-preflight",1,5},{"sr-image",1,3},{"dlss",1,1},{"nr",2,0},{"nr-multipass",2,1},{"inspector",2,2},{"nr-readouts",2,3},{"nr-overrides",2,4},{"object-rules",2,5},{"nr-preflight",2,6},{"fg-game",3,0},{"fg-setup",3,1},{"fg-provider",3,2},{"fg-pacing",3,3},{"advanced",4,0},{"quirks",4,2},{"images",5,0},{"comparison",5,3}};
 auto save=[&](std::string name,int w,int h){renderer->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE pixels{};Check(renderer->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels));ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;Check(factory->CreateStream(&stream));Check(stream->InitializeFromFilename((output/(name+".png")).c_str(),GENERIC_WRITE));ComPtr<IWICBitmapEncoder> encoder;Check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;Check(encoder->CreateNewFrame(&frame,nullptr));Check(frame->Initialize(nullptr));Check(frame->SetSize(w,h));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Check(frame->SetPixelFormat(&format));Check(frame->WritePixels(h,pixels.RowPitch,h*pixels.RowPitch,static_cast<BYTE*>(pixels.pData)));Check(frame->Commit());Check(encoder->Commit());renderer->Unmap(staging.Get(),0);};
 for(const auto* locale:{"en","zh-CN","ru","es","pt-BR","de","ja","fr","pl","ko"}){
  if(std::string_view(locale)=="en")Neurotic::Localization::SetSharedCatalog({});else Neurotic::Localization::SetSharedCatalog(*Neurotic::Localization::IncludedPack("included-"+std::string(locale)));
  io.Fonts->Clear();io.FontDefault=Neurotic::AddInterfaceFont(io.Fonts,18);if(!io.FontDefault)io.FontDefault=io.Fonts->AddFontDefault();Neurotic::Localization::AddFontFallbacks(io.Fonts,18,locale);
  for(bool light:{false,true})for(float scale:{1.f,1.5f})for(bool compact:{false,true})for(const auto& page:pages){
   const float width=(compact?720.f:1100.f)*scale;const float height=720.f*scale;config->LightTheme=light;ImGui::GetStyle()=ImGuiStyle();Neurotic::Sleek::ApplyMetrics(ImGui::GetStyle());Neurotic::Sleek::ApplyTheme(config,light);ImGui::GetStyle().ScaleAllSizes(scale);io.FontGlobalScale=scale;state.currentFeature=(std::string_view(page.name)=="dlss"||std::string_view(page.name)=="sr-active"||std::string_view(page.name)=="sr-image")?&feature:nullptr;ctx.currentFeature=state.currentFeature;ctx.menuResScale=scale;ctx.neuralPage=page.child;ctx.childPage=page.child;
   for(int pass=0;pass<2;++pass){ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,height});ImGui::Begin("Production page fixture",nullptr,ImGuiWindowFlags_NoSavedSettings);switch(page.surface){case 0:MenuCommon::RenderGeneralPage(ctx);break;case 1:MenuCommon::RenderUpscalingPage(ctx);break;case 2:MenuCommon::RenderNeuralRenderingPage(ctx);break;case 3:MenuCommon::RenderFrameGenerationPage(ctx);break;case 4:MenuCommon::RenderAdvancedPage(ctx);break;case 5:MenuCommon::RenderToolsPage(ctx);break;}ImGui::End();ImGui::Render();float clear[]={.07f,.08f,.10f,1};auto* rtv=view.Get();renderer->OMSetRenderTargets(1,&rtv,nullptr);renderer->ClearRenderTargetView(view.Get(),clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());++frames;}
   if(ImGui::GetDrawData()->TotalVtxCount<180)throw std::runtime_error(std::string("Empty production render: ")+locale+"/"+page.name);
   bool capture=(scale==1&&compact==false)||(std::string_view(locale)=="zh-CN"&&compact)||(std::string_view(locale)=="en"&&scale==1.5f&&compact);std::string name=std::string(locale)+(light?"-light":"-dark")+(compact?"-compact":"-normal")+"-"+std::to_string(int(scale*100))+"-"+page.name;if(capture)save(name,int(width),int(height));receipts.push_back({{"locale",locale},{"theme",light?"light":"dark"},{"scale",scale},{"compact",compact},{"page",page.name},{"rendered",true},{"captured",capture},{"vertices",ImGui::GetDrawData()->TotalVtxCount}});
  }
 }
 state.currentFeature=nullptr;std::ofstream(output/L"rendered-pages.json")<<Neurotic::Localization::Json{{"stageControls",stageChecks},{"frames",frames},{"productionDrawingSource",true},{"currentDependencyObjects",true},{"dllMainCalled",false},{"gameHooks",false},{"nrEvaluation",false},{"workerStarted",false},{"cases",receipts}}.dump(2);ImGui_ImplDX11_Shutdown();ImGui::DestroyContext(gui);CoUninitialize();std::cout<<"PASS "<<frames<<" production page frames across ten language choices, themes, widths and scales\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
'''
write_owned(ROOT,OUT/'preview.cpp',cpp.encode('utf-8'))
includes=receipt['include_dirs'];libs=receipt['library_dirs'];definitions=receipt['definitions'];dependencies=receipt['dependencies']
obj=[Path(row['path']) for row in receipt['objects'] if Path(row['path']).name not in ('menu_common.obj','IdentifyGpu.obj')]
flowlibs=[Path(row['path']) for row in receipt['flow_libraries']]
resources=[Path(row['path']) for row in receipt['resources']]
arguments=['/NOLOGO','/LTCG','/SUBSYSTEM:CONSOLE','/INCREMENTAL:NO','/IGNORE:4099','/OUT:'+str(OUT/'preview.exe'),str(OUT/'preview.obj')]+[str(p) for p in flowlibs]+[str(p) for p in obj]+[str(p) for p in resources]+['/LIBPATH:'+p for p in libs]+dependencies+['ole32.lib','windowscodecs.lib','comdlg32.lib','shell32.lib','user32.lib','gdi32.lib','advapi32.lib','shlwapi.lib','delayimp.lib']
write_owned(ROOT,OUT/'link.rsp','\n'.join('"'+p+'"' for p in arguments).encode('utf-8'))
command='"'+receipt['compiler']+'" /nologo /std:c++latest /MD /EHsc /O2 /utf-8 /Y- /showIncludes /DNOMINMAX /DUNICODE /D_UNICODE /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR '+ ' '.join('/I"'+p+'"' for p in includes)+' '+ ' '.join('/D'+d for d in definitions)+' /c "'+str(OUT/'preview.cpp')+'" /Fo:"'+str(OUT/'preview.obj')+'"'
batch='@echo off\ncall "'+receipt['vcvars']+'" -vcvars_ver=14.44 10.0.28000.0 >nul\nif errorlevel 1 exit /b 1\n'+command+'\nif errorlevel 1 exit /b 1\n"'+receipt['linker']+'" /verbose:lib @"'+str(OUT/'link.rsp')+'"\nexit /b %errorlevel%\n';write_owned(ROOT,OUT/'build.cmd',batch.encode())
owned_output(ROOT,OUT,tree=True)
r=subprocess.run(['cmd','/d','/c',str(OUT/'build.cmd')],cwd=ROOT,capture_output=True,text=True)
write_owned(ROOT,OUT/'compile.log',(r.stdout+r.stderr).encode('utf-8'));validate()
known={str(Path(row['path']).resolve()).casefold():row for row in json.loads(Path(receipt['source_manifest_path']).read_bytes())['files']}
for line in (r.stdout+r.stderr).splitlines():
 if 'including file:' in line:
  path=Path(line.split('including file:',1)[1].strip()).resolve();require(str(path).casefold() in known,'Actual renderer include not frozen: '+str(path));require(sha(path)==known[str(path).casefold()]['sha256'],'Actual renderer include changed')
print(r.stdout+r.stderr);sys.exit(r.returncode)
