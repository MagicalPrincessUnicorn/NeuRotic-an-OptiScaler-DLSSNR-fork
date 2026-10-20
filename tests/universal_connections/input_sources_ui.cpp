#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_dx11.h>
#include <menu/Localization.h>
#include <menu/localization/LanguageFonts.h>
#include <menu/localization/LanguageBundles.h>
#include "../NrConfigFixture.h"
#include <dlssnr/MixedInputUi.h>
using Microsoft::WRL::ComPtr;
namespace C=DlssNr::Connections;
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void Hr(HRESULT value){Check(SUCCEEDED(value),"GPU render operation failed");}
struct Fixture:TestConfig {int saves=0;void SaveIni(){++saves;}};
int main(int argc,char** argv){try{
 Check(argc==2,"output directory required");auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
 Hr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> renderer;
 Hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&renderer));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=1100;desc.Height=750;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> target,staging;Hr(device->CreateTexture2D(&desc,nullptr,&target));ComPtr<ID3D11RenderTargetView> view;Hr(device->CreateRenderTargetView(target.Get(),nullptr,&view));
 desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Hr(device->CreateTexture2D(&desc,nullptr,&staging));
 auto* gui=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1100,750};io.DeltaTime=1.f/60;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
 Check(ImGui_ImplDX11_Init(device.Get(),renderer.Get()),"renderer init");Fixture config;
 Check(NeuRotic_ClaimPreparedConnectionV1(3,1,987),"fixture owner");
 auto save=[&](const std::string& name,int width,int height){renderer->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE pixels{};Hr(renderer->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels));
 ComPtr<IWICImagingFactory> factory;Hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;Hr(factory->CreateStream(&stream));Hr(stream->InitializeFromFilename((output/(name+".png")).c_str(),GENERIC_WRITE));
 ComPtr<IWICBitmapEncoder> encoder;Hr(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Hr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;Hr(encoder->CreateNewFrame(&frame,nullptr));Hr(frame->Initialize(nullptr));Hr(frame->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Hr(frame->SetPixelFormat(&format));Hr(frame->WritePixels(height,pixels.RowPitch,height*pixels.RowPitch,static_cast<BYTE*>(pixels.pData)));Hr(frame->Commit());Hr(encoder->Commit());renderer->Unmap(staging.Get(),0);};
 Neurotic::Localization::Json receipts=Neurotic::Localization::Json::array();ImVec2 restore{};int frames=0;
 auto draw=[&](float width,float height){ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,height});ImGui::Begin("Input preferences fixture",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove);
 DlssNr::DrawMixedInputPreferences(&config,true);restore=ImGui::GetItemRectMin();restore.x+=8;restore.y+=8;
 ImGui::End();ImGui::Render();float clear[]={.08f,.09f,.11f,1};auto* rtv=view.Get();renderer->OMSetRenderTargets(1,&rtv,nullptr);renderer->ClearRenderTargetView(rtv,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());++frames;};
 for(const char* locale:{"en","de","es","fr","ja","ko","pl","pt-BR","ru","zh-CN"}){
  if(std::string_view(locale)=="en")Neurotic::Localization::SetSharedCatalog({});else Neurotic::Localization::SetSharedCatalog(*Neurotic::Localization::IncludedPack("included-"+std::string(locale)));
  io.Fonts->Clear();io.FontDefault=Neurotic::AddInterfaceFont(io.Fonts,18);if(!io.FontDefault)io.FontDefault=io.Fonts->AddFontDefault();Neurotic::Localization::AddFontFallbacks(io.Fonts,18,locale,false);
  for(bool light:{false,true})for(float scale:{1.f,1.5f})for(bool narrow:{false,true}){
   ImGui::GetStyle()=ImGuiStyle();if(light)ImGui::StyleColorsLight();else ImGui::StyleColorsDark();ImGui::GetStyle().ScaleAllSizes(scale);io.FontGlobalScale=scale;
   config.DlssNrDepthSource=4;config.DlssNrMotionSource=2;config.DlssNrSignalStrict=false;C::RequestSignalPreferences(4,2,false);
   C::SignalStatusWire status;status.depth=3;status.motion=2;status.depthReason=uint32_t(C::Mixed::Reason::PreferenceFallback);status.motionReason=uint32_t(C::Mixed::Reason::Ready);status.consumed=1;status.capture=7;status.generation=1;status.revision=C::SignalPreferences().revision;
   Check(NeuRotic_PublishInputSignalsV1(3,987,&status,sizeof(status)),"status publication");
   const float width=(narrow?420.f:700.f)*scale,height=460.f*scale;
   for(int pass=0;pass<3;++pass){Check(NeuRotic_PublishInputSignalsV1(3,987,&status,sizeof(status)),"refresh fixture evidence");draw(width,height);}
   Check(ImGui::FindWindowByName("Input preferences fixture")->ScrollMax.x==0,"settled horizontal overflow");
   Check(config.saves==0,"drawing must not write preferences");
   Check(ImGui::GetDrawData()->TotalVtxCount>100,"empty UI");
   const auto title=Neurotic::UiText("ingame.input-sources.title");Check(Neurotic::Localization::MissingGlyphs(io.FontDefault,18,title)==0,"missing title glyphs");
   std::string name=std::string(locale)+(light?"-light":"-dark")+(narrow?"-narrow":"-wide")+"-"+std::to_string(int(scale*100));
   save(name,int(width),int(height));receipts.push_back({{"case",name},{"width",width},{"scale",scale},{"drawWrites",config.saves},{"vertices",ImGui::GetDrawData()->TotalVtxCount}});
  }
 }
 // Real press/release paths: cancellation outside and a complete Restore Auto.
 io.AddMousePosEvent(restore.x,restore.y);io.AddMouseButtonEvent(0,true);draw(630,690);
 io.AddMousePosEvent(1000,700);io.AddMouseButtonEvent(0,false);draw(630,690);Check(config.saves==0,"release outside cancels");
 io.AddMousePosEvent(restore.x,restore.y);draw(630,690);io.AddMouseButtonEvent(0,true);draw(630,690);io.AddMouseButtonEvent(0,false);draw(630,690);
 Check(config.saves==1&&config.DlssNrDepthSource.value_or_default()==0&&config.DlssNrMotionSource.value_or_default()==0&&!config.DlssNrSignalStrict.value_or_default(),"restore commits once");
 std::ofstream(output/"rendered-cases.json")<<Neurotic::Localization::Json{{"frames",frames},{"realProductionControl",true},{"gameHooks",false},{"iniWrites",false},{"cases",receipts}}.dump(2);
 ImGui_ImplDX11_Shutdown();ImGui::DestroyContext(gui);CoUninitialize();std::cout<<"PASS "<<frames<<" real control frames; ten locales, two themes, two widths/scales, no draw writes, release cancellation and Restore Auto\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
