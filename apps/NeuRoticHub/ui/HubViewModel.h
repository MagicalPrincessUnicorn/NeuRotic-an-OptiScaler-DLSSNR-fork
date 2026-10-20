#include <menu/Localization.h>
#pragma once
#include "ManualLibrary.h"
#include "library/GameGrouping.h"
#include "install/OperationController.h"
#include "install/LaunchAdmission.h"
#include "install/BulkUninstall.h"
#include "GameDiagnostics.h"
#include "DiagnosticBundle.h"
#include "FrontDoor.h"
#include "NrModelReminder.h"
#include "FileConflictReview.h"
#include "InstallCompletionState.h"
#include "storage/AppDataMaintenance.h"
#include "install/InstallDefaults.h"
#include "install/NrDllInstallChoice.h"
#include "NrDllManager.h"
#include <functional>
#include <algorithm>
#include "anything/AnythingUiState.h"
#include <vector>
#include <array>
#include <future>
#include <map>
#include <optional>
namespace Neurotic::Semantic::Rules {class Store;struct Editor;}
namespace nh {
class ArtworkService;
class AnythingController;
struct HeroSlides;
enum class LibraryFilter { All, FavoritesFirst, Hidden };
struct GameInstallPreferences {std::string executable,gameId;bool installInspector=false,rememberAntiCheatRisk=false;std::string proxyName;};
struct SearchDirectory {std::string path,source;bool automatic=false;};
struct SettingsField {std::string profileDraft;std::shared_ptr<Neurotic::Semantic::Rules::Store> ruleStore;std::shared_ptr<Neurotic::Semantic::Rules::Editor> ruleEditor;std::string section,key,group,original,label,type,description,labelId,descriptionId,nativeManualScale,originalNativeManualScale,resetValue,resetNativeManualScale;std::vector<std::string> choices;std::map<std::string,std::string> valueLabels,valueLabelIds;std::array<char,128> draft{};bool available=false,hasRange=false,resetKnown=false;double minimum=0,maximum=0;};
inline bool SavedSettingsFieldChanged(const SettingsField& field){
 const std::string value=field.type=="objectrules"?field.profileDraft:std::string(field.draft.data());
 const bool automatic=field.section=="DlssNr"&&field.key=="NativeResolutionChoice"&&(value=="pre"||value=="post"||value=="full");
 return field.available&&(field.original!=value||(automatic&&field.nativeManualScale!=field.originalNativeManualScale));
}
// Pure request construction shared by the real Save planner and UI regression
// harness. Remembered Manual scale is action metadata, never an INI key.
inline Json BuildSavedSettingsPatches(const std::vector<SettingsField>& settings){
 Json patches=Json::array();
 for(const auto& field:settings){
  const std::string value=field.type=="objectrules"?field.profileDraft:std::string(field.draft.data());
  const bool automatic=field.section=="DlssNr"&&field.key=="NativeResolutionChoice"&&(value=="pre"||value=="post"||value=="full");
  if(!SavedSettingsFieldChanged(field))continue;
  Json patch={{"section",field.section},{"key",field.key},{"value",value}};
  // The projection may be a fallback for an unknown or absent saved value.
  // Stage-only edits must not write that fallback back as a user preference.
  if(automatic&&field.nativeManualScale!=field.originalNativeManualScale)patch["nativeManualScale"]=field.nativeManualScale;
  patches.push_back(std::move(patch));
 }
 return patches;
}
struct GameSessionStatus {
 Json inspection;std::vector<SettingsField> settings;int proxy=0;
 std::string message,diagnosticsText,diagnosticsStatus;uint64_t generation=0;
};
struct QueuedInstall {
 enum class Phase {Queued,Preparing,NeedsAttention,Installing,Succeeded,Failed,Cancelled};
 uint64_t id=0;Game game;Json request,defaults,result;std::wstring target;
 std::string executableStamp,summary;Phase phase=Phase::Queued;bool cancelRequested=false;
 NrDllCopySnapshot nrDllSnapshot;std::shared_ptr<const NrDllAssetLease> nrDllLease;bool nrDllAcquireAttempted=false;
 std::wstring directoryIdentity;Json writerResult;std::string writerSummary;bool writeAttempted=false;
};
struct HubModel {
 ~HubModel() noexcept;
 std::vector<Game> games;int selected=-1,page=0,settingsTab=0,lastNavigationPage=-1;
 int proxy=0,decision=0,uninstallMode=0,keepRestoreRecords=3;
 bool light=false,continueWithoutRuntime=false,showReview=false,onlineArtwork=true,reducedMotion=false;
 bool favoritesFirst=false,showUninstallConfirm=false;
 LibraryFilter libraryFilter=LibraryFilter::All;
 BulkUninstall bulkUninstall;bool showBulkUninstall=false;int bulkUninstallMode=0;
 bool sanitizeRevealed=false,dumbfireInstall=false,showSanitize=false;std::array<bool,6> sanitizeCategories{true,true,true,true,true,false};
 std::string dumbfireInstalledTarget;NrModelReminderCache nrModelReminder;
 bool loaded=false;
 std::shared_ptr<NrDllManager> nrDllManager;
 bool nrDllFolderOpenFailed=false;std::string nrDllEdit;bool showNrDllManager=false;std::string nrDllSelectedId;std::array<char,4097> nrDllName{};std::filesystem::path nrDllImportPath;
 NrDllGameChoice NrDllChoiceFor(const Game& game)const;bool SetNrDllChoice(const Game& game,NrDllGameChoice choice);
 bool NrDllPending()const;bool NrDllQueueSourcePending()const;void UseManualNrDllForQueue();
 InstallDefaultsState installDefaults;
 bool SetInstallDefault(const char* key,const Json& value);void ResetInstallDefaults();
 bool freshInstall=false;
 bool InspectorInstallFor(const Game& game)const;bool SetInspectorInstallFor(const Game& game,bool enabled);
 bool RememberedRiskFor(const Game& game)const;
 bool AntiCheatRiskAcceptedFor(const Game& game)const;
 bool uninstallPermanent=false,uninstallRemoveSettings=false;
 std::array<bool,6> uninstallCategories{true,true,true,false,false,false};
 int dataMaintenanceAction=-1;
 bool showDataMaintenance=false,restartForMaintenance=false;
 AppDataClearPlan dataMaintenancePlan;
 std::function<void(bool)> maintenanceQuiesce;
 std::function<bool()> languageMaintenanceBusy;
 void RequestDataMaintenance(int action);void ConfirmDataMaintenance();void CancelDataMaintenance();
 std::vector<std::string> searchRoots,excludedRoots;std::vector<SearchDirectory> knownDirectories;int selectedDirectory=-1;ArtworkService* artwork=nullptr;
 double pageShownAt=0;
 std::shared_ptr<HeroSlides> heroSlides;
 std::shared_ptr<AnythingController> anything;
 AnythingUiState anythingUi;
 uint64_t modelRevision=0;
 std::array<char,256> search{};
 std::string message=Neurotic::UiMessage("desktop.hubviewmodel.scan_your_libraries_or_add_a_game_to_get_started_e05be5f9", "Scan your libraries or add a game to get started.");
 Json inspection,plan,lastResult,preflight;std::vector<SettingsField> settings;
 std::string preflightError;
 std::string gameDiagnosticsText,gameDiagnosticsStatus;
 bool gameDiagnosticsBusy=false,showIssue=false;
 bool bundleBusy=false,inspectionRefreshing=false,inspectionCached=false;
 std::string bundlePath,bundleStatus,profileCheckedUtc,historyCleanupNotice;
 Json operationIssue;std::string requestedOperation,scanFolder;
 FileConflictReview fileReview;
 void DecideFile(const std::string& action);void CancelFileReview();
 InstallCompletionState installCompletion;
 bool showInstallQueue=false,restoreInstallQueueFocus=false;uint64_t reviewedInstallId=0;
 ProcessJob installer,discovery,readiness;
 // True means library selection was accepted now or queued behind an owned
 // read-only inspection. Mutation ownership and pending state stay private.
 bool TrySelectLibraryGame(int index);
 bool InstallQueueBusy()const{
  return std::any_of(installQueue.begin()+std::min(installQueueCursor,installQueue.size()),installQueue.end(),[](const auto& item){
   return item.phase!=QueuedInstall::Phase::Succeeded&&item.phase!=QueuedInstall::Phase::Failed&&item.phase!=QueuedInstall::Phase::Cancelled;
  });
 }
 bool InstallQueueWorkerActive()const{return installQueueOwnsInstaller;}
 const std::vector<QueuedInstall>& InstallQueueItems()const{return installQueue;}
 const QueuedInstall* CurrentQueuedInstall()const;const Game* OperationGame()const;
 const QueuedInstall* FindQueuedInstall(uint64_t id)const;const QueuedInstall* SelectedQueuedInstall()const;
 bool ReviewQueuedInstall(uint64_t id);bool EditQueuedInstall(uint64_t id);void RetryQueuedInstall(uint64_t id);
 void OpenQueuedInstallFolder(uint64_t id);
 void CancelQueuedInstall(uint64_t id);void RetryQueuedInstall();
 bool RemoveQueuedInstall(uint64_t id);size_t ClearInstallQueueHistory();
 std::string QueuedProxyName()const;void ChooseQueuedProxy(int index);
 void Load();bool Save();void Select(int index);void Add(const std::filesystem::path& path,const std::string& store=Neurotic::UiLiteral("desktop.anythingview.manual_9ca08eb3", "Manual"));void Scan();void Poll();void Plan(const std::string& operation,bool adoptModified=false);void PlanSettings();void Execute();void AcknowledgeRisk(bool remember=false);void ProceedWithoutRuntime();
 void ChooseProxy(int index);
 std::string SelectedProxyName() const;
 void PlanHistory(int keep);
 void ConfirmUninstall();void CancelUninstall();
 void RestoreSettingsDefaults();
 bool BulkUninstallBusy() const{return bulkUninstall.Active()||bulkUninstall.GetPhase()==BulkUninstall::Phase::Review;}
 void BeginBulkUninstall();void ConfirmBulkUninstall();void CancelBulkUninstall();void CloseBulkUninstall();
 void OpenSanitize();void PlanSanitize();void ConfirmSanitize();void CancelSanitize();
 void SetDumbfireInstall(bool enabled);
 bool CanDownloadRuntime() const;bool CanOpenSelectedIni() const;void OpenSelectedIni();
 void RefreshPreflight();void OpenComponentFolder(bool streamline);void OpenRuntimeDownload();
 void AddDirectory(const std::filesystem::path& path);void SetExecutable(int index,const std::filesystem::path& path);void MergeDiscovery(const Json& result);void CancelScan();
 void ScanDirectory(const std::string& path);void RemoveDirectory(int index);std::vector<SearchDirectory> SearchDirectories() const;void OpenSelectedGameFolder();void OpenSelectedGameScreenshots();
 void RefreshSelected();bool CanCollectGameDiagnostics() const;void CollectGameDiagnostics();
 std::string SelectedInspectionFailure() const;
 void PollClosing();bool DiagnosticsPending()const;bool BundlePending()const;
 bool BackgroundInspectionActive() const{return installer.busy&&installer.readOnlyInstaller&&installer.action=="Inspect";}
 bool CanStartGame() const;void StartGame();void ExportSelectedDiagnostics(const std::filesystem::path& directory);void OpenBundleFolder();
 bool DiagnosticExportRunning() const{return bundleWorker.valid();}
 void StartCandidateCapture(bool observer);
 std::vector<int> OrderedGames(const std::string& filter={}) const;void ToggleFavorite(int index);
 void ToggleHidden(int index);void SetLibraryFilter(LibraryFilter filter);void LeaveLibrarySelection();
 private:
 void PollImpl(bool closing);void PollNrDllSources(bool closing);
 std::map<std::wstring,NrDllGameChoice> nrDllChoices;
 std::map<std::wstring,GameInstallPreferences> gameInstallPreferences;

 std::future<NrDllSelection> nrDllAcquisition;std::stop_source nrDllAcquireStop;uint64_t nrDllAcquireItem=0;
 std::vector<QueuedInstall> installQueue;size_t installQueueCursor=0;uint64_t installQueueSequence=0;
 bool installQueueOwnsInstaller=false,installQueueClosing=false,installQueueRefreshDeferred=false;
 QueuedInstall* QueueHead();void EnqueueInstall(bool nrDllOnly=false);void AdvanceInstallQueue();void StopInstallQueue();
 void StartInstallQueuePlan(const Json& request);void StartInstallQueueExecute(const Json& request);
 void AcceptInstallQueueReceipt(const Json& result,bool closing);void PauseInstallQueue(const std::string& reason);
 void ClearQueueDecisionState();
 std::string visibilityPinnedGameId;
 LibraryFilter visibilityPinnedFilter=LibraryFilter::All;
 std::map<std::wstring,GameSessionStatus> gameSession;
 std::map<std::wstring,int> proxyChoices;
 std::wstring inspectionTarget,installerTarget,diagnosticsTarget;
 std::wstring inspectionFailureTarget;std::string inspectionFailureReason;
 std::future<GameDiagnosticsResult> diagnosticsWorker;
 std::future<DiagnosticBundleResult> bundleWorker;std::wstring bundleTarget;
 std::map<std::wstring,DiagnosticBundleResult> exportedBundles;
 uint64_t sessionGeneration=0,diagnosticsGeneration=0;
 Json pendingPlan;
 std::shared_ptr<LaunchAdmission> launchAdmission;Game launchGame;
 std::optional<Game> pendingLaunchGame;
 void CompleteLaunch(const Json& result);
 Json uninstallConfirmation;bool uninstallConfirmed=false;
 Json defaultsRequest;bool bulkOwnsInstaller=false;std::string bulkRequestKind;
 std::wstring sanitizeTarget;Json sanitizeReviewedRequest;bool sanitizeConfirmed=false,sanitizeCancelled=false;

 int maintenanceRequestedAction=-1;AppDataClearPlan reviewedMaintenancePlan;
 bool DataMaintenanceBusy() const;bool maintenanceAnythingSuspended=false;
 bool uninstallSanitize=false;
 void AdvanceBulkUninstall();
 int pendingSelection=-2;bool pendingSelectionReload=false;
 void SelectGame(int index,bool reload);
 Json SelectedInspectionSnapshot() const;
 bool riskAcknowledged=false;
 bool libraryReadOnly=false;
 void CacheSelected();void InvalidateTarget(const std::string& target);void StartSelectedOperation(const Json& request);void PollGameDiagnostics();void UpdateInspectionIssue();
 void PollDiagnosticBundle();void ReadInspectionFields();
};
void ApplySharedTheme(bool light);
void RenderHub(HubModel& model,void* brand,float dpi);
}
