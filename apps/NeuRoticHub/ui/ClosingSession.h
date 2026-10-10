#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <windows.h>
#include <menu/Localization.h>
#include "json.hpp"
#include "imgui.h"
#include "storage/UserFile.h"

namespace nh {
// UI-thread coordinator. Readiness comes from the owners, never elapsed time.
// Thread-affine owners stay on this thread; only their asynchronous work stops.
class ClosingSession {
 bool active=false;ULONGLONG began=0;
 std::array<bool,8> pending{};bool observed=false;
 std::filesystem::path logPath;std::string log;
 static constexpr const char* stages[]={"artwork","update","bundle","diagnostics","anything","installer","discovery","preflight"};
public:
 enum Work{Artwork,Update,Bundle,Diagnostics,Anything,Installer,Discovery,Preflight};
 bool Active()const{return active;}
 void Record(const char* event,const char* stage="desktop"){
  if(logPath.empty()||log.size()>60000)return;
  const auto utc=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  log+=nlohmann::json{{"utc_ms",utc},{"elapsed_ms",GetTickCount64()-began},{"thread",GetCurrentThreadId()},{"event",event},{"stage",stage}}.dump()+'\n';
  // Replace the last-session receipt through the existing pinned user-file owner.
  // Never follow an existing log symlink/hardlink or make logging a close gate.
  try{SaveUserFile(logPath,log);}catch(...){}
 }
 void Begin(const std::filesystem::path& root){
  if(active)return;active=true;began=GetTickCount64();
  // Diagnostics must not make closing fail when user-data logging is unavailable.
  logPath=root/L"desktop-close.jsonl";
  Record("close_requested");
 }
 void Observe(const std::array<bool,8>& actual){
  for(size_t i=0;i<actual.size();++i)if(!observed||actual[i]!=pending[i])Record(actual[i]?"waiting":"completed",stages[i]);
  pending=actual;observed=true;
 }
 bool Settled()const{return active&&observed&&std::none_of(pending.begin(),pending.end(),[](bool value){return value;});}
 const std::array<bool,8>& Pending()const{return pending;}
};

inline void RenderClosing(const ClosingSession& session,void* brand,float dpi){
 const char* labels[]={
  Neurotic::UiLiteral("desktop.closing.artwork","Finishing artwork cleanup"),
  Neurotic::UiLiteral("desktop.closing.update","Finishing update check"),
  Neurotic::UiLiteral("desktop.closing.bundle","Finishing diagnostics export"),
  Neurotic::UiLiteral("desktop.closing.diagnostics","Finishing game diagnostics"),
  Neurotic::UiLiteral("desktop.closing.anything","Stopping NR Anything"),
  Neurotic::UiLiteral("desktop.closing.installer","Finishing installation operation"),
  Neurotic::UiLiteral("desktop.closing.discovery","Finishing library scan"),
  Neurotic::UiLiteral("desktop.closing.preflight","Finishing pre-flight check")};
 const auto viewport=ImGui::GetMainViewport();
 ImGui::SetNextWindowPos(viewport->Pos);ImGui::SetNextWindowSize(viewport->Size);
 ImGui::PushFont(nullptr,18);
 ImGui::Begin("NeuRotic###ClosingDesktop",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
 const auto logo=ImGui::GetCursorScreenPos();const float scale=226*dpi/1805.f,split=277*scale,height=318*scale;
 if(brand){auto draw=ImGui::GetWindowDrawList();auto id=(ImTextureID)(intptr_t)brand;
  draw->AddImage(id,logo,{logo.x+split,logo.y+height},{176.f/2155,198.f/730},{453.f/2155,516.f/730});
  draw->AddImage(id,{logo.x+split,logo.y},{logo.x+226*dpi,logo.y+height},{453.f/2155,198.f/730},{1981.f/2155,516.f/730},ImGui::GetColorU32(ImGuiCol_Text));
 }else ImGui::TextUnformatted("NeuRotic");
 ImGui::Dummy({0,72*dpi});
 // A stable scrollable surface handles longer translations and future packs.
 ImGui::BeginChild("ClosingStatus",{0,0});
 ImGui::PushTextWrapPos(0);
 ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.closing.title","Closing NeuRotic"));
 ImGui::Separator();ImGui::Spacing();
 for(size_t i=0;i<session.Pending().size();++i)if(session.Pending()[i]){ImGui::TextUnformatted(labels[i]);ImGui::Spacing();}
 if(session.Settled())ImGui::TextUnformatted(Neurotic::UiLiteral("desktop.closing.resources","Releasing desktop resources"));
 ImGui::PopTextWrapPos();ImGui::EndChild();ImGui::End();ImGui::PopFont();
}
}
