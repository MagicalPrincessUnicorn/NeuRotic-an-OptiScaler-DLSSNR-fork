#include <menu/Localization.h>
#pragma once
#include "AnythingUiState.h"
#include "AnythingHotkey.h"
#include "storage/UserFile.h"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <iterator>
#include <optional>
namespace nh {
inline const const char* AnythingLooks[]={Neurotic::UiLiteral("desktop.settings.dlssnr/style/value.ef6691545d", "Standard"),Neurotic::UiLiteral("desktop.option.1c6ac69f1e5d", "Neutral"),Neurotic::UiLiteral("desktop.settings.dlssnr/style/value.d6acb6d51c", "Natural"),Neurotic::UiLiteral("desktop.settings.dlssnr/style/value.912d0988b0", "Cinematic")};
inline bool ProtectedAnythingProfile(std::string name) {
 auto space=[](unsigned char c){return std::isspace(c)!=0;};
 while(!name.empty()&&space(name.front()))name.erase(name.begin());
 while(!name.empty()&&space(name.back()))name.pop_back();
 for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
 return name=="default";
}
inline bool CanSaveAnythingProfile(const AnythingUiState& p,const std::string& name) {
 return !name.empty()&&name.size()<=96&&!ProtectedAnythingProfile(name)&&
  std::any_of(name.begin(),name.end(),[](unsigned char c){return !std::isspace(c);})&&
  std::none_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;})&&
  (p.profiles.contains(name)||p.profiles.size()<64);
}
enum class AnythingPreferenceEdit : uint32_t {
 Look=1,Detail=2,Colour=4,Resolution=8,FrameGeneration=16,NeuralRendering=32,Output=64,
 Current=127,LastLoaded=128,Countdown=256,Hotkey=512,ScreenshotHotkey=1024
};
inline void MarkAnythingPreferenceEdit(AnythingUiState& p,AnythingPreferenceEdit edit) {
 p.preferenceEdits|=static_cast<uint32_t>(edit);
}
inline void ApplyAnythingLook(AnythingUiState& p,int look) {
 p.look=std::clamp(look,0,3);
 constexpr float strengths[][2]={{1,1},{0,0},{1,1},{1,1}};
 p.transfer=strengths[p.look][0];p.colour=strengths[p.look][1];
 MarkAnythingPreferenceEdit(p,AnythingPreferenceEdit::Look);
 MarkAnythingPreferenceEdit(p,AnythingPreferenceEdit::Detail);
 MarkAnythingPreferenceEdit(p,AnythingPreferenceEdit::Colour);
}
inline void ApplyAnythingDefaultProfile(AnythingUiState& p) {
 ApplyAnythingLook(p,0);p.resolution=0;p.profile="Default";
 MarkAnythingPreferenceEdit(p,AnythingPreferenceEdit::Resolution);
 MarkAnythingPreferenceEdit(p,AnythingPreferenceEdit::LastLoaded);
}
inline nlohmann::json AnythingProfile(const AnythingUiState& p) {
 return {{"look",p.look},{"detail",p.transfer},{"colour",p.colour},{"quality",p.resolution},{"manualPercent",p.manualPercent},{"frameGeneration",p.frameGeneration},
 {"superResolution",p.superResolution?"fsr1":"off"},{"neuralRendering",p.neuralRendering},{"outputMode",p.fullscreen?"fullscreen":p.overlay?"overlay":"preview"}};
}
inline bool SaveAnythingUserProfile(AnythingUiState& p,const std::string& name) {
 if(!CanSaveAnythingProfile(p,name))return false;
 p.profiles[name]=AnythingProfile(p);p.profile=name;
 MarkAnythingPreferenceEdit(p,AnythingPreferenceEdit::LastLoaded);return true;
}
inline bool DeleteAnythingUserProfile(AnythingUiState& p,const std::string& name) {
 if(ProtectedAnythingProfile(name)||!p.profiles.contains(name))return false;
 p.profiles.erase(name);if(p.profile==name){p.profile="Custom";MarkAnythingPreferenceEdit(p,AnythingPreferenceEdit::LastLoaded);}return true;
}
inline bool ApplyAnythingProfile(AnythingUiState& p,const nlohmann::json& value) {
 try {
  const int look=value.at("look").get<int>(),quality=value.at("quality").get<int>();
  if((quality==4&&!value.contains("manualPercent"))||(value.contains("manualPercent")&&!value.at("manualPercent").is_number_integer()))return false;
  const auto manual=value.value("manualPercent",int64_t(100));
  const float detail=value.at("detail").get<float>(),colour=value.at("colour").get<float>();
  if(!value.at("look").is_number_integer()||!value.at("quality").is_number_integer()||look<0||look>3||quality<0||quality>4||manual<25||manual>100||
     !std::isfinite(detail)||!std::isfinite(colour)||detail<0||detail>2||colour<0||colour>2)return false;
  if(value.contains("frameGeneration")&&!value["frameGeneration"].is_boolean())return false;
  if(value.contains("neuralRendering")&&!value["neuralRendering"].is_boolean())return false;
  const auto sr=value.value("superResolution",std::string("off")),output=value.value("outputMode",std::string("overlay"));
  if((sr!="off"&&sr!="fsr1")||(output!="overlay"&&output!="preview"&&output!="fullscreen")||(output=="fullscreen"&&sr!="fsr1"))return false;
  p.superResolution=sr=="fsr1";p.neuralRendering=value.value("neuralRendering",true);p.fullscreen=output=="fullscreen";p.overlay=output!="preview";
  p.look=look;p.resolution=quality;p.manualPercent=static_cast<int>(manual);p.transfer=detail;p.colour=colour;p.frameGeneration=value.value("frameGeneration",false);
  // Retired depth keys in old profiles are intentionally ignored.
  return true;
 }catch(...){return false;}
}
inline bool ApplyAnythingSessionProfile(AnythingUiState& p,const nlohmann::json& value,bool active){
 auto candidate=p;if(!ApplyAnythingProfile(candidate,value))return false;
 if(active&&(candidate.superResolution!=p.superResolution||candidate.neuralRendering!=p.neuralRendering||candidate.fullscreen!=p.fullscreen||candidate.overlay!=p.overlay))return false;
 MarkAnythingPreferenceEdit(candidate,AnythingPreferenceEdit::Current);
 MarkAnythingPreferenceEdit(candidate,AnythingPreferenceEdit::LastLoaded);
 p=std::move(candidate);return true;
}
// Parsing recovery is deliberately separate from strict session/profile Apply.
inline std::optional<int> AnythingPreferenceInteger(const nlohmann::json& object,const char* key,int low,int high) {
 if(!object.is_object()||!object.contains(key)||!object.at(key).is_number_integer())return {};
 const auto& value=object.at(key);
 if(value.is_number_unsigned()){
  const auto n=value.get<uint64_t>();if(n>static_cast<uint64_t>(high)||(low>0&&n<static_cast<uint64_t>(low)))return {};
  return static_cast<int>(n);
 }
 const auto n=value.get<int64_t>();if(n<low||n>high)return {};return static_cast<int>(n);
}
inline bool RecoverAnythingCurrent(AnythingUiState& p,const nlohmann::json& value) {
 bool issues=false;
 if(!value.is_object())return true;
 if(auto look=AnythingPreferenceInteger(value,"look",0,3))p.look=*look;else issues=true;
 auto strength=[&](const char* key,float& target){
  if(!value.contains(key)||!value.at(key).is_number()){issues=true;return;}
  const double v=value.at(key).get<double>();if(!std::isfinite(v)||v<0||v>2){issues=true;return;}target=static_cast<float>(v);
 };
 strength("detail",p.transfer);strength("colour",p.colour);
 const auto quality=AnythingPreferenceInteger(value,"quality",0,4);
 const auto manual=value.contains("manualPercent")?AnythingPreferenceInteger(value,"manualPercent",25,100):std::optional<int>(100);
 if(quality&&manual&&(*quality!=4||value.contains("manualPercent"))){p.resolution=*quality;p.manualPercent=*manual;}else issues=true;
 auto boolean=[&](const char* key,bool& target,bool fallback){
  if(!value.contains(key)){target=fallback;return;}if(!value.at(key).is_boolean()){issues=true;return;}target=value.at(key).get<bool>();
 };
 boolean("frameGeneration",p.frameGeneration,false);boolean("neuralRendering",p.neuralRendering,true);
 std::string sr="off",output="overlay";
 const bool srValid=!value.contains("superResolution")||value.at("superResolution").is_string();
 const bool outputValid=!value.contains("outputMode")||value.at("outputMode").is_string();
 if(srValid&&outputValid){
  sr=value.value("superResolution",sr);output=value.value("outputMode",output);
  if((sr=="off"||sr=="fsr1")&&(output=="overlay"||output=="preview"||output=="fullscreen")&&(output!="fullscreen"||sr=="fsr1")){
   p.superResolution=sr=="fsr1";p.fullscreen=output=="fullscreen";p.overlay=output!="preview";
  }else issues=true;
 }else issues=true;
 return issues;
}
inline nlohmann::json AnythingPreferenceProjection(const AnythingUiState& p) {
 return {{"current",AnythingProfile(p)},{"profiles",p.profiles},{"lastLoaded",p.profile},{"countdown",p.countdownSeconds},
  {"hotkey",{{"enabled",p.hotkeyEnabled},{"modifiers",p.hotkeyModifiers},{"key",p.hotkeyKey}}},
  {"screenshotHotkey",{{"enabled",p.screenshotHotkeyEnabled},{"modifiers",p.screenshotHotkeyModifiers},{"key",p.screenshotHotkeyKey}}}};
}
inline std::string ReadAnythingPreferenceBytes(const std::filesystem::path& path) {
 for(auto folder=path;!folder.empty();){auto attributes=GetFileAttributesW(folder.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.linked_anything_preferences_are_refused_15dc5376", "Linked Anything preferences are refused"));auto parent=folder.parent_path();if(parent==folder)break;folder=parent;}
 if(std::filesystem::is_symlink(path)||std::filesystem::file_size(path)>65536)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.anything_profiles_could_not_be_read_dfbd9c18", "Anything profiles could not be read."));
 std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.anything_profiles_could_not_be_read_dfbd9c18", "Anything profiles could not be read."));
 std::string bytes{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
 if(stream.bad()||bytes.size()>65536)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.anything_profiles_could_not_be_read_dfbd9c18", "Anything profiles could not be read."));return bytes;
}
inline nlohmann::json ParseAnythingPreferenceDocument(const std::string& bytes) {
 try {
  auto value=nlohmann::json::parse(bytes);
  if(!value.is_object()||value.value("schemaVersion",0)!=1||!value.at("profiles").is_object()||value.at("profiles").size()>64)throw std::runtime_error("unsupported");
  return value;
 }catch(...){throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.unsupported_anything_profiles_88dc4e41", "Unsupported Anything profiles."));}
}
inline bool RecoverAnythingPreferenceDocument(AnythingUiState& candidate,const nlohmann::json& value,nlohmann::json* aliases=nullptr) {
 bool issues=RecoverAnythingCurrent(candidate,value.contains("current")?value.at("current"):nlohmann::json());
 candidate.profiles=nlohmann::json::object();
 for(const auto& item:value.at("profiles").items()){
  AnythingUiState clean;
  if(item.key().empty()||item.key().size()>96||!ApplyAnythingProfile(clean,item.value())){issues=true;continue;}
  candidate.profiles[item.key()]=AnythingProfile(clean);
 }
 std::string last="Default";
 if(value.contains("lastLoaded")){if(value.at("lastLoaded").is_string()&&value.at("lastLoaded").get_ref<const std::string&>().size()<=96)last=value.at("lastLoaded").get<std::string>();else issues=true;}
 for(const auto& item:value.at("profiles").items())if(ProtectedAnythingProfile(item.key())&&candidate.profiles.contains(item.key())){
  std::string renamed="Default (saved)";unsigned suffix=2;
  while(value.at("profiles").contains(renamed)||candidate.profiles.contains(renamed))renamed="Default (saved "+std::to_string(suffix++)+")";
  auto saved=candidate.profiles.at(item.key());candidate.profiles.erase(item.key());candidate.profiles[renamed]=std::move(saved);
  if(aliases)(*aliases)[renamed]=item.key();if(last==item.key())last=renamed;
 }
 if(!candidate.profiles.contains(last)&&last!="Default"&&last!="Custom"){
  issues=true;last=candidate.look==0&&candidate.transfer==1&&candidate.colour==1&&candidate.resolution==0?"Default":"Custom";
 }
 candidate.profile=last;
 if(value.contains("countdown")){
  const auto& delay=value.at("countdown");
  if(delay.is_number_unsigned())candidate.countdownSeconds=static_cast<int>(std::clamp(delay.get<uint64_t>(),uint64_t(1),uint64_t(30)));
  else if(delay.is_number_integer())candidate.countdownSeconds=static_cast<int>(std::clamp(delay.get<int64_t>(),int64_t(1),int64_t(30)));
  else issues=true;
 }
 auto hotkey=[&](const char* key,bool& enabled,unsigned& modifiers,unsigned& code){
  if(!value.contains(key))return;const auto& data=value.at(key);
  const auto m=AnythingPreferenceInteger(data,"modifiers",0,7),k=AnythingPreferenceInteger(data,"key",0,VK_F11);
  if(!m||!k||!data.contains("enabled")||!data.at("enabled").is_boolean()||!ValidAnythingHotkey(static_cast<unsigned>(*m),static_cast<unsigned>(*k))){issues=true;return;}
  enabled=data.at("enabled").get<bool>();modifiers=static_cast<unsigned>(*m);code=static_cast<unsigned>(*k);
 };
 hotkey("hotkey",candidate.hotkeyEnabled,candidate.hotkeyModifiers,candidate.hotkeyKey);
 candidate.screenshotHotkeyEnabled=true;candidate.screenshotHotkeyModifiers=MOD_CONTROL|MOD_ALT;candidate.screenshotHotkeyKey='S';
 hotkey("screenshotHotkey",candidate.screenshotHotkeyEnabled,candidate.screenshotHotkeyModifiers,candidate.screenshotHotkeyKey);
 return issues;
}
inline std::string AnythingRecoveryNotice(){return Neurotic::UiMessage("desktop.anythingpreferences.partial_recovery", "Some settings could not be loaded. Unchanged data is preserved.");}
inline void SaveAnythingPreferences(AnythingUiState& p,const std::filesystem::path& path) {
 if(!p.profiles.is_object()||p.profiles.size()>64)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.keep_at_most_64_anything_profiles_dc958730", "Keep at most 64 Anything profiles."));
 AnythingUiState strict;if(!ApplyAnythingProfile(strict,AnythingProfile(p)))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.invalid_anything_settings_82eb8745", "Invalid Anything settings."));
 for(const auto& item:p.profiles.items())if(item.key().empty()||item.key().size()>96||!ApplyAnythingProfile(strict,item.value()))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.invalid_anything_profile_72118df6", "Invalid Anything profile."));
 const bool exists=std::filesystem::exists(path),tracked=p.preferenceSnapshotKnown&&p.preferenceSourcePath==path;
 const std::string bytes=exists?ReadAnythingPreferenceBytes(path):std::string{};
 auto value=exists?ParseAnythingPreferenceDocument(bytes):nlohmann::json{{"schemaVersion",1},{"profiles",nlohmann::json::object()}};
 if((exists&&!tracked)||(tracked&&(exists!=p.preferenceFileExisted||bytes!=p.preferenceSourceBytes)))throw std::runtime_error(Neurotic::UiMessage("desktop.neurotic-hubsettings.settings_changed_externally_outside_edit_preserv_93e1796c", "Settings changed externally; outside edit preserved. Reload before saving."));
 const auto now=AnythingPreferenceProjection(p),before=tracked?p.preferenceBaseline:now;
 if(tracked&&before.contains("legacyProfiles"))for(const auto& item:before.at("legacyProfiles").items()){
  const auto old=item.value().get<std::string>();if(!value.at("profiles").contains(old))continue;
  value["profiles"][item.key()]=value.at("profiles").at(old);value["profiles"].erase(old);
  if(value.contains("lastLoaded")&&value.at("lastLoaded").is_string()&&value.at("lastLoaded")==old)value["lastLoaded"]=item.key();
 }
 auto edited=[&](AnythingPreferenceEdit flag,const char* field){return !exists||(p.preferenceEdits&static_cast<uint32_t>(flag))!=0||!before.contains(field)||before.at(field)!=now.at(field);};
 const auto current=now.at("current"),oldCurrent=before.at("current");
 const struct Group{AnythingPreferenceEdit flag;const char* first;const char* second;} groups[]={
  {AnythingPreferenceEdit::Look,"look",nullptr},{AnythingPreferenceEdit::Detail,"detail",nullptr},{AnythingPreferenceEdit::Colour,"colour",nullptr},
  {AnythingPreferenceEdit::Resolution,"quality","manualPercent"},{AnythingPreferenceEdit::FrameGeneration,"frameGeneration",nullptr},
  {AnythingPreferenceEdit::NeuralRendering,"neuralRendering",nullptr},{AnythingPreferenceEdit::Output,"superResolution","outputMode"}};
 for(const auto& group:groups){
  const bool changed=!exists||(p.preferenceEdits&static_cast<uint32_t>(group.flag))!=0||current.at(group.first)!=oldCurrent.at(group.first)||(group.second&&current.at(group.second)!=oldCurrent.at(group.second));
  if(!changed)continue;if(!value.contains("current")||!value.at("current").is_object())value["current"]=oldCurrent;
  value["current"][group.first]=current.at(group.first);if(group.second)value["current"][group.second]=current.at(group.second);
 }
 for(const auto& item:before.at("profiles").items())if(!p.profiles.contains(item.key()))value["profiles"].erase(item.key());
 for(const auto& item:p.profiles.items())if(!exists||!before.at("profiles").contains(item.key())||before.at("profiles").at(item.key())!=item.value()){
  if(!value.at("profiles").contains(item.key())||!value.at("profiles").at(item.key()).is_object())value["profiles"][item.key()]=nlohmann::json::object();
  for(const auto& field:current.items())if(item.value().contains(field.key()))value["profiles"][item.key()][field.key()]=item.value().at(field.key());
 }
 const struct RootGroup{AnythingPreferenceEdit flag;const char* key;} rootGroups[]={
  {AnythingPreferenceEdit::LastLoaded,"lastLoaded"},{AnythingPreferenceEdit::Countdown,"countdown"},
  {AnythingPreferenceEdit::Hotkey,"hotkey"},{AnythingPreferenceEdit::ScreenshotHotkey,"screenshotHotkey"}};
 for(const auto& group:rootGroups)if(edited(group.flag,group.key)){
  if((group.flag==AnythingPreferenceEdit::Hotkey||group.flag==AnythingPreferenceEdit::ScreenshotHotkey)&&value.contains(group.key)&&value.at(group.key).is_object()){
   for(const auto& field:now.at(group.key).items())value[group.key][field.key()]=field.value();
  }else value[group.key]=now.at(group.key);
 }
 auto output=value.dump();if(output.size()>65536||value.at("profiles").size()>64)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.existing_anything_profiles_are_too_large_to_repl_6d71fe04", "Existing Anything profiles are too large to replace."));
 auto nextBaseline=now;auto nextPath=path;
 AnythingUiState observed;
 const bool clearNotice=!RecoverAnythingPreferenceDocument(observed,value)&&p.notice==AnythingRecoveryNotice();
 SaveUserFile(path,output);
 // All allocating work precedes the disk commit; publication below is noexcept.
 p.preferenceSourcePath.swap(nextPath);p.preferenceSourceBytes.swap(output);p.preferenceBaseline.swap(nextBaseline);
 p.preferenceSnapshotKnown=p.preferenceFileExisted=true;p.preferenceEdits=0;
 if(clearNotice)p.notice.clear();
}
inline void LoadAnythingPreferences(AnythingUiState& p,const std::filesystem::path& path) {
 p.preferencesLoaded=true;
 if(!std::filesystem::exists(path)){
  p.preferenceSnapshotKnown=true;p.preferenceFileExisted=false;p.preferenceSourcePath=path;p.preferenceSourceBytes.clear();p.preferenceBaseline=AnythingPreferenceProjection(p);p.preferenceEdits=0;return;
 }
 const auto bytes=ReadAnythingPreferenceBytes(path);const auto value=ParseAnythingPreferenceDocument(bytes);
 AnythingUiState candidate=p;nlohmann::json aliases=nlohmann::json::object();
 const bool issues=RecoverAnythingPreferenceDocument(candidate,value,&aliases);
 candidate.preferenceSnapshotKnown=candidate.preferenceFileExisted=true;candidate.preferenceSourcePath=path;candidate.preferenceSourceBytes=bytes;
 candidate.preferenceBaseline=AnythingPreferenceProjection(candidate);candidate.preferenceBaseline["legacyProfiles"]=std::move(aliases);candidate.preferenceEdits=0;
 if(issues)candidate.notice=AnythingRecoveryNotice();
 p=std::move(candidate);
}
// WGC arrivals are capture events, not unique source pictures or display FPS.
inline void UpdateAnythingRates(AnythingUiState& p,double now,uint64_t session,uint64_t arrivals,uint64_t completed,bool running,uint64_t presented=UINT64_MAX) {
 if(presented==UINT64_MAX)presented=completed;
 if(!running||!session||session!=p.rateSession||now<p.rateTime||arrivals<p.rateArrivals||completed<p.rateCompleted) {
  p.rateSession=session;p.rateTime=now;p.rateArrivals=arrivals;p.rateCompleted=completed;p.ratePresented=presented;
  p.ratesKnown=p.deficit=false;p.deficitSeconds=0;return;
 }
 const double elapsed=now-p.rateTime;if(elapsed<1)return;
 if(elapsed>3){p.ratesKnown=p.deficit=false;p.deficitSeconds=0;}
 else {
  p.incomingFps=(arrivals-p.rateArrivals)/elapsed;p.processedFps=(completed-p.rateCompleted)/elapsed;p.outputFps=presented>=p.ratePresented?(presented-p.ratePresented)/elapsed:0;p.ratesKnown=true;
  if(p.incomingFps>=5&&p.processedFps<p.incomingFps*.9)p.deficitSeconds+=elapsed;else p.deficitSeconds=0;
  p.deficit=p.deficitSeconds>=5;
 }
 p.rateTime=now;p.rateArrivals=arrivals;p.rateCompleted=completed;p.ratePresented=presented;
}
}
