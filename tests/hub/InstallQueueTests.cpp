#include "OperationFlowTests.cpp"

namespace {
Json QueueDefaults(){
 const auto path=nh::UserRoot()/L"install-defaults.json";const auto state=nh::ReadInstallDefaults(path);
 const auto status=state.readOnly?"Invalid":std::filesystem::exists(path)?"Loaded":"PackageDefaults";
 return {{"status",status},{"preferences",std::string(status)=="Loaded"?state.preferences:Json()}};
}
Json QueuePlan(const Json& request,const char* id,const Json& defaults,bool warning=false){
 auto plan=Planned(request,warning);plan["planId"]=id;plan["planFingerprint"]=std::string(id)+"-fingerprint";plan["installDefaults"]=defaults;return plan;
}
Json QueueDone(const nh::Game& game,const char* id,const char* status="Succeeded"){
 return {{"status",status},{"planId",id},{"errors",Json::array()},{"skipped",Json::array()},{"inspection",{{"target",{{"executable",game.target.path},{"bitness",game.target.bitness}}},{"settings",Json::array()},{"configRevision","queue-fixture"},{"state",{{"status",std::string(status)=="Partial"?"Partial":"Installed"}}}}}};
}
}
int RunInstallQueueTests(){
 int checks=0,failed=0;auto check=[&](bool value,const char* label){++checks;failed+=!value;std::cout<<(value?"PASS ":"FAIL ")<<label<<std::endl;};
 const auto fixture=nh::UserRoot()/L"install-queue-controls";std::filesystem::create_directories(fixture);
 const auto defaults=QueueDefaults();
 {nh::HubModel m;m.games={Game(fixture,"fifo-a"),Game(fixture,"fifo-b"),Game(fixture,"fifo-c"),Game(fixture,"fifo-d")};m.selected=0;m.freshInstall=true;m.ChooseProxy(0);m.Plan("Install");
  const auto a=Read(m.installer.request);const auto childA=m.installer.request;
  check(m.InstallQueueItems().size()==1&&m.installer.busy&&m.installer.readOnlyInstaller&&a.value("freshInstall",false),"A is synchronously accepted with real sole Plan child and immutable Fresh Install true");
  m.Select(1);m.ChooseProxy(1);m.freshInstall=false;m.Plan("Install");m.Select(2);m.ChooseProxy(2);m.freshInstall=true;m.Plan("Install");
  check(m.selected==2&&m.InstallQueueItems().size()==3&&m.installer.request==childA&&m.installer.error.empty(),"browse and enqueue B/C preserve A child without cancellation or replacement");
  check(m.InstallQueueItems()[0].request.value("proxyName","")=="dxgi.dll"&&m.InstallQueueItems()[1].request.value("proxyName","")=="winmm.dll"&&m.InstallQueueItems()[2].request.value("proxyName","")=="version.dll"&&!m.InstallQueueItems()[1].request.value("freshInstall",true),"A/B/C retain distinct per-click proxy and Fresh Install options");
  check(!m.CanStartGame(),"queued installs do not acquire or start manual game launch");
  Feed(m,QueuePlan(a,"plan-a",defaults));const auto executeA=Read(m.installer.request);
  check(m.InstallQueueWorkerActive()&&!m.installer.readOnlyInstaller&&executeA.value("kind","")=="Execute"&&executeA.value("planId","")=="plan-a"&&m.selected==2,"A executes its exact accepted plan while C is browsed");
  m.Select(3);m.ChooseProxy(0);m.Plan("Install");
  check(m.selected==3&&m.InstallQueueItems().size()==4&&Read(m.installer.request)==executeA,"enqueue during actual writer ownership retains one unchanged A Execute child");
  m.Plan("Uninstall");m.BeginBulkUninstall();m.RequestDataMaintenance(0);m.StartGame();
  check(Read(m.installer.request)==executeA&&!m.BulkUninstallBusy()&&!m.showDataMaintenance,"uninstall bulk maintenance and Play cannot steal queue writer ownership");
  Feed(m,QueueDone(m.games[0],"plan-a"));const auto b=Read(m.installer.request);
  check(m.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Succeeded&&b.value("kind","")=="Plan"&&b.value("gameExecutable","")==m.games[1].target.path&&m.selected==3,"A completion advances FIFO B and keeps the browsed page independent");
  Feed(m,QueuePlan(b,"plan-b",defaults,true));const auto warning=m.operationIssue;
  m.Select(0);
  check(!m.installer.busy&&m.CurrentQueuedInstall()->game.id==m.games[1].id&&m.operationIssue==warning&&!m.showIssue,"B anti-cheat decision and exact target survive browsing A without automatic modal");
  m.AcknowledgeRisk();check(m.installer.busy&&!m.installer.readOnlyInstaller&&Read(m.installer.request).value("planId","")=="plan-b","explicit B consent executes only B despite A page selection");
  Feed(m,QueueDone(m.games[1],"plan-b"));const auto c=Read(m.installer.request);
  check(c.value("gameExecutable","")==m.games[2].target.path&&c.value("freshInstall",false)&&c.value("proxyName","")=="version.dll","C starts with its captured options after unrelated page changes");
  Feed(m,QueuePlan(c,"plan-c",defaults));Feed(m,QueueDone(m.games[2],"plan-c","Partial"));
  check(!m.installer.busy&&m.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention&&m.InstallQueueItems()[3].phase==nh::QueuedInstall::Phase::Queued,"partial C result visibly pauses FIFO before D");
  m.RetryQueuedInstall();check(m.installer.busy&&Read(m.installer.request).value("gameExecutable","")==m.games[2].target.path&&m.InstallQueueItems().size()==4,"explicit retry replans the same C item without duplicating intent");
  Feed(m,QueuePlan(Read(m.installer.request),"plan-c-retry",defaults));Feed(m,QueueDone(m.games[2],"plan-c-retry"));
  check(Read(m.installer.request).value("gameExecutable","")==m.games[3].target.path,"resolved C advances D in original FIFO order");
  m.CancelQueuedInstall(m.CurrentQueuedInstall()->id);Feed(m,{{"status","Cancelled"}});
  check(!m.InstallQueueBusy()&&m.InstallQueueItems()[3].phase==nh::QueuedInstall::Phase::Cancelled&&m.installer.action!="LaunchPreflight","explicit pre-write cancellation ends queue without automatic launch");Stop(m.installer);
 }
 {nh::HubModel m;m.games={Game(fixture,"duplicates")};auto alias=m.games[0];alias.id="same-installation-alias";m.games.push_back(alias);m.selected=0;m.freshInstall=true;m.ChooseProxy(0);m.Plan("Install");const auto child=m.installer.request;
  m.freshInstall=false;m.ChooseProxy(1);m.Plan("Install");m.Select(1);m.Plan("Install");
  check(m.InstallQueueItems().size()==1&&m.InstallQueueItems()[0].request.value("freshInstall",false)&&m.InstallQueueItems()[0].request.value("proxyName","")=="dxgi.dll"&&m.installer.request==child&&m.message.find("Already queued")!=std::string::npos,"duplicate alias and changed-option clicks explicitly refuse without replacing accepted intent");
  m.CancelQueuedInstall(m.CurrentQueuedInstall()->id);Feed(m,QueuePlan(Read(child),"cancel-race",defaults));
  check(!m.InstallQueueBusy()&&(!m.installer.busy||m.BackgroundInspectionActive())&&m.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Cancelled,"cancel at already-returned Plan boundary drains without executing stale plan");Stop(m.installer);
 }
 {nh::HubModel m;m.games={Game(fixture,"file-a"),Game(fixture,"file-b")};m.selected=0;m.ChooseProxy(0);m.Plan("Install");m.Select(1);m.ChooseProxy(1);m.Plan("Install");
  Feed(m,{{"status","NeedsDecision"},{"decisionKind","FileConflict"},{"target",m.games[0].target.path},{"canKeepReShade",true},{"fileConflicts",Json::array({{{"path","dxgi.dll"}},{{"path","other.dll"}}})}});
  const auto id=m.CurrentQueuedInstall()->id;m.Select(1);m.DecideFile("KeepReShade");
  check(!m.installer.busy&&m.fileReview.Active()&&m.CurrentQueuedInstall()->id==id,"first A file decision remains scoped to A while B is browsed");
  m.DecideFile("Skip");const auto request=Read(m.installer.request);
  check(request.value("gameExecutable","")==m.games[0].target.path&&request.value("existingProxyAction","")=="RenameReShade"&&request["fileDecisions"].size()==2&&m.InstallQueueItems().size()==2,"completed A file decisions replan the same immutable A request");
  Stop(m.installer);
 }
 {nh::HubModel m;m.games={Game(fixture,"malformed-a"),Game(fixture,"malformed-b")};m.selected=0;m.ChooseProxy(0);m.Plan("Install");const auto request=Read(m.installer.request);m.Select(1);m.ChooseProxy(0);m.Plan("Install");if(m.InstallQueueItems().size()!=2)throw std::runtime_error("SETUP: both queue targets must be admitted before ownership assertions");
  auto wrong=QueuePlan(request,"wrong-target",defaults);wrong["target"]["executable"]=m.games[1].target.path;Feed(m,wrong);
  check(!m.installer.busy&&m.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention&&m.InstallQueueItems()[1].phase==nh::QueuedInstall::Phase::Queued,"wrong-target receipt cannot execute A or advance B");
  m.RetryQueuedInstall();const auto retry=Read(m.installer.request);auto changed=QueuePlan(retry,"wrong-operation",defaults);changed["request"]["operation"]="Uninstall";Feed(m,changed);
  check(!m.installer.busy&&m.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention,"changed-operation plan cannot promote a queued install to destructive work");
  m.RetryQueuedInstall();const auto clean=Read(m.installer.request);Feed(m,QueuePlan(clean,"real-plan",defaults));const auto bound=Read(m.installer.request);
  Feed(m,QueuePlan(clean,"duplicate-plan",defaults));
  check(!m.installer.busy&&m.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention&&bound.value("planId","")=="real-plan","duplicate delayed Planned receipt cannot re-execute an already-owned writer");Stop(m.installer);
 }
 {nh::HubModel m;m.games={Game(fixture,"changed-a"),Game(fixture,"changed-b")};m.selected=0;m.ChooseProxy(0);m.Plan("Install");const auto a=Read(m.installer.request);m.Select(1);m.ChooseProxy(0);m.Plan("Install");
  {std::ofstream file(nh::Wide(m.games[1].target.path),std::ios::binary|std::ios::app);file.put('\0');}
  Feed(m,QueuePlan(a,"changed-a",defaults));Feed(m,QueueDone(m.games[0],"changed-a"));
  check(!m.installer.busy&&m.CurrentQueuedInstall()->game.id==m.games[1].id&&m.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention,"changed queued executable pauses before planning or writing replacement bytes");Stop(m.installer);
 }
 {const auto path=nh::UserRoot()/L"install-defaults.json";const bool existed=std::filesystem::exists(path);std::string original;if(existed){std::ifstream file(path,std::ios::binary);original.assign(std::istreambuf_iterator<char>(file),{});}
  nh::HubModel m;m.games={Game(fixture,"defaults-a"),Game(fixture,"defaults-b")};m.selected=0;m.ChooseProxy(0);m.Plan("Install");const auto a=Read(m.installer.request);const auto captured=QueueDefaults();m.Select(1);m.ChooseProxy(0);m.Plan("Install");
  auto prefs=Json{{"schemaVersion",1},{"style",2}};if(captured.value("preferences",Json())==prefs)prefs["style"]=1;Write(path,prefs.dump());
  Feed(m,QueuePlan(a,"defaults-a",captured));Feed(m,QueueDone(m.games[0],"defaults-a"));
  check(!m.installer.busy&&m.CurrentQueuedInstall()->game.id==m.games[1].id&&m.CurrentQueuedInstall()->phase==nh::QueuedInstall::Phase::NeedsAttention&&Read(path)==prefs,"changed global defaults cannot silently replace queued B click options");
  if(existed)Write(path,original);else std::filesystem::remove(path);m.RetryQueuedInstall();
  check(m.installer.busy&&Read(m.installer.request).value("gameExecutable","")==m.games[1].target.path,"restored captured defaults permit explicit B retry without global preference overwrite");Stop(m.installer);
 }
 {nh::HubModel m;m.games={Game(fixture,"close-plan-a"),Game(fixture,"close-plan-b")};m.selected=0;m.ChooseProxy(0);m.Plan("Install");m.Select(1);m.ChooseProxy(0);m.Plan("Install");if(m.InstallQueueItems().size()!=2)throw std::runtime_error("SETUP: both queue targets must be admitted before ownership assertions");Feed(m,{{"status","Cancelled"}},true);
  check(!m.InstallQueueBusy()&&!m.installer.busy&&m.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Cancelled&&m.InstallQueueItems()[1].phase==nh::QueuedInstall::Phase::Cancelled,"closing cancels planning and every unstarted queued item without advancing");Stop(m.installer);
 }
 {nh::HubModel m;m.games={Game(fixture,"close-write-a"),Game(fixture,"close-write-b")};m.selected=0;m.ChooseProxy(0);m.Plan("Install");Feed(m,QueuePlan(Read(m.installer.request),"close-writer",defaults));m.Select(1);m.ChooseProxy(0);m.Plan("Install");if(m.InstallQueueItems().size()!=2)throw std::runtime_error("SETUP: both queue targets must be admitted before ownership assertions");const auto writer=m.installer.request;m.PollClosing();
  check(m.installer.busy&&!m.installer.readOnlyInstaller&&m.installer.request==writer&&m.InstallQueueItems()[1].phase==nh::QueuedInstall::Phase::Cancelled,"closing preserves accepted active writer and cancels only unstarted B");
  Feed(m,QueueDone(m.games[0],"close-writer"),true);
  check(!m.InstallQueueBusy()&&!m.installer.busy&&m.InstallQueueItems()[0].phase==nh::QueuedInstall::Phase::Succeeded,"closing drains writer receipt without resuming canceled FIFO items");Stop(m.installer);
 }
 {nh::HubModel m;m.games={Game(fixture,"generic-lock-a"),Game(fixture,"generic-lock-b")};m.selected=0;m.installer.busy=true;m.installer.readOnlyInstaller=false;
  check(!m.TrySelectLibraryGame(1)&&m.selected==0,"generic non-queue mutation still locks library selection");m.installer.busy=false;
 }
 {nh::HubModel m;m.games={Game(fixture,"restore-warning-a"),Game(fixture,"restore-cached-b")};m.selected=1;
  auto inspected=[&](int index){return Json{{"status","Inspected"},{"inspection",{{"target",{{"executable",m.games[index].target.path},{"bitness",64}}},{"state",{{"status","Installed"},{"proxy","dxgi.dll"}}},{"configRevision","fixture-revision"},{"settings",Json::array()}}}};};
  m.RefreshSelected();Feed(m,inspected(1));m.Select(0);Feed(m,inspected(0));m.Plan("Install");auto prepared=Planned(Read(m.installer.request),true);Feed(m,prepared);m.Select(1);m.showIssue=false;
  const auto plan=m.plan,issue=m.operationIssue;const auto id=m.CurrentQueuedInstall()->id;
  m.RestoreSettingsDefaults();
  check(m.plan==plan&&m.operationIssue==issue&&m.CurrentQueuedInstall()->id==id&&!m.installer.busy&&!m.showIssue,"rejected cached B Restore Defaults preserves A queue plan, decision and presentation before any mutation");
  m.AcknowledgeRisk();check(m.installer.busy&&!m.installer.readOnlyInstaller&&Read(m.installer.request).value("kind","")=="Execute"&&Read(m.installer.request).value("planId","")==prepared.value("planId",std::string{}),"A consent still executes original plan after rejected B Restore Defaults");Stop(m.installer);
 }
 std::cout<<"RESULT checks="<<checks<<" failed="<<failed<<" ACTUAL_HUBMODEL=true PUBLIC_INERT_FILES=true NO_GAME_LAUNCH=true\n";return failed;
}
#ifdef NEUROTIC_INSTALL_QUEUE_STANDALONE
int main()try{return RunInstallQueueTests()?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
#endif
