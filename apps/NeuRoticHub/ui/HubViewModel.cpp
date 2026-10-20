#include "languages/LanguageMessages.h"
#include <menu/Localization.h>
#include "HubViewModel.h"
#include "StartupTimingProbe.h"
#include "library/GameCatalog.h"
#include "ProfileCache.h"
#include "GameTargetIdentity.h"
#include "storage/UserFile.h"
#include "GameScreenshots.h"
#include "AutomaticSetupNotice.h"
#include "install/DiagnosticRetention.h"
#include "install/PlanIntent.h"
#include "install/NrDllTargetState.h"
#include "anything/AnythingController.h"
#include "anything/AnythingTargetLifecycle.h"
#include <fstream>
#include <algorithm>
#include <map>
#include <set>
#include <nr/semantic/object_rules/ObjectRuleEditor.h>
#include <shellapi.h>
namespace nh {
// The visible selection moves immediately; this marker still owns disposal of
// the superseded non-queue reader until its exact process receipt is drained.
static constexpr int SupersededReader=-3;
HubModel::~HubModel() noexcept {
 // This body runs before queued source leases and ProcessJob members unwind.
 // Rendering/device exceptions must not expose an input still owned by a child.
 nrDllAcquireStop.request_stop();
 if(nrDllManager)nrDllManager->RequestStop();
 const bool held=std::any_of(installQueue.begin(),installQueue.end(),[](const auto& item){return bool(item.nrDllLease);});
 if(!held||!installer.process)return;
 // A read-only worker has no mutation to finish; normal Poll timeout handling
 // is no longer active during unwinding. Request its existing cancellation
 // disposition without translated UI/error allocation in this noexcept body.
 // This request is not completion: the exact process signal below still owns
 // the source lease. Active writers are never terminated here.
 if(installer.readOnlyInstaller&&!TerminateProcess(installer.process,3)&&WaitForSingleObject(installer.process,0)!=WAIT_OBJECT_0)
  OutputDebugStringA("NeuRotic: read-only installer cancellation request failed; retaining source until actual exit.\n");
 bool reportedWaitFailure=false;
 for(;;){
  const auto result=WaitForSingleObject(installer.process,INFINITE);
  if(result==WAIT_OBJECT_0)break;
  // A failed wait is not reader completion. Retain the actual source instead
  // of releasing it on an unobserved exit; never terminate an active writer.
  if(!reportedWaitFailure){OutputDebugStringA("NeuRotic: installer exit could not be observed during exceptional teardown; retaining selected NR source.\n");reportedWaitFailure=true;}
  Sleep(100);
 }
}

static bool HasSettingsDraft(const std::vector<SettingsField>& fields){
 return std::any_of(fields.begin(),fields.end(),[](const auto& field){
  if(field.ruleEditor&&field.ruleEditor->dirty)return true;
  return SavedSettingsFieldChanged(field);
 });
}
static std::wstring RootKey(const std::string& root);
static bool Within(const std::wstring& path,const std::wstring& root);
static Json SanitizeChoices(const std::array<bool,6>& enabled);
static Json UninstallCleanupChoices(const std::array<bool,6>& enabled){auto choices=enabled;choices[0]=choices[1]=choices[2]=false;return SanitizeChoices(choices);}
bool HubModel::DataMaintenanceBusy()const{
 if(languageMaintenanceBusy&&languageMaintenanceBusy())return true;
 if(InstallQueueBusy()||launchAdmission||installer.busy||discovery.busy||readiness.busy||BulkUninstallBusy()||diagnosticsWorker.valid()||bundleWorker.valid())return true;
 if(anything){auto state=anything->Snapshot();if(state.busy||state.active||state.stopping)return true;}
 return false;
}
void HubModel::RequestDataMaintenance(int action)try{
 if(action<0||action>2||restartForMaintenance)return;
 if(DataMaintenanceBusy()||NrDllPending()){message=Neurotic::UiMessage("desktop.hubviewmodel.finish_the_current_operation_scan_or_window_proc_9929e1bd", "Finish the current operation, scan or window processing before maintaining App data.");return;}
 DiagnosticRetention::Suspend(UserRoot());
 CancelDataMaintenance();CancelUninstall();CancelSanitize();showReview=showIssue=false;
 dataMaintenanceAction=action;
 if(action==0){maintenanceAnythingSuspended=bool(anything);anything.reset();if(maintenanceQuiesce)maintenanceQuiesce(true);}
 if(action<2){dataMaintenancePlan=PlanAppDataClear(UserRoot(),action==0?AppDataClearScope::AppData:AppDataClearScope::GameProfiles);}
 else{dataMaintenancePlan={};dataMaintenancePlan.userRoot=UserRoot();dataMaintenancePlan.root=UserRoot()/L"artwork";}
 maintenanceRequestedAction=action;reviewedMaintenancePlan=dataMaintenancePlan;showDataMaintenance=true;
}catch(const std::exception& e){CancelDataMaintenance();message=e.what();}
void HubModel::CancelDataMaintenance(){
 if(restartForMaintenance)return;
 const bool whole=dataMaintenanceAction==0;showDataMaintenance=false;dataMaintenanceAction=maintenanceRequestedAction=-1;dataMaintenancePlan={};reviewedMaintenancePlan={};
 if(whole&&maintenanceQuiesce)maintenanceQuiesce(false);
 if(maintenanceAnythingSuspended){maintenanceAnythingSuspended=false;anything=std::make_shared<AnythingController>(AnythingController::FindWorker(AppRoot()),SharedRuntimeRoot());anything->Connect();}
}
void HubModel::ConfirmDataMaintenance()try{
 if(!showDataMaintenance||restartForMaintenance||dataMaintenanceAction<0||dataMaintenanceAction>2)return;
 DiagnosticRetention::Suspend(UserRoot());
 if(dataMaintenanceAction!=maintenanceRequestedAction||dataMaintenancePlan.root!=reviewedMaintenancePlan.root||dataMaintenancePlan.scope!=reviewedMaintenancePlan.scope){message=Neurotic::UiMessage("desktop.hubviewmodel.maintenance_choices_changed_cancel_and_review_ag_2d11a458", "Maintenance choices changed. Cancel and review again.");return;}
 dataMaintenancePlan=reviewedMaintenancePlan;
 if(DataMaintenanceBusy()||NrDllPending()){message=Neurotic::UiMessage("desktop.hubviewmodel.finish_the_current_operation_before_confirming_m_0819dd80", "Finish the current operation before confirming maintenance.");return;}
 if(!dataMaintenancePlan.Ready()){message=Neurotic::UiMessage("desktop.hubviewmodel.app_data_could_not_be_safely_inventoried_cancel__aaaca2ee", "App data could not be safely inventoried. Cancel and review the reported paths.");return;}
 if(dataMaintenanceAction==0){restartForMaintenance=true;showDataMaintenance=false;return;}
 if(dataMaintenanceAction==1){
  auto result=ExecuteAppDataClear(dataMaintenancePlan);
  // Discard every advisory snapshot, including after partial deletion. Never
  // re-save a pre-clear inspection when the user next changes selection.
  gameSession.clear();inspection=Json();inspectionTarget.clear();inspectionFailureTarget.clear();inspectionFailureReason.clear();settings.clear();inspectionCached=false;profileCheckedUtc.clear();
  gameDiagnosticsText.clear();gameDiagnosticsStatus.clear();plan=Json();pendingPlan=Json();defaultsRequest=Json();
  message=result.complete?Neurotic::UiMessage("desktop.hubviewmodel.game_profiles_cleared_select_a_game_again_to_rea_1a3edf02", "Game Profiles cleared. Select a game again to read fresh details."):Neurotic::Localization::FormatSharedText("desktop.provider.2a204d7d62b2", {{"count",std::to_string(result.deletedFiles)},{"detail",result.issues.empty()?Neurotic::UiText("desktop.hubviewmodel.review_the_remaining_files_e5d2eaae") :result.issues.front()}});
 }else{
  auto folder=EnsureArtworkCacheFolder(UserRoot());
  if((INT_PTR)ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.artwork_cache_could_not_be_opened_f8717a31", "Artwork Cache could not be opened."));
  message=Neurotic::UiMessage("desktop.hubviewmodel.artwork_cache_opened_8ea577f4", "Artwork Cache opened.");
 }
 CancelDataMaintenance();
}catch(const std::exception& e){message=e.what();}

std::vector<int> HubModel::OrderedGames(const std::string& filter) const{
 auto fold=[](std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return (char)std::tolower(c);});return value;};auto query=fold(filter);std::vector<int> order;
 for(int i=0;i<(int)games.size();++i){
  const bool pinned=i==selected&&!visibilityPinnedGameId.empty()&&games[i].id==visibilityPinnedGameId&&libraryFilter==visibilityPinnedFilter;
  if(!pinned&&games[i].hidden!=(libraryFilter==LibraryFilter::Hidden))continue;
  if(query.empty()||fold(games[i].title).find(query)!=std::string::npos)order.push_back(i);
 }
 // Relevance applies only to a nonempty query; preserve the saved list order
 // and visibility/identity rules when the user clears the search.
 auto rank=[&](int index){const auto title=fold(games[index].title);return title==query?0:title.starts_with(query)?1:2;};
 std::stable_sort(order.begin(),order.end(),[&](int a,int b){
  if(!query.empty()){const int leftRank=rank(a),rightRank=rank(b);if(leftRank!=rightRank)return leftRank<rightRank;}
  if(favoritesFirst&&games[a].favorite!=games[b].favorite)return games[a].favorite;
  auto left=Wide(games[a].title),right=Wide(games[b].title);return CompareStringOrdinal(left.c_str(),(int)left.size(),right.c_str(),(int)right.size(),TRUE)==CSTR_LESS_THAN;
 });return order;
}
void HubModel::ToggleFavorite(int index){if(index<0||index>=(int)games.size())return;games[index].favorite=!games[index].favorite;Save();}
void HubModel::ToggleHidden(int index){
 if(index<0||index>=(int)games.size())return;
 auto& game=games[index];
 // The exemption follows the selected game's stable catalog identity, never a
 // row number; discovery merges retain that identity even when metadata changes.
 if(index==selected){visibilityPinnedGameId=game.id;visibilityPinnedFilter=libraryFilter;}
 game.hidden=!game.hidden;Save();
}
void HubModel::LeaveLibrarySelection(){
 visibilityPinnedGameId.clear();
 if(selected>=0&&selected<(int)games.size()&&games[selected].hidden!=(libraryFilter==LibraryFilter::Hidden))Select(-1);
}
void HubModel::SetLibraryFilter(LibraryFilter filter){
 if(filter==libraryFilter)return;
 libraryFilter=filter;
 if(filter!=LibraryFilter::Hidden)favoritesFirst=filter==LibraryFilter::FavoritesFirst;
 LeaveLibrarySelection();Save();
}
void HubModel::StartCandidateCapture(bool observer) try{
 if(installer.busy||selected<0||selected>=(int)games.size())return;auto target=InspectExecutable(Wide(games[selected].target.path));if(!target.suitable)throw std::runtime_error(target.reason);
 if(target.bitness==32)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.paired_recording_is_unavailable_for_the_prepared_f4d0f9aa", "Paired recording is unavailable for the prepared 32-bit route. Use Play and export its diagnostic bundle."));
 auto script=AppRoot()/L"packages"/L"current"/L"support"/L"Start-CandidateCapture.ps1";if(!std::filesystem::is_regular_file(script))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.paired_recording_is_unavailable_in_this_package_fc294e93", "Paired recording is unavailable in this package."));
 wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);auto powershell=std::filesystem::path(system)/L"WindowsPowerShell"/L"v1.0"/L"powershell.exe";
 auto command=Quote(powershell.wstring())+L" -NoProfile -NoExit -ExecutionPolicy Bypass -File "+Quote(script.wstring())+L" -Mode "+(observer?L"On":L"Off")+L" -GameExecutable "+Quote(Wide(target.path));
 STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};if(!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NEW_CONSOLE,nullptr,AppRoot().c_str(),&startup,&process))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_paired_recording_window_could_not_be_opened_0f22fe5d", "The paired recording window could not be opened."));CloseHandle(process.hThread);CloseHandle(process.hProcess);message=Neurotic::UiMessage("desktop.hubviewmodel.follow_the_paired_recording_window_this_workflow_c656820b", "Follow the paired recording window. This workflow launches the selected game.");
}catch(const std::exception& e){message=e.what();}
bool HubModel::CanStartGame() const try {
 if(InstallQueueBusy()||(installer.busy&&!BackgroundInspectionActive())||launchAdmission||pendingLaunchGame||pendingSelection!=-2||showDataMaintenance||restartForMaintenance||BulkUninstallBusy()||selected<0||selected>=(int)games.size())return false;
 return MakeLaunchPlan(games[selected]).allowed;
}catch(...){return false;}
void HubModel::StartGame() try{
 if(!CanStartGame()||launchAdmission)return;
 if(BackgroundInspectionActive()){
  if(installerTarget!=GameTargetKey(games[selected].target.path))return;
  pendingLaunchGame=games[selected];installer.Cancel();message=Neurotic::UiMessage("desktop.hubviewmodel.checking_launch_files_c3895d32", "Checking launch files...");return;
 }
 launchGame=games[selected];auto target=InspectExecutable(Wide(launchGame.target.path));
 if(!target.suitable||target.bitness!=launchGame.target.bitness)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.game_executable_changed_refresh_this_game_before_e92008fa", "Game executable changed. Refresh this game before launching."));
 auto admission=std::make_shared<LaunchAdmission>();admission->Enter(std::filesystem::path(Wide(target.path)).parent_path());
 launchAdmission=std::move(admission);StartSelectedOperation({{"kind","LaunchPreflight"},{"gameExecutable",target.path}});message=Neurotic::UiMessage("desktop.hubviewmodel.checking_launch_files_c3895d32", "Checking launch files...");
}catch(const std::exception& e){launchAdmission.reset();message=e.what();}
void HubModel::CompleteLaunch(const Json& result){
 if(!launchAdmission||result.value("status","")!="LaunchReady")throw std::runtime_error(ResultMessage(result,std::string(Neurotic::UiLiteral("desktop.hubviewmodel.launch_checks_failed_refresh_and_retry_c89ebf2e", "Launch checks failed. Refresh and retry."))));
 const auto& target=result.at("target");
 if(!GameTargetMatches(target.at("executable").get<std::string>(),launchGame.target.path)||target.at("bitness")!=launchGame.target.bitness||Wide(result.at("targetLockName").get<std::string>())!=launchAdmission->name)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.launch_receipt_belongs_to_a_different_target_3da4e1d7", "Launch receipt belongs to a different target."));
 launchAdmission->VerifyStat(Wide(launchGame.target.path),target.at("fileIdentity").get<std::string>());
 const auto launch=MakeLaunchPlan(launchGame);if(!launch.allowed)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.choose_an_available_executable_or_rescan_this_st_a6b69817", "Choose an available executable or rescan this Steam game."));
 SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC;info.lpVerb=L"open";info.lpFile=launch.file.c_str();info.lpDirectory=launch.directory.empty()?nullptr:launch.directory.c_str();info.nShow=SW_SHOWNORMAL;
 if(!ShellExecuteExW(&info))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.windows_could_not_start_this_game_597e0ae6", "Windows could not start this game."));
 if(launch.platform){if(info.hProcess)CloseHandle(info.hProcess);launchAdmission->Hold(nullptr);message=Neurotic::UiMessage("desktop.hubviewmodel.launch_sent_to_steam_steam_chooses_the_executabl_837dabb2", "Launch sent to Steam. Steam chooses the executable.");}
 else{launchAdmission->Hold(info.hProcess);message=Neurotic::UiMessage("desktop.hubviewmodel.selected_game_executable_started_6ec7ea19", "Selected game executable started.");}
}
void HubModel::ExportSelectedDiagnostics(const std::filesystem::path& directory) try{
 if(bundleWorker.valid()||selected<0||selected>=(int)games.size()||!games[selected].target.suitable||directory.empty())return;
 CacheSelected();auto target=games[selected].target.path;bundleTarget=GameTargetKey(target);

 auto snapshot=SelectedInspectionSnapshot();bundlePath.clear();bundleStatus=Neurotic::UiMessage("desktop.hubviewmodel.collecting_the_complete_diagnostic_bundle_56a8fdae", "Collecting the complete diagnostic bundle...");bundleBusy=true;exportedBundles[bundleTarget]={"",bundleStatus,false};
 bundleWorker=std::async(std::launch::async,[target,directory,snapshot]{return ExportDiagnosticBundle(target,directory,snapshot);});
}catch(const std::exception& e){bundleBusy=false;bundleStatus=e.what();}
void HubModel::PollDiagnosticBundle(){
 if(!bundleWorker.valid()||bundleWorker.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
 DiagnosticBundleResult result;try{result=bundleWorker.get();}catch(const std::exception& e){result.status=e.what();}
 exportedBundles[bundleTarget]=result;
 if(selected>=0&&selected<(int)games.size()&&!games[selected].target.path.empty()&&GameTargetKey(games[selected].target.path)==bundleTarget){bundleBusy=false;bundlePath=result.success?result.path:"";bundleStatus=result.status;}
}
void HubModel::OpenBundleFolder() try{if(!bundlePath.empty()){auto folder=std::filesystem::path(Wide(bundlePath)).parent_path();if((INT_PTR)ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_diagnostic_destination_could_not_be_opened_adecbeaf", "The diagnostic destination could not be opened."));}}catch(const std::exception& e){message=e.what();}
static const char* Proxies[]={Neurotic::UiLiteral("desktop.hubshell.dxgi_dll_2766e740", "dxgi.dll"),Neurotic::UiLiteral("desktop.option.d508058f7eba", "winmm.dll"),Neurotic::UiLiteral("desktop.option.e4c456927fa4", "version.dll"),Neurotic::UiLiteral("desktop.option.dcc94e2ae3ad", "dbghelp.dll"),Neurotic::UiLiteral("desktop.option.cbf80229b83a", "d3d12.dll"),Neurotic::UiLiteral("desktop.option.e3dbb87412e9", "wininet.dll"),Neurotic::UiLiteral("desktop.option.bf5d99b5c9ae", "winhttp.dll"),Neurotic::UiLiteral("desktop.option.fd4503a9892c", "OptiScaler.asi"),Neurotic::UiLiteral("desktop.option.5990097c3bc5", "OptiScaler.dll")};
static Json QueueDefaultsSnapshot(){
 const auto path=UserRoot()/L"install-defaults.json";
 const auto current=ReadInstallDefaults(path);
 if(current.readOnly)return {{"status","Invalid"},{"preferences",nullptr}};
 std::error_code error;const bool exists=std::filesystem::exists(path,error);
 if(error)throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.defaults_unavailable","Install defaults are unavailable. Check them before queueing this install."));
 return {{"status",exists?"Loaded":"PackageDefaults"},{"preferences",exists?current.preferences:Json()}};
}
static std::string QueueExecutableStamp(const std::string& path){
 HANDLE file=CreateFileW(Wide(path).c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(file==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.executable_unavailable","The queued executable is unavailable. Cancel this item and choose the executable again."));
 QueueIdentityHandle owned{file};BY_HANDLE_FILE_INFORMATION info{};
 if(!GetFileInformationByHandle(file,&info))throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.identity_unavailable","The queued executable identity could not be checked."));
 const auto identity=QueueHandleIdentity(file);
 if(identity.empty())throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.identity_unavailable","The queued executable identity could not be checked."));
 char value[160];sprintf_s(value,"%08X%08X%08X:%u:%u:%u:%u",info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow,info.nFileSizeHigh,info.nFileSizeLow,info.ftLastWriteTime.dwHighDateTime,info.ftLastWriteTime.dwLowDateTime);return identity+":"+value;
}
// Identity belongs to the physical installation directory, independently of
// the accepted request spelling (including ordinary short-name aliases).
static std::wstring QueueDirectoryIdentity(const std::string& executable){
 const auto directory=std::filesystem::path(Wide(executable)).parent_path();
 HANDLE file=CreateFileW(directory.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 const auto unavailable=[](){return std::runtime_error(Neurotic::UiMessage("desktop.installqueue.folder_identity_unavailable","Installation folder identity could not be checked. Choose the game folder again."));};
 if(file==INVALID_HANDLE_VALUE)throw unavailable();
 QueueIdentityHandle owned{file};BY_HANDLE_FILE_INFORMATION info{};
 if(!GetFileInformationByHandle(file,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw unavailable();
 const auto identity=QueueHandleIdentity(file);if(identity.empty())throw unavailable();
 return std::wstring(identity.begin(),identity.end());
}
void HubModel::ClearQueueDecisionState(){
 plan=pendingPlan=Json();operationIssue=Json();showIssue=showReview=false;reviewedInstallId=0;riskAcknowledged=false;
 fileReview.Cancel();installer.ClearAntiCheatApproval();
}
static void FinishCancelledQueueItem(QueuedInstall& item,bool closing){
 item.cancelRequested=false;item.nrDllLease.reset();
 if(item.writeAttempted){
  // Cancelling a retry is not evidence that the earlier writer left files
  // unchanged. Preserve its observed outcome and warning in the visible row.
  item.phase=QueuedInstall::Phase::Failed;if(item.writerResult.is_object())item.result=item.writerResult;
  if(!item.writerSummary.empty())item.summary=item.writerSummary;
  item.summary+=" "+Neurotic::UiMessage("desktop.installqueue.retries_cancelled","Further retries cancelled.");
 }else{
  item.phase=QueuedInstall::Phase::Cancelled;
  item.summary=closing?Neurotic::UiMessage("desktop.installqueue.closed_cancelled","Cancelled when the App closed. No queued install resumes automatically."):Neurotic::UiMessage("desktop.installqueue.item_cancelled","Queue item cancelled. Existing game files were preserved.");
 }
}
static void RetainQueueAntiCheatEvidence(Json& report,const Json& previousIssue,const std::string& target){
 if(!report.is_object()||report.value("scan_status",std::string{})=="complete"||!previousIssue.is_object()||
    !previousIssue.contains("target")||!previousIssue["target"].is_string()||previousIssue["target"].get<std::string>()!=target||!previousIssue.value("antiCheat",Json()).is_object())return;
 auto retained=report.value("stale_findings",Json::array());if(!retained.is_array())retained=Json::array();
 const auto& previous=previousIssue["antiCheat"];
 for(const auto* key:{"stale_findings","findings"}){const auto findings=previous.value(key,Json::array());if(findings.is_array())for(const auto& finding:findings)if(std::find(retained.begin(),retained.end(),finding)==retained.end())retained.push_back(finding);}
 if(!retained.empty())report["stale_findings"]=std::move(retained);
}
static bool QueueTerminal(QueuedInstall::Phase phase){return phase==QueuedInstall::Phase::Succeeded||phase==QueuedInstall::Phase::Failed||phase==QueuedInstall::Phase::Cancelled;}
const QueuedInstall* HubModel::CurrentQueuedInstall()const{
 return installQueueCursor<installQueue.size()&&!QueueTerminal(installQueue[installQueueCursor].phase)?&installQueue[installQueueCursor]:nullptr;
}
const QueuedInstall* HubModel::FindQueuedInstall(uint64_t id)const{
 const auto found=std::find_if(installQueue.begin(),installQueue.end(),[&](const auto& item){return item.id==id;});
 return found==installQueue.end()?nullptr:&*found;
}
const QueuedInstall* HubModel::SelectedQueuedInstall()const{
 if(selected<0||selected>=(int)games.size()||games[selected].target.path.empty())return nullptr;
 const auto& game=games[selected];
 const auto found=std::find_if(installQueue.begin(),installQueue.end(),[&](const auto& item){return !QueueTerminal(item.phase)&&item.game.id==game.id&&GameTargetMatches(item.game.target.path,game.target.path);});
 return found==installQueue.end()?nullptr:&*found;
}
bool HubModel::ReviewQueuedInstall(uint64_t id){
 const auto item=FindQueuedInstall(id);if(!item||item->phase!=QueuedInstall::Phase::NeedsAttention)return false;
 reviewedInstallId=id;showIssue=true;return true;
}
bool HubModel::EditQueuedInstall(uint64_t id){
 const auto item=FindQueuedInstall(id);if(!item||(item->phase!=QueuedInstall::Phase::NeedsAttention&&item->phase!=QueuedInstall::Phase::Queued))return false;
 const auto gameId=item->game.id;CancelQueuedInstall(id);
 const auto cancelled=FindQueuedInstall(id);if(!cancelled||!QueueTerminal(cancelled->phase))return false;
 reviewedInstallId=0;showIssue=false;
 const auto game=std::find_if(games.begin(),games.end(),[&](const auto& value){return value.id==gameId;});
 if(game!=games.end())SelectGame(static_cast<int>(game-games.begin()),true);return true;
}
void HubModel::RetryQueuedInstall(uint64_t id){
 const auto head=CurrentQueuedInstall();if(!head||head->id!=id)return;RetryQueuedInstall();
}
QueuedInstall* HubModel::QueueHead(){return const_cast<QueuedInstall*>(CurrentQueuedInstall());}
const Game* HubModel::OperationGame()const{if(auto head=CurrentQueuedInstall())return &head->game;return selected>=0&&selected<(int)games.size()?&games[selected]:nullptr;}
void HubModel::PauseInstallQueue(const std::string& reason){
 if(auto head=QueueHead()){head->phase=QueuedInstall::Phase::NeedsAttention;head->summary=reason;
  reviewedInstallId=0;operationIssue={{"status","NeedsDecision"},{"decisionKind","QueuedInstall"},{"target",head->game.target.path},{"reason",reason}};showIssue=false;showReview=false;message=head->game.title+": "+reason;}
 installer.ClearAntiCheatApproval();riskAcknowledged=false;
}

NrDllGameChoice HubModel::NrDllChoiceFor(const Game& game)const{
 if(game.target.path.empty())return {};
 const auto key=RootKey(Utf8(std::filesystem::path(Wide(game.target.path)).parent_path().wstring()));const auto found=nrDllChoices.find(key);
 return found==nrDllChoices.end()?NrDllGameChoice{}:found->second;
}
bool HubModel::SetNrDllChoice(const Game& game,NrDllGameChoice choice){
 if(libraryReadOnly||showDataMaintenance||restartForMaintenance||game.target.path.empty())return false;
 const auto key=RootKey(Utf8(std::filesystem::path(Wide(game.target.path)).parent_path().wstring()));
 if(choice.mode!=NrDllChoiceMode::Profile)choice.profileId.clear();nrDllChoices[key]=std::move(choice);Save();return true;
}
bool HubModel::NrDllPending()const{return nrDllAcquisition.valid()||(nrDllManager&&nrDllManager->Busy());}
bool HubModel::NrDllQueueSourcePending()const{const auto head=CurrentQueuedInstall();return head&&nrDllAcquisition.valid()&&nrDllAcquireItem==head->id;}
void HubModel::PollNrDllSources(bool closing){
 if(closing){nrDllAcquireStop.request_stop();if(nrDllManager)nrDllManager->RequestStop();}
 if(nrDllManager)nrDllManager->Poll();
 if(nrDllAcquisition.valid()&&nrDllAcquisition.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
  auto found=std::find_if(installQueue.begin(),installQueue.end(),[&](const auto& item){return item.id==nrDllAcquireItem;});
  try{auto acquired=nrDllAcquisition.get();
   if(found!=installQueue.end()&&!QueueTerminal(found->phase)&&!closing){
    if(!acquired.lease||!NrDllAcquisitionMatches(found->nrDllSnapshot,acquired))throw NrDllChoiceError(NrDllErrorCode::ChangedAsset);
    found->nrDllLease=std::move(acquired.lease);
   }
  }catch(...){if(found!=installQueue.end()&&!QueueTerminal(found->phase)&&!closing){
   found->phase=QueuedInstall::Phase::NeedsAttention;found->summary=found->request.value("operation","")=="UpdateNrDll"?Neurotic::UiMessage("desktop.nrdll.copy_source_unavailable","The queued NR DLL is unavailable. Cancel this item and queue an available profile."):Neurotic::UiMessage("desktop.nrdll.queued_source_unavailable","The queued NR DLL changed or is unavailable. Choose manual installation for this item, or cancel it and queue a new selection.");
   if(found==installQueue.begin()+std::min(installQueueCursor,installQueue.size()))PauseInstallQueue(found->summary);
  }}
  nrDllAcquireItem=0;
 }
 if(closing||nrDllAcquisition.valid()||!nrDllManager)return;
 for(auto& item:installQueue)if(!QueueTerminal(item.phase)&&item.nrDllSnapshot.mode==NrDllCopyMode::Profile&&!item.nrDllLease&&!item.nrDllAcquireAttempted){
  item.nrDllAcquireAttempted=true;nrDllAcquireItem=item.id;nrDllAcquireStop=std::stop_source{};
  const auto root=nrDllManager->Root();const auto id=item.nrDllSnapshot.profile.id;const auto token=nrDllAcquireStop.get_token();
  try{nrDllAcquisition=std::async(std::launch::async,[root,id,token]{return NrDllProfiles(root,false).Acquire(id,token);});}
  catch(...){nrDllAcquireItem=0;item.phase=QueuedInstall::Phase::NeedsAttention;item.summary=item.request.value("operation","")=="UpdateNrDll"?Neurotic::UiMessage("desktop.nrdll.copy_source_unavailable","The queued NR DLL is unavailable. Cancel this item and queue an available profile."):Neurotic::UiMessage("desktop.nrdll.source_not_held","The selected NR DLL is not available for this queued install. Choose the manual option or queue it again.");}
  break;
 }
}
void HubModel::UseManualNrDllForQueue(){
 auto head=QueueHead();if(!head||installer.busy||NrDllQueueSourcePending()||installQueueClosing)return;
 if(head->request.value("operation","")=="UpdateNrDll"){message=Neurotic::UiMessage("desktop.nrdll.choose_copy_profile","Choose a saved NR DLL profile to add or update the file.");return;}
 head->nrDllLease.reset();head->nrDllSnapshot={};head->request["nrDllCopy"]={{"mode","none"}};head->nrDllAcquireAttempted=true;
 ClearQueueDecisionState();head->phase=QueuedInstall::Phase::Queued;AdvanceInstallQueue();
}

bool HubModel::InspectorInstallFor(const Game& game)const{
 if(game.target.path.empty())return false;
 const auto found=gameInstallPreferences.find(GameTargetKey(game.target.path));
 return found!=gameInstallPreferences.end()&&found->second.gameId==game.id&&found->second.installInspector;
}
bool HubModel::SetInspectorInstallFor(const Game& game,bool enabled){
 if(game.target.path.empty())return false;
 const auto key=GameTargetKey(game.target.path);const auto found=gameInstallPreferences.find(key);
 const auto prior=found==gameInstallPreferences.end()?std::optional<GameInstallPreferences>{}:std::optional<GameInstallPreferences>{found->second};
 auto& value=gameInstallPreferences[key];if(value.gameId!=game.id)value=GameInstallPreferences{};value.executable=game.target.path;value.gameId=game.id;value.installInspector=enabled;
 if(Save())return true;
 if(prior)gameInstallPreferences[key]=*prior;else gameInstallPreferences.erase(key);return false;
}
bool HubModel::RememberedRiskFor(const Game& game)const{
 if(game.target.path.empty())return false;
 const auto found=gameInstallPreferences.find(GameTargetKey(game.target.path));
 return found!=gameInstallPreferences.end()&&!game.id.empty()&&found->second.gameId==game.id&&found->second.rememberAntiCheatRisk;
}
bool HubModel::AntiCheatRiskAcceptedFor(const Game& game)const{
 // An accepted warning is evidence, but no longer a pending decision while its
 // exact plan owns the writer. A new warning or terminal failure stays actionable.
 if(!riskAcknowledged||!installer.busy||installer.readOnlyInstaller)return false;
 try{
  if(!operationIssue.is_object()||operationIssue.value("decisionKind",std::string{})!="AntiCheatRisk"||
     !GameTargetMatches(operationIssue.at("target").get<std::string>(),game.target.path)||
     !plan.is_object()||!GameTargetMatches(plan.at("target").at("executable").get<std::string>(),game.target.path))return false;
  const auto fingerprint=plan.at("antiCheat").at("fingerprint").get<std::string>();
  return !fingerprint.empty()&&operationIssue.at("antiCheat").value("fingerprint",std::string{})==fingerprint;
 }catch(const std::exception&){return false;}
}
void HubModel::EnqueueInstall(bool nrDllOnly){
 if(installQueueClosing||launchAdmission||pendingLaunchGame||showDataMaintenance||restartForMaintenance||BulkUninstallBusy()||
    (!InstallQueueBusy()&&installer.busy&&!BackgroundInspectionActive())||
    (!InstallQueueBusy()&&(plan.is_object()||showUninstallConfirm))){
  message=Neurotic::UiMessage("desktop.installqueue.admission_conflict","Finish or cancel the current operation before queueing an install.");return;
 }
 if(selected<0||selected>=(int)games.size()||!games[selected].target.suitable){message=Neurotic::UiMessage("desktop.installqueue.choose_executable","Choose a suitable game executable before queueing an install.");return;}
 const auto proxyName=nrDllOnly?std::string{}:SelectedProxyName();
 if(!nrDllOnly&&proxyName.empty()){message=Neurotic::UiMessage("desktop.neurotic-hubprotocol.choose_a_proxy_filename_febc5774","Choose a proxy filename.");return;}
 auto game=games[selected];const auto fresh=InspectExecutable(Wide(game.target.path));
 if(!fresh.suitable||fresh.bitness!=game.target.bitness){InvalidateTarget(game.target.path);games[selected].target=fresh;throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.game_executable_changed_refresh_and_retry_d783e30f","Game executable changed. Refresh and retry."));}
 const auto target=RootKey(Utf8(std::filesystem::path(Wide(game.target.path)).parent_path().wstring()));
 const auto directoryIdentity=QueueDirectoryIdentity(game.target.path);
 for(const auto& item:installQueue)if(!QueueTerminal(item.phase)&&item.directoryIdentity==directoryIdentity){message=game.title+": "+Neurotic::UiMessage("desktop.installqueue.already_queued","Already queued. Cancel that item before changing its install options.");return;}
 Json request={{"kind","Plan"},{"operation",nrDllOnly?"UpdateNrDll":"Install"},{"gameExecutable",game.target.path},{"gameRoot",game.root},{"gameTitle",game.title},{"gameStore",game.store},{"gameStoreId",game.storeId}};
 if(!nrDllOnly){request["packageId"]="flagship-approved";request["freshInstall"]=freshInstall;request["installInspector"]=InspectorInstallFor(game);
  request["proxyName"]=proxyName;
  // Retain the click-time filename independently from inspection freshness.
  for(int i=0;i<9;++i)if(proxyName==Proxies[i]){ChooseProxy(i);break;}
 }
 if(game.store=="Steam"&&!game.storeId.empty()&&!game.steamRoot.empty()){request["steamAppId"]=game.storeId;request["steamRoot"]=game.steamRoot;}
 auto defaults=nrDllOnly?Json():QueueDefaultsSnapshot();const auto stamp=QueueExecutableStamp(game.target.path);
 NrDllCopySnapshot nrSnapshot;
 try{const auto state=InspectNrDllTarget(std::filesystem::path(Wide(game.target.path)).parent_path());
  const bool needsSelection=nrDllOnly||state==NrDllTargetState::Initial;
  if(nrDllOnly&&NrDllChoiceFor(game).mode==NrDllChoiceMode::Manual){message=Neurotic::UiMessage("desktop.nrdll.choose_copy_profile","Choose a saved NR DLL profile to add or update the file.");return;}
  if(needsSelection&&nrDllManager&&!nrDllManager->Ready()&&NrDllChoiceFor(game).mode!=NrDllChoiceMode::Manual){message=nrDllOnly?Neurotic::UiMessage("desktop.nrdll.copy_catalog_pending","Wait for NR DLL profiles to finish loading."):Neurotic::UiMessage("desktop.nrdll.catalog_pending","Wait for NR DLL profiles to finish loading, or choose manual installation.");return;}
  if(needsSelection&&nrDllManager&&NrDllChoiceFor(game).mode==NrDllChoiceMode::Default&&!nrDllManager->CatalogCurrent()){message=nrDllOnly?Neurotic::UiMessage("desktop.nrdll.copy_profile_unavailable","The selected NR DLL profile is unavailable. Choose an available profile."):Neurotic::UiMessage("desktop.nrdll.default_not_current","The default NR DLL profile could not be confirmed. Wait for the manager, select a specific profile, or choose manual installation.");return;}
  const NrDllCatalog empty;const auto& catalog=needsSelection&&nrDllManager?nrDllManager->Catalog():empty;
  nrSnapshot=nrDllOnly?ResolveNrDllReplacement(NrDllChoiceFor(game),catalog):ResolveNrDllChoice(NrDllChoiceFor(game),catalog,state);
 }catch(const NrDllChoiceError&){if(nrDllOnly){message=Neurotic::UiMessage("desktop.nrdll.copy_profile_unavailable","The selected NR DLL profile is unavailable. Choose an available profile.");return;}message=NrDllChoiceFor(game).mode==NrDllChoiceMode::Default?Neurotic::UiMessage("desktop.nrdll.default_unreadable","The default NR DLL profile is unavailable. Select a specific profile, choose manual installation, or repair the default in the manager."):Neurotic::UiMessage("desktop.nrdll.selection_unavailable","The selected NR DLL is unavailable. Choose another profile or the manual option before installing.");return;}
 request["nrDllCopy"]=NrDllCopyRequest(nrSnapshot);
 QueuedInstall item;item.nrDllSnapshot=std::move(nrSnapshot);item.id=++installQueueSequence;item.game=std::move(game);item.request=std::move(request);item.defaults=std::move(defaults);item.executableStamp=stamp;item.target=target;item.directoryIdentity=directoryIdentity;
 item.summary=Neurotic::UiMessage("desktop.installqueue.queued","Queued");installQueue.push_back(std::move(item));
 PollNrDllSources(false);
 pendingLaunchGame.reset();CancelUninstall();
 message=installQueue.back().game.title+": "+Neurotic::UiMessage("desktop.installqueue.queued","Queued");
 if(BackgroundInspectionActive())installer.Cancel();
 AdvanceInstallQueue();
}
void HubModel::StartInstallQueuePlan(const Json& request){
 auto head=QueueHead();if(!head||installer.busy||installQueueClosing)throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.worker_busy","Wait for the current queued operation to finish."));
 if(request.value("kind","")!="Plan"||request.value("operation","")!=head->request.value("operation","")||GameTargetKey(request.at("gameExecutable").get<std::string>())!=GameTargetKey(head->game.target.path))throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.request_changed","The queued install request changed. Cancel this item and queue it again."));
 for(const auto* key:{"operation","gameExecutable","gameRoot","packageId","freshInstall","installInspector","gameTitle","gameStore","gameStoreId","steamAppId","steamRoot","nrDllCopy"})
  if(request.value(key,Json())!=head->request.value(key,Json()))throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.request_changed","The queued install request changed. Cancel this item and queue it again."));
 if(head->request.contains("proxyName")&&request.value("proxyName",Json())!=head->request.at("proxyName"))throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.options_changed","This item's proxy choice changed. Cancel it and queue the new choice."));
 if(QueueDirectoryIdentity(head->game.target.path)!=head->directoryIdentity)throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.folder_changed","Installation folder changed while queued. Cancel this item and choose it again."));
 if(QueueExecutableStamp(head->game.target.path)!=head->executableStamp)throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.executable_changed","The executable changed while queued. Cancel this item and choose it again."));
 if(head->request.value("operation","")=="Install"&&QueueDefaultsSnapshot()!=head->defaults)throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.defaults_changed","Install defaults changed after this click. Restore those defaults, or cancel this item and queue it with the new defaults."));
 if(head->nrDllSnapshot.mode==NrDllCopyMode::Profile&&!head->nrDllLease)throw std::runtime_error(head->request.value("operation","")=="UpdateNrDll"?Neurotic::UiMessage("desktop.nrdll.copy_source_unavailable","The queued NR DLL is unavailable. Cancel this item and queue an available profile."):Neurotic::UiMessage("desktop.nrdll.source_not_held","The selected NR DLL is not available for this queued install. Choose the manual option or queue it again."));
 pendingPlan=request;plan=Json();showReview=showIssue=false;riskAcknowledged=false;installer.ClearAntiCheatApproval();fileReview.Cancel();requestedOperation=head->request.at("operation").get<std::string>();
 installer.StartInstaller(request);installerTarget=GameTargetKey(head->game.target.path);installQueueOwnsInstaller=true;inspectionRefreshing=false;
 head->phase=QueuedInstall::Phase::Preparing;head->summary=requestedOperation=="UpdateNrDll"?Neurotic::UiMessage("desktop.nrdll.preparing_copy","Preparing NR DLL update"):Neurotic::UiMessage("desktop.installqueue.preparing","Preparing install");message=head->game.title+": "+head->summary;
}
void HubModel::StartInstallQueueExecute(const Json& request){
 auto head=QueueHead();if(!head||installer.busy||installQueueClosing||head->phase!=QueuedInstall::Phase::NeedsAttention||!plan.is_object()||
    request.value("planId",std::string{})!=plan.value("planId",std::string{})||request.value("planFingerprint",std::string{})!=plan.value("planFingerprint",std::string{}))throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.plan_changed","Prepare this queued install again before applying it."));
 if(QueueDirectoryIdentity(head->game.target.path)!=head->directoryIdentity)throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.folder_changed","Installation folder changed while queued. Cancel this item and choose it again."));
 if(QueueExecutableStamp(head->game.target.path)!=head->executableStamp)throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.executable_changed","The executable changed while queued. Cancel this item and choose it again."));
 const bool inspectedTarget=[&]()noexcept{try{return inspection.is_object()&&inspection.contains("target")&&inspection["target"].is_object()&&GameTargetMatches(inspection["target"].value("executable",std::string{}),head->game.target.path);}catch(...){return false;}}();
 installer.StartInstaller(request);installerTarget=GameTargetKey(head->game.target.path);installQueueOwnsInstaller=true;
 head->writeAttempted=true;
 if(inspectedTarget)inspectionCached=true;
 head->phase=QueuedInstall::Phase::Installing;head->summary=head->request.value("operation","")=="UpdateNrDll"?Neurotic::UiMessage("desktop.nrdll.copying","Updating NR DLL"):Neurotic::UiMessage("desktop.installqueue.installing","Installing");message=head->game.title+": "+head->summary;
}
void HubModel::AdvanceInstallQueue(){
 if(installer.busy||installQueueClosing)return;
 while(installQueueCursor<installQueue.size()&&QueueTerminal(installQueue[installQueueCursor].phase)){installQueue[installQueueCursor].nrDllLease.reset();++installQueueCursor;}
 auto head=QueueHead();if(!head){
  if(installQueueRefreshDeferred){installQueueRefreshDeferred=false;RefreshSelected();}return;
 }
 if(head->phase!=QueuedInstall::Phase::Queued)return;
 if(head->nrDllSnapshot.mode==NrDllCopyMode::Profile&&!head->nrDllLease)return;
 try{StartInstallQueuePlan(head->request);}catch(const std::exception& e){PauseInstallQueue(e.what());}
}
void HubModel::AcceptInstallQueueReceipt(const Json& result,bool closing){
 // Async queue outcomes retain their decision on the item; Resolve opens the modal explicitly.
 installQueueOwnsInstaller=false;auto head=QueueHead();if(!head)return;
 const auto phase=head->phase;const bool writerReceipt=phase==QueuedInstall::Phase::Installing&&!installer.readOnlyInstaller;
 try{
  head->result=result;lastResult=result;
  if(writerReceipt){head->writerResult=result;head->writerSummary=Neurotic::UiMessage("desktop.installqueue.needs_attention","Needs attention");}
  if(head->cancelRequested){FinishCancelledQueueItem(*head,false);ClearQueueDecisionState();}
  else{
   if(result.contains("target")){const auto& target=result.at("target");const auto path=target.is_string()?target.get<std::string>():target.at("executable").get<std::string>();if(!GameTargetMatches(path,head->game.target.path))throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.receipt_target_changed","The response belongs to another queued target."));}
   if(result.contains("inspection")&&!GameTargetMatches(result.at("inspection").at("target").at("executable").get<std::string>(),head->game.target.path))throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.receipt_target_changed","The response belongs to another queued target."));
   const auto status=result.value("status",std::string{});
   if(status=="Planned"){
    if(phase!=QueuedInstall::Phase::Preparing||!installer.readOnlyInstaller||!pendingPlan.is_object()||!result.contains("target")||!result.contains("request"))throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.unexpected_receipt","Unexpected queued operation response. Prepare this item again."));
    ValidatePlanIntent(pendingPlan,result,head->game.target.bitness);
    if(result.at("planId").get<std::string>().empty()||result.at("planFingerprint").get<std::string>().empty())throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.plan_changed","Prepare this queued install again before applying it."));
    if(head->request.value("operation","")=="Install"){
     const auto& defaults=result.at("installDefaults");
     if(Json{{"status",defaults.at("status")},{"preferences",defaults.value("preferences",Json())}}!=head->defaults)throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.defaults_changed","Install defaults changed after this click. Restore those defaults, or cancel this item and queue it with the new defaults."));
    }
    plan=result;riskAcknowledged=false;head->phase=QueuedInstall::Phase::NeedsAttention;
    auto risk=result.value("antiCheat",Json::object());
    RetainQueueAntiCheatEvidence(risk,operationIssue,head->game.target.path);
    if(risk.is_object()&&risk.value("acknowledgementRequired",false)){
     reviewedInstallId=0;operationIssue={{"decisionKind","AntiCheatRisk"},{"target",head->game.target.path},{"antiCheat",risk}};showIssue=false;showReview=false;
     head->summary=Neurotic::UiMessage("desktop.installqueue.review_warning","Review this game's anti-cheat warning");
     if(!closing&&RememberedRiskFor(head->game)&&risk.value("scan_status",std::string{})!="cancelled")AcknowledgeRisk();
    }else if(!closing)Execute();
    return;
   }
   if(status=="Succeeded"||status=="SucceededWithNotes"||status=="Partial"){
    if(phase!=QueuedInstall::Phase::Installing||installer.readOnlyInstaller||!plan.is_object()||result.at("planId")!=plan.at("planId"))throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.unexpected_receipt","Unexpected queued operation response. Prepare this item again."));
    const auto completedTarget=installCompletion.Active()?installCompletion.Active()->target:std::string{};
    installCompletion.Observe(result,installer.action,[completedTarget](const std::string& path){return !completedTarget.empty()&&GameTargetMatches(path,completedTarget)?GameTargetKey(completedTarget):GameTargetKey(path);});
    InvalidateTarget(head->game.target.path);
    if(result.contains("inspection")){
     const auto& fresh=result.at("inspection");(void)fresh.at("settings").get_ref<const Json::array_t&>();
     if(selected>=0&&selected<(int)games.size()&&!games[selected].target.path.empty()&&GameTargetMatches(games[selected].target.path,head->game.target.path)){
      inspection=fresh;inspectionTarget=GameTargetKey(games[selected].target.path);settings.clear();ReadInspectionFields();inspectionCached=inspectionRefreshing=false;installQueueRefreshDeferred=false;CacheSelected();
     }
     SaveGameProfile(head->game.target.path,fresh);
    }
    head->summary=ResultMessage(result,status);
    const auto notes=result.value("notes",Json::array());if(notes.is_array())for(const auto& note:notes)if(note.is_string()||note.is_object())head->summary+=" "+ResultDetail(note);
    if(result.contains("inspection"))if(auto notice=AutomaticSetupNotice(result.at("inspection"));!notice.empty())head->summary=notice+" "+head->summary;
    head->phase=status=="Partial"?QueuedInstall::Phase::NeedsAttention:QueuedInstall::Phase::Succeeded;
    if(status=="Partial"){reviewedInstallId=0;operationIssue=result;operationIssue["target"]=head->game.target.path;showIssue=false;}
    else{operationIssue=Json();showIssue=showReview=false;plan=pendingPlan=Json();}
   }else if(status=="Cancelled"){
    if(phase==QueuedInstall::Phase::Installing||!installer.readOnlyInstaller){
     if(phase!=QueuedInstall::Phase::Installing||installer.readOnlyInstaller||!plan.is_object()||!result.contains("planId")||result.at("planId")!=plan.at("planId"))throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.unexpected_receipt","Unexpected queued operation response. Prepare this item again."));
    }else if(phase!=QueuedInstall::Phase::Preparing)throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.unexpected_receipt","Unexpected queued operation response. Prepare this item again."));
    if(installer.readOnlyInstaller&&head->writeAttempted)FinishCancelledQueueItem(*head,false);
    else{head->phase=QueuedInstall::Phase::Cancelled;head->summary=ResultMessage(result,status);}
    ClearQueueDecisionState();
   }else{
    head->phase=QueuedInstall::Phase::NeedsAttention;head->summary=ResultMessage(result,Neurotic::UiMessage("desktop.installqueue.needs_attention","Needs attention"));
    const auto previousIssue=operationIssue;reviewedInstallId=0;operationIssue=result;operationIssue["target"]=head->game.target.path;
    if(operationIssue.value("decisionKind",std::string{})=="AntiCheatRisk"&&operationIssue.value("antiCheat",Json()).is_object())RetainQueueAntiCheatEvidence(operationIssue["antiCheat"],previousIssue,head->game.target.path);
    showIssue=false;showReview=false;plan=Json();riskAcknowledged=false;installer.ClearAntiCheatApproval();
    if(operationIssue.value("decisionKind",std::string{})=="FileConflict")fileReview.Begin(operationIssue.at("fileConflicts"));
   }
  }
  if(QueueTerminal(head->phase))head->nrDllLease.reset();
  if(writerReceipt)head->writerSummary=head->summary;
  message=head->game.title+": "+head->summary;
  if(!closing)AdvanceInstallQueue();
 }catch(const std::exception& e){PauseInstallQueue(e.what());if(writerReceipt)head->writerSummary=head->summary;}
}
void HubModel::CancelQueuedInstall(uint64_t id){
 auto found=std::find_if(installQueue.begin(),installQueue.end(),[&](const auto& item){return item.id==id;});if(found==installQueue.end()||QueueTerminal(found->phase))return;
 if(found==installQueue.begin()+installQueueCursor&&installQueueOwnsInstaller&&installer.busy){
  if(!installer.readOnlyInstaller){message=Neurotic::UiMessage("desktop.installqueue.writer_cannot_cancel","The current installation must finish before this item can close.");return;}
  found->cancelRequested=true;installer.Cancel();return;
 }
 if(nrDllAcquisition.valid()&&nrDllAcquireItem==found->id)nrDllAcquireStop.request_stop();
 FinishCancelledQueueItem(*found,false);
 if(found==installQueue.begin()+installQueueCursor)ClearQueueDecisionState();
 AdvanceInstallQueue();
}
// Removing finished rows changes history only; the active child's immutable ID,
// target and source lease remain owned by the queue until its receipt drains.
bool HubModel::RemoveQueuedInstall(uint64_t id){
 const auto found=std::find_if(installQueue.begin(),installQueue.end(),[&](const auto& item){return item.id==id;});
 if(found==installQueue.end()||!QueueTerminal(found->phase)||
    (installQueueOwnsInstaller&&found==installQueue.begin()+std::min(installQueueCursor,installQueue.size()))||
    (nrDllAcquisition.valid()&&nrDllAcquireItem==id))return false;
 const auto index=static_cast<size_t>(found-installQueue.begin());
 if(index<installQueueCursor)--installQueueCursor;
 installQueue.erase(found);installQueueCursor=std::min(installQueueCursor,installQueue.size());return true;
}
size_t HubModel::ClearInstallQueueHistory(){
 const auto cursor=std::min(installQueueCursor,installQueue.size());
 const auto* current=CurrentQueuedInstall();const auto owned=installQueueOwnsInstaller&&current?current->id:0;
 const auto before=installQueue.size();size_t index=0,removedBefore=0;
 std::erase_if(installQueue,[&](const auto& item){
  const bool remove=QueueTerminal(item.phase)&&item.id!=owned&&!(nrDllAcquisition.valid()&&nrDllAcquireItem==item.id);
  if(remove&&index<cursor)++removedBefore;++index;return remove;
 });
 installQueueCursor=std::min(cursor-removedBefore,installQueue.size());return before-installQueue.size();
}
void HubModel::RetryQueuedInstall(){
 auto head=QueueHead();if(!head||installer.busy||head->phase!=QueuedInstall::Phase::NeedsAttention)return;
 try{const auto request=pendingPlan.is_object()?pendingPlan:head->request;StartInstallQueuePlan(request);}catch(const std::exception& e){PauseInstallQueue(e.what());}
}
void HubModel::StopInstallQueue(){
 if(installQueueClosing)return;installQueueClosing=true;nrDllAcquireStop.request_stop();if(nrDllManager)nrDllManager->RequestStop();
 for(auto& item:installQueue)if(!QueueTerminal(item.phase)){
  if(&item==QueueHead()&&installQueueOwnsInstaller&&installer.busy){if(installer.readOnlyInstaller){item.cancelRequested=true;installer.Cancel();}continue;}
  FinishCancelledQueueItem(item,true);
 }
 installer.ClearAntiCheatApproval();
}
std::string HubModel::QueuedProxyName()const{if(!CurrentQueuedInstall())return {};return pendingPlan.is_object()?pendingPlan.value("proxyName",std::string{}):CurrentQueuedInstall()->request.value("proxyName",std::string{});}
void HubModel::ChooseQueuedProxy(int index){
 auto head=QueueHead();if(!head||installer.busy||index<0||index>=9||operationIssue.value("decisionKind",std::string{})!="ProxyRequired"||head->request.contains("proxyName"))return;
 if(!pendingPlan.is_object())pendingPlan=head->request;
 pendingPlan["proxyName"]=Proxies[index];proxyChoices[GameTargetKey(head->game.target.path)]=index;
 if(selected>=0&&selected<(int)games.size()&&GameTargetMatches(games[selected].target.path,head->game.target.path))proxy=index;
}

static std::string ReadString(const Json& item,const char* key,size_t limit,bool required=false){
 if(!item.contains(key)){if(required)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.missing_catalog_field_f8da7d21", "Missing catalog field"));return {};}
 if(!item[key].is_string())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_catalog_field_type_b86828c4", "Invalid catalog field type"));auto value=item[key].get<std::string>();if(value.size()>limit||(required&&value.empty()))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_catalog_field_length_32b86fed", "Invalid catalog field length"));Wide(value);return value;
}
static std::wstring RootKey(const std::string& root){auto path=std::filesystem::path(Wide(root));if(!path.is_absolute()||path.wstring().starts_with(L"\\\\"))throw std::runtime_error(Neurotic::UiMessage("desktop.gamegrouping.a_local_absolute_game_path_is_required_356caedf", "A local absolute game path is required"));auto key=path.lexically_normal().wstring();while(key.size()>3&&(key.back()==L'\\'||key.back()==L'/'))key.pop_back();std::replace(key.begin(),key.end(),L'/',L'\\');std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c){return (wchar_t)towlower(c);});return key;}
static bool Within(const std::wstring& path,const std::wstring& root){return path==root||path.starts_with(root+L"\\");}
static bool ExistingSearchRoot(const std::string& value){
 auto path=std::filesystem::path(Wide(value));std::error_code error;if(RootKey(value).size()<=3||!std::filesystem::is_directory(path,error)||error)return false;
 for(auto walk=path;!walk.empty();){auto attrs=GetFileAttributesW(walk.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))return false;auto parent=walk.parent_path();if(parent==walk)break;walk=parent;}return true;
}
static std::vector<SearchDirectory> ReadDirectories(const Json& values){
 if(!values.is_array()||values.size()>512)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_search_directories_e671aa8b", "Invalid search directories"));std::vector<SearchDirectory> result;
 for(auto& value:values){SearchDirectory directory{ReadString(value,Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),32768,true),ReadString(value,"source",64,true)};RootKey(directory.path);if(!value.contains("automatic")||!value["automatic"].is_boolean())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_directory_provenance_57b9df8c", "Invalid directory provenance"));directory.automatic=value["automatic"].get<bool>();result.push_back(std::move(directory));}return result;
}
// Parse into local state for both intake and the Save-time preservation check.
// Neither validation path may publish partially validated preferences or games.
struct LibrarySnapshot {
 std::vector<Game> games;std::vector<std::string> roots;
 std::vector<SearchDirectory> directories;std::vector<std::string> exclusions;
 std::map<std::wstring,NrDllGameChoice> nrDllChoices;
 std::map<std::wstring,GameInstallPreferences> gamePreferences;
 int countdown;bool runtime,favorites,light,artwork,motion;
};
static LibrarySnapshot ReadLibrarySnapshot(const Json& data){
 if(!data.contains("schemaVersion")||!data["schemaVersion"].is_number_integer())throw std::runtime_error(Neurotic::UiMessage("desktop.library-intake.unsupported-schema","This library has an unsupported format. Updates are disabled to preserve it."));const auto& schemaValue=data["schemaVersion"];if(schemaValue!=1&&schemaValue!=2&&schemaValue!=3){const bool newerSchema=schemaValue>3;throw std::runtime_error(newerSchema?Neurotic::UiMessage("desktop.hubviewmodel.this_library_was_saved_by_a_newer_app_updates_ar_0e228ae2", "This library was saved by a newer App. Updates are disabled to preserve it."):Neurotic::UiMessage("desktop.library-intake.unsupported-schema","This library has an unsupported format. Updates are disabled to preserve it."));}const int schema=schemaValue.get<int>();
  auto loaded=std::vector<Game>();auto roots=std::vector<std::string>();if(!data.at("games").is_array()||data["games"].size()>10000)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_cached_library_906fe430", "Invalid cached library"));
  auto addRoot=[&](const std::string& root){auto key=RootKey(root);if(std::none_of(roots.begin(),roots.end(),[&](auto& prior){return RootKey(prior)==key;})){if(roots.size()>=64)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.search_folder_limit_reached_9f47927f", "Search folder limit reached"));roots.push_back(root);}};
  if(schema>=2&&data.contains("searchRoots")){if(!data["searchRoots"].is_array()||data["searchRoots"].size()>64)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_search_roots_e199ba3b", "Invalid search roots"));for(auto& value:data["searchRoots"]){if(!value.is_string())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_search_root_f6280aa0", "Invalid search root"));addRoot(value.get<std::string>());}}
  for(auto& item:data.at("games")){Game game{ReadString(item,"id",32768,true),ReadString(item,Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title"),4096,true),ReadString(item,"store",64,true),ReadString(item,"root",32768,true)};RootKey(game.root);auto exe=ReadString(item,"executable",32768);if(!exe.empty())game.target=InspectExecutable(Wide(exe));game.manualTarget=schema==1?game.target.suitable:item.value("manualTarget",false);game.favorite=item.value("favorite",false);game.hidden=item.value("hidden",false);game.storeId=ReadString(item,"storeId",128);game.steamRoot=ReadString(item,"steamRoot",32768);game.iconHint=ReadString(item,"iconPath",32768);if(item.contains("candidates")){if(!item["candidates"].is_array()||item["candidates"].size()>256)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_executable_choices_a0d61d25", "Invalid executable choices"));for(auto& v:item["candidates"]){if(!v.is_string())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_executable_choice_de70d78b", "Invalid executable choice"));auto p=v.get<std::string>();RootKey(p);if(p.size()>32768)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_executable_choice_length_79ae75ac", "Invalid executable choice length"));game.candidates.push_back(p);}}loaded.push_back(std::move(game));}
  for(size_t index=0;index<loaded.size();++index){auto& item=data["games"][index];auto& game=loaded[index];if(!item.contains("executableChoices"))continue;auto& choices=item["executableChoices"];if(!choices.is_array()||choices.size()>256)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_executable_installation_choices_2edfc2c7", "Invalid executable installation choices"));for(auto& value:choices){GameExecutableChoice choice{ReadString(value,Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),32768,true),ReadString(value,"root",32768,true),ReadString(value,"store",64,true),ReadString(value,"storeId",128),ReadString(value,"steamRoot",32768),ReadString(value,"iconPath",32768)};RootKey(choice.path);RootKey(choice.root);if(!choice.steamRoot.empty())RootKey(choice.steamRoot);if(!choice.iconHint.empty())RootKey(choice.iconHint);AddGameChoice(game,std::move(choice));}}
  loaded=GroupLibraryGames(std::move(loaded));
  std::vector<std::string> exclusions;if(data.contains("excludedRoots")){if(!data["excludedRoots"].is_array()||data["excludedRoots"].size()>64)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_excluded_directories_55e0ec87", "Invalid excluded directories"));for(auto& v:data["excludedRoots"]){if(!v.is_string()||v.get_ref<const std::string&>().size()>32768)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_excluded_directory_eb6a117e", "Invalid excluded directory"));RootKey(v.get<std::string>());exclusions.push_back(v.get<std::string>());}}
  // Prior automatic entries mixed game installs and metadata folders. Their
  // meaning cannot safely be inferred from cached games; the next full scan
  // repopulates actual libraries. Explicit custom roots remain configuration.
  std::vector<SearchDirectory> directories;if(data.contains("knownDirectories")){auto prior=ReadDirectories(data["knownDirectories"]);if(schema==3)directories=std::move(prior);else for(auto& directory:prior)if(!directory.automatic)addRoot(directory.path);}
  auto countdown=data.find("anythingCountdownSeconds");int loadedCountdown=3;
  if(countdown!=data.end()&&countdown->is_number_integer()&&*countdown>=1&&*countdown<=30)loadedCountdown=countdown->get<int>();
  // Validate all preferences before publishing any part of this intake.
  auto runtimePreference=data.find("ignoreMissingVCRuntime");
  const bool loadedRuntime=runtimePreference!=data.end()&&runtimePreference->is_boolean()&&runtimePreference->get<bool>();
  const bool loadedFavorites=data.value("favoritesFirst",false);
  const bool loadedLight=data.value("lightTheme",false);
  const bool loadedArtwork=data.value("onlineArtwork",true);
  const bool loadedMotion=data.value("reducedMotion",false);
 std::map<std::wstring,NrDllGameChoice> dllChoices;
 if(data.contains("nrDllSelections")){
  const auto& records=data.at("nrDllSelections");if(!records.is_array()||records.size()>10000)throw std::runtime_error(Neurotic::UiMessage("desktop.nrdll.invalid_preferences","Saved NR DLL selections could not be read. The library was preserved."));
  for(const auto& record:records){const auto key=RootKey(ReadString(record,"directory",32768,true));const auto mode=ReadString(record,"mode",16,true);const auto id=ReadString(record,"profileId",4096);
   if((mode!="default"&&mode!="manual"&&mode!="profile")||(mode=="profile"&&id.empty())||(mode!="profile"&&!id.empty())||dllChoices.contains(key))throw std::runtime_error(Neurotic::UiMessage("desktop.nrdll.invalid_preferences","Saved NR DLL selections could not be read. The library was preserved."));
   dllChoices.emplace(key,NrDllGameChoice{mode=="profile"?NrDllChoiceMode::Profile:mode=="manual"?NrDllChoiceMode::Manual:NrDllChoiceMode::Default,id});
  }
 }
 std::map<std::wstring,GameInstallPreferences> gamePreferences;
 if(data.contains("gameInstallPreferences")){
  const auto& records=data.at("gameInstallPreferences");
  if(!records.is_array()||records.size()>10000)throw std::runtime_error(Neurotic::UiMessage("desktop.installation.preferences_invalid","Saved game install choices could not be read. Your library was preserved."));
  for(const auto& record:records){
   const auto path=ReadString(record,"executable",32768,true);const auto key=GameTargetKey(path);const auto gameId=ReadString(record,"gameId",32768,true);
   if(!record.contains("installInspector")||!record.at("installInspector").is_boolean()||!record.contains("rememberAntiCheatRisk")||!record.at("rememberAntiCheatRisk").is_boolean()||gamePreferences.contains(key))throw std::runtime_error(Neurotic::UiMessage("desktop.installation.preferences_invalid","Saved game install choices could not be read. Your library was preserved."));
   const auto savedProxy=ReadString(record,"proxyName",64);
   if(!savedProxy.empty()&&std::none_of(std::begin(Proxies),std::end(Proxies),[&](const auto* name){return savedProxy==name;}))throw std::runtime_error(Neurotic::UiMessage("desktop.installation.preferences_invalid","Saved game install choices could not be read. Your library was preserved."));
   gamePreferences.emplace(key,GameInstallPreferences{path,gameId,record.at("installInspector").get<bool>(),record.at("rememberAntiCheatRisk").get<bool>(),savedProxy});
  }
 }
 return {std::move(loaded),std::move(roots),std::move(directories),std::move(exclusions),std::move(dllChoices),std::move(gamePreferences),
  loadedCountdown,loadedRuntime,loadedFavorites,loadedLight,loadedArtwork,loadedMotion};
}
void HubModel::Load(){
 nh::startup_probe::Scope startupLoad("model_load_begin","model_load_end");
 loaded=true;
 libraryReadOnly=true;
 nh::startup_probe::Mark("defaults_begin");installDefaults=ReadInstallDefaults(UserRoot()/L"install-defaults.json");nh::startup_probe::Mark("defaults_end");
 // Legacy transport is enumerated incrementally by Poll, never as a startup scan.
 nh::startup_probe::Mark("preflight_spawn_begin");RefreshPreflight();nh::startup_probe::Mark("preflight_spawn_end");
 nh::startup_probe::Scope startupLibrary("library_read_begin","library_read_end");
 try{auto path=UserRoot()/L"library.json";if(!std::filesystem::exists(path)){libraryReadOnly=false;return;}if(std::filesystem::file_size(path)>8388608)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.library_cache_exceeds_its_limit_48238ba6", "Library cache exceeds its limit"));std::ifstream file(path);Json data=Json::parse(file);auto snapshot=ReadLibrarySnapshot(data);
  anythingUi.countdownSeconds=snapshot.countdown;continueWithoutRuntime=snapshot.runtime;
  favoritesFirst=snapshot.favorites;libraryFilter=favoritesFirst?LibraryFilter::FavoritesFirst:LibraryFilter::All;
  visibilityPinnedGameId.clear();light=snapshot.light;onlineArtwork=snapshot.artwork;reducedMotion=snapshot.motion;
  games=std::move(snapshot.games);searchRoots=std::move(snapshot.roots);
  knownDirectories=std::move(snapshot.directories);excludedRoots=std::move(snapshot.exclusions);nrDllChoices=std::move(snapshot.nrDllChoices);gameInstallPreferences=std::move(snapshot.gamePreferences);
  proxyChoices.clear();for(const auto& [key,value]:gameInstallPreferences)for(int i=0;i<9;++i)if(value.proxyName==Proxies[i])proxyChoices[key]=i;
  libraryReadOnly=false;

 }
 catch(const std::exception& e){message=std::string(Neurotic::UiMessage("desktop.hubviewmodel.library_cache_could_not_be_read_90dac63b", "Library cache could not be read: "))+e.what();}
}
bool HubModel::SetInstallDefault(const char* key,const Json& value){
 try{
  if(installDefaults.readOnly||showDataMaintenance||restartForMaintenance)return false;
  installDefaults.preferences=WriteInstallDefault(UserRoot()/L"install-defaults.json",key,value);return true;
 }catch(const std::exception&){installDefaults=ReadInstallDefaults(UserRoot()/L"install-defaults.json");message=Neurotic::UiMessage("desktop.installdefaults.save_failed","Install defaults could not be saved. Your previous defaults remain selected.");return false;}
}
void HubModel::ResetInstallDefaults(){
 try{
  if(installDefaults.readOnly||showDataMaintenance||restartForMaintenance)return;
  Json next={{"schemaVersion",1}};SaveInstallDefaults(UserRoot()/L"install-defaults.json",next);installDefaults.preferences=std::move(next);
 }catch(const std::exception&){installDefaults=ReadInstallDefaults(UserRoot()/L"install-defaults.json");message=Neurotic::UiMessage("desktop.installdefaults.save_failed","Install defaults could not be saved. Your previous defaults remain selected.");}
}
static void CopyUnknownLibraryFields(Json& destination,const Json& previous){
 if(!previous.is_object())return;
 for(auto it=previous.begin();it!=previous.end();++it)if(!destination.contains(it.key()))destination[it.key()]=it.value();
}
static void PreserveLibraryRecords(Json& destination,const Json& previous,const char* identity,bool pathIdentity=false){
 if(!destination.is_array()||!previous.is_array())return;
 // Exact identities can be indexed once. emplace retains the first prior
 // record, matching the old forward search when identities repeat.
 std::map<std::string,Json::const_iterator> priorByKey;
 if(!pathIdentity)for(auto prior=previous.begin();prior!=previous.end();++prior){
  if(prior->is_object()&&prior->contains(identity)&&prior->at(identity).is_string())priorByKey.emplace(prior->at(identity).get<std::string>(),prior);
 }
 for(auto& record:destination){
  if(!record.is_object()||!record.contains(identity)||!record[identity].is_string())continue;
  const auto key=record[identity].get<std::string>();auto prior=previous.end();
  if(pathIdentity){
   prior=std::find_if(previous.begin(),previous.end(),[&](const auto& item){
    if(!item.is_object()||!item.contains(identity)||!item[identity].is_string())return false;
    const auto other=item[identity].template get<std::string>();return RootKey(key)==RootKey(other);
   });
  }else if(const auto found=priorByKey.find(key);found!=priorByKey.end())prior=found->second;
  if(prior==previous.end())continue;
  CopyUnknownLibraryFields(record,*prior);
  if(record.contains("executableChoices")&&prior->contains("executableChoices"))PreserveLibraryRecords(record["executableChoices"],(*prior)["executableChoices"],"path",true);
 }
}
static void PreserveUnknownLibraryFields(Json& next,const std::filesystem::path& path){
 if(!std::filesystem::exists(path))return;
 if(std::filesystem::file_size(path)>8388608)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.library_cache_exceeds_its_limit_48238ba6", "Library cache exceeds its limit"));
 std::ifstream file(path);const auto previous=Json::parse(file);
 // The file can change after a successful or first-run Load. Revalidate every
 // owned field with the same optional/migration semantics before preserving it.
 (void)ReadLibrarySnapshot(previous);
 CopyUnknownLibraryFields(next,previous);
 PreserveLibraryRecords(next["games"],previous["games"],"id");
 if(previous.contains("knownDirectories"))PreserveLibraryRecords(next["knownDirectories"],previous["knownDirectories"],"path",true);
 if(previous.contains("nrDllSelections"))PreserveLibraryRecords(next["nrDllSelections"],previous["nrDllSelections"],"directory",true);
 if(previous.contains("gameInstallPreferences"))PreserveLibraryRecords(next["gameInstallPreferences"],previous["gameInstallPreferences"],"executable");
}
bool HubModel::Save() try{
 if(libraryReadOnly)throw std::runtime_error(Neurotic::UiMessage("desktop.library-intake.save-protected","Library changes were not saved because the existing library could not be validated. Restore a valid library or use App Data Maintenance to recover it."));
 if(showDataMaintenance||restartForMaintenance)return false;
 if(gameInstallPreferences.size()>10000)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.library_changes_were_not_saved_the_library_excee_bb9a0b60", "Library changes were not saved: the library exceeds its size limit."));
 auto root=UserRoot();Json data={{"schemaVersion",3},{"favoritesFirst",favoritesFirst},{"lightTheme",light},{"onlineArtwork",onlineArtwork},{"reducedMotion",reducedMotion},{"searchRoots",searchRoots},{"games",Json::array()}};for(auto& game:games)data["games"].push_back({{"id",game.id},{Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title"),game.title},{"store",game.store},{"root",game.root},{"executable",game.target.path},{"manualTarget",game.manualTarget},{"favorite",game.favorite},{"hidden",game.hidden},{"storeId",game.storeId},{"steamRoot",game.steamRoot},{"iconPath",game.iconHint},{"candidates",game.candidates}});
 for(size_t index=0;index<games.size();++index){auto game=games[index];NormalizeGameChoices(game);data["games"][index]["candidates"]=game.candidates;auto& choices=data["games"][index]["executableChoices"]=Json::array();for(auto& choice:game.executableChoices)choices.push_back({{Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),choice.path},{"root",choice.root},{"store",choice.store},{"storeId",choice.storeId},{"steamRoot",choice.steamRoot},{"iconPath",choice.iconHint}});}
 data["ignoreMissingVCRuntime"]=continueWithoutRuntime;
 data["anythingCountdownSeconds"]=std::clamp(anythingUi.countdownSeconds,1,30);
 data["excludedRoots"]=excludedRoots;data["knownDirectories"]=Json::array();for(auto& directory:knownDirectories)data["knownDirectories"].push_back({{Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),directory.path},{"source",directory.source},{"automatic",directory.automatic}});
 data["nrDllSelections"]=Json::array();for(const auto& [directory,choice]:nrDllChoices)data["nrDllSelections"].push_back({{"directory",Utf8(directory)},{"mode",choice.mode==NrDllChoiceMode::Profile?"profile":choice.mode==NrDllChoiceMode::Manual?"manual":"default"},{"profileId",choice.profileId}});
 data["gameInstallPreferences"]=Json::array();for(const auto& [key,value]:gameInstallPreferences)data["gameInstallPreferences"].push_back({{"executable",value.executable},{"gameId",value.gameId},{"installInspector",value.installInspector},{"rememberAntiCheatRisk",value.rememberAntiCheatRisk},{"proxyName",value.proxyName}});
 PreserveUnknownLibraryFields(data,root/L"library.json");
 auto serialized=data.dump(2);if(serialized.size()>8388608)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.library_changes_were_not_saved_the_library_excee_bb9a0b60", "Library changes were not saved: the library exceeds its size limit."));
 SaveUserFile(root/L"library.json",serialized);return true;
}catch(const std::exception& e){message=e.what();return false;}
void HubModel::CacheSelected(){
 if(selected<0||selected>=(int)games.size()||inspection.is_null()||inspectionTarget!=GameTargetKey(games[selected].target.path))return;
 auto& cached=gameSession[inspectionTarget];if(!cached.generation)cached.generation=++sessionGeneration;
 cached.inspection=inspection;cached.settings=settings;cached.proxy=proxy;cached.message=message;cached.diagnosticsText=gameDiagnosticsText;cached.diagnosticsStatus=gameDiagnosticsStatus;
}
void HubModel::InvalidateTarget(const std::string& target){
 if(target.empty())return;RemoveGameProfile(target);auto key=GameTargetKey(target);gameSession.erase(key);
 if(inspectionFailureTarget==key){inspectionFailureTarget.clear();inspectionFailureReason.clear();}
 if(inspectionTarget==key){inspectionCached=false;profileCheckedUtc.clear();inspection=Json();settings.clear();inspectionTarget.clear();gameDiagnosticsText.clear();gameDiagnosticsStatus.clear();}
}
void HubModel::StartSelectedOperation(const Json& request){
 // Browsing owns the visible draft immediately; the old reader keeps its child until Poll drains it.
 if(request.value("kind",std::string{})=="Inspect"&&installer.busy&&installer.readOnlyInstaller&&!installQueueOwnsInstaller){installQueueRefreshDeferred=true;inspectionRefreshing=false;return;}
 if(InstallQueueBusy()){
  const auto kind=request.value("kind",std::string{});
  if(kind=="Inspect"){installQueueRefreshDeferred=true;inspectionRefreshing=false;return;}
  try{
   if(kind=="Plan"&&(request.value("operation",std::string{})=="Install"||request.value("operation",std::string{})=="UpdateNrDll")){StartInstallQueuePlan(request);return;}
   if(kind=="Execute"&&(requestedOperation=="Install"||requestedOperation=="UpdateNrDll")){StartInstallQueueExecute(request);return;}
  }catch(const std::exception& e){PauseInstallQueue(e.what());throw;}
  throw std::runtime_error(Neurotic::UiMessage("desktop.installqueue.operation_conflict","Finish or cancel queued installs before another game operation."));
 }
 if(launchAdmission&&request.value("kind","")!="LaunchPreflight")throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.close_the_launched_game_or_wait_for_steam_launch_e3974615", "Close the launched game or wait for Steam launch handoff before changing this installation."));
 if(showDataMaintenance||restartForMaintenance)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.finish_app_data_maintenance_before_another_opera_a80a0349", "Finish App data maintenance before another operation."));
 if(BulkUninstallBusy())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.finish_or_cancel_the_all_games_uninstall_before__1b8ea243", "Finish or cancel the all-games uninstall before another game operation."));
 if(installer.busy)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.wait_for_the_current_operation_to_finish_a856a08d", "Wait for the current operation to finish."));
 if(request.contains("operation"))requestedOperation=request.at("operation").get<std::string>();
 else if(request.value("kind","")=="Inspect")requestedOperation.clear();
 auto payload=request;if(selected>=0&&selected<(int)games.size()&&request.value("kind","")=="Plan"&&(request.value("operation","")==Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install")||request.value("operation","")=="SaveSettings")){
  auto& game=games[selected];payload["gameRoot"]=game.root;if(game.store=="Steam"&&!game.storeId.empty()&&!game.steamRoot.empty()){payload["steamAppId"]=game.storeId;payload["steamRoot"]=game.steamRoot;}
 }if(request.value("kind","")=="Plan"){pendingPlan=payload;riskAcknowledged=false;plan=Json();showReview=false;showIssue=false;}
 installer.StartInstaller(payload);inspectionRefreshing=request.value("kind","")=="Inspect";
 if(inspectionRefreshing){inspectionFailureTarget.clear();inspectionFailureReason.clear();}
 installerTarget=selected>=0&&selected<(int)games.size()?GameTargetKey(games[selected].target.path):std::wstring();
}
bool HubModel::TrySelectLibraryGame(int index){
 if(index<0||index>=(int)games.size()||BulkUninstallBusy()||(installer.busy&&!installer.readOnlyInstaller&&!installQueueOwnsInstaller))return false;
 Select(index);
 return selected==index||pendingSelection==index;
}
void HubModel::Select(int index){SelectGame(index,false);}
void HubModel::SelectGame(int index,bool reload) try{
 if(BulkUninstallBusy()){message=Neurotic::UiMessage("desktop.hubviewmodel.finish_or_cancel_the_all_games_uninstall_before__152ff37e", "Finish or cancel the all-games uninstall before changing game selection.");return;}
 if(index < -1 || index >= (int)games.size())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.game_selection_is_unavailable_b4fdfa70", "Game selection is unavailable"));
 if(!reload&&index==selected){if(pendingSelection!=SupersededReader){pendingSelection=-2;pendingSelectionReload=false;}return;}
 if(pendingSelection==index&&pendingSelectionReload==reload)return;
 pendingLaunchGame.reset();
 if(installer.busy&&!installQueueOwnsInstaller){if(!installer.readOnlyInstaller){message=Neurotic::UiMessage("desktop.hubviewmodel.wait_for_the_current_operation_before_changing_t_7210e1c1", "Wait for the current operation before changing the selected game.");return;}pendingSelection=SupersededReader;pendingSelectionReload=false;installer.Cancel();if(installer.action=="LaunchPreflight")launchAdmission.reset();}
 if(pendingSelection!=SupersededReader){pendingSelection=-2;pendingSelectionReload=false;}freshInstall=false;if(!InstallQueueBusy())pendingPlan=Json();defaultsRequest=Json();CancelUninstall();sanitizeRevealed=dumbfireInstall=false;showSanitize=false;sanitizeConfirmed=false;sanitizeReviewedRequest=Json();if(!InstallQueueBusy())riskAcknowledged=false;inspectionRefreshing=false;inspectionFailureTarget.clear();inspectionFailureReason.clear();decision=0;historyCleanupNotice.clear();
 if(index<0||selected<0||selected>=(int)games.size()||games[index].id!=games[selected].id)visibilityPinnedGameId.clear();
 if(!InstallQueueBusy())installer.ClearAntiCheatApproval();CacheSelected();selected=index;inspection=Json();inspectionTarget.clear();settings.clear();if(!InstallQueueBusy()){plan=Json();showReview=false;operationIssue=Json();showIssue=false;requestedOperation.clear();}proxy=0;gameDiagnosticsText.clear();gameDiagnosticsStatus.clear();gameDiagnosticsBusy=false;bundleBusy=false;bundlePath.clear();bundleStatus.clear();inspectionCached=false;profileCheckedUtc.clear();
 if(index>=0&&!games[index].target.path.empty()){auto previousBitness=games[index].target.bitness;games[index].target=InspectExecutable(Wide(games[index].target.path));if(!games[index].target.suitable||previousBitness!=games[index].target.bitness)InvalidateTarget(games[index].target.path);}
 if(index>=0&&games[index].target.suitable){
  auto key=GameTargetKey(games[index].target.path);auto found=gameSession.find(key);
  if(found!=gameSession.end()){auto& saved=found->second.inspection;const bool diagnosticsOnly=saved.is_null()&&found->second.generation!=0;if(!diagnosticsOnly&&(!saved.is_object()||!saved.contains("target")||!saved["target"].is_object()||!GameTargetMatches(saved["target"].value("executable",std::string()),games[index].target.path)||saved["target"].value("bitness",64)!=games[index].target.bitness)){gameSession.erase(found);found=gameSession.end();}}
  if(found==gameSession.end())if(auto saved=LoadGameProfile(games[index].target.path)){try{inspection=saved->inspection;inspectionTarget=key;ReadInspectionFields();inspectionCached=true;profileCheckedUtc=saved->checkedUtc;CacheSelected();found=gameSession.find(key);}catch(...){RemoveGameProfile(games[index].target.path);gameSession.erase(key);found=gameSession.end();inspection=Json();inspectionTarget.clear();settings.clear();inspectionCached=false;profileCheckedUtc.clear();}}
  bundleBusy=bundleWorker.valid()&&bundleTarget==key;if(auto bundle=exportedBundles.find(key);bundle!=exportedBundles.end()){bundlePath=bundle->second.success?bundle->second.path:"";bundleStatus=bundle->second.status;}
  gameDiagnosticsBusy=diagnosticsWorker.valid()&&diagnosticsTarget==key;
  if(found!=gameSession.end()){gameDiagnosticsText=found->second.diagnosticsText;gameDiagnosticsStatus=found->second.diagnosticsStatus;}
  if(found!=gameSession.end()&&!found->second.inspection.is_null()){auto& cached=found->second;inspection=cached.inspection;inspectionTarget=key;settings=cached.settings;proxy=cached.proxy;message=Neurotic::UiMessage("desktop.hubviewmodel.showing_this_game_s_saved_status_refresh_reloads_e875336b", "Showing this game's saved status. Refresh reloads its installation status.");inspectionCached=true;StartSelectedOperation({{"kind","Inspect"},{"gameExecutable",games[index].target.path}});inspectionRefreshing=!InstallQueueBusy();message=Neurotic::UiMessage("desktop.hubviewmodel.showing_saved_game_profile_while_refreshing_inst_ffd0ca1a", "Showing saved game profile while refreshing installation status...");}
  else{StartSelectedOperation({{"kind","Inspect"},{"gameExecutable",games[index].target.path}});message=Neurotic::UiMessage("desktop.hubviewmodel.loading_installation_status_8b37e9df", "Loading installation status...");}
 }UpdateInspectionIssue();if(auto notice=AutomaticSetupNotice(inspection);!notice.empty())message=notice+" "+message;
}catch(const std::exception& e){message=e.what();}
std::string HubModel::SelectedInspectionFailure() const try{
 if(selected>=0&&selected<(int)games.size()&&!inspectionFailureTarget.empty()&&GameTargetKey(games[selected].target.path)==inspectionFailureTarget)return inspectionFailureReason;
 return {};
}catch(...){return {};}
void HubModel::RefreshSelected() try{
 if(InstallQueueBusy()||installer.busy||BulkUninstallBusy()||selected<0||selected>=(int)games.size()||!games[selected].target.suitable)return;
 // Retain the last readable snapshot while the explicit, verified read runs.
 // It is advisory until the worker returns; a failed refresh must not erase it.
 inspectionCached=inspection.is_object();plan=Json();showReview=false;operationIssue=Json();showIssue=false;gameDiagnosticsBusy=diagnosticsWorker.valid()&&diagnosticsTarget==GameTargetKey(games[selected].target.path);
 auto cached=gameSession.find(GameTargetKey(games[selected].target.path));if(cached!=gameSession.end()){cached->second.generation=++sessionGeneration;cached->second.diagnosticsText.clear();cached->second.diagnosticsStatus.clear();}gameDiagnosticsText.clear();gameDiagnosticsStatus.clear();
 StartSelectedOperation({{"kind","Inspect"},{"gameExecutable",games[selected].target.path}});message=Neurotic::UiMessage("desktop.hubviewmodel.reading_game_installation_details_6bf2d742", "Reading game installation details...");
}catch(const std::exception& e){message=e.what();}
Json HubModel::SelectedInspectionSnapshot() const try{
 if(selected<0||selected>=(int)games.size()||!inspection.is_object())return Json();
 const auto key=GameTargetKey(games[selected].target.path);
 if(inspectionTarget!=key||!GameTargetMatches(inspection.at("target").at("executable").get<std::string>(),games[selected].target.path))return Json();
 return inspection;
}catch(...){return Json();}
bool HubModel::CanCollectGameDiagnostics() const try{
 return (!installer.busy||BackgroundInspectionActive())&&!BulkUninstallBusy()&&selected>=0&&selected<(int)games.size()&&games[selected].target.suitable&&!games[selected].target.path.empty();
}catch(...){return false;}
void HubModel::CollectGameDiagnostics() try{
 if(!CanCollectGameDiagnostics()){gameDiagnosticsStatus=(installer.busy||BulkUninstallBusy())?Neurotic::UiMessage("desktop.hubviewmodel.wait_for_the_current_operation_to_finish_a856a08d", "Wait for the current operation to finish."):Neurotic::UiMessage("desktop.hubshell.required_choose_the_game_executable_to_unlock_it_4281569b", "Required: choose the game executable to unlock its controls.");return;}
 if(diagnosticsWorker.valid()){gameDiagnosticsStatus=Neurotic::UiMessage("desktop.hubviewmodel.a_game_diagnostics_read_is_already_running_18fdf3bc", "A game diagnostics read is already running.");return;}
 CacheSelected();auto target=games[selected].target.path;diagnosticsTarget=GameTargetKey(target);
 auto& session=gameSession[diagnosticsTarget];if(!session.generation)session.generation=++sessionGeneration;diagnosticsGeneration=session.generation;
 auto snapshot=SelectedInspectionSnapshot();
 diagnosticsWorker=std::async(std::launch::async,[target=std::move(target),snapshot=std::move(snapshot)]{return ReadGameDiagnostics(target,snapshot);});
 gameDiagnosticsBusy=true;gameDiagnosticsStatus=Neurotic::UiMessage("desktop.hubviewmodel.reading_the_selected_game_s_log_1b21dd37", "Reading the selected game's log...");
}catch(const std::exception& e){gameDiagnosticsBusy=false;gameDiagnosticsStatus=e.what();}
void HubModel::PollGameDiagnostics(){
 if(!diagnosticsWorker.valid()||diagnosticsWorker.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;
 GameDiagnosticsResult result;try{result=diagnosticsWorker.get();}catch(const std::exception& e){result.status=e.what();}
 auto found=gameSession.find(diagnosticsTarget);bool current=found!=gameSession.end()&&found->second.generation==diagnosticsGeneration;
 if(current){found->second.diagnosticsText=result.text;found->second.diagnosticsStatus=result.status;}
 if(selected>=0&&selected<(int)games.size()&&!games[selected].target.path.empty()&&GameTargetKey(games[selected].target.path)==diagnosticsTarget){gameDiagnosticsBusy=false;if(current){gameDiagnosticsText=result.text;gameDiagnosticsStatus=result.status;}}
}
void HubModel::Add(const std::filesystem::path& path,const std::string& store) try{
 if(installer.busy||BulkUninstallBusy())return;
 auto target=InspectExecutable(path);if(!target.suitable){message=target.reason;return;}
 auto it=std::find_if(games.begin(),games.end(),[&](auto& game){NormalizeGameChoices(game);return std::any_of(game.executableChoices.begin(),game.executableChoices.end(),[&](auto& choice){return GamePathKey(choice.path)==GamePathKey(target.path);});});
 if(it==games.end()){
  std::vector<size_t> enclosing;bool ambiguous=false;
  for(size_t i=0;i<games.size();++i)if(GamePathWithin(target.path,games[i].root)){for(auto prior:enclosing)if(!SameGameIdentity(games[prior],games[i]))ambiguous=true;enclosing.push_back(i);}
  if(!ambiguous&&!enclosing.empty())it=games.begin()+enclosing.front();
 }
 if(it==games.end()){games.push_back({target.path,target.name,store,Utf8(std::filesystem::path(Wide(target.path)).parent_path().wstring()),target});games.back().manualTarget=true;NormalizeGameChoices(games.back());Select((int)games.size()-1);}else SetExecutable((int)(it-games.begin()),path);page=1;Save();
}catch(const std::exception& e){message=e.what();}
void HubModel::Scan() try{if(discovery.busy)return;scanFolder.clear();discovery.StartDiscovery({{"protocolVersion",2},{"kind","DiscoverGames"},{"roots",searchRoots},{"excludedRoots",excludedRoots}});message=Neurotic::UiMessage("desktop.hubviewmodel.finding_games_across_your_libraries_6b9f5be7", "Finding games across your libraries...");}catch(const std::exception& e){message=e.what();}
void HubModel::ScanDirectory(const std::string& path)try{if(discovery.busy)return;RootKey(path);scanFolder=path;discovery.StartDiscovery({{"protocolVersion",2},{"kind","DiscoverFolder"},{"roots",Json::array({path})},{"excludedRoots",excludedRoots}});message=Neurotic::UiMessage("desktop.hubviewmodel.searching_only_the_selected_folder_fa32efa7", "Searching only the selected folder...");}catch(const std::exception& e){message=e.what();}
void HubModel::CancelScan(){discovery.Cancel();message=Neurotic::UiMessage("desktop.hubviewmodel.stopping_scan_32020ea5", "Stopping scan...");}
void HubModel::AddDirectory(const std::filesystem::path& path)try{if(libraryReadOnly){Save();return;}if(discovery.busy)return;auto value=Utf8(std::filesystem::absolute(path).wstring());auto key=RootKey(value);if(key.size()<=3||!std::filesystem::is_directory(path))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.choose_a_game_folder_or_library_directory_26967ecb", "Choose a game folder or library directory"));for(auto walk=path;!walk.empty();){auto attrs=GetFileAttributesW(walk.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.linked_search_folders_are_unavailable_eb16ff50", "Linked search folders are unavailable"));auto parent=walk.parent_path();if(parent==walk)break;walk=parent;}if(std::none_of(searchRoots.begin(),searchRoots.end(),[&](auto& root){return RootKey(root)==key;})){if(searchRoots.size()>=64)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.search_folder_limit_reached_9f47927f", "Search folder limit reached"));searchRoots.push_back(value);}std::erase_if(excludedRoots,[&](auto& root){return RootKey(root)==key;});Save();ScanDirectory(value);}catch(const std::exception& e){message=e.what();}
std::vector<SearchDirectory> HubModel::SearchDirectories() const{
 std::vector<SearchDirectory> result;auto add=[&](SearchDirectory directory){auto key=RootKey(directory.path);if(directory.automatic&&!ExistingSearchRoot(directory.path))return;if(std::any_of(excludedRoots.begin(),excludedRoots.end(),[&](auto& root){return Within(key,RootKey(root));}))return;auto found=std::find_if(result.begin(),result.end(),[&](auto& item){return RootKey(item.path)==key;});if(found==result.end())result.push_back(std::move(directory));else if(directory.automatic)*found=std::move(directory);};
 for(auto& path:searchRoots)add({path,"Custom",false});for(auto& directory:knownDirectories)add(directory);return result;
}
void HubModel::RemoveDirectory(int index)try{if(libraryReadOnly){Save();return;}if(discovery.busy)return;auto directories=SearchDirectories();if(index<0||index>=(int)directories.size())return;auto directory=directories[index];auto key=RootKey(directory.path);if(std::none_of(excludedRoots.begin(),excludedRoots.end(),[&](auto& root){return RootKey(root)==key;})){if(excludedRoots.size()>=64)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.excluded_folder_limit_reached_2298c8b4", "Excluded folder limit reached"));excludedRoots.push_back(directory.path);}std::erase_if(searchRoots,[&](auto& root){return RootKey(root)==key;});std::erase_if(knownDirectories,[&](auto& item){return RootKey(item.path)==key;});selectedDirectory=-1;Save();message=Neurotic::UiMessage("desktop.hubviewmodel.directory_removed_from_future_scans_existing_gam_8e754d3e", "Directory removed from future scans. Existing games remain in your library.");}catch(const std::exception& e){message=e.what();}
void HubModel::SetExecutable(int index,const std::filesystem::path& path)try{if(libraryReadOnly){Save();return;}
 if(index<0||index>=(int)games.size()||(installer.busy&&!installQueueOwnsInstaller)||BulkUninstallBusy())return;auto target=InspectExecutable(path);if(!target.suitable){message=target.reason;return;}
 CacheSelected();auto& game=games[index];NormalizeGameChoices(game);
 if(!ApplyGameChoice(game,target.path)){auto context=GameChoiceContext(game,target.path,target.bitness);if(!GamePathWithin(target.path,game.root)){context.root=Utf8(std::filesystem::path(Wide(target.path)).parent_path().wstring());context.store=Neurotic::UiLiteral("desktop.anythingview.manual_9ca08eb3", "Manual");context.storeId.clear();context.steamRoot.clear();context.iconHint.clear();}AddGameChoice(game,std::move(context));ApplyGameChoice(game,target.path);}
 game.target=target;game.manualTarget=true;game.candidates.clear();for(auto& choice:game.executableChoices)game.candidates.push_back(choice.path);Save();SelectGame(index,true);
}catch(const std::exception& e){message=e.what();}
void HubModel::MergeDiscovery(const Json& result){
 if(result.value("protocolVersion",0)!=2||result.value("kind","")!="DiscoveryResult"||!result.at("games").is_array()||result["games"].size()>10000||!result.at(Neurotic::UiLiteral("desktop.hubshell.errors_5a41a7c1", "errors")).is_array()||result[Neurotic::UiLiteral("desktop.hubshell.errors_5a41a7c1", "errors")].size()>64)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_discovery_receipt_e29f0ddb", "Invalid discovery receipt"));
 auto merged=games;for(auto& game:merged)NormalizeGameChoices(game);auto directories=knownDirectories;if(result.contains("directories")){auto incoming=ReadDirectories(result["directories"]);if(scanFolder.empty())directories=std::move(incoming);else{for(auto& directory:incoming){if(RootKey(directory.path)!=RootKey(scanFolder))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.folder_scan_reported_another_directory_42fc26aa", "Folder scan reported another directory"));auto it=std::find_if(directories.begin(),directories.end(),[&](auto& old){return RootKey(old.path)==RootKey(directory.path);});if(it==directories.end()){if(directories.size()<512)directories.push_back(std::move(directory));}else if(!it->automatic||directory.automatic)*it=std::move(directory);}}}
 for(auto& item:result["games"]){
  Game incoming{ReadString(item,"id",128,true),ReadString(item,Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title"),4096,true),ReadString(item,"store",64,true),ReadString(item,"installRoot",32768,true)};
  auto key=RootKey(incoming.root);if(!scanFolder.empty()&&!Within(key,RootKey(scanFolder)))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.folder_scan_returned_a_game_outside_its_director_313f811b", "Folder scan returned a game outside its directory"));incoming.storeId=ReadString(item,"storeId",128);incoming.steamRoot=ReadString(item,"steamRoot",32768);incoming.iconHint=ReadString(item,"iconPath",32768);auto exe=ReadString(item,"executable",32768);auto reason=ReadString(item,Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),1024);
  if(!incoming.steamRoot.empty())RootKey(incoming.steamRoot);if(!incoming.iconHint.empty())RootKey(incoming.iconHint);
  auto candidates=item.value("executableCandidates",Json::array());if(candidates.is_null())candidates=Json::array();if(!candidates.is_array()||candidates.size()>64)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_executable_candidates_39760f07", "Invalid executable candidates"));
  for(auto& candidate:candidates){if(!candidate.is_string())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_executable_candidate_type_863916c3", "Invalid executable candidate type"));auto path=candidate.get<std::string>();if(path.size()>32768||!RootKey(path).starts_with(key+L"\\"))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.executable_candidate_escapes_installation_62f175b2", "Executable candidate escapes installation"));incoming.candidates.push_back(path);}
  if(!exe.empty()){if(!RootKey(exe).starts_with(key+L"\\"))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.executable_escapes_installation_d0bcb609", "Executable escapes installation"));incoming.target=InspectExecutable(Wide(exe));}else incoming.target.reason=reason;
  NormalizeGameChoices(incoming);auto it=std::find_if(merged.begin(),merged.end(),[&](auto& game){return SameGameIdentity(game,incoming);});
  // Preserve the complete selected snapshot until the mutation owner returns.
  if(it!=merged.end()&&installer.busy&&!installer.readOnlyInstaller&&selected>=0&&it-merged.begin()==selected)continue;
  if(it==merged.end()){if(merged.size()<10000)merged.push_back(std::move(incoming));}else MergeGameEntry(*it,std::move(incoming));
 }
 for(auto& e:result[Neurotic::UiLiteral("desktop.hubshell.errors_5a41a7c1", "errors")])if(!e.is_string()||e.get_ref<const std::string&>().size()>512)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_discovery_warning_7e0e117f", "Invalid discovery warning"));
 const auto selectedId=selected>=0&&selected<(int)games.size()?games[selected].id:std::string();
 if(!installer.busy)merged=GroupLibraryGames(std::move(merged),selectedId);
 bool changed=false;int nextSelection=selected;
 for(size_t i=0;i<games.size();++i){auto found=std::find_if(merged.begin(),merged.end(),[&](auto& game){return game.id==games[i].id;});if(found==merged.end())continue;if((int)i==selected)nextSelection=(int)(found-merged.begin());if(games[i].target.path!=found->target.path||games[i].target.suitable!=found->target.suitable||games[i].target.bitness!=found->target.bitness){InvalidateTarget(games[i].target.path);InvalidateTarget(found->target.path);if((int)i==selected)changed=true;}}
 games=std::move(merged);knownDirectories=std::move(directories);selectedDirectory=-1;selected=nextSelection;if(changed)SelectGame(selected,true);
}
std::string HubModel::SelectedProxyName() const{
 if(selected<0||selected>=(int)games.size())return {};
 const auto& game=games[selected];if(!game.target.suitable||game.target.path.empty())return {};
 const auto choice=proxyChoices.find(GameTargetKey(game.target.path));
 if(choice!=proxyChoices.end()&&choice->second>=0&&choice->second<9)return Proxies[choice->second];
 if(inspection.is_object()&&inspection.contains("target")&&inspection["target"].is_object()&&GameTargetMatches(inspection["target"].value("executable",std::string{}),game.target.path)&&inspection.contains("state")&&inspection["state"].is_object()){
  const auto& state=inspection["state"];
  for(const auto* key:{"proxy","selected_proxy"})if(state.contains(key)&&state[key].is_string())
   for(const auto* name:Proxies)if(state[key].get_ref<const std::string&>()==name)return name;
 }
 const auto recommended=RecommendedProxy(game);
 for(const auto* name:Proxies)if(recommended==name)return recommended;
 return game.target.proxyRecommendation;
}
void HubModel::ReadInspectionFields(){settings.clear();proxy=0;if(inspection.contains("settings")&&!inspection["settings"].is_array())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_settings_projection_b8b16dd0", "Invalid settings projection"));
   if(inspection.contains("settings"))for(auto& value:inspection["settings"]){SettingsField field;field.section=value.value("section","");field.key=value.value("key","");field.group=value.value("group","");field.available=value.value("available",false);field.label=value.value("label",field.key);field.labelId=value.contains("labelId")&&value["labelId"].is_string()?value["labelId"].get<std::string>():std::string{};field.descriptionId=value.contains("descriptionId")&&value["descriptionId"].is_string()?value["descriptionId"].get<std::string>():std::string{};if(value.contains("valueLabelIds")&&value["valueLabelIds"].is_object())for(auto& entry:value["valueLabelIds"].items())if(entry.value().is_string())field.valueLabelIds[entry.key()]=entry.value().get<std::string>();field.nativeManualScale=value.value("nativeManualScale","0.25");field.originalNativeManualScale=field.nativeManualScale;field.type=value.value("type","");field.description=value.value("description","");if(value.contains("minimum")&&value["minimum"].is_number()&&value.contains("maximum")&&value["maximum"].is_number()){field.hasRange=true;field.minimum=value["minimum"].get<double>();field.maximum=value["maximum"].get<double>();}if(value.contains("valueLabels")&&value["valueLabels"].is_object())for(auto& entry:value["valueLabels"].items())if(entry.value().is_string())field.valueLabels[entry.key()]=entry.value().get<std::string>();field.original=value["value"].is_string()?value["value"].get<std::string>():Neurotic::UiLiteral("desktop.anythingview.unavailable_48a4b800", "Unavailable");field.profileDraft=field.original;field.available=field.available&&(field.type=="objectrules"?field.original.size()<=4194304:field.original.size()<field.draft.size())&&field.original.find('\0')==std::string::npos;strncpy_s(field.draft.data(),field.draft.size(),field.original.c_str(),_TRUNCATE);for(auto& choice:value["values"])field.choices.push_back(choice);field.resetKnown=value.contains("resetKnown")&&value["resetKnown"].is_boolean()&&value["resetKnown"].get<bool>()&&value.contains("resetValue")&&value["resetValue"].is_string();if(field.resetKnown){field.resetValue=value["resetValue"].get<std::string>();const bool nativeResolution=field.section=="DlssNr"&&field.key=="NativeResolutionChoice";const bool manualMetadata=value.contains("resetNativeManualScale")&&value["resetNativeManualScale"].is_string();if(manualMetadata)field.resetNativeManualScale=value["resetNativeManualScale"].get<std::string>();field.resetKnown=(!nativeResolution||manualMetadata)&&field.resetValue.size()<field.draft.size()&&field.resetValue.find('\0')==std::string::npos&&field.resetNativeManualScale.size()<field.draft.size()&&field.resetNativeManualScale.find('\0')==std::string::npos&&(field.choices.empty()||std::find(field.choices.begin(),field.choices.end(),field.resetValue)!=field.choices.end());}settings.push_back(field);}
   const auto proxyName=SelectedProxyName();for(int i=0;i<9;i++)if(proxyName==Proxies[i])proxy=i;

}
void HubModel::Poll(){PollImpl(false);}
void HubModel::PollClosing(){PollImpl(true);}
bool HubModel::DiagnosticsPending()const{return diagnosticsWorker.valid()&&diagnosticsWorker.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready;}
bool HubModel::BundlePending()const{return bundleWorker.valid()&&bundleWorker.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready;}
void HubModel::PollImpl(bool closing){
 // This scope ends after every receipt handler, early return and error handler.
 // The exact current/pending Plan is protected before completed transports can
 // become housekeeping candidates. A same-poll Execute may already own a new job.
 auto finishConsumers=[&]{
  const auto root=UserRoot();std::set<std::string> refs;
  const auto keep=[&](const Json& value){if(value.is_object()&&value.contains("planId")&&value["planId"].is_string()){
   const auto id=value["planId"].get<std::string>();if(DiagnosticRetention::detail::Hex(id,32))refs.insert(id);
  }};
  keep(plan);keep(pendingPlan);
  for(const auto& item:installQueue)if(!QueueTerminal(item.phase)){keep(item.request);keep(item.result);}
  DiagnosticRetention::ProtectPlans(root,refs);
  installer.AcknowledgeCompleted();discovery.AcknowledgeCompleted();readiness.AcknowledgeCompleted();
  if(showDataMaintenance||restartForMaintenance)DiagnosticRetention::Suspend(root);
  else DiagnosticRetention::Maintain(root);
 };
 struct ConsumerEnd {decltype(finishConsumers)& finish;~ConsumerEnd(){try{finish();}catch(...){}}} consumerEnd{finishConsumers};
 bool handlingInspectionReceipt=false;
 std::wstring priorOperationFailureTarget;std::string priorOperationFailureReason;
 try{
 if(!closing&&(showDataMaintenance||restartForMaintenance))return;
 PollNrDllSources(closing);
 if(closing){if(pendingSelection!=SupersededReader)pendingSelection=-2;pendingSelectionReload=false;pendingLaunchGame.reset();StopInstallQueue();}
 else AdvanceInstallQueue();
 if(!closing){const auto targetState=anything?anything->Snapshot():AnythingSnapshot{};
 PollAnythingTargetLifecycle(anythingUi,targetState,[&]{if(anything)anything->Stop();});}
 if(launchAdmission&&launchAdmission->Finished())launchAdmission.reset();
 PollGameDiagnostics();PollDiagnosticBundle();
 if(!closing&&bulkUninstall.Active()&&!bulkOwnsInstaller&&!installer.busy)AdvanceBulkUninstall();
 Json result;
 if(readiness.Poll(result)&&!closing){
  if(result.value("status","")=="PreflightChecked"){preflight=result;preflightError.clear();
   if(!anything){anything=std::make_shared<AnythingController>(AnythingController::FindWorker(AppRoot()),SharedRuntimeRoot());anything->Connect();}
  }
  else preflightError=ResultMessage(result,Neurotic::UiLiteral("desktop.hubviewmodel.pre_flight_could_not_be_checked_try_checking_aga_d6f40d19", "Pre-flight could not be checked. Try checking again."));
 }
 if(!closing&&anything){auto state=anything->Snapshot();if(state.modelRevision!=modelRevision){modelRevision=state.modelRevision;
   if(state.modelVerified){preflight["components"][Neurotic::UiLiteral("desktop.hubshell.neuralmodel_47a6b821", "neuralModel")]={{"optional",true},{"status",Neurotic::UiLiteral("desktop.settings.dlssnr/route/value.43f9b89c0b", "Present")},{Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),state.modelPath},{"present",Json::array({"nvngx_dlssnr.dll"})},{"missing",Json::array()},{Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),nullptr}};}
  }
  if(state.connected&&!state.busy&&!state.modelVerified&&!state.modelPath.empty()&&!state.lastError.empty()&&(state.phase=="Locked"||state.phase==Neurotic::UiLiteral("desktop.anythingview.error_eab1d8bf", "Error"))){
   // Failed verification does not imply the selected file disappeared.
   std::error_code modelFileError;bool modelFilePresent=false;
   try{modelFilePresent=std::filesystem::is_regular_file(Wide(state.modelPath),modelFileError);}catch(...){modelFileError=std::make_error_code(std::errc::invalid_argument);}
   const bool modelFileMissing=!modelFilePresent&&(!modelFileError||modelFileError==std::errc::no_such_file_or_directory||modelFileError==std::errc::not_a_directory);
   preflight["components"][Neurotic::UiLiteral("desktop.hubshell.neuralmodel_47a6b821", "neuralModel")]={{"optional",true},{"status",Neurotic::UiLiteral("desktop.anythingview.unavailable_48a4b800", "Unavailable")},{Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),state.modelPath},{"present",modelFilePresent?Json::array({"nvngx_dlssnr.dll"}):Json::array()},{"missing",modelFileMissing?Json::array({"nvngx_dlssnr.dll"}):Json::array()},{Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),state.lastError}};
  }}
 if(discovery.Poll(result)&&!closing){
  if(result.contains("games")){
   MergeDiscovery(result);message=Neurotic::Localization::FormatSharedText("desktop.provider.765dcc67d919", {{"count",std::to_string(result["games"].size())}});if(!result[Neurotic::UiLiteral("desktop.hubshell.errors_5a41a7c1", "errors")].empty())message+=Neurotic::UiMessage("desktop.hubviewmodel.some_locations_were_skipped_accessible_games_rem_075fc419", " Some locations were skipped; accessible games remain listed.");if(result.contains("diagnosticWarning"))message+=" "+result["diagnosticWarning"].get<std::string>();lastResult=result;Save();
  }else message=ResultMessage(result,Neurotic::UiMessage("desktop.hubviewmodel.game_discovery_failed_770cdce5", "Game discovery failed"));
 }
 if(installer.Poll(result)){
  if(pendingSelection==SupersededReader){
   pendingSelection=-2;pendingSelectionReload=false;
   // Target equality cannot revive a pre-navigation read-only decision.
   // A queued owner's receipt always retains its own dispatch authority.
   if(!installQueueOwnsInstaller){inspectionRefreshing=false;return;}
  }
  if(installQueueOwnsInstaller){AcceptInstallQueueReceipt(result,closing);return;}
  // Observe the original accepted writer before any page/selection-specific receipt return.
  const auto completedInstall=installCompletion.Active()?installCompletion.Active()->target:std::string{};
  installCompletion.Observe(result,installer.action,[completedInstall](const std::string& path){return !completedInstall.empty()&&GameTargetMatches(path,completedInstall)?GameTargetKey(completedInstall):GameTargetKey(path);});
  if(!completedInstall.empty()&&(selected<0||selected>=(int)games.size()||GameTargetKey(games[selected].target.path)!=GameTargetKey(completedInstall)))InvalidateTarget(completedInstall);
  if(installer.action=="LaunchPreflight"){if(closing||!launchAdmission){launchAdmission.reset();return;}if(pendingSelection!=-2){launchAdmission.reset();auto next=pendingSelection;auto reload=pendingSelectionReload;pendingSelection=-2;pendingSelectionReload=false;SelectGame(next,reload);return;}try{CompleteLaunch(result);}catch(const std::exception& e){launchAdmission.reset();message=e.what();}return;}
  if(bulkOwnsInstaller){
   auto target=bulkUninstall.CurrentExecutable();auto kind=bulkRequestKind;bulkOwnsInstaller=false;bulkRequestKind.clear();
   bulkUninstall.AcceptReceipt(result);lastResult=result;
   if(kind=="Execute")InvalidateTarget(target);
   if(!closing)AdvanceBulkUninstall();return;
  }
  // Drain the old child before reusing its single process owner. Its receipt has no authority over the next game.
  if(pendingSelection!=-2){int next=pendingSelection;auto reload=pendingSelectionReload;pendingSelection=-2;pendingSelectionReload=false;SelectGame(next,reload);return;}
  if(pendingLaunchGame){
   const auto expected=std::move(*pendingLaunchGame);pendingLaunchGame.reset();inspectionRefreshing=false;
   if(!closing&&selected>=0&&selected<(int)games.size()){
    const auto& current=games[selected];
    if(current.id==expected.id&&GameTargetKey(current.target.path)==GameTargetKey(expected.target.path)&&current.target.bitness==expected.target.bitness&&current.store==expected.store&&current.storeId==expected.storeId&&current.steamRoot==expected.steamRoot)StartGame();
   }return;
  }
  if(requestedOperation=="Sanitize"&&sanitizeCancelled){sanitizeCancelled=false;plan=Json();pendingPlan=Json();inspectionRefreshing=false;showIssue=showReview=false;message=Neurotic::UiMessage("desktop.hubviewmodel.cleanup_review_cancelled_no_cleanup_was_confirme_e307c2b0", "Cleanup review cancelled. No cleanup was confirmed.");return;}
  auto currentTarget=selected>=0&&selected<(int)games.size()?GameTargetKey(games[selected].target.path):std::wstring();
  if(!installerTarget.empty()&&installerTarget!=currentTarget){inspectionRefreshing=false;return;}
  handlingInspectionReceipt=installer.action=="Inspect";
  if(!handlingInspectionReceipt&&result.contains("inspection")){
   priorOperationFailureTarget=inspectionFailureTarget;priorOperationFailureReason=inspectionFailureReason;
  }
  inspectionRefreshing=false;lastResult=result;auto status=result.value("status",Neurotic::UiLiteral("desktop.hubshell.unknown_c67449ba", "Unknown"));message=ResultMessage(result,status);
  const auto failInspection=[&](const std::string& reason,bool cancelled=false){
   inspectionCached=inspection.is_object();
   if(cancelled){inspectionFailureTarget.clear();inspectionFailureReason.clear();message=reason;}
   else{inspectionFailureTarget=currentTarget;inspectionFailureReason=reason;message=inspectionCached?std::string(Neurotic::UiMessage("desktop.hubviewmodel.refresh_failed_showing_the_previous_status_0c1e2b3f", "Refresh failed. Showing the previous status. "))+reason:reason;}
  };
  const auto inspectionParseFailure=[&](const std::exception& error){
   failInspection(std::string(Neurotic::UiMessage("desktop.hubviewmodel.operation_receipt_could_not_be_read_aae07800", "Operation receipt could not be read: "))+error.what());
  };
  const auto validateFreshInspection=[](const Json& fresh){
   (void)fresh.at("target").at("executable").get_ref<const std::string&>();
   (void)fresh.at("settings").get_ref<const Json::array_t&>();
   if(fresh.contains("configRevision"))(void)fresh.at("configRevision").get_ref<const std::string&>();
   const auto& state=fresh.at("state");if(!state.is_null()){
    (void)state.get_ref<const Json::object_t&>();
    if(state.contains("status"))(void)state.at("status").get_ref<const std::string&>();
   }
  };
  if(installer.action=="Inspect"){
   if(status!="Inspected"){
    if(result.contains("target")){const auto& target=result.at("target");auto path=target.is_string()?target.get<std::string>():target.value("executable",std::string{});if(!path.empty()&&!GameTargetMatches(path,games[selected].target.path))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_response_belongs_to_another_executable_827ef838", "The response belongs to another executable."));}
    const bool cancelled=status=="Cancelled";
    failInspection(ResultMessage(result,cancelled?std::string(Neurotic::UiMessage("desktop.hubviewmodel.installation_cancelled_907399ce", "Installation cancelled.")):status),cancelled);
    return;
   }
   // Only a usable, target-identifiable projection can replace the prior
   // advisory snapshot. Null state is valid absence; unknown status is data.
   try{validateFreshInspection(result.at("inspection"));}catch(const std::exception& error){inspectionParseFailure(error);return;}
  }
  if(status=="DefaultsResolved"){
   const auto& fresh=result.at("inspection");const auto& source=result.at("defaultsSource");
   if(!defaultsRequest.is_object()||GameTargetKey(defaultsRequest.at("gameExecutable").get<std::string>())!=currentTarget||fresh.at("configRevision")!=defaultsRequest.at("configRevision")||source.at("packageId")!="flagship-approved"||!GameTargetMatches(source.at("executable").get<std::string>(),games[selected].target.path)||source.at("executableFileIdentity")!=fresh.at("target").at("fileIdentity"))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.defaults_no_longer_match_the_selected_game_and_s_fbed6a45", "Defaults no longer match the selected game and settings. Refresh before trying again."));
  }
  if(result.contains("inspection")){
   if(!handlingInspectionReceipt)validateFreshInspection(result.at("inspection"));
   auto selectedTarget=selected>=0&&selected<(int)games.size()?GameTargetKey(games[selected].target.path):std::wstring();
   if(!installerTarget.empty()&&installerTarget!=selectedTarget)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_installation_receipt_belongs_to_another_sele_0fc9d394", "The installation receipt belongs to another selected target"));
   if(result["inspection"].contains("target")&&!GameTargetMatches(result["inspection"]["target"].at("executable").get<std::string>(),games[selected].target.path))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_inspection_belongs_to_another_executable_f57351c7", "The inspection belongs to another executable"));
   // A fresh disk snapshot is evidence, not permission to replace unsaved edits
   // or to give an old draft a new write revision. Keep the two together.
   const bool retainDraft=inspectionTarget==selectedTarget&&inspection.is_object()&&HasSettingsDraft(settings)&&
       (installer.action=="Inspect"||(requestedOperation=="SaveSettings"&&status!="Succeeded"&&status!="SucceededWithNotes"));
   auto draftFields=retainDraft?settings:std::vector<SettingsField>{};
   const auto draftInspection=retainDraft?inspection:Json();
   const bool inspecting=installer.action=="Inspect";
   Json priorInspection;std::vector<SettingsField> priorSettings;
   const auto priorInspectionTarget=inspectionTarget;const auto priorProxy=proxy;
   if(inspecting){priorInspection=std::move(inspection);priorSettings=std::move(settings);}
   try{
    inspection=result["inspection"];if(result.contains(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")))inspection[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")]=result[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")];if(result.contains("dumbfireHistory"))inspection["dumbfireHistory"]=result["dumbfireHistory"];settings.clear();proxy=0;
    inspectionTarget=selectedTarget;
    ReadInspectionFields();
   }catch(const std::exception& error){
    if(!inspecting)throw;
    // Parsing can fail after clearing or partially filling fields. Restore the
    // complete prior state, including drafts and their original write revision.
    inspection=std::move(priorInspection);settings=std::move(priorSettings);
    inspectionTarget=priorInspectionTarget;proxy=priorProxy;
    inspectionParseFailure(error);return;
   }
   bool retainedOldRevision=false;
   if(retainDraft){
    retainedOldRevision=inspection.value("configRevision",std::string{})!=draftInspection.value("configRevision",std::string{});
    settings=std::move(draftFields);
    inspection["configRevision"]=draftInspection.value("configRevision",std::string{});
    inspection["settings"]=draftInspection.value("settings",Json::array());
    inspectionCached=retainedOldRevision;
    if(status=="Inspected"&&retainedOldRevision)message=Neurotic::UiText("desktop.provider.a8283ade3185")+" \u2192 "+Neurotic::UiText("desktop.hubshell.refresh_details_0ab28849");
   }
   // A usable fresh snapshot resolves the earlier check, regardless of which
   // operation returned it. Rejected operation receipts restore that failure.
   inspectionFailureTarget.clear();inspectionFailureReason.clear();
   if(status=="Inspected"&&!retainedOldRevision)if(auto notice=AutomaticSetupNotice(inspection);!notice.empty())message=notice+" "+message;
   if(status=="Inspected"){inspectionCached=retainedOldRevision;profileCheckedUtc.clear();CacheSelected();if(!retainDraft&&!SaveGameProfile(games[selected].target.path,inspection))message+=Neurotic::UiMessage("desktop.hubviewmodel.game_profile_could_not_be_saved_existing_data_wa_10fab22a", " Game profile could not be saved; existing data was preserved.");UpdateInspectionIssue();}
  }
  if(status=="DefaultsResolved"){
   auto staged=settings;const auto& changes=result.at("defaultSettings");if(!changes.is_array()||changes.size()>staged.size())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.invalid_defaults_projection_8c08c799", "Invalid defaults projection"));
   std::set<std::pair<std::string,std::string>> seen;
   for(const auto& change:changes){auto section=change.at("section").get<std::string>(),key=change.at("key").get<std::string>(),value=change.at("value").get<std::string>();
    if(!seen.emplace(section,key).second||value.find('\0')!=std::string::npos)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.duplicate_or_invalid_default_setting_93d98527", "Duplicate or invalid default setting"));
    auto field=std::find_if(staged.begin(),staged.end(),[&](const auto& f){return f.section==section&&f.key==key&&f.available;});if(field==staged.end())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.default_setting_is_not_editable_for_this_game_d2300eb7", "Default setting is not editable for this game"));
    if(field->type=="objectrules"){if(value.size()>4194304)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.default_profile_is_too_large_2d461bc1", "Default profile is too large"));field->profileDraft=value;field->ruleStore.reset();field->ruleEditor.reset();}
    else{if(value.size()>=field->draft.size())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.default_setting_is_too_long_d9f6610e", "Default setting is too long"));strcpy_s(field->draft.data(),field->draft.size(),value.c_str());}
   }
   settings=std::move(staged);defaultsRequest=Json();inspectionCached=false;showReview=false;message=Neurotic::UiMessage("desktop.hubviewmodel.installation_defaults_are_staged_save_changes_to_9ebe4e3f", "Installation defaults are staged. Save Changes to apply them; automatic game and GPU rules resolve when the game starts.");CacheSelected();
  }
  if(status=="Planned"){
   auto selectedTarget=selected>=0&&selected<(int)games.size()?GameTargetKey(games[selected].target.path):std::wstring();
   if(installerTarget.empty()||installerTarget!=selectedTarget||!pendingPlan.is_object()||GameTargetKey(pendingPlan.at("gameExecutable").get<std::string>())!=selectedTarget)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_plan_belongs_to_another_selected_target_e4289a92", "The plan belongs to another selected target"));
   if(!result.contains("target")||!GameTargetMatches(result.at("target").at("executable").get<std::string>(),games[selected].target.path)||!result.contains("request"))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_returned_plan_target_is_not_the_requested_ga_e07867aa", "The returned plan target is not the requested game"));
   ValidatePlanIntent(pendingPlan,result,games[selected].target.bitness);
   plan=result;riskAcknowledged=false;auto report=result.value(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"),Json::object());
   if(report.is_object()&&report.value(Neurotic::UiLiteral("desktop.hubshell.scan_status_cc2cdbf3", "scan_status"),"")!="complete"&&operationIssue.is_object()&&operationIssue.value("target","")==games[selected].target.path&&operationIssue.contains(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"))&&operationIssue[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")].is_object()){
    const auto& previous=operationIssue[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")];Json retained=previous.value(Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings"),Json::array());if(!retained.is_array())retained=Json::array();auto findings=previous.value("findings",Json::array());if(findings.is_array())for(auto& finding:findings)if(std::find(retained.begin(),retained.end(),finding)==retained.end())retained.push_back(finding);if(!retained.empty())report[Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings")]=std::move(retained);
   }
   if(report.is_object()&&report.value("acknowledgementRequired",false)){operationIssue={{"decisionKind",Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")},{"target",games[selected].target.path},{Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"),report}};showIssue=true;showReview=false;message=Neurotic::UiMessage("desktop.hubviewmodel.review_the_local_anti_cheat_findings_before_cont_90ea2d84", "Review the local anti-cheat findings before continuing.");if(!closing&&RememberedRiskFor(games[selected])&&report.value("scan_status",std::string{})!="cancelled")AcknowledgeRisk();}
   else{showIssue=false;if(!closing)Execute();}
  }
  if(status=="Succeeded"||status=="SucceededWithNotes"||status=="Partial"){
   showReview=false;plan=Json();operationIssue=Json();showIssue=false;
   message=ResultMessage(result,status=="Partial"?std::string(Neurotic::UiMessage("desktop.hubviewmodel.some_files_remain_retry_install_or_uninstall_aft_9520a13c", "Some files remain. Retry Install or Uninstall after resolving the listed paths.")):std::string(Neurotic::UiMessage("desktop.hubviewmodel.operation_completed_648eb724", "Operation completed.")));
   auto notes=result.value(Neurotic::UiLiteral("desktop.hubshell.notes_fd9f1432", "notes"),Json::array());if(notes.is_array())for(const auto& note:notes)if(note.is_string()||note.is_object())message+=" "+ResultDetail(note);
   if(auto notice=AutomaticSetupNotice(inspection);!notice.empty())message=notice+" "+message;
   if(selected>=0&&result.contains("inspection")){inspectionRefreshing=inspectionCached=false;CacheSelected();SaveGameProfile(games[selected].target.path,inspection);}
   if(status=="Partial"){operationIssue=result;operationIssue["target"]=games[selected].target.path;showIssue=true;}
  }
  else if(status=="Cancelled"){showReview=showIssue=false;plan=Json();fileReview.Cancel();CancelUninstall();message=ResultMessage(result,std::string(Neurotic::UiMessage("desktop.hubviewmodel.installation_cancelled_907399ce", "Installation cancelled.")));}
  else if(status=="NeedsDecision"||status=="PreconditionChanged"||status=="Failed"||status=="FailedWithoutChanges"||status=="UnsupportedOperation"){
   // A changed/incomplete scan must keep the earlier positive evidence visible
   // while a fresh plan is obtained. It is evidence to review, not approval.
   auto previousIssue=operationIssue;
   operationIssue=result;if(selected>=0){if(result.contains("target")){auto target=result["target"].is_string()?result["target"].get<std::string>():result["target"].value("executable",std::string{});if(!target.empty()&&!GameTargetMatches(target,games[selected].target.path))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_response_belongs_to_another_executable_827ef838", "The response belongs to another executable."));}operationIssue["target"]=games[selected].target.path;}
   if(operationIssue.value("decisionKind","")==Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")&&operationIssue.contains(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"))&&operationIssue[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")].value(Neurotic::UiLiteral("desktop.hubshell.scan_status_cc2cdbf3", "scan_status"),"")!="complete"&&previousIssue.is_object()&&previousIssue.value("target","")==operationIssue.value("target","")&&previousIssue.contains(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"))){
    const auto& previous=previousIssue[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")];Json retained=previous.value(Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings"),Json::array());if(!retained.is_array())retained=Json::array();auto findings=previous.value("findings",Json::array());if(findings.is_array())for(const auto& finding:findings)if(std::find(retained.begin(),retained.end(),finding)==retained.end())retained.push_back(finding);if(!retained.empty())operationIssue[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")][Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings")]=std::move(retained);
   }
   showIssue=true;showReview=false;message=ResultMessage(result,std::string(Neurotic::UiMessage("desktop.hubviewmodel.review_the_listed_paths_and_retry_the_selected_a_905d9442", "Review the listed paths and retry the selected action.")));
   if(operationIssue.value("decisionKind","")=="FileConflict")fileReview.Begin(operationIssue.at("fileConflicts"));
  }
 }
}catch(const std::exception& e){
 message=std::string(Neurotic::UiMessage("desktop.hubviewmodel.operation_receipt_could_not_be_read_aae07800", "Operation receipt could not be read: "))+e.what();
 if(selected>=0&&selected<(int)games.size()){
  InvalidateTarget(games[selected].target.path);
  if(handlingInspectionReceipt){inspectionFailureTarget=GameTargetKey(games[selected].target.path);inspectionFailureReason=message;}
  else if(!priorOperationFailureTarget.empty()&&priorOperationFailureTarget==GameTargetKey(games[selected].target.path)){
   inspectionFailureTarget=std::move(priorOperationFailureTarget);inspectionFailureReason=std::move(priorOperationFailureReason);
  }

 }
 inspection=Json();plan=Json();showReview=false;
}
}
void HubModel::RefreshPreflight() try{
 if(readiness.busy)return;preflightError.clear();readiness.StartInstaller({{"kind","Preflight"}});
}catch(const std::exception& e){preflightError=e.what();}
void HubModel::OpenComponentFolder(bool streamline) try{
 auto folder=SharedRuntimeRoot()/L"streamline";
 if(!streamline){
  if(!anything){anything=std::make_shared<AnythingController>(AnythingController::FindWorker(AppRoot()),SharedRuntimeRoot());anything->Connect();}
  if(!anything->EnsureModelFolder())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_nr_model_folder_could_not_be_prepared_check__2ff25e7d", "The NR model folder could not be prepared. Check NR Anything session details."));
  folder=anything->ModelFolder();
  auto state=anything->Snapshot();if(!state.connected&&!state.busy&&!state.stopping)anything->Restart();
 }
 for(auto parent=folder;!parent.empty();){
  auto attrs=GetFileAttributesW(parent.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.linked_component_folders_cannot_be_opened_from_t_b6802615", "Linked component folders cannot be opened from the Hub."));
  auto next=parent.parent_path();if(next==parent)break;parent=next;
 }
 std::filesystem::create_directories(folder);
 if((INT_PTR)ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_component_folder_could_not_be_opened_1b19e73f", "The component folder could not be opened."));
 message=streamline?Neurotic::UiMessage("desktop.hubviewmodel.deposit_your_matching_streamline_files_in_the_op_345406da", "Deposit your matching Streamline files in the opened folder, then choose Check again."):Neurotic::UiMessage("desktop.hubviewmodel.place_nvngx_dlssnr_dll_in_the_opened_folder_neur_1c5ce871", "Place nvngx_dlssnr.dll in the opened folder. NeuRotic detects and verifies it automatically.");
}catch(const std::exception& e){message=e.what();}
bool HubModel::CanDownloadRuntime() const{
 return !readiness.busy&&preflight.is_object()&&preflight.contains(Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime"))&&preflight[Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime")].is_object()&&preflight[Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime")].contains("ready")&&preflight[Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime")]["ready"].is_boolean()&&!preflight[Neurotic::UiLiteral("desktop.hubshell.runtime_d63852ad", "runtime")]["ready"].get<bool>();
}
void HubModel::OpenRuntimeDownload() try{
 if(!CanDownloadRuntime())return;
 if((INT_PTR)ShellExecuteW(nullptr,L"open",L"https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist",nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.microsoft_s_download_page_could_not_be_opened_38335e4b", "Microsoft's download page could not be opened."));
 message=Neurotic::UiMessage("desktop.hubviewmodel.install_or_repair_microsoft_s_x64_runtime_then_c_5395fb25", "Install or repair Microsoft's x64 runtime, then choose Check again.");
}catch(const std::exception& e){message=e.what();}
void HubModel::Plan(const std::string& operation,bool) try{
 if(operation=="Install"||operation=="UpdateNrDll"){EnqueueInstall(operation=="UpdateNrDll");return;}
 if(InstallQueueBusy()){message=Neurotic::UiMessage("desktop.installqueue.operation_conflict","Finish or cancel queued installs before another game operation.");return;}
 if(installer.busy||BulkUninstallBusy()||selected<0||selected>=(int)games.size()||!games[selected].target.suitable)return;
 if(operation!=Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install")&&operation!=Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall"))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.use_install_or_uninstall_f5911d3d", "Use Install or Uninstall."));
 auto& game=games[selected];auto fresh=InspectExecutable(Wide(game.target.path));
 if(!fresh.suitable||fresh.bitness!=game.target.bitness){InvalidateTarget(game.target.path);game.target=fresh;throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.game_executable_changed_refresh_and_retry_d783e30f", "Game executable changed. Refresh and retry."));}
 CancelUninstall();Json request={{"kind","Plan"},{"gameExecutable",game.target.path},{"operation",operation}};
 if(operation==Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install")){
  request["packageId"]="flagship-approved";
  const auto proxyName=SelectedProxyName();if(!proxyName.empty())request["proxyName"]=proxyName;
  request["freshInstall"]=freshInstall;request["gameTitle"]=game.title;request["gameStore"]=game.store;request["gameStoreId"]=game.storeId;
 }
 if(operation==Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall")){uninstallConfirmation=request;showUninstallConfirm=true;showReview=showIssue=false;return;}
 StartSelectedOperation(request);message=Neurotic::UiMessage("desktop.hubviewmodel.preparing_install_6050c49c", "Preparing Install...");
}catch(const std::exception& e){message=e.what();}
void HubModel::CancelUninstall(){showUninstallConfirm=false;uninstallConfirmation=Json();uninstallConfirmed=false;}
void HubModel::CancelFileReview(){
 if(auto head=CurrentQueuedInstall()){CancelQueuedInstall(head->id);return;}
 fileReview.Cancel();pendingPlan=Json();plan=Json();operationIssue=Json();showIssue=showReview=false;CancelUninstall();installer.ClearAntiCheatApproval();message=Neurotic::UiMessage("desktop.hubviewmodel.installation_cancelled_no_files_were_changed_b864f0e4", "Installation cancelled. No files were changed.");
}
void HubModel::DecideFile(const std::string& action)try{
 const auto operationGame=OperationGame();
 if(installer.busy||!operationGame||!pendingPlan.is_object()||operationIssue.value("decisionKind","")!="FileConflict"||!GameTargetMatches(pendingPlan.at("gameExecutable").get<std::string>(),operationGame->target.path)||!GameTargetMatches(operationIssue.at("target").get<std::string>(),operationGame->target.path))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.review_this_game_s_files_again_before_continuing_9c243212", "Review this game's files again before continuing."));
 if(action==Neurotic::UiLiteral("desktop.hubshell.cancel_c50f8908", "Cancel")){CancelFileReview();return;}
 const bool keepReShade=action==Neurotic::UiLiteral("desktop.hubshell.keepreshade_b63abfd0", "KeepReShade");
 if(keepReShade&&(fileReview.Current().at(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"))!=Neurotic::UiLiteral("desktop.hubshell.dxgi_dll_2766e740", "dxgi.dll")||!operationIssue.value(Neurotic::UiLiteral("desktop.hubshell.cankeepreshade_5ecf0788", "canKeepReShade"),false)))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.reshade_coexistence_is_unavailable_for_this_file_79822797", "ReShade coexistence is unavailable for this file."));
 const auto reviewedPath=fileReview.Current().at(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path")).get<std::string>();
 const bool complete=fileReview.Choose(keepReShade?Neurotic::UiLiteral("desktop.hubshell.replace_79226e10", "Replace"):action);
 // The DXGI choice belongs to that file, even when other conflicts follow it.
 if(_stricmp(reviewedPath.c_str(),Neurotic::UiLiteral("desktop.hubshell.dxgi_dll_2766e740", "dxgi.dll"))==0){
  if(keepReShade)pendingPlan["existingProxyAction"]="RenameReShade";
  else if(pendingPlan.value("existingProxyAction",std::string{})=="RenameReShade")pendingPlan.erase("existingProxyAction");
 }
 if(!complete)return;
 auto request=pendingPlan;auto choices=request.value("fileDecisions",Json::array());
 for(const auto& choice:fileReview.Decisions()){
  choices.erase(std::remove_if(choices.begin(),choices.end(),[&](const auto& previous){return _stricmp(previous.at(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path")).template get<std::string>().c_str(),choice.at(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path")).get<std::string>().c_str())==0;}),choices.end());choices.push_back(choice);
 }
 request["fileDecisions"]=std::move(choices);fileReview.Cancel();showIssue=false;
 StartSelectedOperation(request);if(requestedOperation==Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall")&&uninstallConfirmed)uninstallConfirmation=pendingPlan;
 message=Neurotic::UiMessage("desktop.hubviewmodel.checking_the_selected_file_decisions_before_appl_f7cdc59c", "Checking the selected file decisions before applying changes...");
}catch(const std::exception& e){message=e.what();}
void HubModel::ConfirmUninstall()try{
 if(installer.busy||BulkUninstallBusy()||!showUninstallConfirm||!uninstallConfirmation.is_object()||selected<0||selected>=(int)games.size())return;
 if(!GameTargetMatches(uninstallConfirmation.at("gameExecutable").get<std::string>(),games[selected].target.path)){CancelUninstall();throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_selected_game_changed_confirm_uninstall_agai_451e172d", "The selected game changed. Confirm uninstall again."));}
 auto request=uninstallConfirmation;showUninstallConfirm=false;uninstallConfirmed=true;StartSelectedOperation(request);uninstallConfirmation=pendingPlan;message=Neurotic::UiMessage("desktop.hubviewmodel.removing_recorded_neurotic_files_0fea0510", "Removing recorded NeuRotic files...");
}catch(const std::exception& e){CancelUninstall();message=e.what();}
void HubModel::PlanHistory(int){message=Neurotic::UiMessage("desktop.hubviewmodel.restore_history_is_no_longer_an_installation_act_8c22f637", "Restore history is no longer an installation action. Use Install or Uninstall.");}
void HubModel::UpdateInspectionIssue(){
 if(InstallQueueBusy())return;
 if(inspection.is_object()&&inspection.value("state",Json()).is_object()&&inspection["state"].value("status",std::string{})=="Partial")message=Neurotic::UiMessage("desktop.hubviewmodel.installation_is_partial_install_retries_the_curr_c5601773", "Installation is partial. Install retries the current package; Uninstall removes recorded files.");
}
static void OpenGameFolder(const std::string& executable){
 if(executable.empty())return;auto folder=std::filesystem::path(Wide(executable)).parent_path();RootKey(Utf8(folder.wstring()));for(auto walk=folder;!walk.empty();){auto attrs=GetFileAttributesW(walk.c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.game_folder_is_unavailable_or_linked_6bfe93e6", "Game folder is unavailable or linked"));auto parent=walk.parent_path();if(parent==walk)break;walk=parent;}if((INT_PTR)ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.game_folder_could_not_be_opened_d4c27b42", "Game folder could not be opened"));
}
void HubModel::OpenSelectedGameFolder()try{if(selected>=0&&selected<(int)games.size())OpenGameFolder(games[selected].target.path);}catch(const std::exception& e){message=e.what();}
void HubModel::OpenQueuedInstallFolder(uint64_t id)try{if(const auto item=FindQueuedInstall(id))OpenGameFolder(item->game.target.path);}catch(const std::exception& e){message=e.what();}
void HubModel::OpenSelectedGameScreenshots()try{
 if((installer.busy&&!BackgroundInspectionActive())||BulkUninstallBusy()||selected<0||selected>=(int)games.size())return;
 auto folder=EnsureSelectedGameScreenshots(games[selected].target);
 if((INT_PTR)ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.game_screenshots_folder_could_not_be_opened_6b3dbbab", "Game screenshots folder could not be opened."));
 message=Neurotic::UiMessage("desktop.hubviewmodel.game_screenshots_folder_opened_f2514890", "Game screenshots folder opened.");
}catch(const std::exception& e){message=e.what();}
bool HubModel::CanOpenSelectedIni() const{
 if((installer.busy&&!BackgroundInspectionActive())||BulkUninstallBusy()||selected<0||selected>=(int)games.size()||games[selected].target.path.empty())return false;
 auto path=std::filesystem::path(Wide(games[selected].target.path)).parent_path()/L"OptiScaler.ini";
 for(auto walk=path;!walk.empty();){auto attrs=GetFileAttributesW(walk.c_str());if(attrs==INVALID_FILE_ATTRIBUTES||(attrs&FILE_ATTRIBUTE_REPARSE_POINT))return false;auto parent=walk.parent_path();if(parent==walk)break;walk=parent;}
 std::error_code error;return std::filesystem::is_regular_file(path,error);
}
void HubModel::OpenSelectedIni()try{
 if(!CanOpenSelectedIni()){message=Neurotic::UiMessage("desktop.hubviewmodel.this_game_s_optiscaler_ini_is_not_available_inst_dee27028", "This game's OptiScaler.ini is not available. Install NeuRotic first.");return;}
 auto path=std::filesystem::path(Wide(games[selected].target.path)).parent_path()/L"OptiScaler.ini";
 if((INT_PTR)ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,path.parent_path().c_str(),SW_SHOWNORMAL)<=32)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.the_ini_file_could_not_be_opened_choose_a_text_e_0f7802b0", "The INI file could not be opened. Choose a text editor for .ini files in Windows."));
}catch(const std::exception& e){message=e.what();}
static Json SanitizeChoices(const std::array<bool,6>& enabled){
 const char* keys[]={Neurotic::UiLiteral("desktop.option.de464f98106c", "rootFiles"),Neurotic::UiLiteral("desktop.option.8bf729ffe074", "assets"),Neurotic::UiLiteral("desktop.option.9c0d294c05fc", "installer"),Neurotic::UiLiteral("desktop.option.9d488c8887ad", "backups"),Neurotic::UiLiteral("desktop.option.0f52749e9294", "privateModels"),Neurotic::UiLiteral("desktop.option.98d875b61ff2", "screenshots")};Json result=Json::array();for(size_t i=0;i<enabled.size();++i)if(enabled[i])result.push_back(keys[i]);return result;
}
void HubModel::SetDumbfireInstall(bool){dumbfireInstall=false;message=Neurotic::UiMessage("desktop.hubviewmodel.use_install_0b898519", "Use Install.");}
void HubModel::OpenSanitize(){message=Neurotic::UiMessage("desktop.hubviewmodel.use_uninstall_606cb15d", "Use Uninstall.");}
void HubModel::PlanSanitize(){message=Neurotic::UiMessage("desktop.hubviewmodel.use_uninstall_606cb15d", "Use Uninstall.");}
void HubModel::ConfirmSanitize(){message=Neurotic::UiMessage("desktop.hubviewmodel.use_uninstall_606cb15d", "Use Uninstall.");}
void HubModel::CancelSanitize(){showSanitize=false;sanitizeConfirmed=false;sanitizeReviewedRequest=Json();uninstallSanitize=false;}
void HubModel::RestoreSettingsDefaults() try{
 if(InstallQueueBusy()){message=Neurotic::UiMessage("desktop.installqueue.finish_before_restore","Finish or cancel queued installs before restoring saved settings defaults.");return;}
 if(installer.busy||BulkUninstallBusy()||selected<0||selected>=(int)games.size()||!games[selected].target.suitable||!inspection.is_object()||inspectionTarget!=GameTargetKey(games[selected].target.path))return;
 const auto revision=inspection.value("configRevision",std::string{});if(revision.empty())throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.refresh_this_game_s_settings_before_restoring_de_f0deaa86", "Refresh this game's settings before restoring defaults."));
 defaultsRequest={{"kind","RestoreDefaults"},{"gameExecutable",games[selected].target.path},{"configRevision",revision}};
 plan=Json();showReview=false;showIssue=false;StartSelectedOperation(defaultsRequest);message=Neurotic::UiMessage("desktop.hubviewmodel.checking_this_game_s_installation_defaults_deede7f8", "Checking this game's installation defaults...");
}catch(const std::exception& e){defaultsRequest=Json();message=e.what();}
void HubModel::BeginBulkUninstall() try{
 if(InstallQueueBusy()||installer.busy||discovery.busy||BulkUninstallBusy()){message=Neurotic::UiMessage("desktop.hubviewmodel.wait_for_the_current_game_operation_or_scan_befo_0caf2fbf", "Wait for the current game operation or scan before checking all installations.");return;}
 auto known=EnumerateKnownInstallations();for(const auto& game:games)if(!game.target.path.empty())known.targets.push_back({game.target.path,game.title,"Library"});
 CancelUninstall();pendingPlan=Json();defaultsRequest=Json();plan=Json();pendingSelection=-2;showReview=showIssue=false;installer.ClearAntiCheatApproval();
 if(!bulkUninstall.Begin(known.targets,known.issues))return;showBulkUninstall=true;bulkUninstallMode=0;AdvanceBulkUninstall();
}catch(const std::exception& e){message=e.what();}
void HubModel::AdvanceBulkUninstall(){
 if(installer.busy||bulkOwnsInstaller)return;
 auto request=bulkUninstall.NextRequest();
 if(request.is_null()){
  switch(bulkUninstall.GetPhase()){
  case BulkUninstall::Phase::Review:message=Neurotic::UiMessage("desktop.hubviewmodel.choose_how_to_remove_neurotic_from_the_listed_ga_ced97dc4", "Choose how to remove NeuRotic from the listed games, then confirm.");break;
  case BulkUninstall::Phase::Finished:message=Neurotic::UiMessage("desktop.hubviewmodel.all_games_uninstall_finished_review_each_game_s__df1226f2", "All-games uninstall finished. Review each game's result; any blocked games were preserved.");break;
  case BulkUninstall::Phase::Cancelled:message=Neurotic::UiMessage("desktop.hubviewmodel.all_games_uninstall_cancelled_15570ca4", "All-games uninstall cancelled.");break;
  default:break;
  }return;
 }
 try{bulkRequestKind=request.at("kind").get<std::string>();installer.StartInstaller(request);bulkOwnsInstaller=true;message=(bulkRequestKind=="Execute"?Neurotic::UiMessage("desktop.hubviewmodel.removing_neurotic_5b74567a", "Removing NeuRotic: "):Neurotic::UiMessage("desktop.hubviewmodel.checking_installation_090ac0b4", "Checking installation: "))+bulkUninstall.CurrentExecutable();}
 catch(const std::exception& e){bulkRequestKind.clear();bulkOwnsInstaller=false;bulkUninstall.AcceptFailure(e.what());message=e.what();}
}
void HubModel::ConfirmBulkUninstall(){
 if(InstallQueueBusy()||installer.busy)return;
 if(bulkUninstall.Confirm(Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall")))AdvanceBulkUninstall();
}
void HubModel::CancelBulkUninstall(){
 if(bulkUninstall.GetPhase()==BulkUninstall::Phase::Running){bulkUninstall.StopAfterCurrent();message=Neurotic::UiMessage("desktop.hubviewmodel.stopping_after_the_current_game_operation_finish_041830a8", "Stopping after the current game operation finishes.");}
 else{bulkUninstall.Cancel();if(bulkOwnsInstaller&&installer.readOnlyInstaller)installer.Cancel();message=Neurotic::UiMessage("desktop.hubviewmodel.cancelling_the_read_only_installation_checks_2af6c49b", "Cancelling the read-only installation checks...");}
}
void HubModel::CloseBulkUninstall(){
 if(bulkUninstall.Active())return;if(bulkUninstall.GetPhase()==BulkUninstall::Phase::Review)bulkUninstall.Cancel();
 showBulkUninstall=false;RefreshSelected();
}
void HubModel::PlanSettings() try{
 if(installer.busy||selected<0||selected>=(int)games.size()||inspection.is_null())return;Json patches=BuildSavedSettingsPatches(settings);
 if(patches.empty()){message=Neurotic::UiMessage("desktop.hubviewmodel.no_settings_changes_to_save_8d53c413", "No settings changes to save.");return;}
 StartSelectedOperation({{"kind","Plan"},{"gameExecutable",games[selected].target.path},{"operation","SaveSettings"},{"configRevision",inspection.value("configRevision","")},{"settings",patches}});message=Neurotic::UiMessage("desktop.hubviewmodel.preparing_settings_review_9def45fd", "Preparing settings review...");
}catch(const std::exception& e){message=e.what();}
void HubModel::AcknowledgeRisk(bool remember)try{
 const auto operationGame=OperationGame();
 if(installer.busy||!operationGame||operationIssue.value("decisionKind","")!=Neurotic::UiLiteral("desktop.hubshell.anticheatrisk_879cbe91", "AntiCheatRisk")||!pendingPlan.is_object()||!GameTargetMatches(pendingPlan.at("gameExecutable").get<std::string>(),operationGame->target.path)||!GameTargetMatches(operationIssue.at("target").get<std::string>(),operationGame->target.path))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.review_this_game_s_warning_again_before_continui_4766d299", "Review this game's warning again before continuing"));
 if(!plan.is_object()||!plan.contains(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"))||operationIssue.at(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")).value("fingerprint","")!=plan.at(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")).value("fingerprint","")){installer.ClearAntiCheatApproval();StartSelectedOperation(pendingPlan);message=Neurotic::UiMessage("desktop.hubviewmodel.the_anti_cheat_evidence_changed_preparing_a_fres_899ec0ae", "The anti-cheat evidence changed. Preparing a fresh warning and review...");return;}
 installer.AcknowledgeAntiCheat(plan);
 bool rememberFailed=false;
 if(remember){
  const auto key=GameTargetKey(operationGame->target.path);const auto found=gameInstallPreferences.find(key);
  const auto prior=found==gameInstallPreferences.end()?std::optional<GameInstallPreferences>{}:std::optional<GameInstallPreferences>{found->second};
  auto& value=gameInstallPreferences[key];if(value.gameId!=operationGame->id)value=GameInstallPreferences{};value.executable=operationGame->target.path;value.gameId=operationGame->id;value.rememberAntiCheatRisk=true;
  if(!Save()){if(prior)gameInstallPreferences[key]=*prior;else gameInstallPreferences.erase(key);rememberFailed=true;}
 }
 riskAcknowledged=true;showIssue=false;Execute();
 if(rememberFailed)message+=" "+Neurotic::UiMessage("desktop.installation.remember_failed","Your choice could not be saved. You will be asked again next time.");
}catch(const std::exception& e){message=e.what();}
void HubModel::ProceedWithoutRuntime()try{
 const auto operationGame=OperationGame();
 if(installer.busy||!operationGame||operationIssue.value("decisionKind","")!=Neurotic::UiLiteral("desktop.hubshell.runtimemissing_8fa29c64", "RuntimeMissing")||!pendingPlan.is_object()||!GameTargetMatches(pendingPlan.at("gameExecutable").get<std::string>(),operationGame->target.path)||!GameTargetMatches(operationIssue.at("target").get<std::string>(),operationGame->target.path))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.review_this_game_s_runtime_warning_again_before__fbc1e263", "Review this game's runtime warning again before continuing"));
 auto request=pendingPlan;request["continueWithoutRuntime"]=true;StartSelectedOperation(request);message=Neurotic::UiMessage("desktop.hubviewmodel.checking_the_requested_action_with_your_one_time_4a2425fa", "Checking the requested action with your one-time runtime choice...");
}catch(const std::exception& e){message=e.what();}
void HubModel::Execute() try{
 const auto operationGame=OperationGame();
 if(installer.busy||!operationGame||!plan.is_object()||!pendingPlan.is_object()||!GameTargetMatches(pendingPlan.at("gameExecutable").get<std::string>(),operationGame->target.path)||!GameTargetMatches(plan.at("target").at("executable").get<std::string>(),operationGame->target.path))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.prepare_this_game_s_action_again_before_applying_c91c2b3f", "Prepare this game's action again before applying it"));
 if(requestedOperation==Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall")&&(!uninstallConfirmed||pendingPlan!=uninstallConfirmation))throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.confirm_this_game_s_uninstall_before_continuing_33f2ae15", "Confirm this game's uninstall before continuing."));
 if(requestedOperation!=Neurotic::UiLiteral("desktop.hubshell.install_d4824a37", "Install")&&requestedOperation!=Neurotic::UiLiteral("desktop.hubshell.uninstall_91f57c6b", "Uninstall")&&requestedOperation!="SaveSettings"&&requestedOperation!="UpdateNrDll")throw std::runtime_error(Neurotic::UiMessage("desktop.planintent.unsupported_action_d06d8072", "Unsupported action."));
 auto risk=plan.value(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"),Json::object());if(risk.is_object()&&risk.value("acknowledgementRequired",false)&&!riskAcknowledged)throw std::runtime_error(Neurotic::UiMessage("desktop.hubviewmodel.acknowledge_this_game_s_anti_cheat_warning_befor_8bca9b8e", "Acknowledge this game's anti-cheat warning before continuing"));
 InstallCompletionState::Identity completion;
 if(requestedOperation=="Install")completion={0,operationGame->id,operationGame->title,operationGame->target.path,plan.at("planId").get<std::string>(),GameTargetKey(operationGame->target.path)};
 StartSelectedOperation({{"kind","Execute"},{"planId",plan.at("planId")},{"planFingerprint",plan.at("planFingerprint")}});
 if(requestedOperation=="Install")installCompletion.Begin(std::move(completion));
 sanitizeConfirmed=false;CancelUninstall();showReview=false;showIssue=false;message=Neurotic::UiMessage("desktop.hubviewmodel.applying_the_requested_operation_00b2d086", "Applying the requested operation...");
}catch(const std::exception& e){message=e.what();}
}

void nh::HubModel::ChooseProxy(int index){
 if(index<0||index>=9||selected<0||selected>=(int)games.size()||games[selected].target.path.empty())return;
 const auto& game=games[selected];const auto key=GameTargetKey(game.target.path);
 proxy=index;proxyChoices[key]=index;
 const auto found=gameInstallPreferences.find(key);const auto prior=found==gameInstallPreferences.end()?std::optional<GameInstallPreferences>{}:std::optional<GameInstallPreferences>{found->second};
 auto& value=gameInstallPreferences[key];if(value.gameId!=game.id)value=GameInstallPreferences{};
 value.executable=game.target.path;value.gameId=game.id;value.proxyName=Proxies[index];
 if(!Save()){if(prior)gameInstallPreferences[key]=*prior;else gameInstallPreferences.erase(key);}
}
