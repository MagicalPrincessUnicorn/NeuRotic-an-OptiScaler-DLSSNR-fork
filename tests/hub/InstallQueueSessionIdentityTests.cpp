#include "InstallQueueIdentityOwnershipTests.cpp"

int RunInstallQueueSessionIdentityTests(){
 int checks=0,failed=0;auto check=[&](bool pass,const char* label){++checks;failed+=!pass;std::cout<<(pass?"PASS ":"FAIL ")<<label<<std::endl;};
 const auto root=nh::UserRoot()/L"queue-session-identity";std::filesystem::create_directories(root);IdentityContract::mode=IdentityContract::Mode::Native;
 {
  nh::HubModel model;model.games={Game(root,"replacement")};model.selected=0;model.RefreshSelected();Feed(model,OwnershipInspection(model.games[0]));OwnershipDraft(model);const auto revision=model.inspection.value("configRevision",std::string{});
  const auto executable=std::filesystem::path(nh::Wide(model.games[0].target.path));auto retired=executable;retired+=L".owned-old";std::filesystem::rename(executable,retired);std::filesystem::copy_file(retired,executable);
  model.RefreshSelected();auto fresh=OwnershipInspection(model.games[0]);fresh["inspection"]["configRevision"]="new-disk-revision";Feed(model,fresh);
  check(OwnershipDraftIntact(model)&&model.inspection.value("configRevision",std::string{})==revision&&model.inspectionCached,"same-path valid executable replacement preserves unsaved draft and original write revision");ResetQueueForNextCase(model);
 }
 {
  nh::HubModel model;model.games={Game(root,"disappearance")};model.selected=0;model.RefreshSelected();
  const auto executable=std::filesystem::path(nh::Wide(model.games[0].target.path));auto retired=executable;retired+=L".owned-removed";std::filesystem::rename(executable,retired);
  const auto receipt=nh::Json{{"status","FailedWithoutMutation"},{"reason","Owned executable disappeared during inspection"}};Feed(model,receipt);
  check(model.lastResult==receipt&&model.SelectedInspectionFailure().find("Owned executable disappeared")!=std::string::npos&&model.message.find("Owned executable disappeared")!=std::string::npos,"same selected path disappearance reports actual terminal inspection failure");ResetQueueForNextCase(model);
 }
 // A one-case public-state trace distinguishes target corruption from a
 // deliberately deferred B refresh. It does not rerun the eight controls.
 {
  const auto traceRoot=root/L"completion-trace";std::filesystem::create_directories(traceRoot);IdentityContract::fixtureRoot=traceRoot.wstring();auto [a,b]=OwnershipPair(traceRoot);
  if(std::filesystem::exists(nh::Wide(b.target.path))){IdentityContract::mode=IdentityContract::Mode::UpperId;nh::HubModel model;model.games={a,b};model.selected=0;model.ChooseProxy(0);model.Plan("Install");Feed(model,QueuePlan(Read(model.installer.request),"trace-completion",QueueDefaults()));model.Select(1);OwnershipDraft(model);model.gameDiagnosticsText="B diagnostics";const auto before=model.inspection;
   Feed(model,QueueDone(a,"trace-completion"));
   std::cout<<"COMPLETION_COMPONENTS "<<nh::Json{{"selectedB",model.selected==1},{"inspectionUnchanged",model.inspection==before},{"draftIntact",OwnershipDraftIntact(model)},{"diagnostics",model.gameDiagnosticsText},{"workerBusy",model.installer.busy},{"workerAction",model.installer.action},{"queueBusy",model.InstallQueueBusy()}}.dump()<<std::endl;ResetQueueForNextCase(model);
  }else std::cout<<"UNAVAILABLE completion trace case-spelling adapter\n";
 }
 IdentityContract::mode=IdentityContract::Mode::Native;std::cout<<"RESULT checks="<<checks<<" failed="<<failed<<" ACTUAL_HUBMODEL=true NATIVE_OWNED_FILES=true NO_GAME_LAUNCH=true\n";return failed;
}
#ifdef NEUROTIC_INSTALL_QUEUE_SESSION_IDENTITY_STANDALONE
int main()try{return RunInstallQueueSessionIdentityTests()?1:0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
#endif
