#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <dbghelp.h>
#pragma comment(lib,"dbghelp.lib")
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_dx11.h>
#include <menu/localization/LanguageFonts.h>
#include <menu/localization/LanguageBundles.h>
#include <menu/localization/LanguageRuntime.h>
#include "ui/InstallCompletionDialog.h"
#include "ui/GameInstallationStatus.h"
using Microsoft::WRL::ComPtr;
struct Fixture{};
void Check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
void Hr(HRESULT hr){Check(SUCCEEDED(hr),"WARP render setup");}
struct Item {ImGuiID id;ImRect rect;std::string label;};
static std::unordered_map<ImGuiID,ImRect> rects;static std::vector<Item> items;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*,ImGuiID id,const ImRect& r,const ImGuiLastItemData*){rects[id]=r;}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID id,const char* label,ImGuiItemStatusFlags){if(label&&rects.contains(id))items.push_back({id,rects[id],label});}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return "";}
static Item DoneItem(){for(const auto& i:items)if(i.label=="Done")return i;throw std::runtime_error("Done button missing");}
int main(int argc,char** argv){try{
 SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e)->LONG{SymInitialize(GetCurrentProcess(),nullptr,TRUE);char data[sizeof(SYMBOL_INFO)+1024]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(data);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=1023;DWORD64 offset=0;SymFromAddr(GetCurrentProcess(),e->ContextRecord->Rip,&offset,symbol);std::cerr<<"Exception at "<<symbol->Name<<" + "<<offset<<'\n';return EXCEPTION_EXECUTE_HANDLER;});
 std::cerr<<"fixture start\n";
 Check(argc==2,"output directory required");auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
 Hr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> renderer;
 Hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&renderer));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=1600;desc.Height=1800;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> target,staging;Hr(device->CreateTexture2D(&desc,nullptr,&target));ComPtr<ID3D11RenderTargetView> view;Hr(device->CreateRenderTargetView(target.Get(),nullptr,&view));
 desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Hr(device->CreateTexture2D(&desc,nullptr,&staging));
 auto* gui=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1600,1800};io.DeltaTime=1.f/60;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
 Check(ImGui_ImplDX11_Init(device.Get(),renderer.Get()),"renderer init");Fixture config;
 
 std::cerr<<"renderer and owner ready\n";
 auto save=[&](const std::string& name,int width,int height){renderer->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE pixels{};Hr(renderer->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels));
 ComPtr<IWICImagingFactory> factory;Hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;Hr(factory->CreateStream(&stream));Hr(stream->InitializeFromFilename((output/(name+".png")).c_str(),GENERIC_WRITE));
 ComPtr<IWICBitmapEncoder> encoder;Hr(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Hr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;Hr(encoder->CreateNewFrame(&frame,nullptr));Hr(frame->Initialize(nullptr));Hr(frame->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Hr(frame->SetPixelFormat(&format));Hr(frame->WritePixels(height,pixels.RowPitch,height*pixels.RowPitch,static_cast<BYTE*>(pixels.pData)));Hr(frame->Commit());Hr(encoder->Commit());renderer->Unmap(staging.Get(),0);};


 std::cout<<std::unitbuf;gui->TestEngineHookItems=true;io.FontDefault=Neurotic::AddInterfaceFont(io.Fonts,18);if(!io.FontDefault)io.FontDefault=io.Fonts->AddFontDefault();nh::ui::AddActionFont(io.Fonts,24);
 nh::InstallCompletionState state;uint64_t accepted=0;
 auto enqueue=[&](std::string title){state.Begin({0,"original-game",std::move(title),"/inert/original.exe","plan-"+std::to_string(++accepted),L"/inert/original.exe"});const auto id=state.Active()->planId;
 nlohmann::json result={{"status","Succeeded"},{"planId",id},{"errors",nlohmann::json::array()},{"skipped",nlohmann::json::array()},{"inspection",{{"target",{{"executable","/inert/original.exe"}}},{"state",{{"status","Installed"}}}}}};
 Check(state.Observe(result,"Install",[](const std::string& p){return std::wstring(p.begin(),p.end());}),"valid inert UI completion fixture");};
 enqueue("Original game %s Ω & <Title>");
 int checks=0,failed=0,frames=0;auto check=[&](bool ok,const char* text){++checks;failed+=!ok;std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';};
 bool light=false,otherModal=true,closeOther=false,cached=false,known=true;float scale=1,width=1000,height=800;std::string status="Installed",page="Library";ImRect badge;
 auto draw=[&](){rects.clear();items.clear();ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,height});ImGui::Begin("Global completion fixture",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove);
 ImGui::TextUnformatted(page.c_str());ImGui::PushFont(nullptr,14);nh::ui::GameInstallationStatus(status,cached,light,known);badge={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};ImGui::PopFont();
 if(otherModal)ImGui::OpenPopup("Existing dialog");if(ImGui::BeginPopupModal("Existing dialog",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("Existing dialog");if(closeOther)ImGui::CloseCurrentPopup();ImGui::EndPopup();}
 nh::ui::InstallationCompletionDialog(state,light,scale);ImGui::End();ImGui::Render();float clear[]={.08f,.09f,.11f,1};auto* rtv=view.Get();renderer->OMSetRenderTargets(1,&rtv,nullptr);renderer->ClearRenderTargetView(rtv,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());++frames;};
 auto hasModal=[&](){auto* w=ImGui::FindWindowByName("##InstallationComplete");return w&&w->Active;};
 auto key=[&](ImGuiKey k){io.AddKeyEvent(k,true);draw();io.AddKeyEvent(k,false);draw();};
 auto clickDone=[&](){
  // A newly opened native popup may spend a frame measuring its auto-size.
  // Wait for the actual visible action, without activating or fabricating it.
  bool ready=false;for(int wait=0;wait<10;++wait){auto* popup=ImGui::FindWindowByName("##InstallationComplete");for(const auto& item:items)if(item.label=="Done"&&popup&&popup->Active&&!popup->Hidden&&popup->InnerClipRect.Contains(item.rect))ready=true;if(ready)break;
   std::cout<<"WAIT_DONE frame="<<frames<<" pending="<<(state.Pending()?state.Pending()->title:"none")<<" open="<<ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel)<<" active="<<(popup&&popup->Active)<<" hidden="<<(popup&&popup->Hidden)<<'\n';draw();
  }Check(ready,"Done did not become visible within ten native frames");auto p=DoneItem().rect.GetCenter();io.AddMousePosEvent(p.x,p.y);draw();io.AddMouseButtonEvent(0,true);draw();io.AddMouseButtonEvent(0,false);draw();draw();};
 for(int i=0;i<3;++i)draw();check(state.Pending()&&!hasModal(),"existing modal defers completion without losing notice");otherModal=false;closeOther=true;draw();closeOther=false;page="Settings";draw();draw();check(hasModal()&&state.Pending(),"completion appears globally on another page after prior dialog closes");
 nlohmann::json observations=nlohmann::json::array();
 for(bool theme:{false,true})for(float factor:{1.f,1.5f})for(float logicalWidth:{880.f,1000.f}){
 light=theme;scale=factor;width=logicalWidth*scale;height=800*scale;io.DisplaySize={width,height};io.FontGlobalScale=scale;ImGui::GetStyle()=ImGuiStyle();if(light)ImGui::StyleColorsLight();else ImGui::StyleColorsDark();ImGui::GetStyle().ScaleAllSizes(scale);
 for(int i=0;i<3;++i)draw();const auto done=DoneItem();auto* popup=ImGui::FindWindowByName("##InstallationComplete");
 check(popup&&popup->Active&&done.rect.Min.x>=popup->InnerClipRect.Min.x&&done.rect.Max.x<=popup->InnerClipRect.Max.x&&done.rect.Max.y<=popup->InnerClipRect.Max.y,"single Done action stays visible within modal clip");
 const auto* viewport=ImGui::GetMainViewport();const ImRect work(viewport->WorkPos,{viewport->WorkPos.x+viewport->WorkSize.x,viewport->WorkPos.y+viewport->WorkSize.y});
 check(popup&&work.Contains(popup->Rect()),"resized modal stays inside current viewport work area");
 const auto center=popup->Rect().GetCenter(),expected=viewport->GetWorkCenter();
 check(std::abs(center.x-expected.x)<=1.f&&std::abs(center.y-expected.y)<=1.f,"open modal follows viewport center after size and scale change");
 check(state.Pending()&&state.Pending()->title=="Original game %s Ω & <Title>","page/theme/scale draws preserve raw original game identity");
 const std::string name=(light?"light-":"dark-")+std::to_string(int(logicalWidth))+"-"+std::to_string(int(scale*100));save(name,int(width),int(height));observations.push_back({{"case",name},{"popup",{popup->Pos.x,popup->Pos.y,popup->Size.x,popup->Size.y}},{"done",{done.rect.Min.x,done.rect.Min.y,done.rect.Max.x,done.rect.Max.y}},{"identity",state.Pending()->title}});
 }
 auto done=DoneItem();auto p=done.rect.GetCenter();io.AddMousePosEvent(p.x,p.y);draw();io.AddMouseButtonEvent(0,true);draw();io.AddMousePosEvent(0,0);io.AddMouseButtonEvent(0,false);draw();check(state.Pending()!=nullptr,"Done release outside cancels native button activation");
 key(ImGuiKey_Escape);draw();check(!state.Pending()&&!hasModal(),"Escape acknowledges and closes pending completion");for(int i=0;i<4;++i)draw();check(!hasModal(),"acknowledged dialog does not reopen on later frames");
 enqueue("Keyboard completion");draw();draw();key(ImGuiKey_Tab);draw();check(gui->NavId==DoneItem().id&&gui->NavCursorVisible,"native Tab visibly focuses Done after mouse interaction");key(ImGuiKey_Enter);draw();check(!state.Pending(),"focused Done accepts keyboard Enter");
 enqueue("First queued game");enqueue("Second queued game");draw();draw();clickDone();check(state.Pending()&&state.Pending()->title=="Second queued game","Done acknowledges only first queued operation identity");clickDone();check(!state.Pending(),"second queued completion acknowledges independently");
 enqueue(std::string(300,'W')+" %s Ω");draw();draw();save("long-game-name",int(width),int(height));check(DoneItem().rect.Max.y<=ImGui::FindWindowByName("##InstallationComplete")->InnerClipRect.Max.y,"long game name preserves visible Done");clickDone();
 float rowHeight=0;for(const auto* value:{"Installed","Partial",""})for(bool saved:{false,true}){status=value;cached=saved;draw();if(!rowHeight)rowHeight=badge.GetHeight();check(std::abs(rowHeight-badge.GetHeight())<.01f,"badge status/cache changes preserve reserved row geometry");save(std::string("badge-")+(status.empty()?"none":status)+(saved?"-saved":"-current"),int(width),int(height));}
 known=false;draw();save("badge-unknown",int(width),int(height));
 std::ofstream(output/"observations.json")<<nlohmann::json{{"checks",checks},{"failures",failed},{"frames",frames},{"cases",observations},{"actualProductionDialogAndBadge",true},{"simulatedCompletionReceipts",true},{"wholeHub",false}}.dump(2);
 ImGui_ImplDX11_Shutdown();ImGui::DestroyContext(gui);CoUninitialize();std::cout<<"RESULT checks="<<checks<<" failures="<<failed<<" frames="<<frames<<'\n';return failed?1:0;
 }catch(const std::exception& e){std::cerr<<"SETUP_OR_RUNTIME_ERROR "<<e.what()<<'\n';return 2;}return 0;}
