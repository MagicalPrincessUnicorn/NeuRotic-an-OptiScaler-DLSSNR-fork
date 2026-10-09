#include <menu/Localization.h>
#pragma once
#include <windows.h>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <memory>

namespace nh {
enum class AppDataClearScope { AppData, GameProfiles };
struct AppDataClearEntry {
 std::filesystem::path relative;
 DWORD volume=0;uint64_t identity=0,bytes=0,written=0;bool directory=false;
 bool operator==(const AppDataClearEntry&)const=default;
};
struct AppDataClearPlan {
 std::filesystem::path userRoot,root;AppDataClearScope scope=AppDataClearScope::GameProfiles;
 std::vector<AppDataClearEntry> entries;
 std::vector<std::filesystem::path> preserved;
 std::vector<std::string> issues;
 uint64_t bytes=0,rootIdentity=0;DWORD rootVolume=0;size_t fileCount=0;bool missing=false;
 bool Ready()const{return issues.empty();}
};
struct AppDataClearResult {
 bool complete=false;size_t deletedFiles=0,deletedDirectories=0;
 std::vector<std::string> issues;
};
namespace maintenance_detail {
namespace fs=std::filesystem;
struct Handle {HANDLE value=INVALID_HANDLE_VALUE;explicit Handle(HANDLE h):value(h){}~Handle(){Close();}void Close(){if(value!=INVALID_HANDLE_VALUE){CloseHandle(value);value=INVALID_HANDLE_VALUE;}}Handle(const Handle&)=delete;};
using Pin=std::shared_ptr<Handle>;
inline void Ordinary(const fs::path& path){
 auto s=path.wstring();if(!path.is_absolute()||path==path.root_path()||path.lexically_normal()!=path||s.starts_with(L"\\\\")||s.find(L':',2)!=s.npos||s.find(L'\0')!=s.npos||path.filename().empty())throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.maintenance_requires_a_specific_ordinary_local_f_a0590a67", "Maintenance requires a specific ordinary local folder"));
}
inline Pin Open(const fs::path& path,bool directory,bool erase=false,bool lockWrites=false){
 HANDLE h=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|(erase?DELETE:0),FILE_SHARE_READ|(lockWrites?0:FILE_SHARE_WRITE),nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
 if(h==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.a_maintenance_path_is_busy_or_unavailable_d5b09b53", "A maintenance path is busy or unavailable"));
 auto p=std::make_shared<Handle>(h);BY_HANDLE_FILE_INFORMATION info{};
 if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||bool(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=directory||(!directory&&info.nNumberOfLinks!=1))throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.linked_or_unexpected_maintenance_entry_refused_ca51f05f", "Linked or unexpected maintenance entry refused"));
 return p;
}
inline AppDataClearEntry Stamp(const Pin& p,const fs::path& relative){
 BY_HANDLE_FILE_INFORMATION i{};if(!GetFileInformationByHandle(p->value,&i))throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.maintenance_identity_unavailable_48b182be", "Maintenance identity unavailable"));
 auto pair=[](DWORD a,DWORD b){return(uint64_t(a)<<32)|b;};
 return {relative,i.dwVolumeSerialNumber,pair(i.nFileIndexHigh,i.nFileIndexLow),pair(i.nFileSizeHigh,i.nFileSizeLow),pair(i.ftLastWriteTime.dwHighDateTime,i.ftLastWriteTime.dwLowDateTime),bool(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)};
}
inline bool Exists(const fs::path& path){if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)return true;auto e=GetLastError();if(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND)return false;throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.maintenance_folder_is_inaccessible_7971bf66", "Maintenance folder is inaccessible"));}
inline std::vector<Pin> Parents(const fs::path& root,bool create=false,bool* complete=nullptr){
 if(complete)*complete=false;
 std::vector<Pin> pins;auto walk=root.root_path();pins.push_back(Open(walk,true));
 for(auto& part:root.relative_path()){
  walk/=part;if(!Exists(walk)){if(!create)return pins;if(!CreateDirectoryW(walk.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.could_not_create_the_artwork_cache_folder_84674e85", "Could not create the artwork cache folder"));}
  pins.push_back(Open(walk,true));
 }if(complete)*complete=true;return pins;
}
inline bool Protected(const fs::path& relative,AppDataClearScope scope){return scope==AppDataClearScope::AppData&&_wcsicmp(relative.begin()->c_str(),L"Dumbfire")==0;}
inline void Walk(AppDataClearPlan& plan,const fs::path& relative,size_t depth,std::vector<Pin>& pins){
 if(depth>32)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.maintenance_folder_nesting_exceeds_its_limit_5e7a34c6", "Maintenance folder nesting exceeds its limit"));
 auto dir=plan.root/relative;auto pin=Open(dir,true);pins.push_back(pin);if(!relative.empty())plan.entries.push_back(Stamp(pin,relative));
 WIN32_FIND_DATAW data{};HANDLE raw=FindFirstFileW((dir/L"*").c_str(),&data);
 if(raw==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return;throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.maintenance_folder_cannot_be_listed_900af627", "Maintenance folder cannot be listed"));}
 struct Find {HANDLE h;~Find(){FindClose(h);}} find{raw};
 do{
  if(wcscmp(data.cFileName,L".")==0||wcscmp(data.cFileName,L"..")==0)continue;
  auto child=relative/data.cFileName;
  if(Protected(child,plan.scope)){plan.preserved.push_back(plan.root/child);continue;}
  if(plan.entries.size()>=20000)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.maintenance_exceeds_20000_entries_nothing_was_de_a3158b99", "Maintenance exceeds 20000 entries; nothing was deleted"));
  if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.linked_maintenance_entry_refused_nothing_was_del_f63c2fc5", "Linked maintenance entry refused; nothing was deleted"));
  if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)Walk(plan,child,depth+1,pins);
  else{auto file=Open(plan.root/child,false);auto entry=Stamp(file,child);plan.bytes+=entry.bytes;++plan.fileCount;plan.entries.push_back(entry);}
 }while(FindNextFileW(raw,&data));
 if(GetLastError()!=ERROR_NO_MORE_FILES)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.maintenance_enumeration_did_not_finish_57d10087", "Maintenance enumeration did not finish"));
}
inline void MarkDelete(const Pin& p){FILE_DISPOSITION_INFO d{TRUE};if(!SetFileInformationByHandle(p->value,FileDispositionInfo,&d,sizeof(d)))throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.an_entry_could_not_be_removed_changed_or_busy_fi_774b2d71", "An entry could not be removed; changed or busy files were kept"));p->Close();}
}
// Caller owns confirmation and must stop every writer before Execute. AppData
// requires exit/restart without a final preference save. Recovery is preserved.
inline AppDataClearPlan PlanAppDataClear(const std::filesystem::path& userRoot,AppDataClearScope scope){
 AppDataClearPlan p;p.userRoot=userRoot;p.scope=scope;p.root=scope==AppDataClearScope::GameProfiles?userRoot/L"profiles":userRoot;
 try{
  maintenance_detail::Ordinary(userRoot);if(scope!=AppDataClearScope::AppData&&scope!=AppDataClearScope::GameProfiles)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.unknown_maintenance_scope_9ee20c87", "Unknown maintenance scope"));
  bool complete=false;auto parents=maintenance_detail::Parents(p.root,false,&complete);if(!complete){p.missing=true;return p;}
  auto identity=maintenance_detail::Stamp(parents.back(),{});p.rootIdentity=identity.identity;p.rootVolume=identity.volume;
  maintenance_detail::Walk(p,{},0,parents);
  std::sort(p.entries.begin(),p.entries.end(),[](const auto& a,const auto& b){return a.relative<b.relative;});
 }catch(const std::exception& e){p.issues.push_back(e.what());}return p;
}
inline AppDataClearResult ExecuteAppDataClear(const AppDataClearPlan& plan){
 AppDataClearResult result;
 try{
  if(!plan.Ready())throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.prepare_a_complete_maintenance_review_before_cle_ba0f0de3", "Prepare a complete maintenance review before clearing data"));
  auto fresh=PlanAppDataClear(plan.userRoot,plan.scope);
  if(!fresh.Ready()||fresh.root!=plan.root||fresh.entries!=plan.entries||fresh.missing!=plan.missing||fresh.preserved!=plan.preserved||fresh.rootIdentity!=plan.rootIdentity||fresh.rootVolume!=plan.rootVolume)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.app_data_changed_since_review_review_again_befor_5e423aef", "App data changed since review; review again before clearing it"));
  if(fresh.missing){result.complete=true;return result;}
  bool complete=false;auto ancestors=maintenance_detail::Parents(plan.root,false,&complete);if(!complete)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.app_data_disappeared_before_clearing_nothing_was_51b7c8bf", "App data disappeared before clearing; nothing was deleted"));
  auto identity=maintenance_detail::Stamp(ancestors.back(),{});if(identity.identity!=plan.rootIdentity||identity.volume!=plan.rootVolume)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.app_data_folder_was_replaced_nothing_was_deleted_b6188b16", "App data folder was replaced; nothing was deleted"));
  std::vector<std::pair<AppDataClearEntry,maintenance_detail::Pin>> held;
  // Acquire every deletion handle before any mutation. This also refuses live
  // writers and holds every parent against replacement until completion.
  for(const auto& entry:plan.entries){
   auto file=maintenance_detail::Open(plan.root/entry.relative,entry.directory,true,!entry.directory);
   if(maintenance_detail::Stamp(file,entry.relative)!=entry)throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.an_app_data_entry_changed_before_clearing_nothin_a2a4d5a5", "An app data entry changed before clearing; nothing was deleted"));
   held.emplace_back(entry,std::move(file));
  }
  for(auto& [entry,file]:held)if(!entry.directory){maintenance_detail::MarkDelete(file);++result.deletedFiles;}
  std::sort(held.begin(),held.end(),[](const auto& a,const auto& b){return a.first.relative.native().size()>b.first.relative.native().size();});
  for(auto& [entry,file]:held)if(entry.directory){maintenance_detail::MarkDelete(file);++result.deletedDirectories;}
  auto remaining=PlanAppDataClear(plan.userRoot,plan.scope);
  if(!remaining.Ready()||!remaining.entries.empty())throw std::runtime_error(Neurotic::UiMessage("desktop.appdatamaintenance.new_or_busy_app_data_remains_review_it_before_an_d2872a44", "New or busy app data remains; review it before another clear"));
  result.complete=true;
 }catch(const std::exception& e){result.issues.push_back(e.what());}return result;
}
inline std::filesystem::path EnsureArtworkCacheFolder(const std::filesystem::path& userRoot){
 maintenance_detail::Ordinary(userRoot);auto root=userRoot/L"artwork";auto pins=maintenance_detail::Parents(root,true);return root;
}
}
