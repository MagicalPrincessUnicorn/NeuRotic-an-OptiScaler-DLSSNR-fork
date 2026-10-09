#pragma once
#include "AnythingUiState.h"
#include "library/ManualLibrary.h"
#include <windows.h>
#include <limits>

namespace nh {
enum class AnythingTargetLife { Alive, Gone, Unknown };

// Selection eligibility (visibility, DWM extent, foreground, etc.) is separate
// from identity lifetime. A minimized, hidden or unfocused target still exists.
inline AnythingTargetLife ProbeAnythingTargetLife(const nlohmann::json& identity) noexcept {
 try {
  for(const char* key:{"hwnd","pid","processCreation"})
   if(!identity.is_object()||!identity.contains(key)||!identity[key].is_number_unsigned()||identity[key].get<uint64_t>()==0)return AnythingTargetLife::Unknown;
  for(const char* key:{"executable","windowClass"})
   if(!identity.contains(key)||!identity[key].is_string()||identity[key].get_ref<const std::string&>().empty())return AnythingTargetLife::Unknown;
  const auto raw=identity["hwnd"].get<uint64_t>(),pid=identity["pid"].get<uint64_t>();
  if(raw>(std::numeric_limits<uintptr_t>::max)()||pid>(std::numeric_limits<DWORD>::max)())return AnythingTargetLife::Unknown;
  const auto hwnd=reinterpret_cast<HWND>(static_cast<uintptr_t>(raw));
  if(!IsWindow(hwnd))return AnythingTargetLife::Gone;
  DWORD actualPid=0;if(!GetWindowThreadProcessId(hwnd,&actualPid))return IsWindow(hwnd)?AnythingTargetLife::Unknown:AnythingTargetLife::Gone;
  if(actualPid!=pid)return AnythingTargetLife::Gone;
  const auto executable=Wide(identity["executable"].get<std::string>()),windowClass=Wide(identity["windowClass"].get<std::string>());
  if(executable.empty()||windowClass.empty())return AnythingTargetLife::Unknown;
  HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,actualPid);
  if(!process)return GetLastError()==ERROR_INVALID_PARAMETER?AnythingTargetLife::Gone:AnythingTargetLife::Unknown;
  FILETIME created{},exited{},kernel{},user{};wchar_t path[32768]{};DWORD length=32768,exitCode=STILL_ACTIVE;
  const bool times=GetProcessTimes(process,&created,&exited,&kernel,&user)!=FALSE;
  const bool image=QueryFullProcessImageNameW(process,0,path,&length)!=FALSE;
  const bool exitKnown=GetExitCodeProcess(process,&exitCode)!=FALSE;CloseHandle(process);
  if(exitKnown&&exitCode!=STILL_ACTIVE)return AnythingTargetLife::Gone;
  if(!times||!image)return AnythingTargetLife::Unknown;
  const auto creation=uint64_t(created.dwHighDateTime)<<32|created.dwLowDateTime;
  if(creation!=identity["processCreation"].get<uint64_t>())return AnythingTargetLife::Gone;
  wchar_t cls[256]{};if(!GetClassNameW(hwnd,cls,256))return IsWindow(hwnd)?AnythingTargetLife::Unknown:AnythingTargetLife::Gone;
  if(windowClass!=cls||CompareStringOrdinal(executable.c_str(),-1,path,-1,TRUE)!=CSTR_EQUAL)return AnythingTargetLife::Gone;
  DWORD finalPid=0;if(!IsWindow(hwnd))return AnythingTargetLife::Gone;
  if(!GetWindowThreadProcessId(hwnd,&finalPid))return AnythingTargetLife::Unknown;
  return finalPid==actualPid?AnythingTargetLife::Alive:AnythingTargetLife::Gone;
 }catch(...){return AnythingTargetLife::Unknown;}
}

template<class Snapshot,class Stop> bool PollAnythingTargetLifecycle(AnythingUiState& page,const Snapshot& state,Stop&& stop,
 uint64_t now=GetTickCount64()) {
 if(page.clearTargetPending){
  if(!state.active&&!state.busy&&!state.stopping)page.clearTargetPending=false;
  return false;
 }
 const nlohmann::json* target=nullptr;
 if((state.active||state.stopping)&&state.status.is_object()){
  auto window=state.status.find("window");if(window!=state.status.end()&&window->is_object())target=&*window;
 }
 if(!target&&page.selected.is_object())target=&page.selected;
 if(!target&&page.pendingStart.is_object()){
  auto window=page.pendingStart.find("window");if(window!=page.pendingStart.end()&&window->is_object())target=&*window;
 }
 if(!target&&page.activeTarget.is_object())target=&page.activeTarget;
 if(!target){page.targetCheckAfter=0;page.targetCheckIdentity=nullptr;return false;}
 if(*target==page.targetCheckIdentity&&now<page.targetCheckAfter)return false;
 page.targetCheckIdentity=*target;page.targetCheckAfter=now+250;
 if(ProbeAnythingTargetLife(*target)!=AnythingTargetLife::Gone)return false;
 if((state.active||state.busy)&&!state.stopping)stop();
 page.selected=page.activeTarget=page.pendingStart=nullptr;
 page.awaitingCountdown=page.selectRequested=page.windowListPending=false;
 page.clearTargetPending=state.active||state.busy||state.stopping;
 page.notice.clear();page.targetCheckIdentity=nullptr;page.targetCheckAfter=0;
 return true;
}
}
