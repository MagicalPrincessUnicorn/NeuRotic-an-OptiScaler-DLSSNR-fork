#include <Windows.h>
#include <imgui/imgui_internal.h>
#include "../NrConfigFixture.h"
#include <dlssnr/MixedInputUi.h>
#include <cassert>
#include <iostream>
struct Fixture:TestConfig {int saves=0;void SaveIni(){++saves;}};
int main(){
 using namespace DlssNr;
 auto& cache=InputSources();SourceAvailability::Context context;context.route=1;
 const auto now=GetTickCount64();
 UpdateSourceAvailability(context,{},true,false,now);
 assert(cache.refresh.Pending()&&!cache.completedAt);
 UpdateSourceAvailability(context,{},true,false,now+999);
 assert(cache.refresh.Pending()&&!cache.completedAt);
 UpdateSourceAvailability(context,{},true,false,now+1000);
 assert(!cache.refresh.Pending()&&cache.completedAt==now+1000);
 for(unsigned i=0;i<100000;++i)UpdateSourceAvailability(context,{},true,false,now+1001+i);
 assert(!cache.refresh.Pending()&&cache.completedAt==now+1000);
 cache.manual=true;UpdateSourceAvailability(context,{},true,false,now+101001);
 assert(cache.refresh.Pending()&&!cache.manual&&!cache.completedAt);
 context.policy=9;UpdateSourceAvailability(context,{},true,false,now+102001);
 assert(cache.refresh.Pending()&&!cache.completedAt); // A newer edit supersedes completion.
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;
 io.DisplaySize={640,480};io.DeltaTime=.1f;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
 Fixture config;config.DlssNrDepthSource=4;config.DlssNrMotionSource=3;config.DlssNrSignalStrict=true;
 ImVec2 center{},size{};float firstWidth=0;bool feedback=false;
 auto frame=[&](ImVec2 mouse,bool down){
  io.AddMousePosEvent(mouse.x,mouse.y);io.AddMouseButtonEvent(0,down);ImGui::NewFrame();
  ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({500,300});ImGui::Begin("Actions");
  UseAutoInputs(&config);const auto lo=ImGui::GetItemRectMin();size=ImGui::GetItemRectSize();center={lo.x+size.x/2,lo.y+size.y/2};
  const float completed=ImGui::GetStateStorage()->GetFloat(ImGui::GetID("##AutoInputsApplied"),-100.f);
  feedback=completed>=0&&ImGui::GetTime()-completed<1.8;
  ImGui::End();ImGui::Render();
 };
 frame({-100,-100},false);firstWidth=size.x;assert(!feedback&&config.saves==0);
 frame(center,false);frame(center,true);frame({600,400},false);assert(!feedback&&config.saves==0);
 frame(center,false);frame(center,true);frame(center,false);assert(feedback&&config.saves==1);
 assert(config.DlssNrDepthSource.value_or_default()==0&&config.DlssNrMotionSource.value_or_default()==0&&!config.DlssNrSignalStrict.value_or_default());
 for(int i=0;i<25;i++){frame({600,400},false);assert(size.x==firstWidth);}
 assert(!feedback&&config.saves==1);ImGui::DestroyContext();
 std::cout<<"PASS real refresh completion, change supersession, 100000 idle updates, cancelled and successful auto-select, transient feedback and stable width\n";
}
