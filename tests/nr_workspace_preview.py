"""Render the actual workspace with inert output-owner snapshots; no model or game."""
from pathlib import Path
import subprocess, sys
root=Path(__file__).resolve().parents[1]
source=(root/'tests/nr_advanced_production_preview.py').read_text(encoding='utf-8')
source=source.replace("DIAGNOSTICS='--diagnostics-only' in sys.argv","DIAGNOSTICS=False").replace("TIMING='--timing-only' in sys.argv","TIMING=False")
source=source.replace("OUT=ROOT/('builds/native-color/frame-cadence-preview' if TIMING else 'builds/native-color/diagnostics-preview' if DIAGNOSTICS else 'builds/universal-input/advanced-production')", "OUT=ROOT/'builds/universal-input/workspace-preview'")
source=source.replace("('menu_common.obj','IdentifyGpu.obj')","('menu_common.obj','IdentifyGpu.obj','RenderingOutput.obj')")
changes=r'''
cpp=cpp.replace('using Microsoft::WRL::ComPtr;', '''+"'''"+r'''
#include "dlssnr/RenderingOutput.h"
#include "dlssnr/NrPipelineUi.h"
#include "dlssnr/NrSignalDetailsUi.h"
#include "dlssnr/MixedInputUi.h"
#include "dlssnr/PreparedGuideStatusV2.h"
DlssNr::RenderingOutput::Snapshot previewOutput;
namespace DlssNr::RenderingOutput {Snapshot Query(){auto s=previewOutput;s.sampledAt=GetTickCount64();return s;}void Signal(uintptr_t,bool,bool)noexcept{}std::string StartupDiagnostics(){return "Inert test owner";}}
namespace nh {
std::wstring Wide(const std::string& value){if(value.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);if(!n)return {};std::wstring out(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n);return out;}
std::string Utf8(const std::wstring& value){if(value.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);if(!n)return {};std::string out(n,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n,nullptr,nullptr);return out;}
}
using Microsoft::WRL::ComPtr;
'''+"'''"+r''')
start=cpp.index('const Page pages[]=');end=cpp.index(';',start)+1
cpp=cpp[:start]+'''+'"""'+r'''const Page pages[]={
{"model",2,0},{"color",2,0,-1,1},{"multipass",2,0,-1,2},{"inputs",2,6},{"depth",2,6,6},{"jitter-source",2,6,8},{"super-resolution",2,6,20},{"compare",2,7},{"upscaling",1,0},{"frame-generation",3,1},{"blocked",2,0},{"blocked-collapsed",2,0},{"message-long",2,0},{"native",2,0},{"detailed",2,0},{"collapsed",2,0},{"color-source",2,6,11},{"motion-source",2,6,7},{"connection",2,6,11},{"capture-override",2,6,11},{"native-depth",2,6,6},{"pilots-on",2,0},{"pipeline-active",2,0},{"detail-0",2,6,0},{"detail-1",2,6,1},{"detail-2",2,6,2},{"detail-3",2,6,3},{"detail-4",2,6,4},{"detail-5",2,6,5},{"detail-9",2,6,9},{"detail-10",2,6,10},{"detail-12",2,6,12},{"detail-13",2,6,13},{"detail-14",2,6,14},{"detail-15",2,6,15},{"detail-16",2,6,16},{"detail-17",2,6,17},{"detail-18",2,6,18},{"detail-19",2,6,19},{"receipt-evaluation",2,6},{"receipt-color",2,6},{"vulkan-capture-source",2,6},{"animate-close",2,0},{"animate-open",2,0},{"animate-detail",2,0},{"animate-compact",2,0}};'''+'"""'+r'''+cpp[end:]
cpp=cpp.replace('   if((light||logicalWidth<736)', '   if(false&&(light||logicalWidth<736)')
cpp=cpp.replace('logicalWidth:{420.f,736.f}', 'logicalWidth:{420.f,736.f,940.f}')
cpp=cpp.replace('const float height=1000.f*scale', 'const float height=(logicalWidth==940.f?878.f:1000.f)*scale')
cpp=cpp.replace('config->LightTheme=light;', 'io.DisplaySize={width,height};config->LightTheme=light;')
cpp=cpp.replace('   const std::string_view pageName=page.name;', '''+"'''"+r'''
   const std::string_view pageName=page.name;
   const bool animate=pageName.starts_with("animate-");
   if(animate&&(light||scale!=1.f||logicalWidth!=940.f))continue;
   config->MenuReduceMotion=!animate;
   config->DlssNrMultipassEnabled=pageName=="pilots-on";
   config->CharacterInspectorEnabled=pageName=="pilots-on";
   std::vector<float> navPositions,railHeights;
'''+"'''"+r''')
cpp=cpp.replace('pass<7','pass<(animate?24:7)')
cpp=cpp.replace('Neurotic::Sleek::Scope sleek(true)', 'config->MenuReduceMotion=!animate||pass==0;Neurotic::Sleek::Scope sleek(!animate||pass==0)')
cpp=cpp.replace('   const std::string_view pageName=page.name;', '''+"'''"+r'''
   const std::string_view pageName=page.name;
   previewOutput={};previewOutput.phase=DlssNr::RenderingOutput::Phase::Off;
   config->DlssNrRoute=(pageName=="native"||pageName=="native-depth")?0u:1u;config->DlssNrInputSource=pageName=="capture-override"?3:0;
   io.DeltaTime=pageName.starts_with("blocked")?.3f:1.f/60.f;
   const bool blocked=pageName.starts_with("blocked")||pageName=="message-long";
   config->SetDlssNrEnabled(blocked||pageName=="pilots-on"||pageName=="vulkan-capture-source");
   if(blocked){previewOutput.requested=true;previewOutput.phase=DlssNr::RenderingOutput::Phase::Blocked;previewOutput.reason=pageName.starts_with("blocked")?"GPU completion is uncertain; restart the game before recovery":std::string(500,'W')+". Waiting for compatible input resources.";}
'''+"'''"+r''')
cpp=cpp.replace('state.currentFeature=(page.stage==3)?&feature:nullptr;', 'state.currentFeature=(page.surface==1||pageName=="super-resolution")?&feature:nullptr;')
cpp=cpp.replace('rootWindow->StateStorage.SetInt(ImGui::GetID("##SelectedMainPage"),2);', 'rootWindow->StateStorage.SetInt(ImGui::GetID("##SelectedMainPage"),page.surface);rootWindow->StateStorage.SetBool(ImGui::GetID("##ProcessingOrderOpen"),pageName!="collapsed");rootWindow->StateStorage.SetBool(ImGui::GetID("##ProcessingOrderDetail"),pageName=="detailed");')
cpp=cpp.replace('pageName!="collapsed"', 'pageName=="animate-close"?pass<2:pageName=="animate-open"?pass>=2:pageName!="collapsed"')
cpp=cpp.replace('pageName!="collapsed"', 'pageName!="collapsed"&&pageName!="blocked-collapsed"')
cpp=cpp.replace('pageName=="detailed"', 'pageName=="animate-detail"?pass>=2:pageName=="animate-compact"?pass<2:pageName!="color-source"&&pageName!="motion-source"')
cpp=cpp.replace('if(std::string_view(window->Name).find("##MainMenuPageContent")', 'if(window->ParentWindow==rootWindow&&std::string_view(window->Name).find("##MainMenuPageContent")')
cpp=cpp.replace('rootWindow->StateStorage.SetInt(ImGui::GetID("##RequestedNeuralPage"),page.child);','rootWindow->StateStorage.SetInt(ImGui::GetID("##RequestedNeuralPage"),page.surface==2?page.child:-1);')
cpp=cpp.replace('if(page.stage>=0)window->StateStorage.SetInt(ImHashStr("##PipelineInspectedStage",0,window->ID),page.stage);', 'if(page.stage>=0)window->StateStorage.SetInt(ImHashStr("##NrSignalChoice",0,window->ID),page.stage);')
cpp=cpp.replace('const auto barId=ImHashStr(page.stage==3?"##PipelineSrControls":"##PipelineFgControls",0,window->ID);','const auto barId=ImHashStr("##NrModelImageSections",0,window->ID);')
cpp=cpp.replace('if(page.stage>=0&&(!nrBefore', 'if((!nrBefore')
cpp=cpp.replace('   if(page.stage>=0)window->StateStorage', '   if((pageName=="connection"||pageName=="capture-override")&&pass==1&&window->LastFrameActive==gui->FrameCount-1)ImGui::OpenPopup(ImHashStr("Capture settings##WorkspaceInputOptions",0,window->ID));\n   if(page.stage>=0)window->StateStorage')
cpp=cpp.replace('MenuCommon::RenderMainMenuTabs(ctx);', '''+"'''"+r'''
if(pageName=="vulkan-capture-source"){
 using namespace DlssNr;
 ImGui::TextUnformatted("Synthetic Vulkan capture - production connection panel");
 InputPanelContext c;c.supported=false;c.observationsSupported=true;c.enabled=true;c.colorUsed=true;
 c.useOwnerSignals=true;c.colorName="Captured game image";c.ownerSignals.fresh=c.ownerSignals.usedFresh=true;
 c.ownerSignals.status.color=c.ownerSignals.used.color=2;c.ownerSignals.used.consumed=1;
 InputSectionTitle("Scene color");
 DrawMixedInputPreferences(config,true,c,0);
}else if(pageName.starts_with("receipt-")){
 using namespace DlssNr;
 SignalReceipts::Reading r;r.state=SignalReceipts::State::Consumed;r.sequence=6821;r.age=16;
 r.fact.present=r.fact.admitted=true;r.epoch.route=2;r.epoch.revision=339;r.epoch.resources=2;
 const bool image=pageName=="receipt-color";
 if(image){r.fact.width=1920;r.fact.height=1080;r.fact.stage=SignalReceipts::Stage::PostSr;r.fact.domain=SignalReceipts::Domain::LinearHdr;r.fact.format=10;}
 ImGui::TextUnformatted("Synthetic receipt - production detail renderer");
 if(BeginInputCard("##SampleDetail",image?"Scene color":"NR evaluation",ImGui::GetFontSize()*20)){
  auto panel=SignalDetails::Receipt(image?SignalReceipts::Signal::Color:SignalReceipts::Signal::Evaluation,r);
  panel.values.insert(panel.values.begin(),{"Method","Present"});
  SignalDetails::Draw(panel);
 }
 EndInputCard();
}else if(pageName=="pipeline-active"){
 namespace P=DlssNr::PipelineView;
 P::Facts f;f.enabled=f.active=f.renderActive=f.presentSeen=f.srAvailable=f.srUsed=true;
 f.requested=f.effective=P::Placement::AfterPresent;
 DlssNr::PipelineUi::Readouts r;
 r.provider[int(P::Stage::Render)]="Direct3D 12";r.provider[int(P::Stage::Sr)]="DLSS Super Resolution";
 r.provider[int(P::Stage::Present)]="Direct3D 12";
 r.provider[int(P::Stage::Nr)]="Present | Color: Present image, depth: Native, motion: Native";
 r.provider[int(P::Stage::Delivery)]="NR complete; display unverified";
 rootWindow->StateStorage.SetBool(ImGui::GetID("##ProcessingOrderDetail"),true);
 ImGui::TextUnformatted("Synthetic activity fixture - actual pipeline drawing");
 DlssNr::PipelineUi::Draw(f,true,nullptr,r);
}else MenuCommon::RenderMainMenuTabs(ctx);
'''+"'''"+r''')
cpp=cpp.replace(' ImGui::End();ImGui::Render();', '''+"'''"+r'''
 if(animate){
  auto* tabs=gui->TabBars.GetByKey(ImHashStr("##NeuralWorkspace",0,rootWindow->ID));
  if(!tabs)throw std::runtime_error("Missing workspace tabs");
  navPositions.push_back(tabs->BarRect.Min.y);
  float railHeight=0;for(auto* w:gui->Windows)if(std::string_view(w->Name).find("##ProcessingOrderRail")!=std::string_view::npos&&w->LastFrameActive==gui->FrameCount)railHeight=w->Size.y;
  railHeights.push_back(railHeight);
 }
 ImGui::End();ImGui::Render();'''+"'''"+r''')
cpp=cpp.replace('   bool capture=true;', '''+"'''"+r'''
   if(pageName=="blocked-collapsed"){
    auto* root=ImGui::FindWindowByName("NeuRotic###AdvancedPreview");
    if(root->StateStorage.GetBool(ImHashStr("##ProcessingOrderOpen",0,root->ID)))throw std::runtime_error("Fault opened a collapsed pipeline");
   }
   if((pageName=="connection"||pageName=="capture-override")){
    auto* popup=ImGui::FindWindowByName("Capture settings##WorkspaceInputOptions");
    if(!popup||popup->LastFrameActive!=gui->FrameCount||popup->Hidden)throw std::runtime_error("Capture settings did not render");
    if(pageName=="capture-override"){
     bool shown=false;for(auto* w:gui->Windows)if(std::string_view(w->Name).find("##CaptureSettingsBody")!=std::string_view::npos&&w->LastFrameActive==gui->FrameCount)
      shown=w->StateStorage.GetBool(ImHashStr("Provider override",0,w->ID),false);
     if(!shown)throw std::runtime_error("Saved capture override was hidden");
    }
   }
   if(animate){
    const bool growing=pageName=="animate-open"||pageName=="animate-detail";
    bool intermediate=false;
    for(size_t i=2;i<navPositions.size();++i){
     const float delta=navPositions[i]-navPositions[i-1],heightDelta=railHeights[i]-railHeights[i-1];
     if((growing&&delta<-.6f)||(!growing&&delta>.6f))throw std::runtime_error("Pipeline height moved against requested direction");
     if(std::abs(delta-heightDelta)>1.6f)throw std::runtime_error("Following controls did not move with pipeline height");
     if(std::abs(delta)>40.f)throw std::runtime_error("Pipeline height jumped");
     if(std::abs(delta)>.1f)intermediate=true;
    }
    if(!intermediate||std::abs(navPositions.back()-navPositions[1])<20.f)throw std::runtime_error("Missing height animation");
   }
   bool capture=true;'''+"'''"+r''')
cpp=cpp.replace('   if(page.stage>=0)window->StateStorage', '   ImGui::SetScrollY(window,(pageName.starts_with("detail-")||pageName.ends_with("-source")||(pageName=="depth"||pageName=="native-depth")||(pageName=="connection"||pageName=="capture-override"))?window->ScrollMax.y:0.f);\n   if(page.stage>=0)window->StateStorage')
cpp=cpp.replace('{"vertices",ImGui::GetDrawData()->TotalVtxCount}', '{"vertices",ImGui::GetDrawData()->TotalVtxCount},{"navigationY",navPositions},{"pipelineHeights",railHeights}')
# Keep providers inert while rendering their existing diagnostic projection.
cpp=cpp.replace(' Neurotic::Localization::Json receipts=', ' DlssNr::Capability::ObservationRefresh::Update({},false,false,false,false);DlssNr::Capability::ObservationRefresh::RequestClose();\n Neurotic::Localization::Json receipts=')
'''
source=source.replace("(OUT/'preview.cpp').write_text(cpp,encoding='utf-8')",changes+"\n(OUT/'preview.cpp').write_text(cpp,encoding='utf-8')")
if '--render-only' not in sys.argv:
    try:
        exec(compile(source,str(root/'tests/nr_advanced_production_preview.py'),'exec'),{'__file__':str(root/'tests/nr_advanced_production_preview.py'),'__name__':'__main__'})
    except SystemExit as result:
        if result.code:
            raise
evidence = root/'docs/implementation-evidence/vulkan-owner-reporting-20261009'
result = subprocess.run([str(root/'builds/universal-input/workspace-preview/preview.exe'), str(evidence/'preview')], cwd=root, capture_output=True, text=True)
log = result.stdout + result.stderr
(evidence/'preview-run.log').write_text(log, encoding='utf-8')
assert result.returncode == 0, 'Production preview failed; see preview-run.log'
assert '[imgui-error]' not in log, 'ImGui stack/layout error; see preview-run.log'
assert 'PASS 3624 production page frames' in log, 'Preview matrix incomplete'
print('PASS 3624 frames / 508 cases; activity cards, input dialog, configuration preservation, and synchronized pipeline height animations')
