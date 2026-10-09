#pragma once
#include "OperationController.h"
#include "storage/UserFile.h"
#include "storage/AppDataMaintenance.h"
#include <fstream>
#include <set>
#include <cctype>

namespace nh {
inline void ValidateInstallDefaults(const Json& value){
 if(!value.is_object()||!value.contains("schemaVersion")||!value["schemaVersion"].is_number_integer()||value["schemaVersion"]!=1)
  throw std::runtime_error("Unsupported install defaults version");
 for(const auto& item:value.items())if(item.key()!="schemaVersion"&&item.key()!="nrMode"&&item.key()!="modelPreset"&&item.key()!="style"&&item.key()!="mfgUnlock"&&item.key()!="mfgConsent")
  throw std::runtime_error("Unknown install default");
 auto integer=[&](const char* key,std::initializer_list<int> choices){if(!value.contains(key))return;const auto& v=value[key];if(!v.is_number_integer()||std::none_of(choices.begin(),choices.end(),[&](int n){return v==n;}))throw std::runtime_error("Invalid install default value");};
 integer("nrMode",{0,2,3});integer("style",{0,1,2});
 if(value.contains("modelPreset")) {
  const auto& v=value["modelPreset"];if(!v.is_string()||(v!="auto"&&v!="0"&&v!="1"&&v!="2"&&v!="3"))throw std::runtime_error("Invalid model preset default");
 }
 if(value.contains("mfgConsent")&&!value["mfgConsent"].is_boolean())throw std::runtime_error("Invalid MFG consent");
 const bool consent=value.value("mfgConsent",false);
 if(value.contains("mfgUnlock")){
  const auto& v=value["mfgUnlock"];
  if(!v.is_string()||(v!="off"&&v!="rtx40"&&v!="rtx30"&&v!="rtx20")||(v!="off"&&!consent))throw std::runtime_error("MFG unlock requires experimental consent");
 }else if(consent)throw std::runtime_error("MFG consent requires an unlock selection");
}
inline Json ParseInstallDefaults(const std::string& text){
 if(text.size()>4096)throw std::runtime_error("Install defaults exceed size limit");
 std::vector<std::set<std::string>> keys;
 auto parsed=Json::parse(text,[&](int depth,nlohmann::json::parse_event_t event,Json& item){
  if(depth>16)throw std::runtime_error("Install defaults exceed depth limit");
  if(event==Json::parse_event_t::object_start)keys.emplace_back();
  if(event==Json::parse_event_t::key){auto key=item.get<std::string>();std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});if(keys.empty()||!keys.back().insert(key).second)throw std::runtime_error("Duplicate install default");}
  if(event==Json::parse_event_t::object_end)keys.pop_back();
  return true;
 });
 ValidateInstallDefaults(parsed);return parsed;
}
struct InstallDefaultsState {
 Json preferences={{"schemaVersion",1}};
 bool readOnly=false;
 std::string issue;
};
inline InstallDefaultsState ReadInstallDefaults(const std::filesystem::path& path){
 InstallDefaultsState state;
 try{
  bool complete=false;auto parents=maintenance_detail::Parents(path.parent_path(),false,&complete);if(!complete)return state;
  if(!maintenance_detail::Exists(path))return state;
  auto file=maintenance_detail::Open(path,false,false,true);auto stamp=maintenance_detail::Stamp(file,{});
  if(stamp.bytes>4096)throw std::runtime_error("Install defaults exceed size limit");
  std::string text(size_t(stamp.bytes),'\0');DWORD read=0;
  // maintenance Open pins identity; use a separately pinned read handle because
  // its metadata handle deliberately does not grant FILE_READ_DATA.
  HANDLE input=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(input==INVALID_HANDLE_VALUE)throw std::runtime_error("Install defaults unavailable");
  const bool ok=ReadFile(input,text.data(),DWORD(text.size()),&read,nullptr)!=0;CloseHandle(input);
  if(!ok||read!=text.size())throw std::runtime_error("Install defaults read failed");
  state.preferences=ParseInstallDefaults(text);
 }catch(...){state.readOnly=true;state.issue=Neurotic::UiMessage("desktop.installdefaults.read_failed","Install defaults could not be read. The file was preserved. Repair it before editing these defaults.");}
 return state;
}
inline void SaveInstallDefaults(const std::filesystem::path& path,const Json& preferences,const Json* expected=nullptr){
 ValidateInstallDefaults(preferences);
 auto current=ReadInstallDefaults(path);
 if(current.readOnly||(expected&&current.preferences!=*expected))throw std::runtime_error("Install defaults changed or cannot be read");
 SaveUserFile(path,preferences.dump(2));
}
inline Json WriteInstallDefault(const std::filesystem::path& path,const char* key,const Json& value){
 auto current=ReadInstallDefaults(path);if(current.readOnly)throw std::runtime_error("Install defaults cannot be read");
 auto next=current.preferences;next[key]=value;
 if(std::string_view(key)=="mfgUnlock")next["mfgConsent"]=value!="off";
 SaveInstallDefaults(path,next,&current.preferences);return next;
}
}
