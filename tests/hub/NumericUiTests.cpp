#include "ui/HubViewModel.h"
#include "ui/NumericSettings.h"
#include "imgui.h"
#include <iostream>
#include <cstring>
int RunNumericUiTests(){
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';failed+=!ok;};
 auto previous=ImGui::GetCurrentContext();auto context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;io.Fonts->AddFontDefault();io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
 nh::SettingsField field;field.type="float";field.available=field.hasRange=true;field.minimum=0;field.maximum=2;field.original="0.913245678901";strcpy_s(field.draft.data(),field.draft.size(),field.original.c_str());std::vector<nh::SettingsField> fields{field};ImVec2 slider{};float sliderWidth=0;
 auto frame=[&]{ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({700,500});ImGui::Begin("Numeric functional test");slider=ImGui::GetCursorScreenPos();sliderWidth=std::min(ImGui::GetFontSize()*20,ImGui::GetContentRegionAvail().x);nh::RenderNumericSetting(field,fields);ImGui::End();ImGui::Render();};
 for(int i=0;i<3;i++)frame();check(field.draft.data()==field.original,"NH-NUMERIC-UI: rendering precise explicit value preserves its exact token");
 strcpy_s(field.draft.data(),field.draft.size(),"auto");for(int i=0;i<3;i++)frame();check(std::string(field.draft.data())=="auto","NH-NUMERIC-UI: untouched Automatic value remains automatic after rendering");
 io.AddMousePosEvent(slider.x+sliderWidth*.75f,slider.y+10);io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();auto parsed=nh::ParseNumericDraft(field.draft.data());check(parsed.valid&&!parsed.automatic&&parsed.value>1&&parsed.value<=2&&field.original=="0.913245678901","NH-NUMERIC-UI: actual slider input updates only the bounded draft");
 strcpy_s(field.draft.data(),field.draft.size(),"9.0000000001");for(int i=0;i<3;i++)frame();check(std::string(field.draft.data())=="9.0000000001","NH-NUMERIC-UI: legacy out-of-range explicit value is preserved for exact editing");
 ImGui::DestroyContext(context);ImGui::SetCurrentContext(previous);return failed;
}
