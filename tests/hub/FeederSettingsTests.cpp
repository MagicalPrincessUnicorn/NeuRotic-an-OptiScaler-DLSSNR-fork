#include "ui/GameSettingsView.h"
#define main RunSavedSettingsPlacementTests
#include "GameSettingsViewTests.cpp"
#undef main
#include <iostream>
#include <map>
struct FeederItem{ImRect rect;ImGuiID id;};
static std::map<std::string,FeederItem> seen;
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*,ImGuiID id,const ImRect& rect,const ImGuiLastItemData*){for(auto& [label,item]:seen)if(item.id==id)item.rect=rect;}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx,ImGuiID id,const char* label,ImGuiItemStatusFlags){seen[label].id=id;if(ctx->LastItemData.ID==id)seen[label].rect=ctx->LastItemData.Rect;}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return "";}
namespace nh { ProcessJob::~ProcessJob(){} void HubModel::PlanSettings(){} void HubModel::RestoreSettingsDefaults(){} }
int main(){
 char executable[]="FeederSettingsTests",schema[]="builds/hub/settings-mirror/schema.json";char* arguments[]={executable,schema};
 int failures=RunSavedSettingsPlacementTests(2,arguments);auto check=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';failures+=!ok;};
 nh::HubModel model;
 auto add=[&](const char* key,const char* value,double lo,double hi){nh::SettingsField f;f.section="DlssNrBasic";f.key=key;f.label=key;f.type=std::string(key)=="MaximumPasses"?"integer":"float";f.hasRange=true;f.available=true;f.minimum=lo;f.maximum=hi;f.original=value;strcpy_s(f.draft.data(),f.draft.size(),value);model.settings.push_back(f);};
 add("MaximumPasses","4",1,10);add("Resolution","0.75",.25,2);add("ModelStrength","2.4",0,10);add("DetailStrength","3.7",0,10);
 auto addBool=[&](const char* section,const char* key,const char* value){nh::SettingsField f;f.section=section;f.key=key;f.label=key;f.type="choice";f.available=true;f.choices={"auto","true","false"};f.original=value;strcpy_s(f.draft.data(),f.draft.size(),value);model.settings.push_back(f);};
 addBool("DlssNr","Enabled","false");addBool("DlssNr","MultipassEnabled","false");addBool("DlssNr","ExperimentalMode","false");addBool("CharacterInspector","Enabled","false");
 addBool("Menu","LightTheme","true");addBool("Menu","ReduceMotion","auto");
 nh::SettingsField unknown;unknown.section="FutureProvider";unknown.key="OpaqueFlags";unknown.original="opaque-127";
 strcpy_s(unknown.draft.data(),unknown.draft.size(),unknown.original.c_str());model.settings.push_back(unknown);
 auto& fields=model.settings;
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1400,1000};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
 nh::KeybindCaptureState capture;std::string text;
 // Exercise the actual tabs rather than inferring their visual order from labels.
 auto render=[&]{seen.clear();GImGui->TestEngineHookItems=true;ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1200,900});ImGui::Begin("Navigation fixture");ImGui::LogToBuffer();nh::RenderSavedGameSettings(model);text=GImGui->LogBuffer.c_str();ImGui::LogFinish();ImGui::End();ImGui::Render();};
 auto find=[&](const char* label)->FeederItem*{for(auto& [name,item]:seen)if(name==label||name.starts_with(std::string(label)+" "))return &item;return nullptr;};
 auto click=[&](const char* label){auto item=find(label);if(!item){check(false,label);return;}auto point=item->rect.GetCenter();io.AddMousePosEvent(point.x,point.y);render();io.AddMouseButtonEvent(0,true);render();io.AddMouseButtonEvent(0,false);render();render();};
 render();render();click("Neural Rendering");
 check(text.find("Rendering")!=text.npos&&text.find("Intake readiness")==text.npos,"Selecting Neural Rendering opens the useful Main settings");
 check(!seen.contains("NR Pre-flight")&&!seen.contains("SR Pre-Flight")&&seen.contains("Diagnostics")&&seen["Diagnostics"].rect.Min.y==seen["Neural Rendering"].rect.Min.y,"Runtime-only saved-settings pages are absent while global Diagnostics remains available");
 check(find("Main")&&find("Multi Pass")&&find("Overrides")&&find("Inspector (Experimental)"),"Off-state saved feature tabs remain visible");
 for(auto [child,index]:{std::pair{0,4},std::pair{1,5},std::pair{3,6},std::pair{4,7}}){
  check(nh::SavedSettingsPilotState(model,child)==Neurotic::Sleek::PilotState::Off,"Disabled saved preference uses a gray pilot light");
  strcpy_s(fields[index].draft.data(),fields[index].draft.size(),"true");
  check(nh::SavedSettingsPilotState(model,child)==Neurotic::Sleek::PilotState::On,"Explicit enabled saved draft uses a green pilot light");
  strcpy_s(fields[index].draft.data(),fields[index].draft.size(),"auto");
  check(nh::SavedSettingsPilotState(model,child)==Neurotic::Sleek::PilotState::Off,"Automatic preference does not claim live service health");
  strcpy_s(fields[index].draft.data(),fields[index].draft.size(),"false");
 }
 click("Multi Pass");check(text.find("ModelStrength")!=text.npos&&text.find("DetailStrength")!=text.npos,"Original Basic model and detail editors remain unchanged");
 check(text.find("MaximumPasses")<text.find("Resolution"),"Basic MaximumPasses is presented before Resolution");
 bool same=true;for(auto& field:fields)same&=field.original==field.draft.data();check(same,"Navigating real saved tabs preserves every saved draft including unequal Basic totals");
 click("General");check(!seen.contains("LightTheme"),"Saved LightTheme is preserved without a duplicate theme toggle");
 check(find("Animations"),"Animations remains visible as the final General settings tab");
 click("Animations");check(seen.contains("Turn animations off")&&!seen.contains("ReduceMotion")&&!seen.contains("Auto"),"Animations presents only the requested checkbox");
 check(std::string(fields[9].draft.data())=="auto","Viewing Animations preserves automatic saved motion preference");
 click("Turn animations off");check(std::string(fields[9].draft.data())=="true","Animation checkbox binds to the existing saved ReduceMotion key");
 click("Turn animations off");check(std::string(fields[9].draft.data())=="false","Animation checkbox re-enables animations without changing saved theme");
 check(std::string(fields[8].draft.data())=="true"&&std::string(fields.back().draft.data())=="opaque-127","Saved LightTheme and unavailable unknown provider flags remain untouched");
 nh::SettingsField experimental;experimental.section="DlssNr";experimental.key="PreSrSoftReset";experimental.label="Soft reset";experimental.description="Requires Experimental mode and active runtime checks.";
 experimental.available=true;experimental.choices={"true","false"};experimental.original="false";strcpy_s(experimental.draft.data(),experimental.draft.size(),"false");
 ImRect help;
 auto renderHelp=[&]{ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({600,300});ImGui::Begin("Overrides help fixture");ImGui::LogToBuffer();nh::RenderSavedSetting(experimental,model,capture,"fixture");help=GImGui->LastItemData.Rect;text=GImGui->LogBuffer.c_str();ImGui::LogFinish();ImGui::End();ImGui::Render();};
 io.AddMousePosEvent(-100,-100);renderHelp();renderHelp();
 check(text.find("(?)")!=text.npos&&text.find(experimental.description)==text.npos,"Experimental setting labels show a help marker without inline descriptions");
 auto helpPoint=help.GetCenter();io.AddMousePosEvent(helpPoint.x,helpPoint.y);for(int frame=0;frame<60;++frame)renderHelp();
 check(text.find(experimental.description)!=text.npos,"Experimental description appears when hovering its help marker");
 check(std::string(experimental.draft.data())=="false","Hovering experimental help does not edit the saved setting");
 ImGui::DestroyContext();return failures?1:0;
}
