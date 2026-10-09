#include <menu/Localization.h>
#pragma once
#include <windows.h>
#include <json.hpp>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <string>

namespace nh {
enum class NrModelPresence { Unknown,Missing,Present };
struct NrModelReminder {
 bool missingApp=false,missingGame=false;
 std::string reason;
 bool Visible()const{return missingApp||missingGame;}
};
// Read-only filename presence, not model verification. No directory walk, hash,
// model load, or mutation; inaccessible/linked locations remain unknown.
inline NrModelPresence ProbeNrModelPresence(const std::filesystem::path& model){
 const auto parent=GetFileAttributesW(model.parent_path().c_str());
 if(parent==INVALID_FILE_ATTRIBUTES||!(parent&FILE_ATTRIBUTE_DIRECTORY)||(parent&FILE_ATTRIBUTE_REPARSE_POINT))return NrModelPresence::Unknown;
 const auto attributes=GetFileAttributesW(model.c_str());
 if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?NrModelPresence::Missing:NrModelPresence::Unknown;}
 if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return NrModelPresence::Unknown;
 return attributes&FILE_ATTRIBUTE_DIRECTORY?NrModelPresence::Missing:NrModelPresence::Present;
}
class NrModelReminderCache {
 using Json=nlohmann::json;
 std::function<NrModelPresence(const std::filesystem::path&)> probe_;
 std::wstring executableKey_;
 std::string executableHash_,evidenceKey_;
 NrModelPresence gamePresence_=NrModelPresence::Unknown;
 uint64_t checkedAt_=0;
 bool checked_=false;
 static std::wstring Key(const std::filesystem::path& path){
  auto value=path.wstring();if(!path.is_absolute()||value.starts_with(L"\\\\")||value.find(L':',2)!=std::wstring::npos||value.find(L'\0')!=std::wstring::npos||path.filename().empty())return {};
  value=path.lexically_normal().wstring();std::replace(value.begin(),value.end(),L'/',L'\\');std::transform(value.begin(),value.end(),value.begin(),[](wchar_t c){return wchar_t(towlower(c));});return value;
 }
 static std::string Text(const Json& object,const char* key){auto found=object.is_object()?object.find(key):object.end();return found!=object.end()&&found->is_string()?found->get<std::string>():std::string{};}
public:
 explicit NrModelReminderCache(std::function<NrModelPresence(const std::filesystem::path&)> probe=ProbeNrModelPresence):probe_(std::move(probe)){}
 NrModelReminder Evaluate(const Json& preflight,bool appChecking,const std::filesystem::path& executable,bool installed,uint64_t nowMs,const Json& inspection=Json()){
  NrModelReminder result;
  if(!appChecking&&preflight.is_object()){
   auto components=preflight.find("components");if(components!=preflight.end()&&components->is_object()){
    auto model=components->find(Neurotic::UiLiteral("desktop.hubshell.neuralmodel_47a6b821", "neuralModel"));if(model!=components->end())result.missingApp=Text(*model,"status")==Neurotic::UiLiteral("desktop.translationeditorwindow.missing_fdf8d84e", "Missing");
   }
  }
  auto key=Key(executable);std::string hash,evidence;bool matching=false;
  try{if(inspection.is_object()&&inspection.contains("target")){
   const auto& target=inspection.at("target");auto inspected=Text(target,"executable");
   matching=!key.empty()&&!inspected.empty()&&Key(std::filesystem::u8path(inspected))==key;
   if(matching){hash=Text(target,"sha256");evidence=Text(inspection,"revision");if(evidence.empty())evidence=Text(inspection,"configRevision");}
  }}catch(...){matching=false;}
  if(!installed||key.empty()){checked_=false;gamePresence_=NrModelPresence::Unknown;executableKey_.clear();executableHash_.clear();evidenceKey_.clear();}
  else{
   const bool changed=key!=executableKey_||hash!=executableHash_;
   if(changed){checked_=false;gamePresence_=NrModelPresence::Unknown;evidenceKey_.clear();executableKey_=key;executableHash_=hash;}
   // Consume a matching fresh inspection once. Current-only inspections usually
   // omit this private DLL, so lack of an inventory entry is not absence proof.
   bool freshEvidence=false;NrModelPresence observed=NrModelPresence::Unknown;
   if(matching&&!evidence.empty()&&evidence!=evidenceKey_){
    auto missing=inspection.find("missing");if(missing!=inspection.end()&&missing->is_array())for(const auto& item:*missing)if(item.is_string()&&_stricmp(item.get_ref<const std::string&>().c_str(),"nvngx_dlssnr.dll")==0)observed=NrModelPresence::Missing;
    auto inventory=inspection.find("inventory");if(observed==NrModelPresence::Unknown&&inventory!=inspection.end()&&inventory->is_array())for(const auto& item:*inventory)if(_stricmp(Text(item,Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path")).c_str(),"nvngx_dlssnr.dll")==0)observed=NrModelPresence::Present;
    evidenceKey_=evidence;if(observed!=NrModelPresence::Unknown){gamePresence_=observed;checkedAt_=nowMs;checked_=freshEvidence=true;}
   }
   if(!freshEvidence&&(!checked_||nowMs<checkedAt_||nowMs-checkedAt_>=2000)){
    gamePresence_=probe_(executable.parent_path()/L"nvngx_dlssnr.dll");checkedAt_=nowMs;checked_=true;
   }
   result.missingGame=gamePresence_==NrModelPresence::Missing;
  }
  if(result.missingApp)result.reason=Neurotic::UiLiteral("desktop.nrmodelreminder.use_the_nr_anything_button_to_open_the_model_fol_ca78eeb6", "Use the NR Anything button to open the model folder and place nvngx_dlssnr.dll there.");
  if(result.missingGame){if(!result.reason.empty())result.reason+="\n";result.reason+=Neurotic::UiLiteral("desktop.nrmodelreminder.neurotic_is_installed_in_this_game_but_nvngx_dls_d93d3bad", "NeuRotic is installed in this game, but nvngx_dlssnr.dll is missing from its executable folder.");}
  return result;
 }
};
}
