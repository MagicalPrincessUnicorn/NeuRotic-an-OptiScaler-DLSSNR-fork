#include "nr/semantic/object_rules/ObjectRuleEditor.h"
#include "nr/semantic/character/CharacterInspectorDraw.h"
#include <imgui/imgui_internal.h>
#include <cassert>
#include <iostream>
using namespace Neurotic::Semantic::Rules;
int main(){ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1000,900};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;Store store;Editor editor;auto p=EmptyProfile();p["rules"].push_back(NewRule("person","Human"));store.Commit(0,p,1);editor.selected=p["rules"][0]["id"];int files=0;Host host;host.desktop=true;host.importFile=[&](){++files;return std::string();};host.exportFile=[&](const std::string&){++files;return true;};
for(float scale:{1.f,1.5f,2.f})for(float width:{350.f,520.f,680.f})for(bool desktop:{false,true}){
io.FontGlobalScale=scale;host.desktop=desktop;ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,700});ImGui::Begin("Host",nullptr,ImGuiWindowFlags_NoSavedSettings);const auto font=ImGui::GetFont();const auto style=ImGui::GetStyle();DrawEditor(store,editor,host);assert(ImGui::GetFont()==font);assert(ImGui::GetStyle().Colors[ImGuiCol_Button].x==style.Colors[ImGuiCol_Button].x);ImGui::End();ImGui::Render();assert(ImGui::GetCurrentContext()->ColorStack.empty());assert(ImGui::GetCurrentContext()->StyleVarStack.empty());assert(editor.selected==p["rules"][0]["id"].get<std::string>());}
assert(files==0&&editor.mode==0);
// Compact square checkboxes keep normal ImGui keyboard semantics and stay inside the On cell.
for(float scale:{1.f,1.5f,2.f}){
 io.FontGlobalScale=scale;bool enabled=false;
 ImGui::NewFrame();ImGui::Begin("Square checkbox");const auto id=ImGui::GetID("##Square");
 GImGui->NavActivateId=GImGui->NavActivateDownId=GImGui->NavActivatePressedId=id;
 assert(ImGui::Checkbox("##Square",&enabled,true));assert(enabled);
 assert(ImGui::GetItemRectSize().x==ImGui::GetFrameHeight());
 ImGui::End();ImGui::Render();
}

// The selected rule opens its working appearance controls without another click.
io.FontGlobalScale=1;editor.detail=true;host.desktop=false;
for(int frame=0;frame<2;++frame){ImGui::NewFrame();ImGui::SetNextWindowSize({680,700});ImGui::Begin("Appearance accessibility");DrawEditor(store,editor,host);
bool appearance=false;for(auto* window:GImGui->Windows)if(window->ParentWindow==ImGui::GetCurrentWindow()&&std::string(window->Name).find("Rule detail")!=std::string::npos){
auto* bar=GImGui->TabBars.GetByKey(window->GetID("Properties"));if(bar&&bar->Tabs.Size)appearance=std::string(ImGui::TabBarGetTabName(bar,&bar->Tabs[0]))=="Appearance";
assert(window->Scroll.x==0);}
assert(appearance);ImGui::End();ImGui::Render();}
// The exposed appearance tab still edits only the selected rule's live overlay.
const auto originalRule=store.Read()->rules[0].value;const auto revision=store.Read()->revision;
ImGuiID showBox=0;for(auto* window:GImGui->Windows)if(std::string(window->Name).find("Appearance accessibility/Rule detail")!=std::string::npos){auto* bar=GImGui->TabBars.GetByKey(window->GetID("Properties"));if(bar&&bar->Tabs.Size)showBox=ImHashStr("Show box",0,bar->Tabs[0].ID);}
assert(showBox);ImGui::NewFrame();ImGui::SetNextWindowSize({680,700});ImGui::Begin("Appearance accessibility");
GImGui->NavActivateId=GImGui->NavActivateDownId=GImGui->NavActivatePressedId=showBox;DrawEditor(store,editor,host);ImGui::End();ImGui::Render();
assert(store.Read()->revision==revision+1);assert(!store.Read()->rules[0].value["overlay"]["show_box"].get<bool>());
assert(store.Read()->rules[0].value["appearance"]==originalRule["appearance"]&&store.Read()->rules[0].value["nr"]==originalRule["nr"]);
auto restored=store.Read()->profile;restored["rules"][0]=originalRule;assert(store.Commit(store.Read()->revision,restored).accepted);
// Dirty selection opens a modal in the same parent scope on the next frame.
editor.dirty=true;Select(editor,"another-rule");assert(editor.selected==p["rules"][0]["id"].get<std::string>());
ImGui::NewFrame();ImGui::Begin("Host");DrawEditor(store,editor,host);ImGui::PushID(&store);assert(ImGui::IsPopupOpen("Unapplied edit"));ImGui::PopID();ImGui::End();ImGui::Render();
editor.pendingSelect.clear();editor.dirty=false;
// A full catalog must still allow choosing Replace when Copies exceeds the limit.
auto full=EmptyProfile();for(int i=0;i<512;++i)full["rules"].push_back(NewRule("person","Human"));Store fullStore;assert(fullStore.Commit(0,full,1).accepted);
Editor importEditor;importEditor.importBytes=Export(p);PreviewImport(fullStore.Read(),importEditor,0);assert(!importEditor.preview&&!importEditor.importError.empty());
PreviewImport(fullStore.Read(),importEditor,2);assert(importEditor.preview&&importEditor.preview->profile["rules"].size()==1);

// Scrolling the host must not resize the nested editor panes.
io.FontGlobalScale=1;editor.detail=true;float paneHeight=0;
for(int frame=0;frame<3;++frame){ImGui::NewFrame();ImGui::SetNextWindowSize({350,450});ImGui::Begin("Scroll stability");DrawEditor(store,editor,host);
for(auto* window:GImGui->Windows)if(window->ParentWindow==ImGui::GetCurrentWindow()&&std::string(window->Name).find("Rule detail")!=std::string::npos){if(frame==0)paneHeight=window->Size.y;else assert(window->Size.y==paneHeight);}
ImGui::SetScrollY(120);ImGui::End();ImGui::Render();}assert(paneHeight>0);
// Existing geometry emitter consumes immutable rules only after normal frame admission.
ImGui::NewFrame();ImGui::Begin("Overlay test");auto* draw=ImGui::GetWindowDrawList();
using namespace Neurotic::Semantic::Character;auto fixture=MakeCharacterFixture();fixture.key.captureNs=10;
DrawStyle style;style.rules=store.Read();int before=draw->VtxBuffer.Size;EmitCharacterGeometry(*draw,fixture,{0,0,800,600},style);assert(draw->VtxBuffer.Size>before);
auto empty=store.Read()->profile;empty["rules"]=Json::array();store.Commit(store.Read()->revision,empty,20);style.rules=store.Read();before=draw->VtxBuffer.Size;EmitCharacterGeometry(*draw,fixture,{0,0,800,600},style);assert(draw->VtxBuffer.Size==before);
store.Undo(store.Read()->revision,30);style.rules=store.Read();EmitCharacterGeometry(*draw,fixture,{0,0,800,600},style);assert(draw->VtxBuffer.Size==before);
style.torsoBand=true;EmitCharacterGeometry(*draw,fixture,{0,0,800,600},style);assert(draw->VtxBuffer.Size==before);
fixture.key.captureNs=31;EmitCharacterGeometry(*draw,fixture,{0,0,800,600},style);assert(draw->VtxBuffer.Size>before);
// Hundreds of equivalent rules share observations without multiplying geometry.
auto crowded=store.Read()->profile;for(int n=1;n<512;++n)crowded["rules"].push_back(NewRule("person","Human"));
const auto singleStart=draw->VtxBuffer.Size;EmitCharacterGeometry(*draw,fixture,{0,0,800,600},style);const auto singleVertices=draw->VtxBuffer.Size-singleStart;
Store crowdedStore;assert(crowdedStore.Commit(0,crowded,1).accepted);auto crowdedStyle=style;crowdedStyle.rules=crowdedStore.Read();const auto crowdedStart=draw->VtxBuffer.Size;EmitCharacterGeometry(*draw,fixture,{0,0,800,600},crowdedStyle);assert(draw->VtxBuffer.Size-crowdedStart==singleVertices);
Selection().armed=true;io.MousePos={200,200};io.MouseClicked[0]=true;io.MouseDown[0]=true;io.MouseDownDuration[0]=0;io.WantCaptureMouse=false;
EmitCharacterGeometry(*draw,fixture,{0,0,800,600},style);assert(Selection().pending.empty());
style.editRules=true;EmitCharacterGeometry(*draw,fixture,{0,0,800,600},style);assert(!Selection().armed&&Selection().pending==p["rules"][0]["id"].get<std::string>());
ImGui::End();ImGui::Render();
ImGui::DestroyContext();std::cout<<"Object Rules real ImGui compact/scale/host/no-side-effects and delete/undo overlay tests passed\n";}
