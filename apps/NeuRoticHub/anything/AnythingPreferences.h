#include <menu/Localization.h>
#pragma once
#include "AnythingUiState.h"
#include "AnythingHotkey.h"
#include "storage/UserFile.h"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <cctype>
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
inline void ApplyAnythingLook(AnythingUiState& p,int look) {
 p.look=std::clamp(look,0,3);
 constexpr float strengths[][2]={{1,1},{0,0},{1,1},{1,1}};
 p.transfer=strengths[p.look][0];p.colour=strengths[p.look][1];
}
inline void ApplyAnythingDefaultProfile(AnythingUiState& p) {
 ApplyAnythingLook(p,0);p.resolution=0;p.profile="Default";
}
inline nlohmann::json AnythingProfile(const AnythingUiState& p) {
 return {{"look",p.look},{"detail",p.transfer},{"colour",p.colour},{"quality",p.resolution},{"manualPercent",p.manualPercent},{"frameGeneration",p.frameGeneration},
 {"superResolution",p.superResolution?"fsr1":"off"},{"neuralRendering",p.neuralRendering},{"outputMode",p.fullscreen?"fullscreen":p.overlay?"overlay":"preview"}};
}
inline bool SaveAnythingUserProfile(AnythingUiState& p,const std::string& name) {
 if(!CanSaveAnythingProfile(p,name))return false;
 p.profiles[name]=AnythingProfile(p);p.profile=name;return true;
}
inline bool DeleteAnythingUserProfile(AnythingUiState& p,const std::string& name) {
 if(ProtectedAnythingProfile(name)||!p.profiles.contains(name))return false;
 p.profiles.erase(name);if(p.profile==name)p.profile="Custom";return true;
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
 p=std::move(candidate);return true;
}
inline void SaveAnythingPreferences(const AnythingUiState& p,const std::filesystem::path& path) {
 if(p.profiles.size()>64)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.keep_at_most_64_anything_profiles_dc958730", "Keep at most 64 Anything profiles."));
 if(std::filesystem::exists(path)){if(std::filesystem::file_size(path)>65536)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.existing_anything_profiles_are_too_large_to_repl_6d71fe04", "Existing Anything profiles are too large to replace."));std::ifstream source(path);auto previous=nlohmann::json::parse(source);if(previous.value("schemaVersion",0)!=1)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.this_version_cannot_replace_newer_anything_profi_796268b1", "This version cannot replace newer Anything profiles."));}
 SaveUserFile(path,nlohmann::json{{"schemaVersion",1},{"lastLoaded",p.profile},{"current",AnythingProfile(p)},
     {"profiles",p.profiles},{"countdown",p.countdownSeconds},{"hotkey",{{"enabled",p.hotkeyEnabled},{"modifiers",p.hotkeyModifiers},{"key",p.hotkeyKey}}},
     {"screenshotHotkey",{{"enabled",p.screenshotHotkeyEnabled},{"modifiers",p.screenshotHotkeyModifiers},{"key",p.screenshotHotkeyKey}}}}.dump(2));
}
inline void LoadAnythingPreferences(AnythingUiState& p,const std::filesystem::path& path) {
 p.preferencesLoaded=true;
 for(auto folder=path;!folder.empty();){auto attributes=GetFileAttributesW(folder.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.linked_anything_preferences_are_refused_15dc5376", "Linked Anything preferences are refused"));auto parent=folder.parent_path();if(parent==folder)break;folder=parent;}
 if(!std::filesystem::exists(path))return;
 if(std::filesystem::is_symlink(path)||std::filesystem::file_size(path)>65536)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.anything_profiles_could_not_be_read_dfbd9c18", "Anything profiles could not be read."));
 std::ifstream stream(path);auto value=nlohmann::json::parse(stream);
 if(value.value("schemaVersion",0)!=1||!value.at("profiles").is_object()||value.at("profiles").size()>64)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.unsupported_anything_profiles_88dc4e41", "Unsupported Anything profiles."));
 for(const auto& item:value.at("profiles").items()) {
  AnythingUiState candidate;
  if(item.key().empty()||item.key().size()>96||!ApplyAnythingProfile(candidate,item.value()))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.invalid_anything_profile_72118df6", "Invalid Anything profile."));
 }
 AnythingUiState candidate=p;
 if(!ApplyAnythingProfile(candidate,value.at("current")))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.invalid_anything_settings_82eb8745", "Invalid Anything settings."));
 auto last=value.value("lastLoaded",std::string("Default"));
 if(last.size()>96)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.invalid_profile_name_7bfc435e", "Invalid profile name."));
 candidate.profiles=nlohmann::json::object();
 for(const auto& item:value.at("profiles").items()){AnythingUiState clean;ApplyAnythingProfile(clean,item.value());candidate.profiles[item.key()]=AnythingProfile(clean);}
 // Default used to be a legal user-profile name. Preserve those profiles and
 // their last-loaded link before reserving the new built-in identity.
 for(const auto& item:value.at("profiles").items())if(ProtectedAnythingProfile(item.key())){
  std::string renamed="Default (saved)";unsigned suffix=2;
  while(candidate.profiles.contains(renamed))renamed="Default (saved "+std::to_string(suffix++)+")";
  auto saved=candidate.profiles.at(item.key());candidate.profiles.erase(item.key());candidate.profiles[renamed]=std::move(saved);
  if(last==item.key())last=renamed;
 }
 if(!candidate.profiles.contains(last)&&last!="Default"&&last!="Custom")
  last=candidate.look==0&&candidate.transfer==1&&candidate.colour==1&&candidate.resolution==0?"Default":"Custom";
 candidate.profile=last;
 candidate.countdownSeconds=std::clamp(value.value("countdown",3),1,30);
 if(value.contains("hotkey")){
  const auto& hotkey=value.at("hotkey");const auto modifiers=hotkey.at("modifiers").get<unsigned>(),key=hotkey.at("key").get<unsigned>();
  if(!ValidAnythingHotkey(modifiers,key))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.invalid_nr_anything_shortcut_3a10370f", "Invalid NR Anything shortcut."));
  candidate.hotkeyEnabled=hotkey.at("enabled").get<bool>();candidate.hotkeyModifiers=modifiers;candidate.hotkeyKey=key;
 }
 candidate.screenshotHotkeyEnabled=true;candidate.screenshotHotkeyModifiers=MOD_CONTROL|MOD_ALT;candidate.screenshotHotkeyKey='S';
 if(value.contains("screenshotHotkey")){
  const auto& hotkey=value.at("screenshotHotkey");const auto modifiers=hotkey.at("modifiers").get<unsigned>(),key=hotkey.at("key").get<unsigned>();
  if(!ValidAnythingHotkey(modifiers,key))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingpreferences.invalid_nr_anything_screenshot_shortcut_9d38b1a5", "Invalid NR Anything screenshot shortcut."));
  candidate.screenshotHotkeyEnabled=hotkey.at("enabled").get<bool>();candidate.screenshotHotkeyModifiers=modifiers;candidate.screenshotHotkeyKey=key;
 }
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
