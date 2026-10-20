#include "InstallQueueTests.cpp"

namespace {
struct ReviewDefaultsRestore {
 std::filesystem::path path=nh::UserRoot()/L"install-defaults.json";bool existed=std::filesystem::exists(path);std::string original;
 ReviewDefaultsRestore(){if(existed){std::ifstream f(path,std::ios::binary);original.assign(std::istreambuf_iterator<char>(f),{});}}
 void Restore(){if(existed)Write(path,original);else std::filesystem::remove(path);}
 ~ReviewDefaultsRestore(){try{Restore();}catch(...){}}
};
void ReviewQueuePair(nh::HubModel& m,const std::filesystem::path& root,const char* a,const char* b){
 m.games={Game(root,a),Game(root,b)};m.selected=0;m.ChooseProxy(0);m.freshInstall=true;m.Plan("Install");
 m.Select(1);m.ChooseProxy(2);m.freshInstall=false;m.Plan("Install");
}
void ReviewStartWriter(nh::HubModel& m,const char* id){Feed(m,QueuePlan(Read(m.installer.request),id,QueueDefaults()));}
Json ReviewWarning(const std::string& target,const char* status,const char* scan){
 return {{"status",status},{"decisionKind","AntiCheatRisk"},{"target",target},{"reason","Fixture fresh anti-cheat decision"},
  {"antiCheat",{{"scan_status",scan},{"acknowledgementRequired",true},{"fingerprint","fixture-fresh-risk"},{"findings",Json::array()}}}};
}
bool ReviewHasFinding(const Json& issue,const Json& finding){
 if(!issue.is_object()||!issue.value("antiCheat",Json()).is_object())return false;
 const auto stale=issue.at("antiCheat").value("stale_findings",Json::array());
 return stale.is_array()&&std::find(stale.begin(),stale.end(),finding)!=stale.end();
}
}

// BEGIN_R3C_HELPERS
namespace {
Json ReviewFreshInspection(const nh::Game& game){
 return {{"status","Inspected"},{"inspection",{{"target",{{"executable",game.target.path},{"bitness",game.target.bitness}}},{"state",{{"status","Installed"},{"proxy","dxgi.dll"}}},{"configRevision","fixture-current-before-writer"},{"settings",Json::array()}}}};
}
void ReviewKeepDraft(nh::HubModel& model){
 nh::SettingsField draft;draft.section="Fixture";draft.key="Draft";draft.type="text";draft.original="saved";draft.available=true;
 strcpy_s(draft.draft.data(),draft.draft.size(),"keep unsaved draft");model.settings.push_back(std::move(draft));
}
bool ReviewDraftIntact(const nh::HubModel& model){return model.settings.size()==1&&std::string(model.settings[0].draft.data())=="keep unsaved draft";}
}
// END_R3C_HELPERS

int RunInstallQueueReviewTests(){
 int checks=0,failed=0;auto check=[&](bool value,const char* label){++checks;failed+=!value;std::cout<<(value?"PASS ":"FAIL ")<<label<<std::endl;};
 const auto root=nh::UserRoot()/L"queue-review-controls";std::filesystem::create_directories(root);
 for(bool wrong:{false,true}){
  nh::HubModel m;ReviewQueuePair(m,root,wrong?"wrong-cancel-a":"missing-cancel-a",wrong?"wrong-cancel-b":"missing-cancel-b");
  ReviewStartWriter(m,"bound-writer");Json result={{"status","Cancelled"},{"target",m.games[0].target.path}};if(wrong)result["planId"]="another-plan";
  Feed(m,result);
  check(!m.installer.busy&&m.CurrentQueuedInstall()&&m.CurrentQueuedInstall()->game.id==m.games[0].id&&m.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention&&m.InstallQueueItems()[1].phase==nh::QueuedInstall::Phase::Queued,
   wrong?"writer Cancelled with wrong plan identity cannot advance FIFO":"writer Cancelled without accepted plan identity cannot advance FIFO");
  ResetQueueForNextCase(m);
 }
 {
  nh::HubModel m;ReviewQueuePair(m,root,"valid-cancel-a","valid-cancel-b");ReviewStartWriter(m,"valid-writer");
  Feed(m,{{"status","Cancelled"},{"target",m.games[0].target.path},{"planId","valid-writer"},{"reason","Fixture accepted writer cancellation"}});
  check(m.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Cancelled&&m.installer.busy&&Read(m.installer.request).value("gameExecutable","")==m.games[1].target.path,
   "genuinely bound writer Cancelled result may advance the next independent installation");ResetQueueForNextCase(m);
 }
 {
  nh::HubModel m;ReviewQueuePair(m,root,"readonly-cancel-a","readonly-cancel-b");m.CancelQueuedInstall(m.CurrentQueuedInstall()->id);
  Feed(m,{{"status","Cancelled"},{"target",m.games[0].target.path}});
  check(m.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Cancelled&&m.installer.busy&&Read(m.installer.request).value("gameExecutable","")==m.games[1].target.path,
   "genuine read-only Plan cancellation still drains without a writer plan identity");ResetQueueForNextCase(m);
 }
 for(const auto status:{"Partial","RecoveryRequired","FailedWithoutChanges"}){
  nh::HubModel m;const auto a=std::string(status)+"-a",b=std::string(status)+"-b";ReviewQueuePair(m,root,a.c_str(),b.c_str());ReviewStartWriter(m,"writer-outcome");
  Json receipt=std::string(status)=="Partial"?QueueDone(m.games[0],"writer-outcome","Partial"):Json{{"status",status},{"target",m.games[0].target.path},{"planId","writer-outcome"}};
  receipt["reason"]=std::string("Fixture actual writer ")+status;Feed(m,receipt);const auto id=m.CurrentQueuedInstall()->id;
  m.CancelQueuedInstall(id);const auto& retained=m.InstallQueueItems()[0];
  check(retained.phase==nh::QueuedInstall::Phase::Failed&&retained.result==receipt&&retained.summary.find(receipt.at("reason").get<std::string>())!=std::string::npos&&retained.summary.find("Existing game files were preserved")==std::string::npos&&m.CurrentQueuedInstall()&&m.CurrentQueuedInstall()->game.id==m.games[1].id,
   (std::string("abandoning further retries preserves actual writer ")+status+" outcome and warning").c_str());ResetQueueForNextCase(m);
 }
 {
  nh::HubModel m;ReviewQueuePair(m,root,"close-recovery-a","close-recovery-b");ReviewStartWriter(m,"close-recovery");
  Json receipt={{"status","RecoveryRequired"},{"target",m.games[0].target.path},{"planId","close-recovery"},{"reason","Fixture retained recovery warning"}};Feed(m,receipt);m.PollClosing();
  check(m.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Failed&&m.InstallQueueItems()[0].result==receipt&&m.InstallQueueItems()[0].summary.find("Fixture retained recovery warning")!=std::string::npos&&m.InstallQueueItems()[1].phase==nh::QueuedInstall::Phase::Cancelled,
   "closing after a writer outcome preserves recovery warning while cancelling unstarted work");Stop(m.installer);
 }
 {
  nh::HubModel m;ReviewQueuePair(m,root,"retry-cancel-partial-a","retry-cancel-partial-b");ReviewStartWriter(m,"retry-partial");
  auto receipt=QueueDone(m.games[0],"retry-partial","Partial");receipt["reason"]="Fixture previous partial writer";Feed(m,receipt);m.RetryQueuedInstall();
  m.CancelQueuedInstall(m.CurrentQueuedInstall()->id);Feed(m,{{"status","Cancelled"},{"target",m.games[0].target.path}});
  check(m.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Failed&&m.InstallQueueItems()[0].result==receipt&&m.InstallQueueItems()[0].summary.find("Fixture previous partial writer")!=std::string::npos&&m.CurrentQueuedInstall()&&m.CurrentQueuedInstall()->game.id==m.games[1].id,
   "cancelling a read-only retry retains the earlier partial writer outcome");ResetQueueForNextCase(m);
 }
 {
  ReviewDefaultsRestore restore;nh::HubModel m;ReviewQueuePair(m,root,"cancel-defaults-a","cancel-defaults-b");const auto captured=QueueDefaults();
  auto changed=Json{{"schemaVersion",1},{"style",2}};if(captured.value("preferences",Json())==changed)changed["style"]=1;Write(restore.path,changed.dump());
  m.CancelQueuedInstall(m.CurrentQueuedInstall()->id);Feed(m,{{"status","Cancelled"},{"target",m.games[0].target.path}});
  check(!m.installer.busy&&m.CurrentQueuedInstall()&&m.CurrentQueuedInstall()->game.id==m.games[1].id&&m.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention,
   "after A Plan cancellation B visibly pauses on its own captured defaults drift");
  restore.Restore();m.RetryQueuedInstall();
  check(m.installer.busy&&Read(m.installer.request).value("gameExecutable","")==m.games[1].target.path&&Read(m.installer.request).value("proxyName","")=="version.dll"&&!Read(m.installer.request).value("freshInstall",true),
   "restoring defaults and retrying B uses B immutable intent rather than cancelled A");ResetQueueForNextCase(m);
 }
 // BEGIN_R3B
 for(const auto status:{"Partial","RecoveryRequired"}){
  nh::HubModel m;const auto a=std::string("automatic-")+status+"-a",b=std::string("automatic-")+status+"-b";ReviewQueuePair(m,root,a.c_str(),b.c_str());ReviewStartWriter(m,"prior-auto-cancel-writer");
  Json receipt=std::string(status)=="Partial"?QueueDone(m.games[0],"prior-auto-cancel-writer","Partial"):Json{{"status",status},{"target",m.games[0].target.path},{"planId","prior-auto-cancel-writer"}};
  receipt["reason"]=std::string("Fixture unresolved prior writer ")+status;Feed(m,receipt);m.RetryQueuedInstall();
  if(!m.installer.busy||!m.installer.readOnlyInstaller)throw std::runtime_error("Fixture retry did not start its real read-only Plan");
  // No CancelQueuedInstall call: the accepted read-only worker itself returns Cancelled.
  Feed(m,{{"status","Cancelled"},{"target",m.games[0].target.path},{"reason","Fixture retry Plan cancelled itself"}});
  const auto& item=m.InstallQueueItems()[0];
  check(item.phase==nh::QueuedInstall::Phase::Failed&&item.result==receipt&&item.summary.find(receipt.at("reason").get<std::string>())!=std::string::npos&&m.CurrentQueuedInstall()&&m.CurrentQueuedInstall()->game.id==m.games[1].id&&m.installer.busy&&Read(m.installer.request).value("gameExecutable","")==m.games[1].target.path,
   (std::string("automatic read-only retry cancellation preserves prior ")+status+" writer result and warning").c_str());ResetQueueForNextCase(m);
 }
 // END_R3B
 // BEGIN_R3C_CASES
 {
  nh::HubModel m;m.games={Game(root,"fresh-view-recovery-a")};m.selected=0;m.RefreshSelected();Feed(m,ReviewFreshInspection(m.games[0]));
  if(m.inspectionCached||m.inspection.value("state",Json()).value("status","")!="Installed")throw std::runtime_error("Fixture requires an authentic fresh Installed observation");
  const auto previous=m.inspection;ReviewKeepDraft(m);m.gameDiagnosticsText="Fixture retained diagnostics";m.ChooseProxy(0);m.Plan("Install");ReviewStartWriter(m,"freshness-recovery");
  const bool savedAtStart=m.inspectionCached&&m.inspection==previous&&ReviewDraftIntact(m)&&m.gameDiagnosticsText=="Fixture retained diagnostics";
  Feed(m,{{"status","RecoveryRequired"},{"target",m.games[0].target.path},{"planId","freshness-recovery"},{"reason","Fixture uncertain writer outcome without fresh inspection"}});
  check(savedAtStart&&m.inspectionCached&&m.inspection==previous&&ReviewDraftIntact(m)&&m.gameDiagnosticsText=="Fixture retained diagnostics",
   "accepted writer and uncertain outcome mark the owned prior inspection saved without deleting snapshot draft or diagnostics");ResetQueueForNextCase(m);
 }
 {
  nh::HubModel m;m.games={Game(root,"fresh-view-success-a")};m.selected=0;m.RefreshSelected();Feed(m,ReviewFreshInspection(m.games[0]));m.ChooseProxy(0);m.Plan("Install");ReviewStartWriter(m,"freshness-success");
  const auto fresh=QueueDone(m.games[0],"freshness-success");Feed(m,fresh);
  check(!m.inspectionCached&&m.inspection==fresh.at("inspection")&&m.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Succeeded,
   "authentic fresh successful writer inspection restores current status");Stop(m.installer);
 }
 {
  nh::HubModel m;m.games={Game(root,"fresh-view-owner-a"),Game(root,"fresh-view-other-b")};m.selected=1;m.RefreshSelected();Feed(m,ReviewFreshInspection(m.games[1]));m.gameDiagnosticsText="Fixture B diagnostics";
  m.Select(0);Feed(m,ReviewFreshInspection(m.games[0]));m.ChooseProxy(0);m.Plan("Install");m.Select(1);const auto previous=m.inspection;const bool cached=m.inspectionCached;ReviewKeepDraft(m);
  ReviewStartWriter(m,"freshness-other-target");Feed(m,{{"status","RecoveryRequired"},{"target",m.games[0].target.path},{"planId","freshness-other-target"},{"reason","Fixture A writer outcome while B displayed"}});
  check(m.selected==1&&m.inspection==previous&&m.inspectionCached==cached&&ReviewDraftIntact(m)&&m.gameDiagnosticsText=="Fixture B diagnostics",
   "A writer never changes the unrelated displayed B inspection freshness snapshot draft or diagnostics");ResetQueueForNextCase(m);
 }
 // END_R3C_CASES
 const Json positive={{"provider","FixturePositiveAntiCheat"},{"state","detected"}};
 for(const auto status:{"NeedsDecision","PreconditionChanged"}){
  nh::HubModel m;const auto a=std::string(status)+"-risk-a",b=std::string(status)+"-risk-b";ReviewQueuePair(m,root,a.c_str(),b.c_str());
  auto plan=QueuePlan(Read(m.installer.request),"prior-risk",QueueDefaults(),true);plan["antiCheat"]["findings"]=Json::array({positive});Feed(m,plan);m.RetryQueuedInstall();
  Feed(m,ReviewWarning(m.games[0].target.path,status,"incomplete"));
  check(ReviewHasFinding(m.operationIssue,positive)&&!m.installer.busy&&!m.showIssue&&m.operationIssue.value("target","")==m.games[0].target.path,
   (std::string("incomplete same-target ")+status+" retains prior positive evidence and requires an explicit new decision").c_str());ResetQueueForNextCase(m);
 }
 {
  nh::HubModel m;ReviewQueuePair(m,root,"cross-risk-a","cross-risk-b");m.operationIssue={{"target",m.games[1].target.path},{"antiCheat",{{"findings",Json::array({positive})}}}};
  Feed(m,ReviewWarning(m.games[0].target.path,"NeedsDecision","incomplete"));
  check(!ReviewHasFinding(m.operationIssue,positive)&&m.operationIssue.value("target","")==m.games[0].target.path,
   "anti-cheat evidence from another queued target is never merged into A");ResetQueueForNextCase(m);
 }
 {
  nh::HubModel m;ReviewQueuePair(m,root,"complete-risk-a","complete-risk-b");auto plan=QueuePlan(Read(m.installer.request),"complete-prior-risk",QueueDefaults(),true);plan["antiCheat"]["findings"]=Json::array({positive});Feed(m,plan);m.RetryQueuedInstall();
  Feed(m,ReviewWarning(m.games[0].target.path,"NeedsDecision","complete"));
  check(!ReviewHasFinding(m.operationIssue,positive),"a new complete scan does not inherit obsolete stale anti-cheat findings");ResetQueueForNextCase(m);
 }
 {
  nh::HubModel m;ReviewQueuePair(m,root,"consent-risk-a","consent-risk-b");auto plan=QueuePlan(Read(m.installer.request),"consented-risk",QueueDefaults(),true);plan["antiCheat"]["findings"]=Json::array({positive});Feed(m,plan);m.AcknowledgeRisk();
  auto changed=ReviewWarning(m.games[0].target.path,"PreconditionChanged","incomplete");changed["planId"]="consented-risk";Feed(m,changed);
  m.Execute();const bool noOldWriter=!m.installer.busy&&!m.showIssue&&m.CurrentQueuedInstall()&&m.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention;
  m.AcknowledgeRisk();
  check(noOldWriter&&m.installer.busy&&m.installer.readOnlyInstaller&&Read(m.installer.request).value("kind","")=="Plan"&&Read(m.installer.request).value("gameExecutable","")==m.games[0].target.path,
   "new precondition warning cannot execute old consent and acknowledgement requests a fresh Plan");ResetQueueForNextCase(m);
 }
 {
  nh::HubModel m;m.games={Game(root,"physical installation with a long ordinary directory name")};m.selected=0;m.ChooseProxy(0);m.freshInstall=true;m.Plan("Install");const auto child=m.installer.request;
  std::vector<wchar_t> buffer(32768);const auto length=GetShortPathNameW(nh::Wide(m.games[0].target.path).c_str(),buffer.data(),static_cast<DWORD>(buffer.size()));
  const auto shortPath=length&&length<buffer.size()?nh::Utf8(std::wstring(buffer.data(),length)):std::string{};
  const bool available=!shortPath.empty()&&_stricmp(shortPath.c_str(),m.games[0].target.path.c_str())!=0;
  std::cout<<"PHYSICAL_ALIAS_CAPABILITY_AVAILABLE="<<available<<" NO_GLOBAL_8DOT3_CHANGES=true"<<std::endl;
  if(available){auto alias=m.games[0];alias.id="physical-short-path-alias";alias.target.path=shortPath;alias.root=nh::Utf8(std::filesystem::path(nh::Wide(shortPath)).parent_path().wstring());m.games.push_back(alias);m.Select(1);m.ChooseProxy(1);m.freshInstall=false;m.Plan("Install");
   check(m.InstallQueueItems().size()==1&&m.installer.request==child&&m.InstallQueueItems()[0].request.value("proxyName","")=="dxgi.dll"&&m.InstallQueueItems()[0].request.value("freshInstall",false)&&m.message.find("Already queued")!=std::string::npos,
    "actual ordinary short-name physical alias refuses conflicting options without changing first request");
  }else std::cout<<"PHYSICAL_ALIAS_RUNTIME_CONTROL_UNAVAILABLE: owned volume supplied no distinct short path"<<std::endl;
  ResetQueueForNextCase(m);
 }
 {
  nh::HubModel m;ReviewQueuePair(m,root,"distinct-physical-folder-a","distinct-physical-folder-b");
  check(m.InstallQueueItems().size()==2&&m.InstallQueueItems()[0].game.target.path!=m.InstallQueueItems()[1].game.target.path,
   "two distinct physical installation directories remain independently queueable");ResetQueueForNextCase(m);
 }
 std::cout<<"RESULT review_checks="<<checks<<" failed="<<failed<<" ACTUAL_HUBMODEL=true NO_GAME_LAUNCH=true"<<std::endl;return failed;
}
#ifdef NEUROTIC_INSTALL_QUEUE_REVIEW_STANDALONE
int main()try{return RunInstallQueueReviewTests()?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 2;}
#endif
