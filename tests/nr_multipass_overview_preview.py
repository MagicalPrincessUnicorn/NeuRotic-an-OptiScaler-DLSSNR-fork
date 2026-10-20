"""Actual Multipass drawing in the inert WARP preview; never enters a game/provider.

Compile DlssNr_Menu.cpp directly in the fixture translation unit, exclude its cached
object, and reuse unchanged runtime dependencies. --prepare-only never compiles.
"""
from pathlib import Path
import subprocess, sys
root = Path(__file__).resolve().parents[1]
evidence = root/'docs/implementation-evidence/multipass-overview-20261010'
if '--evidence' in sys.argv:
    evidence = Path(sys.argv[sys.argv.index('--evidence')+1]).resolve()
evidence.mkdir(parents=True, exist_ok=True)
source = (root/'tests/nr_advanced_production_preview.py').read_text(encoding='utf-8')
source = source.replace("DIAGNOSTICS='--diagnostics-only' in sys.argv", 'DIAGNOSTICS=False').replace("TIMING='--timing-only' in sys.argv", 'TIMING=False')
source = source.replace("OUT=ROOT/('builds/native-color/frame-cadence-preview' if TIMING else 'builds/native-color/diagnostics-preview' if DIAGNOSTICS else 'builds/universal-input/advanced-production')", "OUT=ROOT/'builds/ui-release/multipass-overview'")
source = source.replace("('menu_common.obj','IdentifyGpu.obj')", "('menu_common.obj','IdentifyGpu.obj','DlssNr_Menu.obj','LanguageCatalog.obj')")
changes = r'''
cpp=cpp.replace('#include "menu/menu_common.cpp"', '#include "menu/menu_common.cpp"\n#include "dlssnr/DlssNr_Menu.cpp"\n#include "menu/localization/LanguageCatalog.cpp"')
cpp=cpp.replace('for(const auto* locale:{"en"})','for(const auto* locale:{"en","stress"})')
cpp=cpp.replace('if(std::string_view(locale)=="en")Neurotic::Localization::SetSharedCatalog({});else Neurotic::Localization::SetSharedCatalog(*Neurotic::Localization::IncludedPack("included-"+std::string(locale)));', ''' + '"""' + r'''
  Neurotic::Localization::LanguagePack pack;pack.locale="en";pack.id="fixture-long";
  if(std::string_view(locale)=="stress")for(const auto& [id,e]:Neurotic::Localization::CanonicalEnglish()){
   if(id.starts_with("ingame.multipass.")&&e.placeholders.empty()&&e.english.size()<45)pack.entries[id]={e.english+" extended label", "Synthetic length stress", e.revision};
  }
  Neurotic::Localization::SetSharedCatalog(std::move(pack));
''' + '"""' + r''')
start=cpp.index('const Page pages[]=');end=cpp.index(';',start)+1
cpp=cpp[:start]+''' + '"""' + r'''const Page pages[]={
{"basic",2,0},{"basic-zero",2,0},{"basic-off",2,0},{"basic-ten",2,0},
{"advanced",2,0},{"advanced-one",2,0},{"advanced-ten",2,0},{"advanced-edit",2,0},
{"basic-switch",2,0},{"advanced-close",2,0},{"advanced-reload",2,0},
{"advanced-bottom",2,0},{"advanced-escape",2,0},{"basic-unsupported",2,0},{"advanced-right",2,0}};''' + '"""' + r'''+cpp[end:]
cpp=cpp.replace('   if((light||logicalWidth<736)', '   if(false&&(light||logicalWidth<736)')
cpp=cpp.replace('for(float scale:{1.f,1.25f})','for(float scale:{1.f,1.25f,1.5f})')
cpp=cpp.replace('desc.Height=1400','desc.Height=1800')
cpp=cpp.replace('logicalWidth:{420.f,736.f}','logicalWidth:{420.f,736.f,960.f}')
cpp=cpp.replace('   const std::string_view pageName=page.name;', ''' + '"""' + r'''
   const std::string_view pageName=page.name;
   auto p=DlssNr::BasicMultipass::Profile{};p.advanced=pageName.starts_with("advanced");p.maximum=pageName.ends_with("ten")?10:3;p.model=2.3f;p.detail=1.55f;
   if(pageName=="basic-zero")p.model=p.detail=0;
   config->DlssNrBasicMultipass=p;config->DlssNrMultipassEnabled=pageName!="basic-off";
   config->DlssNrPasses=pageName.ends_with("ten")?10u:pageName.ends_with("one")?1u:3u;
   config->MenuReduceMotion=true;
   state.api=pageName=="basic-unsupported"?API::DX11:API::DX12;
   config->DlssNrRoute=0u;
''' + '"""' + r''')
cpp=cpp.replace('for(int pass=0;pass<7;++pass){', 'for(int pass=0;pass<7;++pass){if(pageName=="advanced-escape"&&(pass==4||pass==5))io.AddKeyEvent(ImGuiKey_Escape,pass==4);')
start=cpp.index(' MenuCommon::RenderMainMenuWindowActions(ctx);');end=cpp.index(' ImGui::End();ImGui::Render();',start)
cpp=cpp[:start]+''' + '"""' + r'''
 Neurotic::Sleek::WindowSectionHeader("Multipass");
 if(pass<2){auto* table=gui->Tables.GetByKey(ImHashStr("##NrAdvancedOverview",0,rootWindow->ID));if(table)ImGui::SetScrollX(table->InnerWindow,0);}
 if(pageName=="advanced-right"&&pass>=4){auto* table=gui->Tables.GetByKey(ImHashStr("##NrAdvancedOverview",0,rootWindow->ID));if(table)ImGui::SetScrollX(table->InnerWindow,table->InnerWindow->ScrollMax.x);}
 if((pageName=="advanced-edit"||pageName=="advanced-close"||pageName=="advanced-reload"||pageName=="advanced-bottom"||pageName=="advanced-escape")&&pass==2){
  rootWindow->StateStorage.SetInt(ImGui::GetID("##NrMultipassEditPass"),1);
  ImGui::OpenPopup("Pass settings###NrMultipassEditor");
 }
 if(pageName=="advanced-bottom"&&pass>=4)for(auto* w:gui->Windows)if(std::string_view(w->Name).find("##NrMultipassEditorBody")!=std::string_view::npos&&w->LastFrameActive==gui->FrameCount-1)ImGui::SetScrollY(w,w->ScrollMax.y);
 if(pageName=="basic-switch"&&(pass==2||pass==4)){
  gui->NavActivateId=gui->NavActivateDownId=rootWindow->GetID(pass==2?"Advanced":"Basic");
 }
 if(pageName=="advanced-close"&&pass==4){
  auto* modal=ImGui::FindWindowByName("Pass settings###NrMultipassEditor");
  if(!modal)throw std::runtime_error("Cannot activate missing editor Close");
  gui->NavActivateId=gui->NavActivateDownId=modal->GetID("Close");
 }
 if(pageName=="advanced-reload"&&pass==4){DlssNr::BasicMultipass::Update(*config,[](auto& profile){profile.advanced=false;});}
 DlssNr::RenderMultipassMenu(config,scale);
 const bool isAdvanced=config->DlssNrBasicMultipass.value_or_default().advanced;
 const char* tableName=isAdvanced?"##NrAdvancedOverview":"##NrBasicOverview";
 if(pass>=3){
  auto* table=gui->Tables.GetByKey(ImHashStr(tableName,0,rootWindow->ID));
  if(!table||table->LastFrameActive!=gui->FrameCount)throw std::runtime_error(std::string("Missing native overview: ")+tableName);
  if(table->ColumnsCount!=(isAdvanced?6:4))throw std::runtime_error("Wrong overview column count");
  const int expected=isAdvanced?int(config->DlssNrPasses.value_or_default()):int(p.maximum);
  if(table->CurrentRow!=expected)throw std::runtime_error("Overview omitted configured passes");
  if(isAdvanced){
   const auto caption=Neurotic::Translate(Neurotic::UiLiteral("ingame.multipass.rendering_link","Rendering >"));
   const float actionWidth=ImGui::CalcTextSize(caption.c_str()).x+ImGui::GetStyle().FramePadding.x*2;
   if(table->Columns[5].WidthGiven+1<actionWidth)throw std::runtime_error("Advanced action column clips translated navigation button");
   if(pageName=="advanced-right"&&pass==6&&table->Columns[5].WorkMaxX>table->InnerClipRect.Max.x+1.f)throw std::runtime_error("Rightmost action remains clipped at maximum horizontal scroll");
   if(pageName=="advanced-ten"&&pass==6&&table->OuterRect.Max.y>rootWindow->ClipRect.Max.y+1.f)throw std::runtime_error("Bottom of ten-pass table is unreachable");
   if(table->InnerWindow->ScrollMax.y>1)throw std::runtime_error(std::string("Overview acquired unintended vertical scrolling: ")+page.name+" locale="+locale+" width="+std::to_string(logicalWidth)+" scale="+std::to_string(scale)+" scroll="+std::to_string(table->InnerWindow->ScrollMax.y)+" height="+std::to_string(table->InnerWindow->Size.y)+" content="+std::to_string(table->InnerWindow->ContentSize.y));
  }
 }
 if(pageName=="basic-switch"&&pass==3&&!isAdvanced)throw std::runtime_error("Advanced button did not switch modes");
 if(pageName=="basic-switch"&&pass==5&&isAdvanced)throw std::runtime_error("Basic button did not restore Basic mode");
 if((pageName=="advanced-close"||pageName=="advanced-reload"||pageName=="advanced-escape")&&pass>=5&&!gui->OpenPopupStack.empty())throw std::runtime_error("Editor left an input-blocking popup after Close/Esc/mode reload");
 if(pageName=="advanced-edit"&&pass>=3){
  auto* modal=ImGui::FindWindowByName("Pass settings###NrMultipassEditor");
  if(!modal||modal->LastFrameActive!=gui->FrameCount)throw std::runtime_error("Missing additional-pass modal");
  if(modal->Pos.x<0||modal->Pos.y<0||modal->Pos.x+modal->Size.x>io.DisplaySize.x+1||modal->Pos.y+modal->Size.y>io.DisplaySize.y+1)throw std::runtime_error("Editor escaped viewport");
 }
''' + '"""' + r'''+cpp[end:]
cpp=cpp.replace('if(page.stage>=0&&(!nrBefore','if(pageName!="advanced-reload"&&(!nrBefore')
cpp=cpp.replace('config->LightTheme=light;', 'io.DisplaySize={width,height};config->LightTheme=light;')
cpp=cpp.replace('PASS "<<frames<<" production page frames across all pipeline destinations, themes, widths and scales', 'PASS "<<frames<<" Multipass production frames')
'''
source = source.replace("(OUT/'preview.cpp').write_text(cpp,encoding='utf-8')", changes+"\n(OUT/'preview.cpp').write_text(cpp,encoding='utf-8')")
source = source[:source.index("r=subprocess.run(['cmd'")]
scope={'__file__':str(root/'tests/nr_advanced_production_preview.py'),'__name__':'__main__'}
instrument = '''
menu=(ROOT/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
header=(ROOT/'OptiScaler/dlssnr/DlssNr_MultipassMenu.h').read_text(encoding='utf-8')
begin=header.index('static void RenderMultipassObservation');end=header.index('static void RenderMultipassMenu',begin)
observation=header[begin:end].replace('SelectedNativeTelemetry()','MultipassFixture::native').replace('PresentTelemetry()','MultipassFixture::present').replace('GetVulkanPresentStatus().Snapshot()','MultipassFixture::vk')
import re
observation=re.sub(r'PassChainStatusVk\\([^;]*\\)', 'MultipassFixture::chain', observation)
header=header[:begin]+observation+header[end:]
menu=menu.replace('#include "DlssNr_MultipassMenu.h"',header)
menu=menu.replace('Neurotic::Sleek::WindowSectionHeader(shown.c_str());','Neurotic::Sleek::WindowSectionHeader(shown.c_str()); MultipassFixture::headings.push_back({full,shown,GImGui->LastItemData.Rect,window->WorkRect.Max.x,clipped}); if(ImGui::GetItemRectMax().x>window->WorkRect.Max.x+1.f)throw std::runtime_error("Multipass heading escaped its card");')
menu=menu.replace('ImGui::TextUnformatted(full.c_str());','ImGui::TextUnformatted(full.c_str());MultipassFixture::headingTooltip=full;')
menu=re.sub(r'#include "([^"\\n]+)"',lambda m:'#include "'+(ROOT/'OptiScaler/dlssnr'/m[1]).resolve().as_posix()+'"' if (ROOT/'OptiScaler/dlssnr'/m[1]).exists() else m[0],menu)
cpp=cpp.replace('#include "dlssnr/DlssNr_Menu.cpp"', '#include "../../../tests/nr_multipass_interactions.inc"\\n#define SliderInt MultipassFixtureSliderInt\\n#define Button MultipassFixtureButton\\n#define SmallButton MultipassFixtureSmallButton\\n#define Combo MultipassFixtureCombo\\n#define TextWrapped MultipassFixtureTextWrapped\\n'+menu+'\\n#undef TextWrapped\\n#undef Combo\\n#undef SmallButton\\n#undef Button\\n#undef SliderInt\\n#include "../../../tests/nr_multipass_gestures.inc"\\n#include "../../../tests/nr_multipass_status.inc"')
cpp=cpp.replace('  for(bool light:', '  if(argc>2){if(std::string_view(argv[2])=="gestures")MultipassGestures(config);else if(std::string_view(argv[2])=="status")MultipassStatusCases(config);else MultipassLocalization(config);ImGui_ImplDX11_Shutdown();ImGui::DestroyContext(gui);CoUninitialize();return 0;}\\n  for(bool light:')
cpp=cpp.replace(' auto* rootWindow=ImGui::GetCurrentWindow();',' ImGui::BeginChild("##MainMenuPageContent",{0,0}); auto* rootWindow=ImGui::GetCurrentWindow(); if(pageName=="advanced-ten"&&pass>=4)ImGui::SetScrollY(rootWindow,rootWindow->ScrollMax.y);')
cpp=cpp.replace(' ImGui::End();ImGui::Render();',' ImGui::EndChild();ImGui::End();ImGui::Render();')
cpp=cpp.replace('#include "../../../tests/nr_multipass_status.inc"','#include "../../../tests/nr_multipass_status.inc"\\n#include "../../../tests/nr_multipass_heading.inc"')
cpp=cpp.replace('else MultipassLocalization(config);','else if(std::string_view(argv[2])=="heading")MultipassHeadingLocalization(config,[&](const char* name){float clear[]={.07f,.08f,.10f,1};auto* rtv=view.Get();renderer->OMSetRenderTargets(1,&rtv,nullptr);renderer->ClearRenderTargetView(view.Get(),clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());save(name,1200,1300);});else MultipassLocalization(config);')
'''
source=source.replace("(OUT/'preview.cpp').write_text(cpp,encoding='utf-8')",instrument+"\n(OUT/'preview.cpp').write_text(cpp,encoding='utf-8')")
exec(compile(source,str(root/'tests/nr_advanced_production_preview.py'),'exec'),scope)
out=scope['OUT']
if '--prepare-only' in sys.argv:
    print('Prepared source/commands only:',out)
    raise SystemExit(0)
result=subprocess.run(['cmd','/d','/c',str(out/'build.cmd')],cwd=root,capture_output=True,text=True)
(evidence/'preview-build.log').write_text(result.stdout+result.stderr,encoding='utf-8')
print(result.stdout+result.stderr)
if result.returncode: raise SystemExit(result.returncode)
failed=False
cases=sys.argv[sys.argv.index('--cases')+1].split(',') if '--cases' in sys.argv else ['gestures','status','localization','geometry']
for case in cases:
    args=[] if case=='geometry' else [case]
    result=subprocess.run([str(out/'preview.exe'),str(evidence/'preview'),*args],cwd=root,capture_output=True,text=True)
    (evidence/f'preview-{case}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    print(case,result.returncode,result.stdout+result.stderr)
    failed=failed or result.returncode!=0 or '[imgui-error]' in result.stdout+result.stderr
raise SystemExit(int(failed))
