#pragma once
#include "json.hpp"
#include <cstdint>
#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <utility>
namespace nh {
// The accepted writer owns this identity; page/selection and later library edits do not.
class InstallCompletionState {
 public:
 struct Identity {uint64_t serial=0;std::string gameId,title,target,planId;std::wstring targetKey;};
 const Identity* Active()const{return active?&*active:nullptr;}
 const Identity* Pending()const{return pending.empty()?nullptr:&pending.front();}
 const std::deque<Identity>& PendingItems()const{return pending;}
 void Begin(Identity identity){identity.serial=++generation;active=std::move(identity);}
 template<class Normalize> bool Observe(const nlohmann::json& result,const std::string& action,Normalize normalize){
  if(!active)return false;
  auto identity=std::move(*active);active.reset(); // one terminal observation per accepted child
  try{
   if(action!="Install"||identity.planId.empty()||identity.targetKey.empty()||identity.title.empty()||!result.is_object())return false;
   const auto status=result.value("status",std::string{});
   if(status!="Succeeded"&&status!="SucceededWithNotes")return false;
   if(result.value("planId",std::string{})!=identity.planId)return false;
   if(!result.at("errors").is_array()||!result.at("errors").empty()||!result.at("skipped").is_array()||!result.at("skipped").empty())return false;
   const auto& inspection=result.at("inspection");
   if(normalize(inspection.at("target").at("executable").template get<std::string>())!=identity.targetKey||
      inspection.at("state").value("status",std::string{})!="Installed")return false;
   pending.push_back(std::move(identity));return true;
  }catch(const nlohmann::json::exception&){return false;}
 }
 void Acknowledge(uint64_t serial){if(!pending.empty()&&pending.front().serial==serial)pending.pop_front();}
 // A rendered batch owns its last observed serial. Never acknowledge a later,
 // unseen receipt or interpret an unknown/stale serial as "clear everything".
 void AcknowledgeThrough(uint64_t serial){
  if(std::none_of(pending.begin(),pending.end(),[&](const auto& item){return item.serial==serial;}))return;
  while(!pending.empty()){
   const bool last=pending.front().serial==serial;pending.pop_front();if(last)break;
  }
 }

 private:
 uint64_t generation=0;std::optional<Identity> active;std::deque<Identity> pending;
};
}
