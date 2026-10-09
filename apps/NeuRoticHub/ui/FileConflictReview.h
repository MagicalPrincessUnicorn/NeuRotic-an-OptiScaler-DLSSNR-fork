#include <menu/Localization.h>
#pragma once
#include <json.hpp>
#include <stdexcept>
#include <string>
namespace nh {
using Json=nlohmann::json;
// Choices name one unfamiliar destination. The shared backend validates its
// lightweight identity at the copy boundary; there is no content hash contract.
class FileConflictReview {
 Json files=Json::array(),decisions=Json::array();size_t index=0;
public:
 void Begin(const Json& conflicts){
  if(!conflicts.is_array()||conflicts.empty()||conflicts.size()>10000)throw std::runtime_error(Neurotic::UiMessage("desktop.fileconflictreview.invalid_file_review_67b3a8ed", "Invalid file review"));
  for(const auto& file:conflicts)if(!file.is_object()||!file.contains(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"))||!file[Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path")].is_string()||file[Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path")].get_ref<const std::string&>().empty()||file[Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path")].get_ref<const std::string&>().size()>32768)throw std::runtime_error(Neurotic::UiMessage("desktop.fileconflictreview.invalid_file_review_path_7f74fcc0", "Invalid file review path"));
  files=conflicts;decisions=Json::array();index=0;
 }
 bool Active()const{return index<files.size();}
 const Json& Current()const{if(!Active())throw std::runtime_error(Neurotic::UiMessage("desktop.fileconflictreview.no_file_awaiting_a_decision_05e004d1", "No file awaiting a decision"));return files.at(index);}
 const Json& Decisions()const{return decisions;}
 size_t Count()const{return files.size();}size_t Index()const{return index;}
 bool Choose(const std::string& action){
  if(action!=Neurotic::UiLiteral("desktop.hubshell.replace_79226e10", "Replace")&&action!=Neurotic::UiLiteral("desktop.hubshell.skip_9e5fb0c9", "Skip"))throw std::runtime_error(Neurotic::UiMessage("desktop.fileconflictreview.unknown_file_decision_513b8e69", "Unknown file decision"));
  decisions.push_back(Json{{Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),Current().at(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"))},{"action",action}});++index;return !Active();
 }
 void Cancel(){files=Json::array();decisions=Json::array();index=0;}
};
}
