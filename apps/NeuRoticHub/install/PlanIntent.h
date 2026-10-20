#include <menu/Localization.h>
#pragma once
#include "OperationController.h"
namespace nh {
inline void ValidatePlanIntent(const Json& requested,const Json& plan,int /*bitness*/){
 const auto& returned=plan.at("request");
 if(!returned.is_object())throw std::runtime_error(Neurotic::UiMessage("desktop.planintent.invalid_returned_request_2c2f2f0c", "Invalid returned request."));
 const auto operation=requested.value("operation",std::string{});
 for(const auto& item:returned.items()){
  if(requested.contains(item.key())||(item.key()=="protocolVersion"&&item.value()==1))continue;
  // Omitting a proxy selects the existing worker catalog/receipt resolution.
  // Its concrete supported filename stays bound by the returned plan fingerprint.
  if(operation=="Install"&&item.key()=="proxyName"&&item.value().is_string()){
   auto proxy=item.value().get<std::string>();for(auto& c:proxy)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
   if(proxy=="dxgi.dll"||proxy=="winmm.dll"||proxy=="version.dll"||proxy=="dbghelp.dll"||proxy=="d3d12.dll"||
      proxy=="wininet.dll"||proxy=="winhttp.dll"||proxy=="optiscaler.asi"||proxy=="optiscaler.dll")continue;
  }
  throw std::runtime_error(Neurotic::UiMessage("desktop.planintent.the_plan_added_an_unrequested_action_control_83bada70", "The plan added an unrequested action control."));
 }
 if(operation!=Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install")&&operation!=Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall")&&operation!="SaveSettings"&&operation!="UpdateNrDll")throw std::runtime_error(Neurotic::UiMessage("desktop.planintent.unsupported_action_d06d8072", "Unsupported action."));
 for(const auto& item:requested.items())if(!returned.contains(item.key())||returned.at(item.key())!=item.value())throw std::runtime_error(Neurotic::UiMessage("desktop.planintent.the_returned_plan_differs_from_the_requested_act_5726736c", "The returned plan differs from the requested action."));
}
}
