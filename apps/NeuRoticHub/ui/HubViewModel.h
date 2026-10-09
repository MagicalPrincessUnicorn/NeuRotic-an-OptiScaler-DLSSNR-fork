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
#include "storage/AppDataMaintenance.h"
#include "install/InstallDefaults.h"
#include <functional>
#include "anything/AnythingUiState.h"
#include <vector>
#include <array>
#include <future>
#include <map>
namespace Neurotic::Semantic::Rules {class Store;struct Editor;}
namespace nh {
class ArtworkService;
class AnythingController;
struct HeroSlides;
enum class LibraryFilter { All, FavoritesFirst, Hidden };
struct SearchDirectory {std::string path,source;bool automatic=false;};
struct SettingsField {std::string profileDraft;std::shared_ptr<Neurotic::Semantic::Rules::Store> ruleStore;std::shared_ptr<Neurotic::Semantic::Rules::Editor> ruleEditor;std::string section,key,group,original,label,type,description,labelId,descriptionId;std::vector<std::string> choices;std::map<std::string,std::string> valueLabels,valueLabelIds;std::array<char,128> draft{};bool available=false,hasRange=false;double minimum=0,maximum=0;};
struct GameSessionStatus {
 Json inspection;std::vector<SettingsField> settings;int proxy=0;
 std::string message,diagnosticsText,diagnosticsStatus;uint64_t generation=0;
};
struct HubModel {
 std::vector<Game> games;int selected=-1,page=0,settingsTab=0,lastNavigationPage=-1;
 int proxy=0,decision=0,uninstallMode=0,keepRestoreRecords=3;
 bool light=false,continueWithoutRuntime=false,showReview=false,onlineArtwork=true,reducedMotion=false;
 bool favoritesFirst=false,showUninstallConfirm=false;
 LibraryFilter libraryFilter=LibraryFilter::All;
 BulkUninstall bulkUninstall;bool showBulkUninstall=false;int bulkUninstallMode=0;
 bool sanitizeRevealed=false,dumbfireInstall=false,showSanitize=false;std::array<bool,6> sanitizeCategories{true,true,true,true,true,false};
 std::string dumbfireInstalledTarget;NrModelReminderCache nrModelReminder;
 bool loaded=false;
 InstallDefaultsState installDefaults;
 bool SetInstallDefault(const char* key,const Json& value);void ResetInstallDefaults();
 bool freshInstall=false;
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
 ProcessJob installer,discovery,readiness;
 void Load();void Save();void Select(int index);void Add(const std::filesystem::path& path,const std::string& store=Neurotic::UiLiteral("desktop.anythingview.manual_9ca08eb3", "Manual"));void Scan();void Poll();void Plan(const std::string& operation,bool adoptModified=false);void PlanSettings();void Execute();void AcknowledgeRisk();void ProceedWithoutRuntime();
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
 bool CanStartGame() const;void StartGame();void ExportSelectedDiagnostics(const std::filesystem::path& directory);void OpenBundleFolder();
 bool DiagnosticExportRunning() const{return bundleWorker.valid();}
 void StartCandidateCapture(bool observer);
 std::vector<int> OrderedGames(const std::string& filter={}) const;void ToggleFavorite(int index);
 void ToggleHidden(int index);void SetLibraryFilter(LibraryFilter filter);void LeaveLibrarySelection();
private:
 std::string visibilityPinnedGameId;
 LibraryFilter visibilityPinnedFilter=LibraryFilter::All;
 std::map<std::wstring,GameSessionStatus> gameSession;
 std::map<std::wstring,int> proxyChoices;
 std::wstring inspectionTarget,installerTarget,diagnosticsTarget;
 std::future<GameDiagnosticsResult> diagnosticsWorker;
 std::future<DiagnosticBundleResult> bundleWorker;std::wstring bundleTarget;
 std::map<std::wstring,DiagnosticBundleResult> exportedBundles;
 uint64_t sessionGeneration=0,diagnosticsGeneration=0;
 Json pendingPlan;
 std::shared_ptr<LaunchAdmission> launchAdmission;Game launchGame;
 void CompleteLaunch(const Json& result);
 Json uninstallConfirmation;bool uninstallConfirmed=false;
 Json defaultsRequest;bool bulkOwnsInstaller=false;std::string bulkRequestKind;
 std::wstring sanitizeTarget;Json sanitizeReviewedRequest;bool sanitizeConfirmed=false,sanitizeCancelled=false;

 int maintenanceRequestedAction=-1;AppDataClearPlan reviewedMaintenancePlan;
 bool DataMaintenanceBusy() const;bool maintenanceAnythingSuspended=false;
 bool uninstallSanitize=false;
 void AdvanceBulkUninstall();
 int pendingSelection=-2;
 bool riskAcknowledged=false;
 bool libraryReadOnly=false;
 void CacheSelected();void InvalidateTarget(const std::string& target);void StartSelectedOperation(const Json& request);void PollGameDiagnostics();void UpdateInspectionIssue();
 void PollDiagnosticBundle();void ReadInspectionFields();
};
void ApplySharedTheme(bool light);
void RenderHub(HubModel& model,void* brand,float dpi);
}
