#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <dbghelp.h>
#pragma comment(lib,"dbghelp.lib")
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_dx11.h>
#include <menu/Localization.h>
#include <menu/localization/LanguageFonts.h>
#include <menu/localization/LanguageBundles.h>
#include <menu/localization/LanguageRuntime.h>
#include "../NrConfigFixture.h"
// Observe the real production text call in this fixture TU, then delegate to
// unmodified ImGui. No renderer/font/rectangle substitute is used.
namespace ImGui {void ObservePipelineText(ImDrawList*,const ImVec2&,const ImVec2&,float,const char*,const char*,const ImVec2*);}
#define RenderTextEllipsis ObservePipelineText
#include <dlssnr/MixedInputUi.h>
#undef RenderTextEllipsis
using Microsoft::WRL::ComPtr;
namespace C=DlssNr::Connections;
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void Hr(HRESULT value){Check(SUCCEEDED(value),"GPU render operation failed");}
struct Fixture:TestConfig {CustomOptional<bool> MenuReduceMotion { false };int saves=0;void SaveIni(){++saves;}};

#include <unordered_map>
#include <vector>
#include <algorithm>
struct UiItem {ImGuiID id;ImRect rect;std::string label;float fontSize=0;};
static std::unordered_map<ImGuiID,ImRect> uiRects;
static std::vector<UiItem> uiItems;
static std::unordered_map<int,UiItem> stageItems;
struct TitleWitness {ImGuiID id;ImRect span,clip,ink;std::string text,font;float size,fullWidth;int vertices,fullGlyphs;bool terminalEllipsis;};
static std::unordered_map<int,TitleWitness> titles;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx,ImGuiID id,const ImRect& r,const ImGuiLastItemData*){
 uiRects[id]=r;
 if(ctx->CurrentWindow&&!ctx->CurrentWindow->IDStack.empty())for(int stage=0;stage<7;++stage){
  const auto label="###NrPipelineStage"+std::to_string(stage);
  if(id==ImHashStr(label.c_str(),0,ctx->CurrentWindow->IDStack.back()))stageItems[stage]={id,r,label,ctx->FontSize};
 }
 // This checkout's BeginCombo has ItemAdd but no ItemInfo hook. Match its
 // observed ID against the current production ID stack, never screen guesses.
 if(ctx->CurrentWindow&&!ctx->CurrentWindow->IDStack.empty())for(const char* label:{"##SourcePreference","##InputSourcePreset"})
  if(id==ImHashStr(label,0,ctx->CurrentWindow->IDStack.back()))uiItems.push_back({id,r,label});
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID id,const char* label,ImGuiItemStatusFlags){if(label&&uiRects.contains(id))uiItems.push_back({id,uiRects[id],label});}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return "";}
static UiItem Find(const std::string& label,size_t index=0){
 std::vector<UiItem> found;for(const auto& item:uiItems)if(item.label.find(label)!=std::string::npos)found.push_back(item);
 std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.rect.Min.y<b.rect.Min.y;});
 if(index>=found.size())throw std::runtime_error("Missing control "+label);
 return found[index];
}
void ImGui::ObservePipelineText(ImDrawList* draw,const ImVec2& a,const ImVec2& b,float ellipseX,const char* text,const char* end,const ImVec2* known){
 const int first=draw->VtxBuffer.Size;const auto clip=draw->_ClipRectStack.back();
 auto* font=draw->_Data->Font;const float size=draw->_Data->FontSize;const auto id=GetItemID();
 RenderTextEllipsis(draw,a,b,ellipseX,text,end,known);
 for(const auto& [stage,item]:stageItems)if(item.id==id&&!titles.contains(stage)){
  const char* begin=text;const char* finish=end?end:FindRenderedTextEnd(text);Neurotic::LocalizedRange localized(begin,finish);
  TitleWitness w{id,ImRect(a,b),ImRect({clip.x,clip.y},{clip.z,clip.w}),ImRect(),std::string(begin,finish),font->GetDebugName(),size,CalcTextSize(begin,finish,false,0).x,draw->VtxBuffer.Size-first,0,false};
  auto* baked=font->GetFontBaked(size);
  for(const char* p=begin;p<finish;){unsigned int cp=0;const int n=ImTextCharFromUtf8(&cp,p,finish);if(n<=0)break;p+=n;const auto* glyph=baked->FindGlyph((ImWchar)cp);if(glyph&&glyph->Visible)++w.fullGlyphs;}
  if(w.vertices>0)w.ink=ImRect(draw->VtxBuffer[first].pos,draw->VtxBuffer[first].pos);
  for(int v=first;v<draw->VtxBuffer.Size;++v)w.ink.Add(draw->VtxBuffer[v].pos);
  if(w.vertices>=4){const auto* glyph=baked->FindGlyph(font->EllipsisChar);w.terminalEllipsis=glyph&&glyph->Visible;
   for(int v=draw->VtxBuffer.Size-4;v<draw->VtxBuffer.Size&&w.terminalEllipsis;++v){const auto uv=draw->VtxBuffer[v].uv;w.terminalEllipsis=uv.x>=glyph->U0&&uv.x<=glyph->U1&&uv.y>=glyph->V0&&uv.y<=glyph->V1;}}
  titles[stage]=std::move(w);break;
 }
}
static ImGuiWindow* Rail(){auto* parent=ImGui::FindWindowByName("NR Advanced control fixture");if(parent)for(auto* child:parent->DC.ChildWindows)if(std::string(child->Name).find("##ProcessingOrderRail_")!=std::string::npos)return child;throw std::runtime_error("Missing actual processing rail");}
static void CheckTitle(int stage){
 Check(stageItems.contains(stage)&&titles.contains(stage),"stage item/title observation missing");const auto& item=stageItems.at(stage);const auto& w=titles.at(stage);
 Check(w.id==item.id&&!w.text.empty()&&w.text.find("###")==std::string::npos,"title must observe rendered caption, not hidden ID");
 Check(w.span.Min.x>=item.rect.Min.x&&w.span.Max.x<=item.rect.Max.x&&w.span.Min.y>=item.rect.Min.y&&w.span.Max.y<=item.rect.Max.y,"title geometry must belong to actual card");
 Check(w.clip.Contains(item.rect),"stage must be wholly visible before title fit check");
 Check(w.vertices>=4&&w.vertices%4==0,"visible title must emit actual glyphs");
 Check(!w.font.empty()&&w.size>=item.fontSize,"title cannot shrink below the observed base font");
 // ImFont::RenderText truncates the origin; layout/fine-clip far edges stay fractional.
 // Keep actual draw-clip containment strict as well as the unchanged far edges.
 const ImVec2 inkOrigin=ImTrunc(w.span.Min);
 Check(w.ink.Min.x>=inkOrigin.x&&w.ink.Max.x<=w.span.Max.x&&w.ink.Min.y>=inkOrigin.y&&w.ink.Max.y<=w.span.Max.y&&w.clip.Contains(w.ink),"rendered title ink outside its actual layout rectangle");
 Check(w.fullWidth<=w.span.GetWidth(),"pipeline rendered caption clipped");
 Check(w.vertices/4==w.fullGlyphs,"fitting caption lost glyphs");
}
int main(int argc,char** argv){try{
 SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e)->LONG{SymInitialize(GetCurrentProcess(),nullptr,TRUE);char data[sizeof(SYMBOL_INFO)+1024]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(data);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=1023;DWORD64 offset=0;SymFromAddr(GetCurrentProcess(),e->ContextRecord->Rip,&offset,symbol);std::cerr<<"Exception at "<<symbol->Name<<" + "<<offset<<'\n';return EXCEPTION_EXECUTE_HANDLER;});
 std::cerr<<"fixture start\n";
 Check(argc==2,"output directory required");auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
 Hr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> renderer;
 Hr(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&renderer));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=1400;desc.Height=1800;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> target,staging;Hr(device->CreateTexture2D(&desc,nullptr,&target));ComPtr<ID3D11RenderTargetView> view;Hr(device->CreateRenderTargetView(target.Get(),nullptr,&view));
 desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;Hr(device->CreateTexture2D(&desc,nullptr,&staging));
 auto* gui=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1400,1800};io.DeltaTime=1.f/60;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
 Check(ImGui_ImplDX11_Init(device.Get(),renderer.Get()),"renderer init");Fixture config;
 Check(NeuRotic_ClaimPreparedConnectionV1(3,1,987),"fixture owner");
 std::cerr<<"renderer and owner ready\n";
 auto save=[&](const std::string& name,int width,int height){renderer->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE pixels{};Hr(renderer->Map(staging.Get(),0,D3D11_MAP_READ,0,&pixels));
 ComPtr<IWICImagingFactory> factory;Hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));ComPtr<IWICStream> stream;Hr(factory->CreateStream(&stream));Hr(stream->InitializeFromFilename((output/(name+".png")).c_str(),GENERIC_WRITE));
 ComPtr<IWICBitmapEncoder> encoder;Hr(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));Hr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));ComPtr<IWICBitmapFrameEncode> frame;Hr(encoder->CreateNewFrame(&frame,nullptr));Hr(frame->Initialize(nullptr));Hr(frame->SetSize(width,height));WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;Hr(frame->SetPixelFormat(&format));Hr(frame->WritePixels(height,pixels.RowPitch,height*pixels.RowPitch,static_cast<BYTE*>(pixels.pData)));Hr(frame->Commit());Hr(encoder->Commit());renderer->Unmap(staging.Get(),0);};

 Neurotic::Localization::Json receipts=Neurotic::Localization::Json::array(),titleReceipts=Neurotic::Localization::Json::array();int frames=0;
 gui->TestEngineHookItems=true;Check(!config.DlssNrEnabled.value_or_default(),"fresh NR default must stay off");config.DlssNrInputSource=3;config.DlssNrEnabled=true;
 DlssNr::PipelineView::Facts facts;facts.enabled=facts.active=facts.srAvailable=facts.srUsed=facts.presentSeen=facts.fgRequested=facts.fgUsed=facts.inputsUsed=true;
 facts.requested=facts.effective=DlssNr::PipelineView::Placement::AfterPresent;facts.fgOrder=DlssNr::PipelineView::FgOrder::AfterPresent;
 float width=736,height=1100;
 auto draw=[&](){if(!frames)std::cerr<<"first frame\n";uiRects.clear();uiItems.clear();stageItems.clear();titles.clear();ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,height});
 ImGui::Begin("NR Advanced control fixture",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove);
 if(!frames)std::cerr<<"pipeline\n";DlssNr::PipelineUi::Draw(facts);DlssNr::InputPanelContext ctx;ctx.colorSource=3;ctx.colorUsed=true;
 if(!frames)std::cerr<<"sources\n";DlssNr::DrawMixedInputPreferences(&config,true,ctx);DlssNr::UseAutoInputs(&config);ImGui::End();ImGui::Render();
 float clear[]={.08f,.09f,.11f,1};auto* rtv=view.Get();renderer->OMSetRenderTargets(1,&rtv,nullptr);renderer->ClearRenderTargetView(rtv,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());++frames;};
 auto click=[&](const std::string& label,size_t index=0){const auto point=Find(label,index).rect.GetCenter();
 io.AddMousePosEvent(point.x,point.y);draw();io.AddMouseButtonEvent(0,true);draw();io.AddMouseButtonEvent(0,false);draw();draw();};
 auto key=[&](ImGuiKey k){io.AddKeyEvent(k,true);draw();io.AddKeyEvent(k,false);draw();};
 for(const char* locale:{"en","de","ja"}){
  if(std::string_view(locale)=="en")Neurotic::Localization::SetSharedCatalog({});else Neurotic::Localization::SetSharedCatalog(*Neurotic::Localization::IncludedPack("included-"+std::string(locale)));
  io.Fonts->Clear();io.FontDefault=Neurotic::AddInterfaceFont(io.Fonts,18);if(!io.FontDefault)io.FontDefault=io.Fonts->AddFontDefault();Neurotic::Localization::AddFontFallbacks(io.Fonts,18,locale,false);
  for(bool light:{false,true})for(float scale:{1.f,1.5f})for(float logicalWidth:{320.f,736.f}){
   // Preserve the original four English cases and add two representative
   // translated/scaled cases without multiplying the entire matrix.
   if(std::string_view(locale)=="en"?scale!=1.f:std::string_view(locale)=="de"?(scale!=1.5f||light||logicalWidth!=320.f):(scale!=1.5f||!light||logicalWidth!=736.f))continue;
   ImGui::GetStyle()=ImGuiStyle();if(light)ImGui::StyleColorsLight();else ImGui::StyleColorsDark();ImGui::GetStyle().FramePadding={16,6};ImGui::GetStyle().ScaleAllSizes(scale);io.FontGlobalScale=scale;
   width=logicalWidth*scale;height=1100*scale;
   config.DlssNrDepthSource=4;config.DlssNrMotionSource=2;config.DlssNrSignalStrict=false;C::RequestSignalPreferences(4,2,false);
   C::SignalStatusWireV2 status;status.color=3;status.depth=3;status.motion=2;status.depthReason=uint32_t(C::Mixed::Reason::PreferenceFallback);status.motionReason=uint32_t(C::Mixed::Reason::Ready);status.consumed=1;status.capture=7;status.generation=1;status.revision=C::SignalPreferences().revision;
   for(int pass=0;pass<3;++pass){status.consumed=0;Check(NeuRotic_PublishInputSignalsV2(3,987,&status,sizeof(status)),"fixture admission");status.consumed=1;Check(NeuRotic_PublishInputSignalsV2(3,987,&status,sizeof(status)),"fixture receipt");draw();}
   Check(config.saves==0,"drawing cannot save preferences");Check(config.DlssNrInputSource.value_or_default()==3,"redraw preserves saved route3");if(ImGui::FindWindowByName("NR Advanced control fixture")->ScrollMax.x>1){save("overflow",int(width),int(height));std::cerr<<"overflow width="<<width<<" scale="<<scale<<" max="<<ImGui::FindWindowByName("NR Advanced control fixture")->ScrollMax.x<<'\n';throw std::runtime_error("horizontal overflow");}
   std::string name=std::string(locale)+(light?"-light-":"-dark-")+std::to_string(int(logicalWidth))+"-"+std::to_string(int(scale*100));
   const auto first=stageItems.at(0).rect;Check(!stageItems.contains(1),"Inputs belongs to the separate source panel");
   float previousX=first.Min.x;for(int stage:{0,3,4,2,5,6}){Check(stageItems.contains(stage),"current ordered stage missing");const auto r=stageItems.at(stage).rect;Check(std::abs(r.Min.y-first.Min.y)<1,"pipeline must stay one row");Check(r.Min.x>=previousX,"stage ordering changed");previousX=r.Min.x;}
   // ItemAdd sees genuine offscreen cards too; ItemInfo intentionally skips
   // clipped buttons. Scroll each actual card into view before testing text.
   save(name,int(width),int(height));
   for(int stage:{0,3,4,2,5,6}){
    auto* rail=Rail();const auto r=stageItems.at(stage).rect;ImGui::SetScrollX(rail,ImFloor(rail->Scroll.x+r.Min.x-rail->InnerClipRect.Min.x));draw();draw();
    Check(titles.contains(stage),"stage title observation missing");const auto& w=titles.at(stage);const auto& item=stageItems.at(stage);
    if(stage==0)Check(w.text==Neurotic::Translate("Game Render"),"Game Render caption missing");
    if(stage==4)Check(w.text==Neurotic::Translate("Game Present"),"Game Present caption missing");
    titleReceipts.push_back({{"case",name},{"stage",stage},{"id",w.id},{"caption",w.text},{"font",w.font},{"fontSize",w.size},{"fullWidth",w.fullWidth},{"vertices",w.vertices},{"fullGlyphs",w.fullGlyphs},{"terminalEllipsis",w.terminalEllipsis},{"card",{item.rect.Min.x,item.rect.Min.y,item.rect.Max.x,item.rect.Max.y}},{"span",{w.span.Min.x,w.span.Min.y,w.span.Max.x,w.span.Max.y}},{"clip",{w.clip.Min.x,w.clip.Min.y,w.clip.Max.x,w.clip.Max.y}},{"ink",{w.ink.Min.x,w.ink.Min.y,w.ink.Max.x,w.ink.Max.y}}});
    std::ofstream(output/"pipeline-title-observations.json")<<titleReceipts.dump(2);
    try{CheckTitle(stage);}catch(...){save(name+"-fit-failure-stage"+std::to_string(stage),int(width),int(height));throw;}
    if(stage==2||stage==6)save(name+"-rail-stage"+std::to_string(stage),int(width),int(height));
   }
   ImGui::SetScrollX(Rail(),0);draw();draw();
   save(name,int(width),int(height));receipts.push_back({{"case",name},{"width",width},{"scale",scale},{"drawWrites",config.saves}});
  }
 }
 Neurotic::Localization::SetSharedCatalog({});
 ImGui::GetStyle()=ImGuiStyle();ImGui::StyleColorsDark();io.FontGlobalScale=1;width=736;height=1100;draw();draw();
 const int before=config.DlssNrDepthSource.value_or_default();click("###NrPipelineStage3");
 Check(config.saves==0&&config.DlssNrDepthSource.value_or_default()==before,"stage inspection cannot mutate preferences");
 const auto combo=Find("##SourcePreference",0);click("##SourcePreference",0);Check(gui->OpenPopupStack.Size>0,"source menu opened");save("source-menu",int(width),int(height));
 key(ImGuiKey_DownArrow);key(ImGuiKey_Escape);Check(gui->OpenPopupStack.Size==0&&config.saves==0,"Escape cancels menu");Check(gui->NavId==combo.id,"Escape restores source focus");
 click("##SourcePreference",0);click("###Provider1");Check(config.saves==1&&config.DlssNrColorSource.value_or_default()==1&&config.DlssNrDepthSource.value_or_default()==4&&config.DlssNrMotionSource.value_or_default()==2,"one-signal choice preserves other intent");
 config.saves=0;config.DlssNrEnabled=false;facts.enabled=false;draw();draw();Check(config.saves==0&&config.DlssNrColorSource.value_or_default()==1,"NR disable preserves preferences");save("nr-off",int(width),int(height));
 config.DlssNrDepthSource=99;config.DlssNrSignalStrict=true;C::RequestSavedSignalPreferences(99,2,true);draw();draw();Check(config.DlssNrDepthSource.value_or_default()==99&&config.saves==0,"unknown provider preserved");save("unknown-provider",int(width),int(height));
 const auto restore=Find("##NrAutoInputs").rect.GetCenter();io.AddMousePosEvent(restore.x,restore.y);draw();io.AddMouseButtonEvent(0,true);draw();io.AddMousePosEvent(1300,1700);io.AddMouseButtonEvent(0,false);draw();Check(config.saves==0,"release outside cancels");
 config.DlssNrInputSource=4;click("##NrAutoInputs");Check(config.saves==1&&!config.DlssNrColorSource.value_or_default()&&!config.DlssNrDepthSource.value_or_default()&&!config.DlssNrMotionSource.value_or_default()&&!config.DlssNrSignalStrict.value_or_default()&&config.DlssNrInputSource.value_or_default()==4,"Auto resets only signal intent");
 config.saves=0;config.DlssNrEnabled=true;facts.enabled=true;draw();draw();click("##SourcePreference",0);click("###Provider5");
 Check(config.DlssNrColorSource.value_or_default()==5&&config.DlssNrDepthSource.value_or_default()==0&&config.DlssNrMotionSource.value_or_default()==0,"None is independently saved");
 draw();save("none-color",int(width),int(height));
 config.saves=0;config.DlssNrEnabled=true;facts.enabled=true;facts.rrIntegrated=true;facts.effective=DlssNr::PipelineView::Placement::AfterSr;facts.fgOrder=DlssNr::PipelineView::FgOrder::Unknown;draw();draw();Check(!stageItems.contains(1)&&!stageItems.contains(5),"unknown FG order must not fabricate an ordered card; Inputs remains separate");save("rr-fg-order-unknown",int(width),int(height));
 std::ofstream(output/"pipeline-title-observations.json")<<titleReceipts.dump(2);
 std::ofstream(output/"rendered-cases.json")<<Neurotic::Localization::Json{{"frames",frames},{"productionControls",true},{"simulatedOwnerFacts",true},{"gameHooks",false},{"cases",receipts}}.dump(2);
 ImGui_ImplDX11_Shutdown();ImGui::DestroyContext(gui);CoUninitialize();std::cout<<"PASS "<<frames<<" production control frames; one-row pipeline, source menu and keyboard focus, no draw writes, separate signal choice, off/unknown persistence, scoped Auto reset\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
