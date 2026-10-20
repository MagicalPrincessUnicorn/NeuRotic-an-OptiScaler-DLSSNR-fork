#pragma once
#include <filesystem>
#include <cstdint>
#include <windows.h>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <memory>
#include "json.hpp"
#include "storage/UserDataSession.h"
#include "storage/UserFile.h"
#include "ui/ProfileCache.h"
namespace nh::DiagnosticRetention {
// Kept for source compatibility. Consumed transport has no age/count exemption.
struct Limits { uint64_t maximumBytes=128ull*1024*1024; unsigned keepNewest=20; };
struct Result {uint64_t beforeBytes=0,afterBytes=0,removedBytes=0;unsigned removedGroups=0,removedPlans=0;};
namespace detail {
using Json=nlohmann::json;
struct Handle {
 HANDLE value=INVALID_HANDLE_VALUE;
 explicit Handle(HANDLE h):value(h){}
 Handle(Handle&& other) noexcept:value(other.value){other.value=INVALID_HANDLE_VALUE;}
 Handle(const Handle&)=delete;
 ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
inline uint64_t Ticks(FILETIME t){return uint64_t(t.dwHighDateTime)<<32|t.dwLowDateTime;}
inline bool Hex(const std::string& value,size_t size){return value.size()==size&&std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
inline bool Guid(const std::wstring& n){
 if(n.size()<38||n[0]!=L'{'||n[37]!=L'}')return false;
 for(size_t i=1;i<37;++i){if(i==9||i==14||i==19||i==24){if(n[i]!=L'-')return false;}else if(!((n[i]>=L'0'&&n[i]<=L'9')||(n[i]>=L'a'&&n[i]<=L'f')||(n[i]>=L'A'&&n[i]<=L'F')))return false;}return true;
}
inline std::wstring Fold(std::wstring key){for(auto& c:key)c=towlower(c);return key;}
inline std::wstring Key(const std::filesystem::path& root){return Fold(root.lexically_normal().wstring());}
inline bool PinDirectories(const std::filesystem::path& root,std::vector<Handle>& pins){
 const auto name=root.wstring();if(!root.is_absolute()||name.starts_with(L"\\\\")||name.find(L':',2)!=std::wstring::npos||root.lexically_normal()!=root)return false;
 const auto pin=[&](const std::filesystem::path& path){pins.emplace_back(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));BY_HANDLE_FILE_INFORMATION info{};return pins.back().value!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(pins.back().value,&info)&&(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);};
 auto walk=root.root_path();if(!pin(walk))return false;for(const auto& part:root.relative_path()){walk/=part;if(!pin(walk))return false;}return true;
}
struct File {std::filesystem::path path;uint64_t size=0,time=0;std::wstring suffix;};
struct Group {std::vector<File> files;bool unknown=false;};
inline void Add(Group& group,const std::filesystem::path& path){
 WIN32_FILE_ATTRIBUTE_DATA data{};if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data)){group.unknown=true;return;}
 const auto suffix=path.filename().wstring().substr(38);
 if(data.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))group.unknown=true;
 if(suffix!=L".request.json"&&suffix!=L".result.json"&&suffix!=L".log"&&suffix!=L".discovery.result.json"&&suffix!=L".discovery.log")group.unknown=true;
 if(group.files.size()>=5||std::any_of(group.files.begin(),group.files.end(),[&](const auto& f){return f.suffix==suffix;})){group.unknown=true;return;}
 group.files.push_back({path,uint64_t(data.nFileSizeHigh)<<32|data.nFileSizeLow,Ticks(data.ftLastWriteTime),suffix});
}
inline Group Collect(const std::filesystem::path& directory,const std::wstring& id){
 Group group;WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW((directory/(id+L"*")).c_str(),&data);
 if(find==INVALID_HANDLE_VALUE){const auto error=GetLastError();group.unknown=error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND;return group;}unsigned visited=0;bool bounded=false;
 do{if(++visited>8){group.unknown=true;bounded=true;break;}auto n=std::wstring(data.cFileName);if(Guid(n)&&Fold(n.substr(0,38))==id)Add(group,directory/n);else group.unknown=true;}while(FindNextFileW(find,&data));
 const auto error=GetLastError();FindClose(find);if(!bounded&&error!=ERROR_NO_MORE_FILES)group.unknown=true;return group;
}
inline bool Open(const Group& group,std::vector<Handle>& files){
 if(group.unknown||group.files.empty())return false;
 for(const auto& file:group.files){files.emplace_back(CreateFileW(file.path.c_str(),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));BY_HANDLE_FILE_INFORMATION info{};
  if(files.back().value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(files.back().value,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1||(uint64_t(info.nFileSizeHigh)<<32|info.nFileSizeLow)!=file.size||Ticks(info.ftLastWriteTime)!=file.time)return false;
 }return true;
}
inline Json ReadJson(HANDLE file,uint64_t size,uint64_t maximum){
 if(size==0||size>maximum)return {};
 std::string bytes(size_t(size),'\0');DWORD read=0;if(!ReadFile(file,bytes.data(),DWORD(bytes.size()),&read,nullptr)||read!=bytes.size())return {};
 return Json::parse(bytes,nullptr,false);
}
inline Json SharedJson(const File& file,uint64_t maximum){
 Handle handle(CreateFileW(file.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));BY_HANDLE_FILE_INFORMATION info{};
 if(handle.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(handle.value,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||info.nNumberOfLinks!=1||(uint64_t(info.nFileSizeHigh)<<32|info.nFileSizeLow)!=file.size||Ticks(info.ftLastWriteTime)!=file.time)return {};
 return ReadJson(handle.value,uint64_t(info.nFileSizeHigh)<<32|info.nFileSizeLow,maximum);
}
inline bool Text(const Json& j,const char* key){return j.is_object()&&j.contains(key)&&j[key].is_string()&&!j[key].get_ref<const std::string&>().empty();}
inline std::string String(const Json& j,const char* key){return Text(j,key)?j[key].get<std::string>():std::string{};}
inline bool KnownRequest(const Json& request){
 if(!request.is_object()||!request.contains("protocolVersion")||!request["protocolVersion"].is_number_integer()||request["protocolVersion"]!=1)return false;
 const auto kind=String(request,"kind");
 if(kind=="Preflight")return true;
 if(kind=="Execute")return Hex(String(request,"planId"),32)&&Hex(String(request,"planFingerprint"),64);
 if(kind!="Inspect"&&kind!="LaunchPreflight"&&kind!="RestoreDefaults"&&kind!="Plan")return false;
 if(!Text(request,"gameExecutable"))return false;
 if(kind!="Plan")return true;
 const auto op=String(request,"operation");return op=="Install"||op=="Uninstall"||op=="SaveSettings"||op=="UpdateNrDll";
}
inline bool Discovery(const Json& value){return value.is_object()&&value.contains("protocolVersion")&&value["protocolVersion"].is_number_integer()&&value["protocolVersion"]==2&&String(value,"kind")=="DiscoveryResult"&&value.contains("games")&&value["games"].is_array()&&value.contains("errors")&&value["errors"].is_array();}
inline bool Plan(const Json& value,const std::string& id){
 return value.is_object()&&Hex(id,32)&&String(value,"status")=="Planned"&&String(value,"planId")==id&&Hex(String(value,"planFingerprint"),64)&&value.contains("request")&&KnownRequest(value["request"])&&String(value["request"],"kind")=="Plan"&&value.contains("target")&&String(value["target"],"executable")==String(value["request"],"gameExecutable")&&value.contains("changes")&&value["changes"].is_array();
}
struct Documents {Json request,result;bool log=false,discovery=false,discoveryLog=false;};
inline Documents Parse(const Group& group,const std::vector<Handle>& files){
 Documents d;for(size_t i=0;i<group.files.size();++i){const auto& f=group.files[i];
  if(f.suffix==L".request.json")d.request=ReadJson(files[i].value,f.size,6*1048576);
  else if(f.suffix==L".result.json")d.result=ReadJson(files[i].value,f.size,16*1048576);
  else if(f.suffix==L".discovery.result.json"){d.result=ReadJson(files[i].value,f.size,16*1048576);d.discovery=true;}
  else if(f.suffix==L".log")d.log=true;else if(f.suffix==L".discovery.log")d.discoveryLog=true;
 }return d;
}
inline Json Fields(const Json& value,std::initializer_list<const char*> keys){
 if(!value.is_object())return {};auto out=Json::object();for(auto key:keys)if(value.contains(key))out[key]=value[key];return out;
}
inline Json RequestSnapshot(const Json& value){return Fields(value,{"protocolVersion","kind","operation","gameExecutable","gameTitle","planId","planFingerprint"});}
inline Json ReceiptSnapshot(const Json& value){
 auto out=Fields(value,{"status","planId","planFingerprint","reason","reasonCode","decisionKind","target"});
 if(value.is_object()&&value.contains("inspection")&&value["inspection"].is_object()){
  const auto& inspection=value["inspection"];out["inspection"]=Fields(inspection,{"target"});
  if(inspection.contains("state"))out["inspection"]["state"]=Fields(inspection["state"],{"kind","schemaVersion","schema_version","status"});
 }return out;
}
struct Transport {Json request,receipt;bool discovery=false,created=false,drained=false,acknowledged=false;ULONGLONG retryAt=0;};
struct RootState {
 uint64_t epoch=0,revision=0;std::map<std::wstring,Transport> transports;std::set<std::string> protectedPlans;
 std::string journal;bool journalStarted=false;
};
inline std::map<std::wstring,RootState> states;
inline RootState* State(const std::filesystem::path& root){
 const auto epoch=UserDataSession::Authority(root);if(!epoch)return nullptr;
 auto& state=states[Key(root)];if(state.epoch!=epoch){state=RootState{};state.epoch=epoch;}return &state;
}
inline Json Compact(const Json& value,unsigned depth=0){
 if(value.is_discarded())return "[unreadable JSON]";
 if(value.is_string()){const auto& s=value.get_ref<const std::string&>();return s.size()>2048?s.substr(0,2048)+" [truncated]":s;}
 if(value.is_primitive())return value;
 if(depth>=4)return "[bounded summary]";
 if(value.is_array()){auto out=Json::array();size_t n=0;for(const auto& v:value){if(++n>16){out.push_back("[remaining items omitted]");break;}out.push_back(Compact(v,depth+1));}return out;}
 auto out=Json::object();size_t n=0;for(auto it=value.begin();it!=value.end();++it){if(++n>32){out["summaryLimit"]=true;break;}out[it.key()]=Compact(it.value(),depth+1);}return out;
}
inline bool Append(const std::filesystem::path& root,RootState& state,const Json& event){
 try{
  std::string line=Compact(event).dump(-1,' ',false,Json::error_handler_t::replace)+'\n';
  constexpr size_t maximum=1048576;std::string journal=state.journal;
  if(!state.journalStarted)journal=Json{{"event","session_started"},{"utc_ticks",[](){FILETIME t{};GetSystemTimeAsFileTime(&t);return Ticks(t);}()}}.dump()+'\n';
  if(line.size()>65536)line=Json{{"event","diagnostic_summary_limit"},{"bytes",line.size()}}.dump()+'\n';
  if(journal.size()+line.size()>maximum){const auto start=journal.find('\n',journal.size()/2);journal=start==std::string::npos?std::string{}:journal.substr(start+1);}
  journal+=line;SaveUserFile(root/L"last-session.log",journal);state.journal=std::move(journal);state.journalStarted=true;return true;
 }catch(...){return false;}
}
inline bool Delete(const Group& group,const std::vector<Handle>& files){
 size_t marked=0;for(;marked<files.size();++marked){FILE_DISPOSITION_INFO disposition{TRUE};if(!SetFileInformationByHandle(files[marked].value,FileDispositionInfo,&disposition,sizeof(disposition)))break;}
 if(marked!=files.size()){for(size_t i=0;i<marked;++i){FILE_DISPOSITION_INFO cancel{FALSE};SetFileInformationByHandle(files[i].value,FileDispositionInfo,&cancel,sizeof(cancel));}return false;}return true;
}
inline bool PreserveKnowledge(const Json& request,const Json& receipt){return PreserveOperationKnowledge(request,receipt);}
inline std::string LogTail(const Group& group,const std::vector<Handle>& files){
 for(size_t i=0;i<group.files.size();++i)if(group.files[i].suffix==L".log"||group.files[i].suffix==L".discovery.log"){
  const auto size=group.files[i].size;LARGE_INTEGER at{};at.QuadPart=size>16384?LONGLONG(size-16384):0;if(!SetFilePointerEx(files[i].value,at,nullptr,FILE_BEGIN))return {};
  std::string tail(size_t((std::min)(size,uint64_t(16384))),'\0');DWORD n=0;if(!ReadFile(files[i].value,tail.data(),DWORD(tail.size()),&n,nullptr))return {};tail.resize(n);return tail;
 }return {};
}
inline void References(const Documents& d,std::set<std::string>& refs,bool& complete,bool active){
 if(d.discovery||d.discoveryLog){if(!Discovery(d.result))complete=false;return;}
 if(!KnownRequest(d.request)){complete=false;return;}
 if(String(d.request,"kind")=="Plan"&&active)complete=false;
 for(const auto* j:{&d.request,&d.result}){auto id=String(*j,"planId");if(Hex(id,32))refs.insert(id);}
}
inline Documents SharedDocuments(const Group& group){
 Documents d;for(const auto& f:group.files){if(f.suffix==L".request.json")d.request=SharedJson(f,6*1048576);else if(f.suffix==L".result.json")d.result=SharedJson(f,16*1048576);else if(f.suffix==L".discovery.result.json"){d.result=SharedJson(f,16*1048576);d.discovery=true;}else if(f.suffix==L".log")d.log=true;else if(f.suffix==L".discovery.log")d.discoveryLog=true;}return d;
}
}
// These calls only register ownership/consumer facts; no transport is removed here.
inline void Register(const std::filesystem::path& root,const std::wstring& id,const Json& request,bool discovery=false){if(auto state=detail::State(root)){state->transports[detail::Fold(id)]={detail::RequestSnapshot(request),{},discovery};++state->revision;}}
inline void Started(const std::filesystem::path& root,const std::wstring& id){if(auto state=detail::State(root)){auto it=state->transports.find(detail::Fold(id));if(it!=state->transports.end())it->second.created=true;}}
inline void Completed(const std::filesystem::path& root,const std::wstring& id,const nlohmann::json& receipt){if(auto state=detail::State(root)){auto it=state->transports.find(detail::Fold(id));if(it!=state->transports.end()){it->second.drained=true;it->second.receipt=detail::ReceiptSnapshot(receipt);++state->revision;}}}
inline void Abandon(const std::filesystem::path& root,const std::wstring& id){if(auto state=detail::State(root)){state->transports.erase(detail::Fold(id));++state->revision;}}
inline void Acknowledge(const std::filesystem::path& root,const std::wstring& id){if(auto state=detail::State(root)){auto it=state->transports.find(detail::Fold(id));if(it!=state->transports.end()&&it->second.drained){it->second.acknowledged=true;++state->revision;}}}
inline void ProtectPlans(const std::filesystem::path& root,const std::set<std::string>& refs){if(auto state=detail::State(root))if(state->protectedPlans!=refs){state->protectedPlans=refs;++state->revision;}}
inline bool AppendSessionEvent(const std::filesystem::path& root,const nlohmann::json& event) noexcept try{auto state=detail::State(root);return state&&detail::Append(root,*state,event);}catch(...){return false;}
class Sweep {
 struct NamespaceAnchor : UserDataSession::LifetimeResource {
  std::vector<detail::Handle> pins;std::filesystem::directory_iterator entries;
  // Only the live directory namespace is pinned across a bounded pass. Child
  // file exclusives remain local to one group; ordinary helper IO can continue.
  void Revoke() noexcept override {entries={};pins.clear();}
  ~NamespaceAnchor(){Revoke();}
 };
 std::shared_ptr<NamespaceAnchor> anchor;
 std::filesystem::path directory,root;
 std::filesystem::directory_iterator end;
 std::map<std::wstring,detail::Group> groups;
 std::map<std::wstring,detail::Group>::iterator group;
 std::set<std::string> references;
 uint64_t epoch=0,revision=0;size_t count=0;
 DWORD directoryVolume=0,directoryHigh=0,directoryLow=0;bool directoryIdentity=false,directoryFullId=false;
 FILE_ID_INFO directoryId{};
 unsigned phase=0;bool completeReferences=true,done=false;ULONGLONG finishedAt=0;
 Result result;
 void ReleaseAnchor() noexcept {if(anchor){anchor->Revoke();anchor.reset();}}
 void Finish() noexcept {ReleaseAnchor();done=true;finishedAt=GetTickCount64();}
 void Reset(){ReleaseAnchor();directoryIdentity=false;groups.clear();references.clear();count=0;phase=0;completeReferences=true;done=false;result={};}
 bool Retire(const std::wstring& id,const detail::Group& candidate,detail::RootState& state){
  if(candidate.unknown){completeReferences=false;return false;}
  auto registered=state.transports.find(id);const bool owned=registered!=state.transports.end();
  if(owned&&(!registered->second.drained||!registered->second.acknowledged)){
   detail::Documents d;d.request=registered->second.request;d.discovery=registered->second.discovery;d.result=registered->second.receipt;
   if(!d.discovery)detail::References(d,references,completeReferences,true);return false;
  }
  std::vector<detail::Handle> files;
  if(!detail::Open(candidate,files)){files.clear();auto d=detail::SharedDocuments(candidate);detail::References(d,references,completeReferences,true);return false;}
  auto d=detail::Parse(candidate,files);
  const bool standard=std::any_of(candidate.files.begin(),candidate.files.end(),[](const auto& f){return f.suffix==L".request.json"||f.suffix==L".result.json"||f.suffix==L".log";});
  if(standard&&(d.discovery||d.discoveryLog)){completeReferences=false;return false;}
  if(owned){if(!d.request.is_object())d.request=registered->second.request;if(!d.result.is_object())d.result=registered->second.receipt;d.discovery=registered->second.discovery;}
  const bool recognized=owned||(d.discovery&&d.discoveryLog&&!d.log&&candidate.files.size()==2&&detail::Discovery(d.result))||(!d.discovery&&!d.discoveryLog&&d.log&&detail::KnownRequest(d.request));
  if(!recognized){detail::References(d,references,completeReferences,true);return false;}
  // Functional knowledge is committed from the already parsed, exclusively held
  // receipt. The inventory fallback must never reopen these pinned group files.
  if(!detail::PreserveKnowledge(d.request,d.result)){detail::References(d,references,completeReferences,false);return false;}
  if(detail::String(d.request,"kind")=="Execute"&&!d.result.contains("inspection")){
   const auto planId=detail::String(d.request,"planId");
   if(detail::Hex(planId,32)){
    const auto path=root/(std::wstring(planId.begin(),planId.end())+L".plan.json");
    WIN32_FILE_ATTRIBUTE_DATA info{};if(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info)){
     detail::File f{path,uint64_t(info.nFileSizeHigh)<<32|info.nFileSizeLow,detail::Ticks(info.ftLastWriteTime),{}};auto plan=detail::SharedJson(f,6*1048576);
     if(!detail::Plan(plan,planId)||!RememberKnownInstallation(detail::String(plan["request"],"gameExecutable"),detail::String(plan["request"],"gameTitle"))){detail::References(d,references,completeReferences,false);return false;}
    }
   }
  }
  detail::Json event={{"event","operation_consumed"},{"transport",std::string(id.begin(),id.end())},{"request",d.request},{"receipt",d.result},{"log_tail",detail::LogTail(candidate,files)}};
  event["files"]=detail::Json::array();for(const auto& file:candidate.files)event["files"].push_back({{"suffix",std::string(file.suffix.begin(),file.suffix.end())},{"bytes",file.size}});
  if(owned)event["consumerReceipt"]=registered->second.receipt;
  if(!detail::Append(root,state,event)||!detail::Delete(candidate,files)){detail::References(d,references,completeReferences,false);return false;}
  for(const auto& f:candidate.files)result.removedBytes+=f.size;++result.removedGroups;
  if(owned)state.transports.erase(registered);return true;
 }
 public:
 explicit Sweep(std::filesystem::path dir={}):directory(std::move(dir)),root(directory.parent_path()){}
 bool Done()const{return done;}
 const Result& Summary()const{return result;}
 // One group or one bounded directory chunk per UI poll. File IO/atomic flush
 // has a soft scheduling budget, not a hard nonblocking latency guarantee.
 void Step() noexcept try{
  auto state=detail::State(root);if(!state){Finish();return;}
  const auto deadline=GetTickCount64()+8;
  if(epoch!=state->epoch||revision!=state->revision){Reset();epoch=state->epoch;revision=state->revision;}
  if(done){if(GetTickCount64()-finishedAt<1000)return;Reset();}
  if(!state->journalStarted){detail::Append(root,*state,{{"event","session_ready"}});return;}
  std::vector<detail::Handle> pins;if(!detail::PinDirectories(directory,pins)){Finish();return;}
  BY_HANDLE_FILE_INFORMATION directoryInfo{};if(!GetFileInformationByHandle(pins.back().value,&directoryInfo)){Finish();return;}
  FILE_ID_INFO fullId{};const bool full=GetFileInformationByHandleEx(pins.back().value,FileIdInfo,&fullId,sizeof(fullId))!=FALSE;
  const bool changed=directoryIdentity&&(directoryFullId!=full||(full?(directoryId.VolumeSerialNumber!=fullId.VolumeSerialNumber||!std::equal(directoryId.FileId.Identifier,directoryId.FileId.Identifier+sizeof(directoryId.FileId.Identifier),fullId.FileId.Identifier)):(directoryVolume!=directoryInfo.dwVolumeSerialNumber||directoryHigh!=directoryInfo.nFileIndexHigh||directoryLow!=directoryInfo.nFileIndexLow)));
  if(changed){Reset();return;}
  directoryFullId=full;directoryId=fullId;directoryVolume=directoryInfo.dwVolumeSerialNumber;directoryHigh=directoryInfo.nFileIndexHigh;directoryLow=directoryInfo.nFileIndexLow;directoryIdentity=true;
  if(!anchor){
   auto next=std::make_shared<NamespaceAnchor>();
   if(!UserDataSession::RegisterResource(root,epoch,next)){Finish();return;}
   next->pins=std::move(pins);anchor=std::move(next);
  }
  // On later polls fresh path/reparse validation above accompanies the live
  // anchor. IDs aid diagnostics; held namespace handles establish continuity.

  // Consumer-acknowledged current jobs get the same one-group work cap as migration.
  for(auto& [id,transport]:state->transports)if(transport.drained&&transport.acknowledged&&GetTickCount64()>=transport.retryAt){auto candidate=detail::Collect(directory,id);if(candidate.files.empty()&&!candidate.unknown){if(detail::PreserveKnowledge(transport.request,transport.receipt)&&detail::Append(root,*state,{{"event","operation_consumed_without_transport"},{"request",transport.request},{"receipt",transport.receipt}}))state->transports.erase(id);else transport.retryAt=GetTickCount64()+1000;return;}try{if(!Retire(id,candidate,*state))transport.retryAt=GetTickCount64()+1000;}catch(...){completeReferences=false;transport.retryAt=GetTickCount64()+1000;}return;}
  if(phase==0){anchor->entries=std::filesystem::directory_iterator(directory);phase=1;}
  if(phase==1){
   unsigned budget=64;while(anchor->entries!=end&&budget--&&GetTickCount64()<deadline){const auto path=anchor->entries->path();++anchor->entries;++count;
    const auto name=path.filename().wstring();if(detail::Guid(name)){if(groups.size()>=20000&&!groups.contains(detail::Fold(name.substr(0,38)))){completeReferences=false;}else detail::Add(groups[detail::Fold(name.substr(0,38))],path);}
    if(count>=100000){completeReferences=false;anchor->entries=end;break;}
   }
   if(anchor->entries!=end)return;group=groups.begin();phase=2;return;
  }
  if(phase==2){if(group!=groups.end()){if(GetTickCount64()>=deadline)return;const auto current=group++;try{Retire(current->first,current->second,*state);}catch(...){completeReferences=false;}return;}
   for(const auto& [id,transport]:state->transports)if(!transport.discovery){detail::Documents d;d.request=transport.request;d.result=transport.receipt;detail::References(d,references,completeReferences,!transport.drained||!transport.acknowledged);}
   if(!completeReferences){Finish();return;}
   anchor->entries=std::filesystem::directory_iterator(root);phase=3;return;
  }
  if(phase==3){
   unsigned budget=64;while(anchor->entries!=end&&budget--&&GetTickCount64()<deadline){const auto path=anchor->entries->path();++anchor->entries;const auto name=path.filename().wstring();if(name.size()!=42||!name.ends_with(L".plan.json"))continue;
    const auto id=std::string(name.begin(),name.begin()+32);if(!detail::Hex(id,32)||references.contains(id)||state->protectedPlans.contains(id))continue;
    WIN32_FILE_ATTRIBUTE_DATA info{};if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info))continue;
    detail::Group candidate;candidate.files.push_back({path,uint64_t(info.nFileSizeHigh)<<32|info.nFileSizeLow,detail::Ticks(info.ftLastWriteTime),{}});std::vector<detail::Handle> files;if(!detail::Open(candidate,files))continue;
    detail::Json plan;try{plan=detail::ReadJson(files[0].value,candidate.files[0].size,6*1048576);if(!detail::Plan(plan,id))continue;}catch(...){continue;}
    // No conflicting launch/consumer can run between this same-thread final
    // reference check and deletion. A stale reference pass never proves absence.
    if(UserDataSession::Authority(root)!=epoch||state->revision!=revision||state->protectedPlans.contains(id)){Reset();return;}
    if(!detail::Append(root,*state,{{"event","unused_plan_retired"},{"planId",id},{"target",plan["target"]}}))return;
    if(detail::Delete(candidate,files))++result.removedPlans;return;
   }
   if(anchor->entries==end){Finish();}
  }
 }catch(...){Finish();}
};
inline std::map<std::wstring,std::unique_ptr<Sweep>> sweeps;
inline void Suspend(const std::filesystem::path& root) noexcept try{sweeps.erase(detail::Key(root));}catch(...){}
inline void Maintain(const std::filesystem::path& root) noexcept try{
 if(!UserDataSession::Authority(root))return;
 auto& sweep=sweeps[detail::Key(root)];if(!sweep)sweep=std::make_unique<Sweep>(root/L"requests");sweep->Step();
}catch(...){}
// Standalone small-fixture/maintenance adapter. The desktop uses Step, never
// this complete-pass loop at startup. Authority is still an actual root lease.
inline Result Prune(const std::filesystem::path& directory,Limits={}) noexcept{
 Sweep sweep(directory);for(size_t i=0;i<200000&&!sweep.Done();++i)sweep.Step();return sweep.Summary();
}
}
