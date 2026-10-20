#include "imgui_internal.h"
#include <menu/localization/LanguageRuntime.h>
#include <cstring>
static ImRect observedReset,observedUndo,observedValue;
static bool resetObserved=false,undoObserved=false,valueObserved=false;
static float measuredAvailable=0,measuredCaption=0,measuredPreferred=0,measuredWidth=0,measuredFont=0;
static float measuredPaddingX=0,measuredPaddingY=0,measuredFrame=0,measuredRow=0;
static std::string measuredLabels[3];static float measuredLabelsWidth[3]{};
static void ObserveSavedButton(const char* label,ImVec2 minimum,ImVec2 maximum){
 if(std::strcmp(label,"Reset")==0){observedReset={minimum,maximum};resetObserved=true;}
 if(std::strcmp(label,"Undo")==0){observedUndo={minimum,maximum};undoObserved=true;}
}
#define NEUROTIC_SAVED_CONTROL_BUTTON_PROBE ObserveSavedButton
#define NEUROTIC_SAVED_CONTROL_VALUE_PROBE(minimum,maximum) (observedValue=ImRect(minimum,maximum),valueObserved=true)
#define main RunSavedSettingsPlacementTests
#include "GameSettingsViewTests.cpp"
#undef main
#include <cassert>

int RunKeybindUiTests();
int RunNumericUiTests();
// This headless UI fixture never starts a process or writes settings. Record the
// review seam; the production planner/writer is covered by the settings suite.
namespace nh { ProcessJob::~ProcessJob(){assert(!process&&!output);} void HubModel::PlanSettings(){showReview=true;} void HubModel::RestoreSettingsDefaults(){} }

int main(int argc,char** argv){
 int failures=RunSavedSettingsPlacementTests(argc,argv)+RunKeybindUiTests()+RunNumericUiTests();
 auto check=[&](bool ok,const char* what){std::cout<<(ok?"PASS ":"FAIL ")<<what<<'\n';failures+=!ok;};
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1800,1100};io.DeltaTime=1.f/60;
 io.Fonts->AddFontDefault();io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
 nh::HubModel model;nh::KeybindCaptureState capture;nh::SettingsField field;
 field.section="Plugins";field.key="LoadReShade";field.label="Load ReShade";field.available=true;field.choices={"auto","true","false"};field.original="auto";
 strcpy_s(field.draft.data(),field.draft.size(),"auto");
 ImRect reset;ImVec2 combo;bool popup=false;float width=1600,rowHeight=0,textHeight=0;int missingProbes=0;
 auto frame=[&]{
  ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({width,950});ImGui::Begin("Saved settings fixture");
  combo=ImGui::GetCursorScreenPos();const bool row=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*34.f;
  combo.y+=row?ImGui::GetFrameHeight()*.5f:ImGui::GetFrameHeight()+ImGui::GetStyle().ItemSpacing.y+8;
  combo.x+=row?ImGui::GetFontSize()*14.f+ImGui::GetStyle().ItemInnerSpacing.x+40:40;
  rowHeight=ImGui::GetTextLineHeightWithSpacing();textHeight=ImGui::GetTextLineHeight();
  observedReset=observedUndo=observedValue={};resetObserved=undoObserved=valueObserved=false;
  measuredFont=ImGui::GetFontSize();measuredPaddingX=ImGui::GetStyle().FramePadding.x;measuredPaddingY=ImGui::GetStyle().FramePadding.y;
  measuredFrame=ImGui::GetFrameHeight();measuredRow=ImGui::GetContentRegionAvail().x;
  nh::RenderSavedSetting(field,model,capture,"fixture",true);reset=observedReset;combo=observedValue.GetCenter();if(!resetObserved||!valueObserved)++missingProbes;
  const char* widthLabels[]={Neurotic::UiLiteral("desktop.settings.tristate.auto","AUTO"),Neurotic::UiLiteral("desktop.settings.tristate.true","TRUE"),Neurotic::UiLiteral("desktop.settings.tristate.false","FALSE")};
  measuredCaption=0;for(int i=0;i<3;++i){measuredLabels[i]=Neurotic::Translate(widthLabels[i]);measuredLabelsWidth[i]=ImGui::CalcTextSize(measuredLabels[i].c_str()).x;measuredCaption=std::max(measuredCaption,measuredLabelsWidth[i]);}
  const float probeGap=ImGui::GetStyle().ItemInnerSpacing.x,probeLabel=std::min(measuredFont*14.f,measuredRow*.30f);
  const auto probeReset=nh::MeasureSavedReviewAction("Reset",std::max(1.f,measuredRow*.32f)),probeUndo=nh::MeasureSavedReviewAction("Undo",std::max(1.f,measuredRow*.26f));
  const float probeActions=probeReset.width+probeUndo.width+2*probeGap;
  const bool probeRow=measuredRow>=probeLabel+probeGap+measuredFont*8.f+probeActions;
  measuredAvailable=std::max(1.f,measuredRow-probeActions-(probeRow?probeLabel+probeGap:0.f));
  measuredPreferred=std::max(measuredFont*6.f,measuredCaption+2*measuredPaddingX+measuredFrame);
  measuredWidth=nh::MeasureSavedChoiceWidth(measuredAvailable,measuredCaption);
  popup=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);ImGui::End();ImGui::Render();
 };
 auto click=[&](ImVec2 point){io.AddMousePosEvent(point.x,point.y);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();};
 frame();frame();frame();check(std::string(field.draft.data())=="auto","compatibility render preserves automatic default");
 std::cout<<"GEOMETRY compact: min="<<observedValue.Min.x<<','<<observedValue.Min.y<<" max="<<observedValue.Max.x<<','<<observedValue.Max.y<<" widget="<<observedValue.GetWidth()<<" helper="<<measuredWidth<<" preferred="<<measuredPreferred<<" caption="<<measuredCaption<<" row="<<measuredRow<<" available="<<measuredAvailable<<" rowFont="<<measuredFont<<" afterFont="<<ImGui::GetFontSize()<<" padding="<<measuredPaddingX<<','<<measuredPaddingY<<" frame="<<measuredFrame<<" scale="<<io.FontGlobalScale<<" context="<<ImGui::GetCurrentContext()<<'\n';
 for(int i=0;i<3;++i)std::cout<<"GEOMETRY caption: "<<measuredLabels[i]<<" width="<<measuredLabelsWidth[i]<<'\n';
 check(observedValue.GetWidth()<=measuredFont*12.f,"short saved dropdown stays compact at a wide desktop width");
 click(combo);frame();frame();check(popup,"compatibility boolean uses an actual dropdown");
 auto menu=ImGui::FindWindowByName("##Combo_00");
 if(menu)click({menu->Pos.x+30,menu->Pos.y+menu->WindowPadding.y+2*rowHeight+textHeight*.5f});
 check(!popup&&std::string(field.draft.data())=="false","actual compatibility dropdown selects Disabled");
 field.original="false";frame();frame();click(reset.GetCenter());
 check(std::string(field.draft.data())=="auto"&&field.original=="false","Reset restores automatic draft while preserving original compatibility value");
 check(observedReset.Min.x>=observedValue.Max.x&&std::abs(observedReset.Min.y-observedValue.Min.y)<1.f,"Reset is immediately right of the actual dropdown on its row");
 // Authoritative package enum default differs from both current and original.
 field.section="Log";field.key="LogLevel";field.label="Log level";field.choices={"0","1","2"};field.original="1";field.resetKnown=true;field.resetValue="0";strcpy_s(field.draft.data(),field.draft.size(),"2");
 frame();frame();const auto dirtyWidth=observedValue.GetWidth();click(observedReset.GetCenter());
 check(std::string(field.draft.data())=="0"&&field.original=="1","ordinary enum Reset stages package default instead of Undo or first edited value");
 frame();frame();check(undoObserved,"dirty enum exposes its actual Undo widget");click(observedUndo.GetCenter());
 check(std::string(field.draft.data())=="1","Undo restores saved enum independently from Reset");
 frame();frame();check(std::abs(observedValue.GetWidth()-dirtyWidth)<1.f,"dropdown width remains stable when Undo becomes clean");
 field.resetKnown=false;frame();frame();click(observedReset.GetCenter());check(std::string(field.draft.data())=="1","unknown package default disables only Reset");
 field.resetKnown=true;field.resetValue="0";width=300;frame();frame();
 check(observedReset.Min.x>=observedValue.Max.x&&std::abs(observedReset.Min.y-observedValue.Min.y)<1.f,"narrow pane moves label above intact dropdown and Reset strip");
 click(observedReset.GetCenter());check(std::string(field.draft.data())=="0","narrow actual Reset remains clickable");width=1600;
 nh::SettingsField route,scale,resolution;
 route.section="DlssNr";route.key="Route";strcpy_s(route.draft.data(),route.draft.size(),"0");
 scale.section="DlssNr";scale.key="WorkingScale";scale.original="1.75";strcpy_s(scale.draft.data(),scale.draft.size(),"1.75");
 resolution.section="DlssNr";resolution.key="NativeResolutionChoice";strcpy_s(resolution.draft.data(),resolution.draft.size(),"manual");model.settings={route,scale,resolution};
 field.section="DlssNr";field.key="NativeResolutionChoice";field.label="Native resolution";field.choices={"full","pre","post","manual"};field.original="manual";strcpy_s(field.draft.data(),field.draft.size(),"manual");field.originalNativeManualScale=field.nativeManualScale="1.75";field.resetValue="manual";field.resetNativeManualScale="1.25";
 frame();frame();click(observedReset.GetCenter());
 check(std::string(field.draft.data())=="manual"&&field.nativeManualScale=="1.25"&&std::string(model.settings[1].draft.data())=="1.25","manual resolution Reset stages default choice and coupled scale together");
 frame();frame();click(observedUndo.GetCenter());
 check(field.nativeManualScale=="1.75"&&std::string(model.settings[1].draft.data())=="1.75","manual resolution Undo restores both saved scale values");
 field.resetValue="full";frame();frame();click(observedReset.GetCenter());
 check(std::string(field.draft.data())=="full"&&field.nativeManualScale=="1.25"&&std::string(model.settings[1].draft.data())=="1","full resolution Reset stages full scale and preserves package remembered manual scale");model.settings.clear();
 model.settings={route,scale,resolution};field.valueLabels={{"full","Full output resolution"},{"pre","Match Native before Super Resolution with a long wrapped caption"},{"post","Match Native after Super Resolution with a long wrapped caption"},{"manual","Manual percent"}};
 Neurotic::Localization::LanguagePack longLabels;longLabels.locale="en";longLabels.entries["desktop.anythingview.reset_51fe8741"]={"Restore the package default value",{},Neurotic::Localization::CanonicalEnglish().at("desktop.anythingview.reset_51fe8741").revision};Neurotic::Localization::SetSharedCatalog(std::move(longLabels));
 const auto savedStyle=ImGui::GetStyle();io.FontGlobalScale=1.5f;ImGui::GetStyle().ScaleAllSizes(1.5f);width=420;frame();frame();
 check(observedValue.GetHeight()>measuredFrame&&observedReset.GetHeight()>measuredFrame&&observedReset.Min.x>=observedValue.Max.x&&std::abs(observedReset.Min.y-observedValue.Min.y)<1.f,"long translated Reset and Native preview wrap on one row at scaled narrow width");
 ImGui::GetStyle()=savedStyle;io.FontGlobalScale=1;Neurotic::Localization::SetSharedCatalog({});width=1600;model.settings.clear();field.valueLabels.clear();
 field.section="Plugins";field.key="LoadReShade";field.label="Load ReShade";field.choices={"auto","true","false"};field.resetKnown=false;field.resetValue.clear();
 field.available=false;field.original="false";strcpy_s(field.draft.data(),field.draft.size(),"false");frame();frame();
 check(std::string(field.draft.data())=="false","unavailable compatibility entry is preserved");
 check(missingProbes==0,"every focused frame observed fresh dropdown and Reset widgets without stale rectangles");
 field.available=true;field.section="Menu";field.key="Rainbow";field.label="Rainbow accent";field.original="auto";strcpy_s(field.draft.data(),field.draft.size(),"auto");
 ImGui::GetStyle().FrameRounding=30;
 auto checkboxFrame=[&]{ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({width,950});ImGui::Begin("Checkbox fixture");nh::RenderSavedSetting(field,model,capture,"fixture");ImGui::End();ImGui::Render();};
 checkboxFrame();checkboxFrame();check(ImGui::GetStyle().FrameRounding==30&&std::string(field.draft.data())=="auto","three-state dropdown preserves automatic draft and shared theme");
 // Exercise all fields through their real widget renderer at compact and wide
 // sizes. This catches ImGui assertions and accidental draft normalization.
 nh::Json schema;std::ifstream input(argv[1]);input>>schema;
 auto language=std::find_if(schema.begin(),schema.end(),[](const auto& item){return item.at("section")=="Menu"&&item.at("key")=="Language";});
 check(language!=schema.end()&&language->at("missingValue")=="en"&&language->at("valueLabels").at("en")=="English","missing game language displays English, matching the current English desktop app");
 int widgetChanges=0;
 for(const auto& item:schema){
  nh::SettingsField f;f.section=item.at("section");f.key=item.at("key");f.label=item.at("label");f.type=item.at("type");f.available=true;
  f.choices=item.at("values").get<std::vector<std::string>>();f.original=item.value("missingValue",std::string("auto"));strcpy_s(f.draft.data(),f.draft.size(),f.original.c_str());
  f.profileDraft=f.original;f.description=item.value("description",std::string());
  model.settings.push_back(std::move(f));
 }
 io.AddMousePosEvent(-100,-100);
 for(float size:{520.f,1600.f}){width=size;for(auto& f:model.settings){
  ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({size,950});ImGui::Begin("Schema widget fixture");
  if(nh::SavedSettingsPlacement(f.section,f.key).page!=-2)nh::RenderSavedSetting(f,model,capture,"fixture",nh::IsInstallationCompatibilitySetting(f.section,f.key));
  ImGui::End();ImGui::Render();
  if(f.original!=f.draft.data()||(f.type=="objectrules"&&f.original!=f.profileDraft)){++widgetChanges;std::cout<<"FAIL render changed "<<f.section<<'/'<<f.key<<'\n';}
 }
  // The actual saved-tabs renderer must also retain hidden theme/layout tokens.
  for(int settle=0;settle<3;++settle){ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({size,950});ImGui::Begin("Saved tabs preservation fixture");nh::RenderSavedGameSettings(model);ImGui::End();ImGui::Render();}
  for(const auto& f:model.settings)if(f.original!=f.draft.data()||(f.type=="objectrules"&&f.original!=f.profileDraft))++widgetChanges;
 }
 check(widgetChanges==0,"all saved schema widgets render at compact and maximized widths while preserving editable drafts and hidden preferences");


 // A regular click must allow direct typing in the actual DMFG field, with no
 // slider modifier key or second popup. The baseline slider does not do this.
 nh::SettingsField dmfg;dmfg.section="DLSSG";dmfg.key="FramerateTargetDMFG";
 dmfg.type="float";dmfg.available=dmfg.hasRange=true;dmfg.minimum=0;dmfg.maximum=200;
 dmfg.original="auto";strcpy_s(dmfg.draft.data(),dmfg.draft.size(),"auto");
 std::vector<nh::SettingsField> dmfgFields{dmfg};ImVec2 dmfgStart{};
 auto dmfgFrame=[&]{
  ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({900,500});
  ImGui::Begin("Direct DMFG typing fixture");dmfgStart=ImGui::GetCursorScreenPos();
  nh::RenderNumericSetting(dmfg,dmfgFields);ImGui::End();ImGui::Render();
 };
 dmfgFrame();dmfgFrame();check(std::string(dmfg.draft.data())=="auto","direct DMFG visit preserves Auto");
 io.AddMousePosEvent(dmfgStart.x+60,dmfgStart.y+10);dmfgFrame();
 io.AddMouseButtonEvent(0,true);dmfgFrame();io.AddMouseButtonEvent(0,false);dmfgFrame();
 io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);dmfgFrame();
 io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);dmfgFrame();
 io.AddInputCharactersUTF8("240");dmfgFrame();
 check(std::string(dmfg.draft.data())=="240","ordinary click and typing stages DMFG 240 directly without a slider or popup");
 io.AddMousePosEvent(-100,-100);io.AddMouseButtonEvent(0,true);dmfgFrame();io.AddMouseButtonEvent(0,false);dmfgFrame();

 // Deliberate interaction with legacy unknown binary values must be a checkbox,
 // while merely displaying them preserves the exact original draft.
 width=1000;model.settings.clear();io.AddMousePosEvent(-100,-100);
 float mfgRight=0;
 auto mfgFrame=[&]{
  ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({width,950});ImGui::Begin("MFG binary fixture");
  valueObserved=resetObserved=undoObserved=false;mfgRight=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
  nh::RenderSavedSetting(field,model,capture,"mfg-fixture");
  ImGui::End();ImGui::Render();
 };
 auto mfgClick=[&](ImVec2 at){io.AddMousePosEvent(at.x,at.y);mfgFrame();io.AddMouseButtonEvent(0,true);mfgFrame();io.AddMouseButtonEvent(0,false);mfgFrame();};
 for(const char* key:{"NativeMfgExperimental","ExperimentalUnlockRTX30","ExperimentalUnlockRTX20"}){
  field=nh::SettingsField{};field.section="DLSSG";field.key=key;field.label=key;field.type="choice";
  field.available=true;field.choices={"false","true"};field.original="auto";field.resetKnown=true;field.resetValue="false";
  strcpy_s(field.draft.data(),field.draft.size(),"auto");mfgFrame();mfgFrame();
  check(std::string(field.draft.data())=="auto","opening a mixed MFG value preserves its exact stored text");
  check(valueObserved,"actual MFG widget reports its hit rectangle");
  if(valueObserved){
   const auto at=observedValue.GetCenter();io.AddMousePosEvent(at.x,at.y);mfgFrame();
   io.AddMouseButtonEvent(0,true);mfgFrame();io.AddMouseButtonEvent(0,false);mfgFrame();
   check(std::string(field.draft.data())=="true","one deliberate mixed MFG checkbox click writes canonical TRUE");
  }
  mfgFrame();mfgFrame();check(resetObserved&&undoObserved,"MFG exposes actual Reset and dirty Undo");
  if(resetObserved)mfgClick(observedReset.GetCenter());
  check(std::string(field.draft.data())=="false"&&field.original=="auto","MFG Reset chooses default false while preserving the original raw token");
  mfgFrame();if(undoObserved)mfgClick(observedUndo.GetCenter());
  check(std::string(field.draft.data())=="auto","MFG Undo restores the exact legacy Auto token");
  field.original="future-value";strcpy_s(field.draft.data(),field.draft.size(),"future-value");mfgFrame();mfgFrame();
  check(std::string(field.draft.data())=="future-value","MFG visit preserves a future unknown saved token");
  model.installer.busy=true;mfgFrame();mfgClick(observedValue.GetCenter());model.installer.busy=false;
  check(std::string(field.draft.data())=="future-value","busy installer disables actual MFG interaction");
  field.available=false;mfgFrame();mfgClick(observedValue.GetCenter());field.available=true;
  check(std::string(field.draft.data())=="future-value","ambiguous unavailable MFG entries remain disabled");
  mfgFrame();mfgClick(observedValue.GetCenter());mfgFrame();
  if(undoObserved)mfgClick(observedUndo.GetCenter());
  check(std::string(field.draft.data())=="future-value","MFG deliberate replacement and Undo restore unknown saved text exactly");
  ImGui::ClosePopupsOverWindow(nullptr,false);io.AddMousePosEvent(-100,-100);
 }
 // Actual widgets under font/style scaling and long translated actions. These
 // CPU checks establish layout geometry, not physical-monitor DPI behavior.
 const auto beforeScaleStyle=ImGui::GetStyle();
 Neurotic::Localization::LanguagePack longReview;longReview.locale="en";
 longReview.entries["desktop.anythingview.reset_51fe8741"]={"Restore the package default value",{},Neurotic::Localization::CanonicalEnglish().at("desktop.anythingview.reset_51fe8741").revision};
 longReview.entries["desktop.provider.a8283ade3185"]={"Restore the previously saved value",{},Neurotic::Localization::CanonicalEnglish().at("desktop.provider.a8283ade3185").revision};
 Neurotic::Localization::SetSharedCatalog(std::move(longReview));
 for(float scaleFactor:{1.f,1.5f,2.f})for(float logicalWidth:{260.f,700.f}){
  ImGui::GetStyle()=beforeScaleStyle;ImGui::GetStyle().ScaleAllSizes(scaleFactor);io.FontGlobalScale=scaleFactor;width=logicalWidth*scaleFactor;
  field.original="auto";field.label="Experimental MFG with a long translated compatibility label";strcpy_s(field.draft.data(),field.draft.size(),"true");
  mfgFrame();mfgFrame();
  check(valueObserved&&resetObserved&&undoObserved&&observedReset.Min.x>=observedValue.Max.x&&
   observedReset.Min.x-observedValue.Max.x<=ImGui::GetStyle().ItemInnerSpacing.x+1.f&&
   std::abs(observedReset.Min.y-observedValue.Min.y)<1.f&&observedUndo.Max.x<=mfgRight+1.f,
   "scaled narrow/wide MFG checkbox keeps translated Reset and Undo adjacent and inside the pane");
  mfgClick(observedReset.GetCenter());check(std::string(field.draft.data())=="false","scaled translated MFG Reset remains clickable");
  mfgFrame();if(undoObserved)mfgClick(observedUndo.GetCenter());check(std::string(field.draft.data())=="auto","scaled translated MFG Undo retains raw Auto");
 }
 ImGui::GetStyle()=beforeScaleStyle;io.FontGlobalScale=1;Neurotic::Localization::SetSharedCatalog({});width=1000;
 field=nh::SettingsField{};field.section="DLSSG";field.key="FramerateTargetDMFG";field.label="DMFG FPS Target";
 field.type="float";field.available=field.hasRange=field.resetKnown=true;field.minimum=0;field.maximum=3.4028234663852886e38;field.resetValue="auto";
 field.original="auto";strcpy_s(field.draft.data(),field.draft.size(),"240");mfgFrame();mfgFrame();
 check(valueObserved&&resetObserved&&undoObserved,"direct DMFG field exposes adjacent Reset and Undo");
 if(resetObserved)mfgClick(observedReset.GetCenter());check(std::string(field.draft.data())=="auto","direct DMFG Reset restores package Auto");
 field.original="unknown-target";strcpy_s(field.draft.data(),field.draft.size(),"unknown-target");mfgFrame();mfgFrame();
 check(std::string(field.draft.data())=="unknown-target","direct DMFG visit preserves an unknown saved token");
 if(resetObserved)mfgClick(observedReset.GetCenter());mfgFrame();if(undoObserved)mfgClick(observedUndo.GetCenter());
 check(std::string(field.draft.data())=="unknown-target","direct DMFG Undo restores the unknown original after deliberate Reset");
 ImGui::DestroyContext();return failures?1:0;
}
