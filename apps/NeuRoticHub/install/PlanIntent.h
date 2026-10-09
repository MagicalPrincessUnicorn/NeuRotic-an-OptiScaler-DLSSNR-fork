#include <menu/Localization.h>
#pragma once
#include "OperationController.h"
namespace nh {
inline void ValidatePlanIntent(const Json& requested,const Json& plan,int bitness){
 const auto& returned=plan.at("request");
 if(!returned.is_object())throw std::runtime_error(Neurotic::UiMessage("desktop.planintent.invalid_returned_request_2c2f2f0c", "Invalid returned request."));
 for(const auto& item:returned.items())if(!requested.contains(item.key())&&!(item.key()=="protocolVersion"&&item.value()==1))throw std::runtime_error(Neurotic::UiMessage("desktop.planintent.the_plan_added_an_unrequested_action_control_83bada70", "The plan added an unrequested action control."));
 const auto operation=requested.value("operation",std::string{});
 if(operation!=Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install")&&operation!=Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall")&&operation!="SaveSettings")throw std::runtime_error(Neurotic::UiMessage("desktop.planintent.unsupported_action_d06d8072", "Unsupported action."));
 if(bitness!=64&&operation!=Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall"))throw std::runtime_error(Neurotic::UiMessage("desktop.planintent.a_compatible_in_game_integration_is_unavailable__5a817f1b", "A compatible in-game integration is unavailable. Use NR Anything."));
 for(const auto& item:requested.items())if(!returned.contains(item.key())||returned.at(item.key())!=item.value())throw std::runtime_error(Neurotic::UiMessage("desktop.planintent.the_returned_plan_differs_from_the_requested_act_5726736c", "The returned plan differs from the requested action."));
}
}
