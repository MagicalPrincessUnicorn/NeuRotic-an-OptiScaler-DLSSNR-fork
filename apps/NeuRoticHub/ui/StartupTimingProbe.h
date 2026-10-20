#pragma once
// DIAGNOSTIC PROPOSAL ONLY. Not yet applied, compiled, or accepted for shipping.
// Enabled only with an explicit trace destination, isolated fixture root and
// probe-only offline opt-in. No model, GUI context or D3D ownership changes.
#include <windows.h>
#include <cstdio>
#include <cstdint>

namespace nh::startup_probe {
struct State {
 HANDLE file=INVALID_HANDLE_VALUE;
 LARGE_INTEGER entry{},frequency{};
 LONGLONG writeTicks=0;
 unsigned events=0;
 bool enabled=false,traceFailed=false,firstFrame=false,presented=false,paint=false,visiblePaint=false,preflight=false;
};
inline State& Get(){static State state;return state;}
inline LARGE_INTEGER Clock(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value;}
inline bool Active(){return Get().enabled;}
inline bool Offline(){return Active();}
inline void MarkAt(const char* name,LARGE_INTEGER when,unsigned long long detail=0) noexcept {
 auto& state=Get();if(!state.enabled)return;if(state.events++>=160){state.traceFailed=true;return;}
 const auto before=Clock();char line[512]{};
 const int length=std::snprintf(line,sizeof(line),"%s\t%lld\t%.3f\t%lu\t%llu\t%lld\n",name,
     static_cast<long long>(when.QuadPart),1000.0*double(when.QuadPart-state.entry.QuadPart)/double(state.frequency.QuadPart),
     GetCurrentThreadId(),detail,static_cast<long long>(state.writeTicks));
 DWORD written=0;
 if(length<=0||size_t(length)>=sizeof(line)||!WriteFile(state.file,line,DWORD(length),&written,nullptr)||written!=DWORD(length))state.traceFailed=true;
 state.writeTicks+=Clock().QuadPart-before.QuadPart;
}
inline void Mark(const char* name,unsigned long long detail=0) noexcept {if(Active())MarkAt(name,Clock(),detail);}
struct Session {
 bool valid=true;
 Session() noexcept {
  const auto entry=Clock();auto& state=Get();
  wchar_t path[32768]{},fixture[32768]{},offline[4]{};
  const DWORD count=GetEnvironmentVariableW(L"NEUROTIC_STARTUP_TRACE_PATH",path,32768);
  if(!count)return;
  const DWORD root=GetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture,32768);
  const DWORD option=GetEnvironmentVariableW(L"NEUROTIC_STARTUP_PROBE_OFFLINE",offline,4);
  if(count>=32768||!root||root>=32768||option!=1||offline[0]!=L'1'){valid=false;return;}
  state.entry=entry;QueryPerformanceFrequency(&state.frequency);
  state.file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(state.file==INVALID_HANDLE_VALUE||state.frequency.QuadPart<=0){valid=false;return;}
  state.enabled=true;
  constexpr char header[]="event\tqpc\telapsed_ms\ttid\tdetail\tprior_trace_write_ticks\n";
  const auto headerStart=Clock();DWORD written=0;
  if(!WriteFile(state.file,header,sizeof(header)-1,&written,nullptr)||written!=sizeof(header)-1){state.traceFailed=true;valid=false;return;}
  state.writeTicks+=Clock().QuadPart-headerStart.QuadPart;
  MarkAt("entry",entry);Mark("qpc_frequency",state.frequency.QuadPart);
  FILETIME created{},exited{},kernel{},user{},utc{};
  if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))
   Mark("process_created_filetime",(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime);
  GetSystemTimePreciseAsFileTime(&utc);Mark("correlated_utc_filetime",(uint64_t(utc.dwHighDateTime)<<32)|utc.dwLowDateTime);
  Mark("probe_offline_isolated",1);
 }
 ~Session(){auto& state=Get();if(state.file!=INVALID_HANDLE_VALUE){Mark("process_scope_exit",state.traceFailed?1:0);CloseHandle(state.file);state.file=INVALID_HANDLE_VALUE;state.enabled=false;}}
};
struct Scope {
 const char* end;bool enabled;
 Scope(const char* begin,const char* finish,bool selected=true):end(finish),enabled(selected&&Active()){if(enabled)Mark(begin);}
 ~Scope(){if(enabled)Mark(end);}
};
inline bool FirstFrame(){auto& state=Get();if(!state.enabled||state.firstFrame)return false;state.firstFrame=true;return true;}
inline void Paint(HWND window){auto& state=Get();if(!state.enabled)return;if(!state.paint){state.paint=true;Mark("first_wm_paint");}if(!state.visiblePaint&&IsWindowVisible(window)){state.visiblePaint=true;Mark("first_visible_wm_paint");}}
inline void Presented(HRESULT result){auto& state=Get();if(!state.enabled||state.presented||result!=S_OK)return;state.presented=true;Mark("first_present_s_ok");}
inline void Preflight(bool busy,bool resultAvailable){auto& state=Get();if(!state.enabled||state.preflight||busy)return;state.preflight=true;Mark("preflight_settled",resultAvailable?1:0);}
}
