#pragma once
#include <json.hpp>
#include <menu/Localization.h>
#include <string>
#include "../languages/LanguageMessages.h"
namespace nh {
inline std::string AutomaticSetupNotice(const nlohmann::json& inspection){
 if(!inspection.is_object()||!inspection.contains("state")||!inspection["state"].is_object())return {};
 const auto& state=inspection["state"];if(!state.contains("autoSetup")||!state["autoSetup"].is_object())return {};
 const auto& setup=state["autoSetup"];
 if(!setup.contains("schemaVersion")||!setup["schemaVersion"].is_number_integer()||setup["schemaVersion"].get<int>()!=1||!setup.contains("state")||!setup["state"].is_string())return {};
 if(setup["state"].get_ref<const std::string&>()=="ReadyForRuntime")
  return Neurotic::UiMessage("desktop.automatic_setup.prepared_pending","Prepared. Game validation is pending.");
 if(setup.contains("reason")&&setup["reason"].is_string()){
  auto message=ResultMessage(setup,setup["reason"].get<std::string>());
  if(setup.contains("reasonCode")&&setup.value("preferencesPreserved",false))message+=" "+Neurotic::UiMessage("desktop.automatic_setup.preserved_suffix","Existing Neural Rendering preferences were preserved.");
  return message;
 }
 return {};
}
}
