#include <menu/Localization.h>
#pragma once
#include <filesystem>
#include <cstdint>
#include <windows.h>
#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>
#include "json.hpp"
namespace nh::DiagnosticRetention {
struct Limits { uint64_t maximumBytes=128ull*1024*1024; unsigned keepNewest=20; };
struct Result { uint64_t beforeBytes=0,afterBytes=0,removedBytes=0; unsigned removedGroups=0; };
namespace detail {
struct Handle {
 HANDLE value=INVALID_HANDLE_VALUE;
 explicit Handle(HANDLE h):value(h) {}
 Handle(Handle&& other) noexcept:value(other.value) { other.value=INVALID_HANDLE_VALUE; }
 Handle(const Handle&)=delete;
 ~Handle() { if(value!=INVALID_HANDLE_VALUE)CloseHandle(value); }
};
inline uint64_t Ticks(FILETIME time) { return uint64_t(time.dwHighDateTime)<<32|time.dwLowDateTime; }
struct File { std::filesystem::path path; uint64_t size=0,time=0; std::wstring suffix; };
struct Group { std::vector<File> files; uint64_t time=0,bytes=0; bool unknown=false; };
inline bool Guid(const std::wstring& name) {
 if(name.size()<38||name[0]!=L'{'||name[37]!=L'}')return false;
 for(size_t i=1;i<37;++i) {
  if(i==9||i==14||i==19||i==24) { if(name[i]!=L'-')return false; }
  else if(!((name[i]>=L'0'&&name[i]<=L'9')||(name[i]>=L'A'&&name[i]<=L'F')||(name[i]>=L'a'&&name[i]<=L'f')))return false;
 }
 return true;
}
// Pin every ancestor without delete sharing, and open reparse points themselves.
// A concurrent rename/junction replacement therefore cannot redirect cleanup.
inline bool PinDirectories(const std::filesystem::path& root,std::vector<Handle>& pins) {
 if(!root.is_absolute()||root.wstring().starts_with(L"\\\\")||root.lexically_normal()!=root)return false;
 auto current=root.root_path();
 const auto pin=[&](const std::filesystem::path& path) {
  pins.emplace_back(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
      OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  return pins.back().value!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(pins.back().value,&info)&&
      (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
 };
 if(!pin(current))return false;
 for(const auto& part:root.relative_path()) { current/=part;if(!pin(current))return false; }
 return true;
}
inline nlohmann::json ReadJson(HANDLE file,uint64_t size,uint64_t limit) {
 if(size==0||size>limit)return {};
 std::string bytes(size_t(size),'\0');DWORD read=0;
 if(!ReadFile(file,bytes.data(),DWORD(size),&read,nullptr)||read!=size)return {};
 return nlohmann::json::parse(bytes,nullptr,false);
}
inline bool CompletedReadOnly(const Group& group,const std::vector<Handle>& handles) {
 nlohmann::json request,result;bool discovery=false,log=false,discoveryLog=false;
 for(size_t i=0;i<group.files.size();++i) {
  const auto& file=group.files[i];
  if(file.suffix==L".request.json")request=ReadJson(handles[i].value,file.size,1024*1024);
  else if(file.suffix==L".result.json")result=ReadJson(handles[i].value,file.size,16*1024*1024);
  else if(file.suffix==L".discovery.result.json") { result=ReadJson(handles[i].value,file.size,16*1024*1024);discovery=true; }
  else if(file.suffix==L".log")log=true;
  else if(file.suffix==L".discovery.log")discoveryLog=true;
 }
 if(!result.is_object())return false;
 if(discovery)return discoveryLog&&!log&&group.files.size()==2&&result.value("protocolVersion",0)==2&&
     result.value("kind","")=="DiscoveryResult"&&result.contains("games")&&result["games"].is_array()&&
     result.contains(Neurotic::UiLiteral("desktop.hubshell.errors_5a41a7c1", "errors"))&&result[Neurotic::UiLiteral("desktop.hubshell.errors_5a41a7c1", "errors")].is_array();
 if(!log||discoveryLog||group.files.size()!=3||!request.is_object()||request.value("protocolVersion",0)!=1)return false;
 const auto kind=request.value("kind","");const auto status=result.value("status","");
 // Plan/Execute, failed/unknown results and recovery receipts remain evidence.
 return (kind=="Inspect"&&status=="Inspected")||(kind=="Preflight"&&status=="PreflightChecked");
}
}
// Best-effort startup housekeeping of completed read-only helper groups only.
// The cap is soft: protected evidence is never sacrificed to reach it.
inline Result Prune(const std::filesystem::path& directory,Limits limits={}) noexcept {
 Result result;
 try {
  std::vector<detail::Handle> pins;if(!detail::PinDirectories(directory,pins))return result;
  std::map<std::wstring,detail::Group> groups;size_t entries=0;
  for(const auto& entry:std::filesystem::directory_iterator(directory)) {
   if(++entries>20000)return result;
   const auto name=entry.path().filename().wstring();WIN32_FILE_ATTRIBUTE_DATA data{};
   if(!GetFileAttributesExW(entry.path().c_str(),GetFileExInfoStandard,&data))return result;
   const bool ordinary=!(data.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));
   const uint64_t bytes=uint64_t(data.nFileSizeHigh)<<32|data.nFileSizeLow;
   if(ordinary)result.beforeBytes+=bytes;
   if(!detail::Guid(name))continue;
   auto key=name.substr(0,38);for(auto& character:key)if(character>=L'a'&&character<=L'f')character-=L'a'-L'A';
   auto& group=groups[key];const auto suffix=name.substr(38);
   const bool known=suffix==L".request.json"||suffix==L".result.json"||suffix==L".log"||
       suffix==L".discovery.result.json"||suffix==L".discovery.log";
   if(!ordinary||!known) { group.unknown=true;continue; }
   group.files.push_back({entry.path(),bytes,detail::Ticks(data.ftLastWriteTime),suffix});
   group.time=(std::max)(group.time,detail::Ticks(data.ftLastWriteTime));group.bytes+=bytes;
  }
  result.afterBytes=result.beforeBytes;
  std::vector<detail::Group*> ordered;for(auto& [id,group]:groups)ordered.push_back(&group);
  std::stable_sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->time>b->time;});
  FILETIME clock{};GetSystemTimeAsFileTime(&clock);const auto now=detail::Ticks(clock);
  constexpr uint64_t day=24ull*60*60*10000000;
  for(size_t index=ordered.size();index>limits.keepNewest;) {
   auto& group=*ordered[--index];
   if(group.unknown||group.files.empty()||group.time>now||now-group.time<=day)continue;
   if(now-group.time<=30*day&&result.afterBytes<=limits.maximumBytes)continue;
   std::vector<detail::Handle> files;bool safe=true;
   for(const auto& file:group.files) {
    // Exclusive handles also refuse an active helper's inherited log handle.
    files.emplace_back(CreateFileW(file.path.c_str(),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if(files.back().value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(files.back().value,&info)||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1||
       (uint64_t(info.nFileSizeHigh)<<32|info.nFileSizeLow)!=file.size||detail::Ticks(info.ftLastWriteTime)!=file.time) { safe=false;break; }
   }
   if(!safe)continue;
   try { if(!detail::CompletedReadOnly(group,files))continue; }catch(...) { continue; }
   // Delete only the opened identities. No path-based recursive deletion.
   bool complete=true;
   for(size_t i=0;i<files.size();++i) {
    FILE_DISPOSITION_INFO disposition{TRUE};
    if(SetFileInformationByHandle(files[i].value,FileDispositionInfo,&disposition,sizeof(disposition))) {
     result.removedBytes+=group.files[i].size;result.afterBytes-=group.files[i].size;
    }else complete=false;
   }
   if(complete)++result.removedGroups;
  }
 }catch(...) {} // Housekeeping never blocks startup or weakens protected records.
 return result;
}
}
