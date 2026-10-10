#include <menu/MfgRequestReadout.h>
#include "languages/LanguageManagerView.h"
#include "../../OptiScaler/menu/localization/LanguageRuntime.h"
#include "../../OptiScaler/menu/localization/LanguageFonts.h"
#include "storage/UserDataSession.h"
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <functional>
#include <memory>
#include <optional>
#include <cstdlib>
#include <crtdbg.h>
#include "storage/AppDataMaintenance.h"
#include "ui/ThemeTransition.h"
#include "ui/UpdateStatus.h"
#include "ui/ClosingSession.h"
#include "ui/ActionFont.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "ui/HubViewModel.h"
#include "ui/ObjectRuleHost.h"
#include "artwork/ArtworkService.h"
#include "anything/AnythingView.h"
#include "anything/AnythingController.h"
#include "anything/AnythingHotkey.h"
#include "../../tests/hub/LibraryLayoutFixture.h"
#include "../../tests/hub/ResizeTimingFixture.h"
#include "../../tests/hub/InstallationProgressFixture.h"
#include "../../OptiScaler/menu/font/Hack_Compressed.h"
using Microsoft::WRL::ComPtr;
int RunHubSelfTests();
int RunLoadingUiTests();
int RunScrollLayoutTests();
int RunFocusedUxTests();
int RunHiddenLibraryTests();
int RunLibraryPolishUiTests();
int RunHubScanTest();
int RunArtworkOnlineTest(bool online);
int RunUpdateOnlineTest();
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
static UINT resizeWidth=0,resizeHeight=0;
static std::function<void()> liveRender;
static std::function<void(WPARAM,LPARAM)> anythingShortcut;
static bool sizing=false,rendering=false,modalRender=false;
// Exit rendering still belongs to the native sizing callback after sizing=false.
struct ModalRenderScope {bool previous=modalRender;ModalRenderScope(){modalRender=true;}~ModalRenderScope(){modalRender=previous;}};
static nh::ResizeTimingFixture* sizingProbe=nullptr;
// Only the hidden Library render fixture may override the host tracking limit.
static POINT libraryPreviewWorkArea{};
static std::function<bool()> mainCloseGate;
static LRESULT WINAPI WindowProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
 if(msg==WM_HOTKEY&&(w==nh::AnythingHotkey::Id||w==nh::AnythingHotkey::ScreenshotId)){if(anythingShortcut)anythingShortcut(w,l);return 0;}
 if(ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd,msg,w,l))return 1;
 if(sizingProbe)sizingProbe->Observe(msg);
 switch(msg){case WM_CLOSE:if(mainCloseGate&&!mainCloseGate())return 0;break;case WM_SIZE:if(w!=SIZE_MINIMIZED){resizeWidth=LOWORD(l);resizeHeight=HIWORD(l);}return 0;
 case WM_ENTERSIZEMOVE:sizing=true;SetTimer(hwnd,1,16,nullptr);return 0;
 case WM_TIMER:if(w==1&&sizing&&liveRender&&!rendering){ModalRenderScope modal;nh::ResizeTimingFixture::Scope timing(sizingProbe,nh::ResizeTimingFixture::Timer);if(sizingProbe){try{sizingProbe->BeforeTimer();liveRender();}catch(const std::exception& error){sizingProbe->RecordFailure(error.what());}}else liveRender();}else if(w==1&&rendering&&sizingProbe)++sizingProbe->reentrantCount;return 0;
 case WM_EXITSIZEMOVE:{ModalRenderScope modal;nh::ResizeTimingFixture::Scope timing(sizingProbe,nh::ResizeTimingFixture::Exit);sizing=false;KillTimer(hwnd,1);if(liveRender&&!rendering){if(sizingProbe){try{liveRender();}catch(const std::exception& error){sizingProbe->RecordFailure(error.what());}}else liveRender();}return 0;}
 case WM_DPICHANGED:{auto rect=*reinterpret_cast<RECT*>(l);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST),&monitor);auto width=std::min(rect.right-rect.left,monitor.rcWork.right-monitor.rcWork.left),height=std::min(rect.bottom-rect.top,monitor.rcWork.bottom-monitor.rcWork.top);SetWindowPos(hwnd,nullptr,std::clamp(rect.left,monitor.rcWork.left,monitor.rcWork.right-width),std::clamp(rect.top,monitor.rcWork.top,monitor.rcWork.bottom-height),width,height,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
 case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(l);UINT dpi=GetDpiForWindow(hwnd);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);info->ptMinTrackSize={std::min<LONG>(MulDiv(880,dpi,96),monitor.rcWork.right-monitor.rcWork.left),std::min<LONG>(MulDiv(600,dpi,96),monitor.rcWork.bottom-monitor.rcWork.top)};if(libraryPreviewWorkArea.x&&libraryPreviewWorkArea.y){info->ptMaxTrackSize=libraryPreviewWorkArea;info->ptMaxSize=libraryPreviewWorkArea;}return 0;}
 case WM_SYSCOMMAND:if((w&0xfff0)==SC_KEYMENU)return 0;break;
 case WM_DESTROY:PostQuitMessage(0);return 0;}
 return DefWindowProcW(hwnd,msg,w,l);
}
static void Check(HRESULT hr,const char* message){if(FAILED(hr))throw std::runtime_error(message);}
static ComPtr<ID3D11ShaderResourceView> LoadBrand(ID3D11Device* device){
 HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(102),RT_RCDATA);if(!resource)throw std::runtime_error("Embedded brand asset missing");auto handle=LoadResource(nullptr,resource);auto bytes=static_cast<BYTE*>(LockResource(handle));DWORD length=SizeofResource(nullptr,resource);
 ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Image decoder unavailable");ComPtr<IWICStream> stream;Check(factory->CreateStream(&stream),"Image stream unavailable");Check(stream->InitializeFromMemory(bytes,length),"Brand stream invalid");ComPtr<IWICBitmapDecoder> decoder;Check(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"Brand decode failed");ComPtr<IWICBitmapFrameDecode> frame;Check(decoder->GetFrame(0,&frame),"Brand frame unavailable");ComPtr<IWICFormatConverter> converter;Check(factory->CreateFormatConverter(&converter),"Brand converter unavailable");Check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Brand format failed");UINT width,height;converter->GetSize(&width,&height);if(width>8192||height>8192)throw std::runtime_error("Oversize brand asset");std::vector<BYTE> pixels(width*height*4);Check(converter->CopyPixels(nullptr,width*4,(UINT)pixels.size(),pixels.data()),"Brand pixels unavailable");D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA data{pixels.data(),width*4,0};ComPtr<ID3D11Texture2D> texture;Check(device->CreateTexture2D(&desc,&data,&texture),"Brand texture unavailable");ComPtr<ID3D11ShaderResourceView> view;Check(device->CreateShaderResourceView(texture.Get(),nullptr,&view),"Brand view unavailable");return view;
}
static void SaveFrame(ID3D11Device* device,ID3D11DeviceContext* context,IDXGISwapChain* chain,const std::filesystem::path& path){
 ComPtr<ID3D11Texture2D> source;Check(chain->GetBuffer(0,IID_PPV_ARGS(&source)),"Screenshot source missing");D3D11_TEXTURE2D_DESC desc;source->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;ComPtr<ID3D11Texture2D> staging;Check(device->CreateTexture2D(&desc,nullptr,&staging),"Screenshot staging failed");context->CopyResource(staging.Get(),source.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Screenshot map failed");
 ComPtr<IWICImagingFactory> factory;Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Screenshot factory failed");ComPtr<IWICStream> stream;Check(factory->CreateStream(&stream),"Screenshot stream failed");Check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Screenshot output failed");ComPtr<IWICBitmapEncoder> encoder;Check(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder),"Screenshot encoder failed");Check(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Screenshot initialization failed");ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;Check(encoder->CreateNewFrame(&frame,&properties),"Screenshot frame failed");frame->Initialize(properties.Get());frame->SetSize(desc.Width,desc.Height);WICPixelFormatGUID format=GUID_WICPixelFormat32bppRGBA;frame->SetPixelFormat(&format);std::vector<BYTE> encoded(desc.Width*desc.Height*4); for(UINT y=0;y<desc.Height;y++){auto src=(BYTE*)mapped.pData+y*mapped.RowPitch;auto dst=encoded.data()+y*desc.Width*4;for(UINT x=0;x<desc.Width;x++){dst[x*4]=src[x*4+2];dst[x*4+1]=src[x*4+1];dst[x*4+2]=src[x*4];dst[x*4+3]=src[x*4+3];}} if(format!=GUID_WICPixelFormat32bppBGRA)throw std::runtime_error("Unexpected PNG output format");Check(frame->WritePixels(desc.Height,desc.Width*4,(UINT)encoded.size(),encoded.data()),"Screenshot write failed");frame->Commit();encoder->Commit();context->Unmap(staging.Get(),0);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
 int previewWidth=0,previewHeight=0,previewWorkWidth=0,previewWorkHeight=0;bool libraryLayoutTest=false,libraryLongLabels=false;
 bool resizeTimingTest=false;std::string resizeTimingControl="none";
 int count;auto arguments=CommandLineToArgvW(GetCommandLineW(),&count);bool installDefaultsTest=false;bool languageTest=false;bool anythingTest=false;bool installerTest=false;bool polishPreview=false;bool cleanupTest=false;bool objectRulesTest=false;bool selfTest=false,smoke=false,themeTest=false,heroTest=false,riskTest=false,settingsTest=false,scanTest=false,smokeLight=false,compact=false;float smokeScale=1;std::filesystem::path screenshotDir;std::string smokeLocale;std::filesystem::path diagnosticsFixtureWorker;bool diagnosticsLongLabels=false,anythingTargetLongLabels=false,anythingLayoutScroll=false,mfgReadoutTest=false,mfgReadoutLongLabels=false;Neurotic::Mfg::MfgRequestJournal mfgReadoutJournal;
 if(count==2&&std::wstring(arguments[1])==L"--hidden-library-tests"){LocalFree(arguments);return RunHiddenLibraryTests();}
 if(count==2&&std::wstring(arguments[1])==L"--library-polish-ui-test"){LocalFree(arguments);return RunLibraryPolishUiTests();}
 if(count==2&&std::wstring(arguments[1])==L"--loading-ui-test"){LocalFree(arguments);return RunLoadingUiTests();}
 if(count==2&&std::wstring(arguments[1])==L"--scroll-ui-test"){LocalFree(arguments);return RunScrollLayoutTests();}
 if(count==2&&std::wstring(arguments[1])==L"--focused-ui-test"){LocalFree(arguments);return RunFocusedUxTests();}
 for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--library-polish-preview"){polishPreview=smoke=true;screenshotDir=arguments[++i];}
 for(int i=1;i+2<count;i++)if(std::wstring(arguments[i])==L"--preview-size"){previewWidth=_wtoi(arguments[i+1]);previewHeight=_wtoi(arguments[i+2]);}
 for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--library-layout-preview"){libraryLayoutTest=smoke=true;screenshotDir=arguments[++i];auto fixture=screenshotDir/L"user-data";SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture.c_str());}
 for(int i=1;i+2<count;i++)if(std::wstring(arguments[i])==L"--preview-work-area"){previewWorkWidth=_wtoi(arguments[i+1]);previewWorkHeight=_wtoi(arguments[i+2]);}
 for(int i=1;i<count;i++)if(std::wstring(arguments[i])==L"--library-layout-long-labels")libraryLongLabels=true;
 for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--resize-timing-test"){resizeTimingTest=smoke=true;screenshotDir=arguments[++i];auto fixture=screenshotDir/L"user-data";SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture.c_str());}
 for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--resize-control")resizeTimingControl=nh::Utf8(arguments[++i]);
 for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--installer-ui-test"){installerTest=smoke=true;screenshotDir=arguments[++i];auto fixture=screenshotDir/L"user-data";SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture.c_str());}
 for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--anything-ui-test"){anythingTest=themeTest=smoke=true;screenshotDir=arguments[++i];auto fixture=screenshotDir/L"user-data";SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture.c_str());}
 for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--install-defaults-ui-test"){installDefaultsTest=smoke=true;screenshotDir=arguments[++i];auto fixture=screenshotDir/L"user-data";SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture.c_str());}
 for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--anything-diagnostics-fixture")diagnosticsFixtureWorker=arguments[++i];
 for(int i=1;i<count;i++)if(std::wstring(arguments[i])==L"--diagnostics-long-labels")diagnosticsLongLabels=true;
 for(int i=1;i<count;i++)if(std::wstring(arguments[i])==L"--anything-target-long-labels")anythingTargetLongLabels=true;
 for(int i=1;i<count;i++)if(std::wstring(arguments[i])==L"--anything-layout-scroll")anythingLayoutScroll=true;
 for(int i=1;i<count;i++)if(std::wstring(arguments[i])==L"--mfg-readout-long-labels")mfgReadoutLongLabels=true;
 for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--mfg-readout-ui-test"){mfgReadoutTest=smoke=true;screenshotDir=arguments[++i];SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",(screenshotDir/L"user-data").c_str());}
 for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--language-ui-test"){languageTest=smoke=true;screenshotDir=arguments[++i];auto fixture=screenshotDir/L"user-data";SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture.c_str());}
 bool progressTest=false,progressLongLabels=false,progressReducedMotion=false;nh::Json progressScenes=nh::Json::array();
 for(int i=1;i<count;i++){auto argument=std::wstring(arguments[i]);if(argument==L"--installation-progress-ui-test"&&i+1<count){progressTest=smoke=true;screenshotDir=arguments[++i];SetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",(screenshotDir/L"user-data").c_str());}if(argument==L"--progress-long-labels")progressLongLabels=true;if(argument==L"--reduced-motion")progressReducedMotion=true;}
 bool activityTest=false;for(int i=1;i+1<count;i++)if(std::wstring(arguments[i])==L"--activity-test"){activityTest=smoke=true;screenshotDir=arguments[++i];}
 for(int i=1;i<count;i++){auto argument=std::wstring(arguments[i]);if(argument==L"--update-online-test"){LocalFree(arguments);return RunUpdateOnlineTest();}if(argument==L"--artwork-online-test"||argument==L"--artwork-cache-test"){bool online=argument==L"--artwork-online-test";LocalFree(arguments);return RunArtworkOnlineTest(online);}}
 for(int i=1;i<count;i++){auto argument=std::wstring(arguments[i]);if(argument==L"--locale"&&i+1<count)smokeLocale=nh::Utf8(arguments[++i]);if(argument==L"--cleanup-ui-test")cleanupTest=true;if(argument==L"--object-rules-test")objectRulesTest=true;if(argument==L"--light")smokeLight=true;if(argument==L"--compact")compact=true;if(argument==L"--scale"){smokeScale=1.5f;if(i+1<count){const float requested=float(_wtof(arguments[i+1]));if(requested>=.5f&&requested<=3.f){smokeScale=requested;++i;}}}if(argument==L"--self-test")selfTest=true;if(argument==L"--live-scan-test")scanTest=true;if((argument==L"--cleanup-ui-test"||argument==L"--smoke-test"||argument==L"--theme-test"||argument==L"--hero-test"||argument==L"--risk-test"||argument==L"--settings-test"||argument==L"--object-rules-test")&&i+1<count){smoke=true;themeTest=argument==L"--theme-test";heroTest=argument==L"--hero-test";riskTest=argument==L"--risk-test";settingsTest=argument==L"--settings-test";screenshotDir=arguments[++i];}}LocalFree(arguments);
 if(smoke){_set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);}
 if(selfTest){try{return RunHubSelfTests();}catch(const std::exception& error){std::fprintf(stderr,"Native test exception: %s\n",error.what());return 1;}}
 if(scanTest){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int code=RunHubScanTest();CoUninitialize();return code;}
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 try{
  if(!diagnosticsFixtureWorker.empty()&&(!anythingTest||diagnosticsFixtureWorker.filename()!=L"AnythingProtocolFixture.exe"||!std::filesystem::is_regular_file(diagnosticsFixtureWorker)))throw std::runtime_error("Diagnostics preview requires the owned Anything UI/protocol fixture");
  nh::UserDataSession dataSession(nh::UserRoot());
  if(!dataSession.Owns()){MessageBoxW(nullptr,nh::Wide(Neurotic::UiMessage("desktop.native.3ce0c9d3ddbd", "NeuRotic is already open. Close the other App before opening this version.")).c_str(),L"NeuRotic",MB_OK|MB_ICONINFORMATION);CoUninitialize();return 0;}
  if(!smoke)nh::ImportLegacyUserDocuments(nh::LegacyUserRoot(),nh::UserRoot());
  if(smoke)std::filesystem::create_directories(screenshotDir);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  WNDCLASSEXW windowClass{sizeof(windowClass)};windowClass.style=CS_CLASSDC;windowClass.lpfnWndProc=WindowProc;windowClass.hInstance=instance;windowClass.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));windowClass.hIconSm=windowClass.hIcon;windowClass.lpszClassName=L"NeuRoticHubWindow";RegisterClassExW(&windowClass);
  POINT pointer{};GetCursorPos(&pointer);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(pointer,MONITOR_DEFAULTTONEAREST),&monitor);
  if(libraryLayoutTest&&previewWorkWidth>=880&&previewWorkWidth<=10000&&previewWorkHeight>=600&&previewWorkHeight<=10000){monitor.rcWork={0,0,previewWorkWidth,previewWorkHeight};libraryPreviewWorkArea={previewWorkWidth,previewWorkHeight};}
  auto bounds=nh::FitHubWindow(monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top,(smoke?smokeScale:GetDpiForSystem()/96.f),compact);
  HWND window=CreateWindowExW(0,windowClass.lpszClassName,L"NeuRotic",WS_OVERLAPPEDWINDOW,bounds.x,bounds.y,bounds.width,bounds.height,nullptr,nullptr,instance,nullptr);if(!window)throw std::runtime_error(Neurotic::UiMessage("desktop.native.eba33b785bb8", "Application window could not be created"));
  bounds=nh::FitHubWindow(monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top,(smoke?smokeScale:GetDpiForWindow(window)/96.f),compact);SetWindowPos(window,nullptr,bounds.x,bounds.y,bounds.width,bounds.height,SWP_NOZORDER|SWP_NOACTIVATE);
  DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=2;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.OutputWindow=window;desc.SampleDesc.Count=1;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
  ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain> chain;D3D_FEATURE_LEVEL level;D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};HRESULT created=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,1,D3D11_SDK_VERSION,&desc,&chain,&device,&level,&context);
  if(FAILED(created))created=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,1,D3D11_SDK_VERSION,&desc,&chain,&device,&level,&context);Check(created,"D3D11 UI device unavailable");
  ComPtr<ID3D11RenderTargetView> target;auto createTarget=[&](){ComPtr<ID3D11Texture2D> texture;Check(chain->GetBuffer(0,IID_PPV_ARGS(&texture)),"Window buffer unavailable");Check(device->CreateRenderTargetView(texture.Get(),nullptr,&target),"Window target unavailable");};createTarget();
  auto brand=LoadBrand(device.Get());IMGUI_CHECKVERSION();ImGui::CreateContext();auto& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.IniFilename=nullptr;
  if(smoke&&!smokeLocale.empty()){nh::LanguageStore fixtureStore(nh::UserRoot());auto applied=fixtureStore.Apply("included-"+smokeLocale);if(!applied.success)throw std::runtime_error(applied.message);}
  Neurotic::Localization::LoadSharedCatalogOnce();
  auto addFonts=[&](){wchar_t windows[MAX_PATH];GetWindowsDirectoryW(windows,MAX_PATH);auto font=nh::Utf8((std::filesystem::path(windows)/L"Fonts"/L"segoeui.ttf").wstring());if(!io.Fonts->AddFontFromFileTTF(font.c_str(),18.f))io.Fonts->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,18.f);
  Neurotic::Localization::AddFontFallbacks(io.Fonts,18.f,Neurotic::Localization::SelectedLocale());nh::ui::AddActionFont(io.Fonts,18.f);Neurotic::Localization::AddFontFallbacks(io.Fonts,18.f,Neurotic::Localization::SelectedLocale());};addFonts();auto fontLocale=Neurotic::Localization::SelectedLocale();
  if(!ImGui_ImplWin32_Init(window)||!ImGui_ImplDX11_Init(device.Get(),context.Get()))throw std::runtime_error(Neurotic::UiMessage("desktop.native.41c6feb59149", "UI initialization failed"));auto modelOwner=std::make_unique<nh::HubModel>();auto& model=*modelOwner;auto languageManager=std::make_unique<nh::LanguageManagerView>(window,device.Get(),context.Get(),nh::UserRoot());nh::SetLanguageManager(languageManager.get());model.languageMaintenanceBusy=[&](){return languageManager->EditorOpen()&&languageManager->Editor().Model()->Dirty();};bool pendingMainClose=false,closeRequested=false;nh::ClosingSession closing;mainCloseGate=[&](){if(closeRequested)return false;bool ready=languageManager->RequestClose();pendingMainClose=!ready;if(ready)closeRequested=true;return false;};if(!objectRulesTest&&!installerTest&&!anythingTest&&!languageTest&&!libraryLayoutTest&&!resizeTimingTest&&!mfgReadoutTest&&!progressTest)model.Load();if(smoke){while(model.readiness.busy){model.Poll();Sleep(10);}}else ShowWindow(window,SW_SHOWDEFAULT);
  auto artwork=std::make_unique<nh::ArtworkService>();artwork->Attach(device.Get());model.artwork=artwork.get();nh::ThemeTransition water;if(!water.Attach(device.Get(),context.Get()))throw std::runtime_error(Neurotic::UiMessage("desktop.native.dde1c3564ae1", "Theme transition could not be initialized"));if(!smoke)nh::StartUpdateCheck();if(smoke){model.onlineArtwork=false;model.light=smokeLight;model.reducedMotion=!themeTest&&!heroTest;if(!model.games.empty()){model.Select(0);auto start=GetTickCount64();while(model.installer.busy&&GetTickCount64()-start<50000){model.Poll();Sleep(10);}}}
  if(objectRulesTest){using namespace Neurotic::Semantic::Rules;nh::Game game;game.id="object-rules-fixture";game.title="Object Rules UI fixture";game.target.suitable=true;model.games={game};model.selected=0;model.page=1;model.inspection={{"status","Fixture"}};nh::SettingsField field;field.section="ObjectRules";field.key="ProfileHex";field.type="objectrules";field.group="Neural Rendering";field.available=true;auto profile=DefaultProfile();profile["rules"][0]["enabled"]=true;field.profileDraft=field.original=EncodeIni(profile);field.ruleStore=std::make_shared<Store>();field.ruleStore->Commit(0,profile);field.ruleEditor=std::make_shared<Editor>();field.ruleEditor->selected=profile["rules"][0]["id"].get<std::string>();field.ruleEditor->detail=true;model.settings={field};}
  if(cleanupTest){if(model.games.empty())throw std::runtime_error("Cleanup UI fixture needs a game");model.page=1;model.inspection=nh::Json();model.showIssue=false;}
  if(polishPreview){if(model.games.empty())throw std::runtime_error("Library polish preview needs an inert fixture game");model.page=1;model.showIssue=false;}
  if(installerTest){model.page=1;model.showIssue=false;}
  if(installDefaultsTest){model.page=4;model.showIssue=false;}
  if(anythingTest){model.page=2;model.loaded=false;model.anythingUi.preferencesLoaded=true;}
  if((anythingTest||installDefaultsTest)&&!smokeLocale.empty()){nh::LanguageStore fixtureStore(nh::UserRoot());auto applied=fixtureStore.Apply("included-"+smokeLocale);if(!applied.success)throw std::runtime_error(applied.message);Neurotic::Localization::PublishQueuedCatalog();}
  if(anythingTest&&anythingTargetLongLabels){model.anythingUi.selected={{"title",std::string(300,'W')}};model.anythingUi.countdownSeconds=30;}
  if(mfgReadoutTest){model.loaded=false;model.reducedMotion=true;}
  if(!diagnosticsFixtureWorker.empty()){
   model.anything=std::make_shared<nh::AnythingController>(diagnosticsFixtureWorker,nh::UserRoot()/L"diagnostics",nh::AnythingController::SessionPolicy{true,false});model.anything->Connect();
   auto waitFixture=[&](auto ready){const auto deadline=GetTickCount64()+6000;while(!ready()&&GetTickCount64()<deadline)Sleep(10);if(!ready())throw std::runtime_error("Owned diagnostics fixture did not reach its expected state");};
   waitFixture([&]{auto state=model.anything->Snapshot();return state.connected&&!state.busy;});
   model.anything->SelectModel(nh::UserRoot()/(diagnosticsLongLabels?L"probe-complete.dll":L"probe-pending.dll"),false);waitFixture([&]{auto state=model.anything->Snapshot();return state.ready&&!state.busy;});
   if(!model.anything->Start({{"mode","countdown"},{"seconds",1},{"nrScalePercent",100}}))throw std::runtime_error("Owned protocol fixture could not start");
   waitFixture([&]{auto state=model.anything->Snapshot();return state.phase=="Running"&&!state.busy;});
   if(!model.anything->MeasureOneFrame())throw std::runtime_error("Owned protocol fixture could not request a measurement");
   waitFixture([&]{return !model.anything->Snapshot().busy;});nh::RequestAnythingDiagnosticsDialog();
  }
  if(languageTest){model.page=4;model.loaded=false;auto result=languageManager->Store().AddLanguage("pl","Polski fixture");if(!result.success||!languageManager->Editor().Open(result.packId))throw std::runtime_error("Actual second-window fixture could not open.");auto* draft=languageManager->Editor().Model();for(const auto& [id,entry]:Neurotic::Localization::CanonicalEnglish())if(entry.surface=="desktop"&&entry.english=="Install"){draft->Select(id);draft->SetTranslation("Tłumaczenie przykładowe — dłuższy tekst interfejsu");break;}if(compact)SetWindowPos(languageManager->Editor().Handle(),nullptr,0,0,760,760,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);}
  if(settingsTest)model.page=1;
  if(riskTest){model.page=1;model.Plan("Install");auto start=GetTickCount64();while(model.installer.busy&&GetTickCount64()-start<30000){model.Poll();Sleep(5);}if(model.operationIssue.value("decisionKind","")!="AntiCheatRisk")throw std::runtime_error("Expected explicit anti-cheat warning before any installation review");}
  if(heroTest){if(model.games.size()<2)throw std::runtime_error("Hero fixture needs two games");model.page=1;auto start=GetTickCount64();while(GetTickCount64()-start<10000){artwork->Poll();auto first=artwork->Get(model.games[0],"hero",false),second=artwork->Get(model.games[1],"hero",false);if(first&&second)break;Sleep(5);}}
  auto globalAnythingHotkey=std::make_unique<nh::AnythingHotkey>();
  bool maintenancePaused=false;
  model.maintenanceQuiesce=[&](bool pause){
   if(pause==maintenancePaused)return;maintenancePaused=pause;
   if(pause){anythingShortcut={};globalAnythingHotkey.reset();model.artwork=nullptr;artwork->Shutdown();nh::StopUpdateCheck();}
   else{artwork=std::make_unique<nh::ArtworkService>();artwork->Attach(device.Get());model.artwork=artwork.get();globalAnythingHotkey=std::make_unique<nh::AnythingHotkey>();if(!smoke){anythingShortcut=[&](WPARAM id,LPARAM binding){if(model.showDataMaintenance||model.restartForMaintenance)return;if(globalAnythingHotkey->Accept(id,binding,model.anythingUi))nh::ToggleAnythingSelected(model);else if(globalAnythingHotkey->AcceptScreenshot(id,binding,model.anythingUi))nh::CaptureAnythingScreenshot(model);};nh::StartUpdateCheck();}}
  };
  if(!smoke)anythingShortcut=[&](WPARAM id,LPARAM binding){if(model.showDataMaintenance||model.restartForMaintenance)return;if(globalAnythingHotkey->Accept(id,binding,model.anythingUi))nh::ToggleAnythingSelected(model);else if(globalAnythingHotkey->AcceptScreenshot(id,binding,model.anythingUi))nh::CaptureAnythingScreenshot(model);};
  struct ShortcutScope{~ShortcutScope(){anythingShortcut={};}} shortcutScope;
  bool done=false;int frames=0;float lastDpi=0;bool lastLight=!model.light;
  auto renderFrame=[&](){if(rendering||done)return;rendering=true;struct Reset{~Reset(){rendering=false;}} reset;
   if(closeRequested&&!closing.Active()){
    closing.Begin(nh::UserRoot());anythingShortcut={};globalAnythingHotkey.reset();water.Cancel();
    artwork->RequestShutdown();closing.Record("cancellation_requested","artwork");
    nh::RequestUpdateStop();closing.Record("cancellation_requested","update");
    if(model.anything){model.anything->RequestShutdown();closing.Record("quit_requested","anything");}
    if(IsIconic(window))ShowWindow(window,SW_RESTORE);
   }
   // Apply explicit fixture dimensions after the initial monitor/DPI fit settles.
   if(smoke&&frames==2&&previewWidth>=880&&previewWidth<=10000&&previewHeight>=600&&previewHeight<=10000)SetWindowPos(window,nullptr,0,0,previewWidth,previewHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
   if(resizeWidth&&resizeHeight){nh::ResizeTimingFixture::Scope timing(sizingProbe,nh::ResizeTimingFixture::Resize);if(sizingProbe&&sizingProbe->measuring)++sizingProbe->resizeCount;water.Cancel();context->OMSetRenderTargets(0,nullptr,nullptr);target.Reset();Check(chain->ResizeBuffers(0,resizeWidth,resizeHeight,DXGI_FORMAT_UNKNOWN,0),"Window resize failed");resizeWidth=resizeHeight=0;createTarget();}
   {nh::ResizeTimingFixture::Scope timing(sizingProbe,nh::ResizeTimingFixture::Model);if(libraryLayoutTest)model.discovery.busy=false;if(closing.Active())model.PollClosing();else if(!progressTest)model.Poll();if(libraryLayoutTest)nh::PrepareLibraryLayoutFixture(model,frames,screenshotDir);}if(closing.Active())closing.Observe({!artwork->ShutdownReady(),!nh::UpdateStopReady(),model.BundlePending(),model.DiagnosticsPending(),model.anything&&!model.anything->ShutdownReady(),model.installer.busy,model.discovery.busy,model.readiness.busy});if(!closing.Active()&&!maintenancePaused){nh::ResizeTimingFixture::Scope timing(sizingProbe,nh::ResizeTimingFixture::Artwork);artwork->SetOnlineEnabled(model.onlineArtwork);artwork->Poll();}
   if(!closing.Active()&&!smoke&&!maintenancePaused&&!model.showDataMaintenance)globalAnythingHotkey->Update(window,model.anythingUi);
   // Deterministic presentation fixture only: no helper or transaction is started.
   if(progressTest)nh::PrepareInstallationProgressFixture(model,frames,progressLongLabels,progressReducedMotion);
   if(activityTest){model.discovery.busy=frames<60;model.discovery.started=GetTickCount64()-12000;model.installer.busy=frames>=20;model.installer.readOnlyInstaller=frames<80;model.inspectionRefreshing=false;}
   if(anythingTest&&frames==22)model.anythingUi.selected={{"title","Inert targeted window — UI preview"}};
   if(anythingTest&&frames==202)model.anythingUi.selected={{"title","Inert targeted window — "+std::string(180,'W')}};
   if(IsIconic(window)&&!smoke)return;
   if(model.reducedMotion&&water.Active())water.Cancel();double now=themeTest?frames/60.:GetTickCount64()/1000.;float dpi=smoke?smokeScale:GetDpiForWindow(window)/96.f;if(lastDpi!=dpi||lastLight!=model.light){if(!closing.Active()&&lastDpi>0&&lastLight!=model.light&&!model.reducedMotion){ComPtr<ID3D11Texture2D> source;Check(chain->GetBuffer(0,IID_PPV_ARGS(&source)),"Theme source missing");water.Begin(source.Get(),now,model.light);}else if(model.reducedMotion)water.Cancel();nh::ApplySharedTheme(model.light);ImGui::GetStyle().ScaleAllSizes(dpi);io.FontGlobalScale=dpi;lastDpi=dpi;lastLight=model.light;}
   Neurotic::Localization::PublishQueuedCatalog();
   if(!diagnosticsFixtureWorker.empty()&&diagnosticsLongLabels&&frames==0){
    Neurotic::Localization::LanguagePack labels;labels.locale="qps-ploc";
    labels.entries["desktop.anything.probe_title"]={"Numerical evidence from one rendering frame",{},1};
    labels.entries["desktop.anything.probe_description"]={"Compare a small fixed set of samples from exactly one completed rendering frame. The result contains aggregate numerical evidence only, and no captured image is saved or exported.",{},1};
    labels.entries["desktop.anything.probe_requirements"]={"This measurement requires neural rendering, full resolution, standard dynamic range and the comparison control set to On.",{},1};
    labels.entries["desktop.anything.probe_action"]={"Measure this complete frame for numerical evidence",{},1};
    labels.entries["desktop.anything.probe_complete"]={"The one-frame measurement is complete. Copy the session status to share its four separate stage summaries.",{},1};
     const auto expected=labels.entries;
     for(auto& [id,entry]:labels.entries)entry.revision=Neurotic::Localization::CanonicalEnglish().at(id).revision;
     Neurotic::Localization::SetSharedCatalog(std::move(labels));
     for(const auto& [id,entry]:expected)if(Neurotic::Localization::SharedText(id).text!=entry.text)throw std::runtime_error("Diagnostics long-label fixture catalog was not applied");
   }
   if((anythingTargetLongLabels||mfgReadoutLongLabels)&&frames==0){
    Neurotic::Localization::LanguagePack labels;labels.locale="qps-ploc";
    if(anythingTargetLongLabels){
     labels.entries["desktop.anythingview.targeted_window_4e518b23"]={"Window selected as the processing target",{},1};
     labels.entries["desktop.anything.countdown_seconds_short"]={"%d sec",{},1};
    }
    if(mfgReadoutLongLabels){
     labels.entries["ingame.menu-common.game_s_u_generated_selected_s_c67c8b33"]={"Game configuration observed: %s, %u generated intermediate images; selected configuration: %s",{},1};
     labels.entries["ingame.menu-common.forwarded_s_u_generated_setoptions_s_08aa5c1c"]={"Forwarded provider configuration observed: %s, %u generated intermediate images; SetOptions result: %s",{},1};
     labels.entries["ingame.menu-common.reported_maximum_u_distinct_frames_unverified_f389287e"]={"Reported maximum: %u; distinct generated intermediate frames are still unverified by this observation.",{},1};
     labels.entries["ingame.menu-common.reported_presentations_u_distinct_frames_unverif_245aed56"]={"Reported presentations: %u; distinct generated intermediate frames are still unverified by this observation.",{},1};
     labels.entries["ingame.menu-common.selected_ratio_rejected_restoring_the_game_reque_9161f84f"]={"Selected ratio rejected; restoring the game request failed. This message must remain readable at the narrowest supported width.",{},1};
    }
    const auto expected=labels.entries;for(auto& [id,entry]:labels.entries)entry.revision=Neurotic::Localization::CanonicalEnglish().at(id).revision;
    Neurotic::Localization::SetSharedCatalog(std::move(labels));
    for(const auto& [id,entry]:expected)if(Neurotic::Localization::SharedText(id).text!=entry.text)throw std::runtime_error("Readout fixture long-label catalog was not applied");
   }
   if(anythingTest&&anythingTargetLongLabels&&frames==120)model.anythingUi.selected=nh::Json();
   if(anythingTest&&anythingLayoutScroll&&frames>=95)for(auto* pane:GImGui->Windows)if(pane->Active&&std::strstr(pane->Name,"AnythingPage")){
    float scroll=pane->ScrollMax.y;
    if(frames<180)for(auto* look:GImGui->Windows)if(look->Active&&look->ParentWindow==pane&&std::strstr(look->Name,"AnythingLookCard"))scroll=std::clamp(pane->Scroll.y+look->Pos.y-pane->InnerRect.Min.y+(frames>=120?250*dpi:0.f),0.f,pane->ScrollMax.y);
    ImGui::SetScrollY(pane,scroll);
   }
   if(mfgReadoutTest&&frames%20==0){
    using namespace Neurotic::Mfg;
    if(frames==0||frames==20||frames==60||frames==80){const auto attempt=mfgReadoutJournal.Begin(1,7,2,true,1,MfgSelection::X4,{MfgDecisionStatus::Fixed,true,3,7});
     if(frames==60||frames==80)mfgReadoutJournal.CompleteFallback(attempt,39,frames==60?39:0,true,1);else mfgReadoutJournal.Complete(attempt,0);
     if(frames==0)mfgReadoutJournal.Observe(1,7,3,4);
    }else if(frames==40)mfgReadoutJournal.Invalidate(8);
   }
   if(libraryLayoutTest&&libraryLongLabels&&frames==0)nh::ApplyLibraryLayoutLongLabels(smokeLocale=="ja");
   // The dynamic atlas retires its own textures. Invalidating the renderer here
   // destroys the still-associated old atlas texture before Clear rebuilds it.
   if(fontLocale!=Neurotic::Localization::SelectedLocale()){io.Fonts->Clear();addFonts();fontLocale=Neurotic::Localization::SelectedLocale();}
   ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();if(heroTest||progressTest)io.DeltaTime=1.f/60.f;ImGui::NewFrame();
   if(polishPreview&&frames==105){ImGui::Begin("NeuRotic");ImGui::OpenPopup("Window list");ImGui::End();}
   {nh::ResizeTimingFixture::Scope timing(sizingProbe,nh::ResizeTimingFixture::Ui);if(closing.Active()){nh::RenderClosing(closing,brand.Get(),dpi);}else if(mfgReadoutTest){
    const auto viewport=ImGui::GetMainViewport();ImGui::SetNextWindowPos({viewport->Pos.x+32*dpi,viewport->Pos.y+32*dpi});
    ImGui::SetNextWindowSize({std::min((compact?300.f:620.f)*dpi,viewport->Size.x-64*dpi),viewport->Size.y-64*dpi});
    ImGui::Begin("MFG request diagnostics###MfgReadoutFixture",nullptr,ImGuiWindowFlags_NoSavedSettings);
    Neurotic::Sleek::DrawMfgRequestReadout(frames>=100?Neurotic::Mfg::MfgRequestReceipt{}:mfgReadoutJournal.Current());
    ImGui::Button("Following control###MfgFollowingFixture");ImGui::End();
   }else nh::RenderHub(model,brand.Get(),dpi);if(!closing.Active()&&model.restartForMaintenance)done=true;
   if(anythingTest&&frames==95)std::ofstream(screenshotDir/L"locale-observation.json")<<nh::Json{{"requested",smokeLocale},{"effective",Neurotic::Localization::SelectedLocale()},{"clearTarget",Neurotic::Localization::SharedText("desktop.anythingview.clear_target_1d772e62").text},{"saveProfile",Neurotic::Localization::SharedText("desktop.anythingview.save_profile_1a567671").text}}.dump(2);
   ImGui::Render();float clear[]={0.078f,0.086f,0.102f,1.f};auto rt=target.Get();context->OMSetRenderTargets(1,&rt,nullptr);context->ClearRenderTargetView(target.Get(),clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());}
   {nh::ResizeTimingFixture::Scope timing(sizingProbe,nh::ResizeTimingFixture::Remember);ComPtr<ID3D11Texture2D> source;Check(chain->GetBuffer(0,IID_PPV_ARGS(&source)),"Theme buffer missing");if(sizingProbe){D3D11_TEXTURE2D_DESC remembered{};source->GetDesc(&remembered);sizingProbe->NoteRemember(remembered.Width,remembered.Height);}if(water.Active())water.Render(source.Get(),target.Get(),now);water.Remember(source.Get());}
   ++frames;
   {nh::ResizeTimingFixture::Scope timing(sizingProbe,nh::ResizeTimingFixture::Editor);languageManager->RenderEditor(model.light,smoke?smokeScale:0);if(pendingMainClose){if(!languageManager->EditorOpen()){closeRequested=true;pendingMainClose=false;}else if(languageManager->Editor().Model()->CloseStatus()==nh::CloseState::Open)pendingMainClose=false;}}
   if(progressTest){
    if(frames%12==10){auto measured=nh::MeasureInstallationProgressFixture(model,frames-1,dpi);SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/nh::Wide(std::string(nh::InstallationProgressScene(frames-1))+".png"));progressScenes.push_back(std::move(measured));}
    if(frames>=120){
     if(progressScenes.size()!=10)throw std::runtime_error("Progress fixture did not capture all real states");
     for(const auto& scene:progressScenes)if(scene["logoWaterVisible"].get<bool>())throw std::runtime_error("Shelved logo-water appeared in restored installation UI");
     std::ofstream(screenshotDir/L"progress-scenes.json")<<progressScenes.dump(2);
     model.installer.busy=model.discovery.busy=false;done=true;
    }
   }
   else if(mfgReadoutTest){if(frames%20==15)SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/(L"mfg-readout-"+std::to_wstring(frames/20)+L".png"));if(frames>=120)done=true;}
   else if(libraryLayoutTest){if(frames%20==0){auto name=nh::LibraryLayoutState(frames-1);SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/nh::Wide(std::string(name)+".png"));auto measured=nh::MeasureLibraryLayout(window,dpi);measured["state"]=name;measured["workArea"]={previewWorkWidth,previewWorkHeight};nh::Json art=nh::Json::array();for(int i=0;i<std::min(4,(int)model.games.size());++i){auto cover=artwork->Get(model.games[i],"cover",false);art.push_back({{"loaded",bool(cover)},{"width",cover.width},{"height",cover.height}});}measured["artwork"]=art;std::ofstream(screenshotDir/nh::Wide(std::string(name)+".json"))<<measured.dump(2);}if(frames>=220){model.discovery.busy=false;done=true;}}
   else if(languageTest){
    auto* draft=languageManager->Editor().Model();auto id=draft->Document()["packId"].get<std::string>();
    if(frames==20||frames==45||frames==54||frames==80)SaveFrame(device.Get(),context.Get(),languageManager->Editor().SwapChain(),screenshotDir/(L"editor-"+std::to_wstring(frames)+L".png"));
    if(frames==21){if(languageManager->RequestClose())throw std::runtime_error("Dirty editor closed without a decision.");}
    if(frames==46){draft->CancelClose();model.light=!model.light;}
    if(frames==50){auto result=languageManager->Store().Apply(id,draft->Document());if(!result.success)throw std::runtime_error(result.message);Neurotic::Localization::QueueSharedCatalog(languageManager->Store().ActivePack());}
    if(frames==54){if(Neurotic::Localization::SelectedLocale()!="pl"||!draft->Dirty()||!languageManager->EditorOpen())throw std::runtime_error("Live Apply lost locale, independent draft or second window.");}
    if(frames==55){SendMessageW(window,WM_CLOSE,0,0);if(!IsWindow(window)||draft->CloseStatus()!=nh::CloseState::Ask)throw std::runtime_error("Main shutdown did not retain dirty editor for review.");}
    if(frames==58)draft->CancelClose();
    if(frames==60){auto saved=languageManager->Store().SaveDraft(id,draft->Document());if(!saved.success)throw std::runtime_error(saved.message);draft->MarkSaved();auto exported=screenshotDir/L"edited-share.nrlang";auto result=languageManager->Store().ExportPack(id,exported);if(!result.success)throw std::runtime_error(result.message);nh::LanguageStore recipient(screenshotDir/L"recipient");if(!recipient.ImportPack(exported).success||!recipient.Apply(id).success||recipient.ActivePack().entries.at(draft->Selection()).text!=draft->Translation(draft->Selection()))throw std::runtime_error("Portable edited export differs for independent recipient.");}
    if(frames==70){auto result=languageManager->Store().Apply("included-ja");if(!result.success)throw std::runtime_error(result.message);Neurotic::Localization::QueueSharedCatalog(languageManager->Store().ActivePack());}
    if(frames==80){if(Neurotic::Localization::SelectedLocale()!="ja"||draft->Dirty()||!languageManager->EditorOpen())throw std::runtime_error("Second atlas rebuild changed saved draft state.");}
    if(frames==85){if(!languageManager->RequestClose()||languageManager->EditorOpen())throw std::runtime_error("Saved editor refused to close.");std::ofstream(screenshotDir/L"language-window.json")<<nh::Json{{"secondWindow",true},{"dirtyClosePrompt",true},{"cancelPreserved",true},{"saveDraftClosed",true},{"liveApplyWithEditor",true},{"mainCloseReviewed",true},{"editedExportIndependentRecipient",true},{"cjkAtlasSwitchWithEditor",true},{"mainContextRestored",ImGui::GetCurrentContext()!=nullptr},{"captureStarted",false}}.dump(2);done=true;}
   }
   else if(installDefaultsTest){
    if(!smokeLocale.empty()&&Neurotic::Localization::SelectedLocale()!=smokeLocale)throw std::runtime_error("Install defaults fixture did not activate requested locale");
    for(auto& bar:GImGui->TabBars.Buf)for(auto& tab:bar.Tabs)if(std::strcmp(ImGui::TabBarGetTabName(&bar,&tab),Neurotic::UiLiteral("desktop.installdefaults.tab","Install Defaults"))==0)bar.NextSelectedTabId=tab.ID;
    if(frames==20){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"defaults.png");std::ofstream(screenshotDir/L"notice-locale.json")<<nh::Json{{"requestedLocale",smokeLocale},{"effectiveLocale",Neurotic::Localization::SelectedLocale()}}.dump(2);if(!model.SetInstallDefault("nrMode",0)||!model.SetInstallDefault("style",2)||!model.SetInstallDefault("modelPreset","3"))throw std::runtime_error("Install defaults UI fixture could not save selections");}
    if(frames==40){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"custom.png");auto loaded=nh::ReadInstallDefaults(nh::UserRoot()/L"install-defaults.json");if(loaded.readOnly||loaded.preferences!=model.installDefaults.preferences)throw std::runtime_error("Install defaults UI selections did not survive reload");model.ResetInstallDefaults();}
    if(frames==60){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"reset.png");if(nh::ReadInstallDefaults(nh::UserRoot()/L"install-defaults.json").preferences.size()!=1)throw std::runtime_error("Install defaults reset did not restore package inheritance");done=true;}
   }
   else if(installerTest){
    auto capture=[&](const wchar_t* name){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/name);};
    if(frames==20){capture(L"empty.png");nh::Game game;game.id="installer-preview";game.title="Inert preview game";game.store="Manual";game.root="C:/Inert Preview";game.target={"C:/Inert Preview/FixtureGame.exe","FixtureGame",true,"Controlled UI fixture",64};model.games={game};model.selected=0;model.inspection={{"state",nullptr},{"settings",nh::Json::array()}};}
    if(frames==40){capture(L"not-installed.png");model.inspection["state"]={{"kind","neurotic-game-install"},{"schemaVersion",3},{"status","Installed"},{"proxy","dxgi.dll"}};}
    if(frames==60){capture(L"installed.png");model.inspection["state"]["status"]="Partial";model.freshInstall=true;model.message="Required file skipped. Other package files were copied. Install retries the current package.";}
    if(frames==61)for(auto* pane:GImGui->Windows)if(pane->Active&&std::strstr(pane->Name,"GameDetails"))ImGui::SetScrollY(pane,pane->ScrollMax.y);
    if(frames==78)capture(L"installation-actions.png");
    if(frames==80){capture(L"partial.png");model.showIssue=true;model.operationIssue={{"decisionKind","FileConflict"},{"target",model.games[0].target.path},{"fileConflicts",nh::Json::array({{{"path","dxgi.dll"}}})},{"canKeepReShade",true}};model.fileReview.Begin(model.operationIssue["fileConflicts"]);}
    if(frames==100){capture(L"foreign-file.png");model.fileReview.Cancel();ImGui::ClosePopupToLevel(0,true);model.operationIssue={{"decisionKind","AntiCheatRisk"},{"target",model.games[0].target.path},{"antiCheat",{{"headline","possible"},{"scan_status","incomplete"},{"acknowledgementRequired",true},{"reason","Local anti-cheat evidence is incomplete."},{"findings",nh::Json::array()}}}};}
    if(frames==120){capture(L"anti-cheat.png");model.showIssue=false;ImGui::ClosePopupToLevel(0,true);model.showUninstallConfirm=true;}
    if(frames==140){capture(L"uninstall.png");model.showUninstallConfirm=false;ImGui::ClosePopupToLevel(0,true);model.showIssue=true;model.operationIssue={{"status","SucceededWithNotes"},{"reason","NeuRotic removed. ReShade was left as ReShade64.dll because dxgi.dll is occupied."},{"notes",nh::Json::array()}};}
    if(frames==160){capture(L"reshade-note.png");ImGui::ClosePopupToLevel(0,true);model.inspection["state"]=nullptr;model.operationIssue={{"decisionKind","ProxyRequired"},{"reason","Choose the game's integration filename to continue."}};model.showIssue=true;}
    if(frames==180){capture(L"proxy-choice.png");ImGui::ClosePopupToLevel(0,true);model.operationIssue={{"status","SucceededWithNotes"},{"reasonCode","desktop.installdefaults.package_defaults_fallback"},{"messageParameters",nh::Json::object()},{"reason","Install defaults could not be read. Package defaults were used; the preference file was preserved."},{"notes",nh::Json::array()}};model.showIssue=true;}
    if(frames==200){
     if(!smokeLocale.empty()&&Neurotic::Localization::SelectedLocale()!=smokeLocale)throw std::runtime_error("Fallback notice fixture did not activate requested locale");
     if(!model.showIssue||!ImGui::GetTopMostPopupModal()||model.operationIssue.value("reasonCode",std::string{})!="desktop.installdefaults.package_defaults_fallback")throw std::runtime_error("Actual fallback attention notice was not visible");
     capture(L"package-defaults-fallback.png");std::ofstream(screenshotDir/L"notice-locale.json")<<nh::Json{{"requestedLocale",smokeLocale},{"effectiveLocale",Neurotic::Localization::SelectedLocale()},{"noticePayload",model.operationIssue},{"resolvedNotice",Neurotic::UiText("desktop.installdefaults.package_defaults_fallback")},{"actualModalVisible",true},{"displayOnly",true},{"gameOperations",false}}.dump(2);
     model.showIssue=false;ImGui::ClosePopupToLevel(0,true);done=true;
    }
   }
   else if(objectRulesTest){for(auto& bar:GImGui->TabBars.Buf)for(auto& tab:bar.Tabs){auto label=ImGui::TabBarGetTabName(&bar,&tab);if(std::strcmp(label,"Game Settings")==0||std::strcmp(label,"Neural Rendering")==0||std::strcmp(label,"Object Rules (Experimental)")==0)bar.NextSelectedTabId=tab.ID;}if(frames==10)SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"object-rules-top.png");if((frames>=15&&frames<=18)||(frames>=25&&frames<=28))for(auto* w:GImGui->Windows){auto name=std::string(w->Name);name=name.substr(name.find_last_of('/')+1);if(name.starts_with("GameDetails_"))ImGui::SetScrollY(w,std::min(w->ScrollMax.y,280.f*dpi));}if(frames==40){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"object-rules.png");nh::Json layout=nh::Json::array();for(auto* w:GImGui->Windows)if(w->Active)layout.push_back({{"name",w->Name},{"scroll",w->Scroll.y},{"maxScroll",w->ScrollMax.y},{"height",w->Size.y}});std::ofstream(screenshotDir/L"layout.json")<<layout.dump(2);}if(frames>=41)done=true;}
   else if(polishPreview){
    // Display-only states: no confirmation handler, deletion or game operation.
    if(frames==10){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"library.png");model.showUninstallConfirm=true;model.uninstallPermanent=false;model.uninstallRemoveSettings=false;model.uninstallCategories={true,true,true,false,false,false};}
    if(frames==25){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"uninstall-default.png");model.uninstallPermanent=true;}
    if(frames==40){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"uninstall-permanent.png");model.showUninstallConfirm=false;ImGui::ClosePopupToLevel(0,true);model.page=4;model.dataMaintenanceAction=0;model.dataMaintenancePlan=nh::PlanAppDataClear(nh::UserRoot(),nh::AppDataClearScope::AppData);model.showDataMaintenance=true;}
    if(frames==60){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"clear-app-data.png");model.dataMaintenanceAction=1;model.dataMaintenancePlan=nh::PlanAppDataClear(nh::UserRoot(),nh::AppDataClearScope::GameProfiles);}
    if(frames==80){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"clear-game-profiles.png");model.dataMaintenanceAction=2;}
    if(frames==100){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"open-artwork.png");model.showDataMaintenance=false;ImGui::ClosePopupToLevel(0,true);model.page=2;}
    if(frames==120){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"anything-split-popup.png");done=true;}
   }
   else if(cleanupTest){
    if(frames==10)for(auto* pane:GImGui->Windows)if(pane->Active&&std::strstr(pane->Name,"GameDetails"))ImGui::SetScrollY(pane,pane->ScrollMax.y);
    if(frames==20){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"cleanup-hidden.png");model.sanitizeRevealed=true;}
    if(frames==24){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"cleanup-access.png");model.OpenSanitize();}
    if(frames==40){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"cleanup-choices.png");model.plan={{"sanitation",{{"retained",nh::Json::array({{{"path","dxgi.dll"},{"reason","Shared DLL has no matching trusted NR payload hash; preserved"}},{{"path","NeuroticScreenshots/example.png"},{"reason","Screenshots kept"}}})}}},{"changes",nh::Json::array({{{"path","OptiScaler.ini"}},{{"path","NeuRotic-backups/old/INSTALL-MANIFEST.json"}}})}};}
    if(frames==60){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"cleanup-review.png");model.showSanitize=false;}
    if(frames==62){model.showIssue=true;model.operationIssue={{"decisionKind","SanitizeCleanup"},{"status","FailedWithChanges"},{"reason","A reviewed file changed during cleanup."},{"deleted",nh::Json::array({"OptiScaler.ini"})},{"remaining",nh::Json::array({"NeuRotic-backups/old/INSTALL-MANIFEST.json"})},{"originalsRestored",false}};}
    if(frames==80){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"cleanup-partial.png");model.showIssue=false;model.SetDumbfireInstall(true);model.preflight["components"]["neuralModel"]={{"status","Missing"}};for(auto* pane:GImGui->Windows)if(pane->Active&&std::strstr(pane->Name,"GameDetails"))ImGui::SetScrollY(pane,0);}
    if(frames==90)SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"missing-model-reminder.png");
    if(frames==92)for(auto* pane:GImGui->Windows)if(pane->Active&&std::strstr(pane->Name,"GameDetails"))ImGui::SetScrollY(pane,pane->ScrollMax.y);
    if(frames==100){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"dumbfire-enabled.png");done=true;}
   }
   else if(themeTest){if(frames==1||frames==21||frames==45||frames==95||frames==101||frames==125||frames==180||frames==201||frames==225||frames==246||frames==251)SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/(L"water-"+std::to_wstring(frames)+L".png"));if(frames==20||frames==100||frames==180||frames==200)model.light=!model.light;if(frames==245||frames==250){model.reducedMotion=true;model.light=!model.light;}}
   else if(riskTest){if(frames==20)SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"anti-cheat-warning.png");if(frames==30)done=true;}
   else if(heroTest){if(frames==25||frames==26||frames==30||frames==34||frames==45||frames==60||frames==61||frames==64||frames==80||frames==101)SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/(L"hero-"+std::to_wstring(frames)+L".png"));if(frames==25||frames==60){model.Select(frames==25?1:0);while(model.installer.busy){model.Poll();Sleep(5);}}if(frames==90){model.reducedMotion=true;model.Select(1);while(model.installer.busy){model.Poll();Sleep(5);}}if(frames==102)done=true;}
   else if(settingsTest){
    if(frames==5)for(auto* pane:GImGui->Windows)if(pane->Active&&std::strstr(pane->Name,"PageContent"))pane->StateStorage.SetBool(pane->GetID("##LibraryListCollapsed"),true);
    if(frames%20==5)for(auto* pane:GImGui->Windows)if(pane->Active&&std::strstr(pane->Name,"GameDetails"))ImGui::SetScrollY(pane,280*smokeScale);
    for(auto& bar:GImGui->TabBars.Buf){for(auto& tab:bar.Tabs)if(std::strcmp(ImGui::TabBarGetTabName(&bar,&tab),"Game Settings")==0)bar.NextSelectedTabId=tab.ID;if(bar.Tabs.Size==7)bar.NextSelectedTabId=bar.Tabs[std::min(6,frames/20)].ID;}
    if(frames%20==0)SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/(L"settings-"+std::to_wstring(frames/20-1)+L".png"));if(frames>=140)done=true;
   }
   else if(smoke&&!resizeTimingTest&&frames%20==0){SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/(L"page-"+std::to_wstring(model.page)+L".png"));if(model.page==5)done=true;else model.page++;}
   {nh::ResizeTimingFixture::Scope timing(sizingProbe,nh::ResizeTimingFixture::Present);const UINT sync=modalRender?0:1,flags=modalRender?DXGI_PRESENT_DO_NOT_WAIT:0;HRESULT presented=sizingProbe?sizingProbe->PresentCall(chain.Get(),sync,flags):chain->Present(sync,flags);if(sizingProbe)sizingProbe->NotePresent(presented);if(modalRender&&(presented==DXGI_ERROR_WAS_STILL_DRAWING||presented==DXGI_STATUS_OCCLUDED)){if(sizingProbe)sizingProbe->NoteSkippedPresent();return;}if(FAILED(presented))throw std::runtime_error(Neurotic::UiMessage("desktop.native.a6d008d5f6da", "Desktop device lost; reopen the application"));}if(closing.Settled())done=true;if(smoke&&!resizeTimingTest)Sleep(5);
  };liveRender=renderFrame;
  int fixtureExit=0;
  if(resizeTimingTest){nh::ResizeTimingFixture fixture;sizingProbe=&fixture;struct ProbeReset{~ProbeReset(){sizingProbe=nullptr;}} probeReset;fixtureExit=nh::RunResizeTimingFixture(window,chain.Get(),fixture,renderFrame,model,screenshotDir,resizeTimingControl);done=true;}
  while(!done){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);if(message.message==WM_QUIT)done=true;}if(done)break;renderFrame();if(themeTest&&frames==260){SendMessageW(window,WM_ENTERSIZEMOVE,0,0);SetWindowPos(window,nullptr,0,0,1040,760,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);int before=frames;SendMessageW(window,WM_TIMER,1,0);bool rendered=frames>before&&io.DisplaySize.x<1200;SaveFrame(device.Get(),context.Get(),chain.Get(),screenshotDir/L"resize-during-drag.png");std::ofstream(screenshotDir/L"resize.json")<<nh::Json{{"renderedBeforeRelease",rendered},{"width",io.DisplaySize.x},{"height",io.DisplaySize.y}}.dump(2);SendMessageW(window,WM_EXITSIZEMOVE,0,0);if(!rendered)throw std::runtime_error("Resize did not render during drag");done=true;}if(IsIconic(window)&&!smoke)Sleep(100);}
  liveRender={};anythingShortcut={};mainCloseGate=[](){return false;};nh::SetLanguageManager(nullptr);languageManager.reset();globalAnythingHotkey.reset();KillTimer(window,1);water.Cancel();
  const bool restartForMaintenance=model.restartForMaintenance;std::optional<nh::AppDataClearPlan> clearPlan;if(restartForMaintenance)clearPlan=model.dataMaintenancePlan;model.maintenanceQuiesce={};
  if(smoke){nh::Json result={{"pagesRendered",progressTest?3:(libraryLayoutTest||installDefaultsTest||installerTest||polishPreview||cleanupTest||themeTest||heroTest||riskTest||settingsTest||objectRulesTest)?1:6},{"installationProgressTest",progressTest},{"installerPresentationTest",installerTest},{"themeTransitionTest",themeTest},{"heroSwipeTest",heroTest},{"antiCheatWarningTest",riskTest},{"objectRulesRenderTest",objectRulesTest},{"settingsRenderTest",settingsTest},{"activityPresentationTest",activityTest},{"brandEmbedded",true},{"ngxLoaded",GetModuleHandleW(L"nvngx_dlssnr.dll")!=nullptr},{"proxyLoaded",GetModuleHandleW(L"OptiScaler.dll")!=nullptr},{"captureStarted",false}};std::ofstream(screenshotDir/L"smoke.json")<<result.dump(2);}
  if(closing.Active())closing.Record("resources_begin");artwork->Shutdown();model.artwork=nullptr;nh::StopUpdateCheck();modelOwner.reset();ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();target.Reset();brand.Reset();chain.Reset();context.Reset();device.Reset();CoUninitialize();if(closing.Active())closing.Record("resources_completed");mainCloseGate={};DestroyWindow(window);UnregisterClassW(windowClass.lpszClassName,instance);if(closing.Active())closing.Record("window_destroyed");
  if(clearPlan){
   // All callbacks, workers and the old model are gone. Never save stale state
   // after any successful or partial deletion; the new process reads disk anew.
   auto result=nh::ExecuteAppDataClear(*clearPlan);
   if(!result.complete){std::string explanation=Neurotic::Localization::FormatSharedText("desktop.native.49455c38f559", {{"count",std::to_string(result.deletedFiles)}});for(const auto& issue:result.issues)explanation+="\n\n"+issue;MessageBoxW(nullptr,nh::Wide(explanation).c_str(),L"NeuRotic app data",MB_OK|MB_ICONWARNING);}
   wchar_t executable[32768]{};auto length=GetModuleFileNameW(nullptr,executable,32768);
   if(!length||length>=32768){MessageBoxW(nullptr,nh::Wide(Neurotic::UiMessage("desktop.native.1cff6366675c", "NeuRotic could not resolve its executable for restart. Open NeuRotic.exe again.")).c_str(),L"NeuRotic",MB_OK|MB_ICONERROR);return 1;}
   auto command=std::wstring(L"\"")+executable+L"\"";auto directory=std::filesystem::path(executable).parent_path().wstring();STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION process{};
   dataSession.Release();
   if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,0,nullptr,directory.c_str(),&start,&process)){MessageBoxW(nullptr,nh::Wide(Neurotic::UiMessage("desktop.native.28c63915dfef", "NeuRotic could not restart. Open NeuRotic.exe again. Old in-memory settings were not saved.")).c_str(),L"NeuRotic",MB_OK|MB_ICONERROR);return 1;}
   CloseHandle(process.hThread);CloseHandle(process.hProcess);return result.complete?0:1;
  }
  return fixtureExit;
 }catch(const std::exception& error){liveRender={};mainCloseGate={};nh::SetLanguageManager(nullptr);nh::StopUpdateCheck();if(smoke){std::ofstream(screenshotDir/L"error.txt")<<error.what();}else MessageBoxW(nullptr,nh::Wide(error.what()).c_str(),L"NeuRotic",MB_OK|MB_ICONERROR);CoUninitialize();return 1;}
}
