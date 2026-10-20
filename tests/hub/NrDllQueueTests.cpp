#include <install/NrDllInstallChoice.h>
#include "OperationFlowTests.cpp"
#include <thread>
namespace {
void DrainNr(nh::HubModel& model){
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(model.NrDllPending()&&std::chrono::steady_clock::now()<deadline){model.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 if(model.NrDllPending())throw std::runtime_error("NR source acquisition did not drain");
 model.Poll();
}
}
int RunNrDllQueueTests(){
 int checks=0,failed=0;auto check=[&](bool value,const char* why){++checks;failed+=!value;std::cout<<(value?"PASS ":"FAIL ")<<why<<'\n';};
 const auto fixture=nh::UserRoot()/L"nr-dll-queue";std::filesystem::create_directories(fixture);
 const auto vault=fixture/L"vault",source=fixture/L"nvngx_dlssnr.dll";Write(source,"inert user source A");
 nh::NrDllProfiles storage(vault,true);const auto a=storage.Import(source,"Selected A");Write(source,"inert user source B");const auto b=storage.Import(source,"Selected B");storage.SetDefault(a.id);
 {nh::HubModel m;m.nrDllManager=std::make_shared<nh::NrDllManager>(vault,true);m.nrDllManager->Refresh();DrainNr(m);
  m.games={Game(fixture,"initial-a"),Game(fixture,"initial-b")};m.selected=0;m.ChooseProxy(0);m.Plan("Install");
  storage.SetDefault(b.id);storage.Rename(a.id,"Renamed after submit");DrainNr(m);
  const auto request=Read(m.installer.request);
  check(request.at("nrDllCopy").at("profileId")==a.id&&request.at("nrDllCopy").at("profileName")=="Selected A","actual queue freezes default identity and label at submit before async acquisition");
  try{storage.Remove(a.id);check(false,"queued source must be held");}catch(const nh::NrDllError& e){check(e.code==nh::NrDllErrorCode::InUse,"real queued source lease denies catalog removal while child can read");}
  auto changed=Planned(request);changed["request"]["nrDllCopy"]["profileId"]=b.id;Feed(m,changed);
  check(!m.installer.busy&&m.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention,"returned plan cannot substitute another profile");
  m.CancelQueuedInstall(m.CurrentQueuedInstall()->id);DrainNr(m);storage.Remove(a.id);
  check(!m.InstallQueueBusy()&&storage.List().profiles.size()==1,"cancelled drained queue releases source before removal");
 }
 {nh::HubModel m;m.nrDllManager=std::make_shared<nh::NrDllManager>(vault,true);m.nrDllManager->Refresh();DrainNr(m);m.games={Game(fixture,"corrupt-default")};m.selected=0;m.ChooseProxy(0);
  Write(vault/L"default.json","{ invalid default");m.nrDllManager->Refresh();DrainNr(m);m.Plan("Install");DrainNr(m);
  check(m.InstallQueueItems().empty(),"corrupt default prevents ambiguous queue acceptance instead of silently choosing no-copy");
  ResetQueueForNextCase(m);m.SetNrDllChoice(m.games[0],{nh::NrDllChoiceMode::Profile,b.id});m.Plan("Install");DrainNr(m);
  check(m.installer.busy&&Read(m.installer.request).at("nrDllCopy").at("profileId")==b.id,"explicit valid profile remains usable while default metadata is corrupt");ResetQueueForNextCase(m);
  storage.SetDefault(b.id);
 }
 {nh::HubModel m;m.nrDllManager=std::make_shared<nh::NrDllManager>(vault,true);m.nrDllManager->Refresh();DrainNr(m);m.games={Game(fixture,"existing-update")};m.selected=0;
  const auto root=std::filesystem::path(nh::Wide(m.games[0].target.path)).parent_path();std::filesystem::create_directories(root/L"NeuRotic/Installer");Write(root/L"NeuRotic/Installer/Current-Install.json","malformed existing receipt");
  m.SetNrDllChoice(m.games[0],{nh::NrDllChoiceMode::Profile,"removed-profile"});m.ChooseProxy(0);m.Plan("Install");
  check(m.installer.busy&&Read(m.installer.request).at("nrDllCopy")==Json{{"mode","preserve"}},"actual update path bypasses unavailable source acquisition for malformed current receipt");
  ResetQueueForNextCase(m);
 }
 {nh::HubModel m;m.nrDllManager=std::make_shared<nh::NrDllManager>(vault,true);m.nrDllManager->Refresh();DrainNr(m);m.games={Game(fixture,"explicit-removed")};m.selected=0;
  m.SetNrDllChoice(m.games[0],{nh::NrDllChoiceMode::Profile,"removed-profile"});m.Plan("Install");
  check(!m.installer.busy&&m.NrDllChoiceFor(m.games[0]).profileId=="removed-profile","missing explicit initial choice has no default fallback and remains saved");
  const auto saved=Read(nh::UserRoot()/L"library.json");bool persisted=false;for(const auto& row:saved.at("nrDllSelections"))persisted|=row.at("mode")=="profile"&&row.at("profileId")=="removed-profile";
  check(persisted,"unavailable explicit profile ID remains in actual saved library data");
  auto extended=saved;for(auto& row:extended.at("nrDllSelections"))row["futureSelectionMetadata"]={{"keep",true},{"opaque",Json::array({1,"user"})}};
  Write(nh::UserRoot()/L"library.json",extended.dump());m.Save();const auto roundTrip=Read(nh::UserRoot()/L"library.json");bool unknownKept=false;
  for(const auto& row:roundTrip.at("nrDllSelections"))unknownKept|=row.contains("futureSelectionMetadata")&&row.at("futureSelectionMetadata")==Json{{"keep",true},{"opaque",Json::array({1,"user"})}};
  check(unknownKept,"actual Save preserves unknown fields inside per-directory selection records");
  m.SetNrDllChoice(m.games[0],{nh::NrDllChoiceMode::Manual,{}});m.ChooseProxy(0);m.Plan("Install");
  auto alias=m.games[0];alias.id="same-folder-other-row";
  check(m.NrDllChoiceFor(alias).mode==nh::NrDllChoiceMode::Manual,"per-game choice follows actual installation folder across catalog row identities");
  check(m.installer.busy&&Read(m.installer.request).at("nrDllCopy")==Json{{"mode","none"}},"explicit manual choice installs without a managed provider");ResetQueueForNextCase(m);
 }
 {nh::HubModel m;m.nrDllManager=std::make_shared<nh::NrDllManager>(vault,true);m.nrDllManager->Refresh();DrainNr(m);m.games={Game(fixture,"cancel-source")};m.selected=0;m.SetNrDllChoice(m.games[0],{nh::NrDllChoiceMode::Profile,b.id});m.Plan("Install");
  check(!m.installer.busy&&m.NrDllPending(),"actual source acquisition precedes any Plan child");
  const auto id=m.CurrentQueuedInstall()->id;m.CancelQueuedInstall(id);DrainNr(m);
  check(!m.InstallQueueBusy()&&!m.NrDllPending()&&!m.installer.busy,"cancellation drains acquisition without starting a child or resuming the item");
 }
 {nh::HubModel m;m.nrDllManager=std::make_shared<nh::NrDllManager>(vault,true);m.nrDllManager->Refresh();DrainNr(m);m.games={Game(fixture,"completed-source")};m.selected=0;m.SetNrDllChoice(m.games[0],{nh::NrDllChoiceMode::Profile,b.id});m.ChooseProxy(0);m.Plan("Install");DrainNr(m);
  const auto request=Read(m.installer.request);const auto plan=Planned(request);Feed(m,plan);
  check(m.installer.busy&&!m.installer.readOnlyInstaller&&m.CurrentQueuedInstall()->nrDllLease!=nullptr,"accepted real queue writer retains the selected source lease");
  const Json done={{"status","Succeeded"},{"planId",plan.at("planId")},{"errors",Json::array()},{"skipped",Json::array()},{"inspection",{{"target",{{"executable",m.games[0].target.path},{"bitness",64}}},{"settings",Json::array()},{"configRevision","fixture"},{"state",{{"status","Installed"}}}}}};
  Feed(m,done);storage.Remove(b.id);
  check(!m.InstallQueueBusy()&&!m.InstallQueueItems()[0].nrDllLease&&storage.List().profiles.empty(),"natural writer receipt drain releases source despite retained completed history");
 }
 std::cout<<"RESULT checks="<<checks<<" failed="<<failed<<" ACTUAL_HUBMODEL=true INERT_PROVIDER_BYTES=true NO_GAME_LAUNCH=true\n";return failed;
}
#ifdef NEUROTIC_NR_DLL_QUEUE_STANDALONE
int main()try{return RunNrDllQueueTests()?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
#endif
