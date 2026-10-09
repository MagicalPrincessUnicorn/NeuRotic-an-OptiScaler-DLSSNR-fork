#include <menu/Localization.h>
#pragma once
#include "OperationController.h"
#include "ManualLibrary.h"
#include <algorithm>
#include <cwctype>
#include <set>
#include <map>
#include <vector>

namespace nh {
struct BulkUninstallTarget { std::string executable,title,source; };
struct BulkUninstallRow {
 BulkUninstallTarget target;
 std::string status="Pending",detail,planId;
 Json inspection;
};

// Pure sequential workflow. The Hub's sole ProcessJob owns all requests/results;
// this class never launches a process or changes an installed game's files.
class BulkUninstall {
public:
 enum class Phase { Idle,Inventory,Review,Running,Finished,Cancelled };
 static constexpr size_t MaximumTargets=4096;
 Phase GetPhase()const{return phase_;}
 const std::vector<BulkUninstallRow>& Rows()const{return rows_;}
 const std::vector<std::string>& Issues()const{return issues_;}
 size_t ReadyCount()const{return std::count_if(rows_.begin(),rows_.end(),[](const auto& r){return r.status=="Ready";});}
 bool AwaitingReceipt()const{return !request_.is_null();}
 bool Active()const{return phase_==Phase::Inventory||phase_==Phase::Running||AwaitingReceipt();}
 const std::string& Mode()const{return mode_;}
 std::string CurrentExecutable()const{return current_<rows_.size()?rows_[current_].target.executable:std::string{};}
 bool Begin(const std::vector<BulkUninstallTarget>& targets,const std::vector<std::string>& issues={}){
  if(Active())return false;
  rows_.clear();alternatives_.clear();candidateNotes_.clear();issues_=issues;confirmed_.clear();mode_.clear();request_=nullptr;current_=0;stop_=cancel_=false;step_=Step::Inspect;phase_=Phase::Inventory;
  std::map<std::wstring,size_t> directories;
  for(const auto& target:targets){
   if(rows_.size()>=MaximumTargets){issues_.push_back(Neurotic::UiLiteral("desktop.bulkuninstall.known_install_inventory_limit_reached_additional_548e2891", "Known-install inventory limit reached; additional targets were not inspected."));break;}
   BulkUninstallRow row;row.target={target.executable.substr(0,32768),target.title.substr(0,4096),target.source.substr(0,256)};
   try{const auto directory=Key(target.executable,true);auto found=directories.find(directory);if(found!=directories.end()){
     const auto index=found->second;if(Key(target.executable)!=Key(rows_[index].target.executable)&&std::none_of(alternatives_[index].begin(),alternatives_[index].end(),[&](const auto& old){return Key(old.executable)==Key(target.executable);})){if(alternatives_[index].size()<16)alternatives_[index].push_back(row.target);else issues_.push_back(Neurotic::UiLiteral("desktop.bulkuninstall.too_many_executable_candidates_in_one_installati_814024f9", "Too many executable candidates in one installation directory; extra candidates were omitted."));}continue;
    }directories.emplace(directory,rows_.size());}
   catch(const std::exception& e){row.status="Blocked";row.detail=e.what();}
   rows_.push_back(std::move(row));alternatives_.emplace_back();candidateNotes_.emplace_back();
  }
  AdvanceInventory();return true;
 }
 Json NextRequest(){
  if(AwaitingReceipt())return nullptr;
  if(phase_==Phase::Inventory){AdvanceInventory();if(phase_!=Phase::Inventory)return nullptr;request_={{"kind","Inspect"},{"gameExecutable",CurrentExecutable()}};rows_[current_].status="Inspecting";}
  else if(phase_==Phase::Running){
   if(stop_){FinishStopped();return nullptr;}
   if(current_>=rows_.size()){phase_=Phase::Finished;return nullptr;}
   auto& row=rows_[current_];
   if(step_==Step::Verify){row.status=Neurotic::UiLiteral("desktop.bulkuninstall.checking_again_f8635e12", "Checking again");request_={{"kind","Inspect"},{"gameExecutable",CurrentExecutable()}};}
   else if(step_==Step::Plan){row.status="Planning";request_={{"kind","Plan"},{"gameExecutable",CurrentExecutable()},{"operation",Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall")}};}
   else {row.status="Uninstalling";request_={{"kind","Execute"},{"planId",row.planId},{"planFingerprint",fingerprint_}};}
  }
  return request_;
 }
 bool Confirm(const std::string& mode){
  if(phase_!=Phase::Review||AwaitingReceipt()||!ReadyCount()||mode!=Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall"))return false;
  mode_=mode;confirmed_.clear();for(size_t i=0;i<rows_.size();++i)if(rows_[i].status=="Ready")confirmed_.push_back(i);
  confirmedIndex_=0;current_=confirmed_[0];step_=Step::Verify;phase_=Phase::Running;stop_=false;return true;
 }
 void Cancel(){
  if(phase_==Phase::Running){StopAfterCurrent();return;}
  if(phase_==Phase::Inventory||phase_==Phase::Review){cancel_=true;phase_=Phase::Cancelled;for(auto& row:rows_)if(row.status=="Pending"||row.status=="Inspecting"||row.status=="Ready"){row.status="Cancelled";row.detail=Neurotic::UiLiteral("desktop.bulkuninstall.no_uninstall_was_started_b33f08f0", "No uninstall was started.");}}
 }
 // Never kills an Execute. If its Plan/Inspect is still running, drain that
 // read-only result and stop before issuing the next mutation.
 void StopAfterCurrent(){if(phase_==Phase::Running){stop_=true;if(!AwaitingReceipt())FinishStopped();}}
 void AcceptFailure(const std::string& reason){AcceptReceipt(Json{{"status","Failed"},{Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),reason}});}
 void AcceptReceipt(const Json& receipt){
  if(!AwaitingReceipt())return;
  const auto sent=request_;request_=nullptr;
  if(cancel_)return;
  auto& row=rows_.at(current_);
  try{
   if(!receipt.is_object())throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.malformed_installer_response_no_further_action_w_54d0826c", "Malformed installer response; no further action was taken for this game."));
   CheckOptionalTarget(receipt,row.target.executable);
   const auto status=receipt.value("status",std::string{});
   if(phase_==Phase::Inventory){
    if(status!="Inspected")Fail(row,status,receipt);else Inventory(row,receipt.at("inspection"));
    CompleteInventoryRow();return;
   }
   if(phase_!=Phase::Running)return;
   if(step_==Step::Verify){
    if(status!="Inspected")Fail(row,status,receipt);
    else {const auto& fresh=receipt.at("inspection");ValidateInspection(fresh,row.target.executable);
     if(fresh.at("target").at("fileIdentity")!=row.inspection.at("target").at("fileIdentity"))throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.game_or_installation_changed_after_confirmation__a360172f", "Game or installation changed after confirmation. Review it again before uninstalling."));
     if(!Managed(fresh)||fresh.value("recoveryRequired",false))throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.installation_is_no_longer_ready_for_uninstall_re_b656adc5", "Installation is no longer ready for uninstall. Review its current state."));
     if(stop_){FinishStopped();return;}step_=Step::Plan;return;
    }
   }else if(step_==Step::Plan){
    if(status!="Planned")Fail(row,status,receipt);
    else {
     if(!receipt.contains("request")||!receipt["request"].is_object())throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.plan_request_is_missing_75158436", "Plan request is missing."));
     auto actual=receipt["request"];if(actual.contains("protocolVersion")&&actual["protocolVersion"]!=1)throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.plan_protocol_version_changed_0fb433bb", "Plan protocol version changed."));actual.erase("protocolVersion");if(actual!=sent)throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.plan_changed_the_confirmed_target_mode_or_operat_cc083dcc", "Plan changed the confirmed target, mode or operation."));
     CheckTarget(receipt.at("target"),row.target.executable);
     if(receipt.at("target").at("fileIdentity")!=row.inspection.at("target").at("fileIdentity"))throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.game_or_settings_changed_while_preparing_uninsta_1e604d0b", "Game or settings changed while preparing uninstall."));
     row.planId=receipt.at("planId").get<std::string>();fingerprint_=receipt.at("planFingerprint").get<std::string>();
     if(!Hex(row.planId,32)||!Hex(fingerprint_,64))throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.uninstall_plan_identity_is_invalid_452939f5", "Uninstall plan identity is invalid."));
     if(receipt.value(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"),Json()).is_object()&&receipt[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")].value("acknowledgementRequired",false))throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.unexpected_approval_requirement_review_this_game_bffa04ec", "Unexpected approval requirement; review this game individually."));
     if(stop_){FinishStopped();return;}step_=Step::Execute;return;
    }
   }else {
    if(receipt.contains("planId")&&receipt["planId"]!=row.planId)throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.result_belongs_to_another_uninstall_plan_2d3fbae7", "Result belongs to another uninstall plan."));
    if(status=="Succeeded"||status=="SucceededWithNotes"){
     if(receipt.value("planId",std::string{})!=row.planId)throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.uninstall_success_is_missing_its_plan_identity_15e1ed1c", "Uninstall success is missing its plan identity."));
     const auto& after=receipt.at("inspection");CheckTarget(after.at("target"),row.target.executable);
     row.status="Uninstalled";row.detail=receipt.value(Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),std::string(Neurotic::UiLiteral("desktop.bulkuninstall.recorded_neurotic_files_removed_24dd95e3", "Recorded NeuRotic files removed.")));

    }else Fail(row,status,receipt);
   }
  }catch(const std::exception& e){row.status="Blocked";row.detail=e.what();}
  if(phase_==Phase::Inventory)CompleteInventoryRow();else if(phase_==Phase::Running)AdvanceRun();
 }
private:
 enum class Step { Inspect,Verify,Plan,Execute };
 Phase phase_=Phase::Idle;Step step_=Step::Inspect;
 std::vector<BulkUninstallRow> rows_;std::vector<std::string> issues_;std::vector<size_t> confirmed_;
 std::vector<std::vector<BulkUninstallTarget>> alternatives_;std::vector<std::string> candidateNotes_;
 size_t current_=0,confirmedIndex_=0;bool stop_=false,cancel_=false;Json request_;
 std::string mode_,fingerprint_;
 static bool Hex(const std::string& value,size_t n){return value.size()==n&&std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');});}
 static std::wstring Key(const std::string& value,bool directory=false){
  if(value.empty()||value.size()>32768||value.find('\0')!=std::string::npos)throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.known_game_path_is_missing_or_invalid_ed6ffd69", "Known game path is missing or invalid."));
  auto path=std::filesystem::path(Wide(value));auto text=path.wstring();
  if(!path.is_absolute()||text.starts_with(L"\\\\")||text.find(L':',2)!=std::wstring::npos||path.filename().empty())throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.known_game_requires_a_local_absolute_executable__712f808d", "Known game requires a local absolute executable path."));
  if(directory)path=path.parent_path();text=path.lexically_normal().wstring();std::replace(text.begin(),text.end(),L'/',L'\\');std::transform(text.begin(),text.end(),text.begin(),[](wchar_t c){return wchar_t(towlower(c));});return text;
 }
 static void CheckTarget(const Json& target,const std::string& executable){
  if(!target.is_object()||Key(target.at("executable").get<std::string>())!=Key(executable))throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.installer_response_belongs_to_another_game_8818ec7d", "Installer response belongs to another game."));
  if(target.contains("directory")){auto probe=std::filesystem::path(Wide(target.at("directory").get<std::string>()))/L"identity.exe";if(Key(Utf8(probe.wstring()),true)!=Key(executable,true))throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.installer_response_changed_the_game_directory_8557cf2b", "Installer response changed the game directory."));}
 }
 static void CheckOptionalTarget(const Json& result,const std::string& executable){
  if(result.contains("target")){if(result["target"].is_string()){if(Key(result["target"].get<std::string>())!=Key(executable))throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.installer_response_belongs_to_another_game_8818ec7d", "Installer response belongs to another game."));}else CheckTarget(result["target"],executable);}
  if(result.contains("inspection"))CheckTarget(result["inspection"].at("target"),executable);
 }
 static bool Managed(const Json& inspection){const auto& state=inspection.at("state");return state.is_object()&&((state.value("kind",std::string{})=="neurotic-game-install"&&state.value("schemaVersion",0)==3)||state.value("kind",std::string{})=="neurotic-public-install");}
 static void ValidateInspection(const Json& inspection,const std::string& executable){CheckTarget(inspection.at("target"),executable);if(!inspection.at("target").at("fileIdentity").is_string()||inspection.at("target").at("fileIdentity").get<std::string>().empty())throw std::runtime_error(Neurotic::UiMessage("desktop.bulkuninstall.fresh_inspection_lacks_selected_executable_ident_2dfdaa17", "Fresh inspection lacks selected executable identity."));}
 static void Inventory(BulkUninstallRow& row,const Json& inspection){
  ValidateInspection(inspection,row.target.executable);row.inspection={{"target",inspection["target"]}};
  if(Managed(inspection)){row.status="Ready";row.detail=Neurotic::UiLiteral("desktop.bulkuninstall.recorded_installation_changed_and_missing_files__73dd3f40", "Recorded installation. Changed and missing files do not block removal.");}
  else if(inspection.at("state").is_null()){row.status=Neurotic::UiLiteral("desktop.bulkuninstall.not_installed_40cdadc4", "Not installed");row.detail=Neurotic::UiLiteral("desktop.bulkuninstall.no_recorded_installation_was_found_legacy_remova_d84a98da", "No recorded installation was found. Legacy removal is available individually.");}
  else{row.status="Blocked";row.detail=Neurotic::UiLiteral("desktop.bulkuninstall.review_this_game_individually_0eab54cb", "Review this game individually.");}
 }
 static void Fail(BulkUninstallRow& row,const std::string& status,const Json& receipt){row.status=status=="RecoveryRequired"?Neurotic::UiLiteral("desktop.bulkuninstall.recovery_required_5606a694", "Recovery required"):status=="PreconditionChanged"?"Changed":status=="FailedWithChanges"?Neurotic::UiLiteral("desktop.bulkuninstall.failed_with_changes_9e987f77", "Failed with changes"):status=="Cancelled"?"Cancelled":"Blocked";row.detail=receipt.value(Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),std::string(Neurotic::UiLiteral("desktop.bulkuninstall.the_installer_did_not_complete_this_operation_re_f6303468", "The installer did not complete this operation. Review this game individually."))).substr(0,8192);}
 void CompleteInventoryRow(){
  auto& row=rows_[current_];
  if(row.status=="Blocked"&&!alternatives_[current_].empty()){
   candidateNotes_[current_]+=Neurotic::UiLiteral("desktop.bulkuninstall.earlier_executable_candidate_e8a78424", "Earlier executable candidate: ")+row.detail+" ("+row.target.executable+")\n";candidateNotes_[current_].resize(std::min(candidateNotes_[current_].size(),size_t(8192)));
   auto next=alternatives_[current_].front();alternatives_[current_].erase(alternatives_[current_].begin());if(next.title.empty())next.title=row.target.title;row.target=std::move(next);row.status="Pending";row.detail.clear();return;
  }
  if(!candidateNotes_[current_].empty())row.detail+="\n"+candidateNotes_[current_];++current_;AdvanceInventory();
 }
 void AdvanceInventory(){while(current_<rows_.size()&&rows_[current_].status!="Pending")++current_;if(current_==rows_.size())phase_=Phase::Review;}
 void AdvanceRun(){fingerprint_.clear();if(stop_){FinishStopped();return;}if(++confirmedIndex_>=confirmed_.size()){phase_=Phase::Finished;return;}current_=confirmed_[confirmedIndex_];step_=Step::Verify;}
 void FinishStopped(){for(auto index:confirmed_)if(rows_[index].status=="Ready"||rows_[index].status==Neurotic::UiLiteral("desktop.bulkuninstall.checking_again_f8635e12", "Checking again")||rows_[index].status=="Planning"){rows_[index].status=Neurotic::UiLiteral("desktop.bulkuninstall.not_attempted_de99aa8b", "Not attempted");rows_[index].detail=Neurotic::UiLiteral("desktop.bulkuninstall.stopped_before_this_game_s_uninstall_began_3ded61af", "Stopped before this game's uninstall began.");}phase_=Phase::Finished;}
};
}
