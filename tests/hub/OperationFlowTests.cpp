#include "ui/HubViewModel.h"
#include "library/GameCatalog.h"
#include "install/PlanIntent.h"
#include "ui/GameScreenshots.h"
#include <fstream>
#include <iostream>
#include <nr/semantic/object_rules/ObjectRuleEditor.h>

namespace {
using nh::Json;
void Write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary);file<<text;if(!file)throw std::runtime_error("Fixture write failed");}
Json Read(const std::filesystem::path& path){std::ifstream file(path);return Json::parse(file);}
void Stop(nh::ProcessJob& job){if(job.process){if(WaitForSingleObject(job.process,0)!=WAIT_OBJECT_0){TerminateProcess(job.process,3);WaitForSingleObject(job.process,5000);}CloseHandle(job.process);job.process=nullptr;}job.busy=false;job.error.clear();}
void Feed(nh::HubModel& model,const Json& value){
 Stop(model.installer);Write(model.installer.result,value.dump());
 wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);auto exe=std::filesystem::path(system)/L"cmd.exe";auto command=nh::Quote(exe.wstring())+L" /d /c exit 0";
 STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))throw std::runtime_error("Fixture child failed");
 CloseHandle(child.hThread);WaitForSingleObject(child.hProcess,5000);model.installer.process=child.hProcess;model.installer.busy=true;model.Poll();
}
nh::Game Game(const std::filesystem::path& root,const char* id){auto dir=root/id;std::filesystem::create_directories(dir);auto exe=dir/L"Game.exe";wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);std::filesystem::copy_file(std::filesystem::path(system)/L"cmd.exe",exe,std::filesystem::copy_options::overwrite_existing);nh::Game game;game.id=game.title=id;game.store="Manual";game.root=nh::Utf8(dir.wstring());game.target.path=nh::Utf8(exe.wstring());game.target.bitness=64;game.target.suitable=true;return game;}
Json Planned(const Json& request,bool warning=false){return {{"status","Planned"},{"planId","fixture-plan"},{"planFingerprint","fixture-fingerprint"},{"request",request},{"target",{{"executable",request.at("gameExecutable")},{"directory",nh::Utf8(std::filesystem::path(nh::Wide(request.at("gameExecutable").get<std::string>())).parent_path().wstring())},{"fileIdentity","fixture-sha"}}},{"antiCheat",{{"scan_status","complete"},{"acknowledgementRequired",warning},{"fingerprint","fixture-risk"}}}};}
}

int RunAutomaticSetupNoticeTests();
int RunOperationFlowTests(){
 int failed=0;auto check=[&](bool pass,const char* label){std::cout<<(pass?"PASS ":"FAIL ")<<label<<std::endl;failed+=!pass;};
#ifdef NEUROTIC_OPERATION_FLOW_STANDALONE
 failed+=RunAutomaticSetupNoticeTests();
#endif
 auto fixture=nh::UserRoot()/L"operation-flow";std::filesystem::create_directories(fixture);nh::HubModel model;model.games={Game(fixture,"one"),Game(fixture,"two")};model.selected=0;
 {nh::HubModel returned;returned.games={Game(fixture,"receipt")};returned.selected=0;
  auto receipt=Read("tests/hub/fixtures/control-install-cd94d8e1.json");
  // Rebind only the inert target; retain the returned 421 setting rows and null metadata.
  receipt["inspection"]["target"]["executable"]=returned.games[0].target.path;
  returned.Plan("Install");Feed(returned,receipt);
  check(returned.inspection.is_object()&&returned.inspection["state"]["status"]=="Installed"&&returned.settings.size()==421&&returned.message.find("could not be read")==std::string::npos,"returned successful install receipt preserves Installed and all settings through actual Poll");
  receipt["status"]="Inspected";returned.RefreshSelected();Feed(returned,receipt);
  check(returned.inspection.is_object()&&returned.inspection["state"]["status"]=="Installed"&&returned.settings.size()==421,"Inspect refresh accepts old nullable translation metadata");
  returned.Select(-1);returned.Select(0);Stop(returned.installer);
  check(returned.inspection.is_object()&&returned.inspection["state"]["status"]=="Installed"&&returned.settings.size()==421,"cached selection keeps Installed and returned settings");
  receipt["status"]="Succeeded";receipt["inspection"]["state"]["autoSetup"]={{"schemaVersion",1},{"state","NeedsModel"},{"runtime","NotAttempted"},{"reason","Import the Neural Rendering model in NR Runtime."}};returned.Plan("Install");Feed(returned,receipt);
  check(returned.inspection["state"]["status"]=="Installed"&&returned.message.find("Import")!=std::string::npos,"successful base installation exposes missing model preparation");
  receipt["inspection"]["state"]["autoSetup"]["state"]="ReadyForRuntime";receipt["inspection"]["state"]["autoSetup"]["reason"]="Ready";returned.Plan("Install");Feed(returned,receipt);
  check(returned.inspection["state"]["status"]=="Installed"&&returned.message.find("Game validation is pending")!=std::string::npos,"prepared installation reports pending game validation after actual Poll");
 }
 {nh::HubModel choices;choices.games=model.games;choices.selected=0;choices.Plan("Install");
  Feed(choices,{{"status","NeedsDecision"},{"decisionKind","FileConflict"},{"target",choices.games[0].target.path},{"canKeepReShade",true},{"fileConflicts",Json::array({{{"path","dxgi.dll"}},{{"path","other.dll"}}})}});
  choices.DecideFile("KeepReShade");check(!choices.installer.busy&&choices.fileReview.Active(),"Keep ReShade waits for a following foreign-file decision without starting mutation");
  choices.DecideFile("Skip");auto request=Read(choices.installer.request);check(request.value("existingProxyAction","")=="RenameReShade"&&request["fileDecisions"].size()==2&&request["fileDecisions"][1]["action"]=="Skip","remaining Skip retains the focused ReShade choice in the shared request");Stop(choices.installer);
 }
 {auto one=Game(fixture,"screenshots-one"),two=Game(fixture,"screenshots-two");auto root=std::filesystem::path(nh::Wide(one.target.path)).parent_path();auto folder=root/L"NeuroticScreenshots";
  if(std::filesystem::exists(folder)&&std::filesystem::is_empty(folder))std::filesystem::remove(folder);
  check(!std::filesystem::exists(folder),"screenshots are not created during game selection or inspection");
  check(nh::EnsureSelectedGameScreenshots(one.target)==folder&&std::filesystem::is_directory(folder),"explicit native screenshots shortcut creates only the selected default leaf");
  auto second=nh::EnsureSelectedGameScreenshots(two.target);check(second!=folder&&second.parent_path()==std::filesystem::path(nh::Wide(two.target.path)).parent_path(),"screenshots shortcut follows the current game and never the NR Anything store");
  one.target.bitness=32;auto legacy=root/L"NeuRotic/Prepared/NeuRotic.GpuHost/NeuroticScreenshots";for(auto clear=legacy;clear!=root&&std::filesystem::exists(clear)&&std::filesystem::is_empty(clear);clear=clear.parent_path())std::filesystem::remove(clear);bool refused=false;
  try{nh::EnsureSelectedGameScreenshots(one.target);}catch(...){refused=true;}check(refused&&!std::filesystem::exists(root/L"NeuRotic"),"retired screenshot route never recreates an absent helper tree");
  std::filesystem::create_directories(legacy);check(nh::EnsureSelectedGameScreenshots(one.target)==legacy,"existing historical helper captures remain accessible");
  one.target.bitness=64;std::filesystem::remove(folder);Write(folder,"foreign screenshots-name occupant");refused=false;try{nh::EnsureSelectedGameScreenshots(one.target);}catch(...){refused=true;}std::ifstream occupant(folder);std::string bytes((std::istreambuf_iterator<char>(occupant)),{});occupant.close();check(refused&&bytes=="foreign screenshots-name occupant","screenshots shortcut preserves an unexpected file and reports its path unavailable");std::filesystem::remove(folder);
 }
 {auto path=nh::UserRoot()/L"library.json";const bool existed=std::filesystem::exists(path);std::ifstream input(path,std::ios::binary);std::string previous((std::istreambuf_iterator<char>(input)),{});input.close();
  nh::HubModel bounded;bounded.Save();auto before=Read(path);auto entry=model.games[0];entry.title=std::string(4000,'x');bounded.games.assign(2200,entry);bounded.Save();
  check(bounded.message.find("size limit")!=std::string::npos&&Read(path)==before,"AUDIT-DESK04: oversized library preserves the reloadable previous document");
  if(existed)Write(path,previous);else std::filesystem::remove(path);
 }
 model.StartGame();check(model.installer.busy&&Read(model.installer.request).value("kind","")=="LaunchPreflight"&&!model.CanStartGame(),"AUDIT-DESK02: Play performs current preflight before launching");
 {auto root=std::filesystem::path(nh::Wide(model.games[0].target.path)).parent_path();auto competing=std::async(std::launch::async,[root]{try{nh::LaunchAdmission second;second.Enter(root);return true;}catch(...){return false;}});check(!competing.get(),"AUDIT-DESK03: launch admission excludes another operation during preflight");}
 Feed(model,{{"status","FailedWithoutMutation"},{"reason","Fixture pending recovery"}});check(model.CanStartGame()&&model.message=="Fixture pending recovery","AUDIT-DESK02: failed preflight launches nothing and releases admission");
 {nh::LaunchAdmission admission;admission.Enter(std::filesystem::path(nh::Wide(model.games[0].target.path)).parent_path());nh::ProcessJob preflight;preflight.StartInstaller({{"kind","LaunchPreflight"},{"gameExecutable",model.games[0].target.path}});Json receipt;bool complete=false;auto deadline=GetTickCount64()+30000;while(GetTickCount64()<deadline){if(preflight.Poll(receipt)){complete=true;break;}Sleep(5);}bool verified=false;if(complete&&receipt.value("status","")=="LaunchReady"){try{admission.VerifyStat(nh::Wide(model.games[0].target.path),receipt["target"]["fileIdentity"].get<std::string>());verified=true;}catch(const std::exception& e){std::cout<<e.what()<<'\n';}}else std::cout<<receipt.dump()<<'\n';if(preflight.busy)Stop(preflight);check(verified,"AUDIT-DESK02: actual PowerShell preflight stat passes native pinned-file verification without launching a game");}
 {Json intent={{"kind","Plan"},{"operation","Install"},{"packageId","flagship-approved"}};Json returned={{"request",intent},{"requestedPackageId","flagship-approved"},{"packageDigest",std::string(64,'a')},{"resolvedPackage",{{"id","prepared-x86"},{"digest",std::string(64,'a')},{"architecture",32},{"route","prepared-x86"}}}};returned["request"]["packageId"]="prepared-x86";
  bool accepted=false;try{nh::ValidatePlanIntent(intent,returned,32);accepted=true;}catch(...){}check(!accepted,"AUDIT-DESK01: unsupported x86 package resolution cannot promote a prepared route");
  for(int change=0;change<5;++change){auto bad=returned;if(change==0)bad["request"]["operation"]="Uninstall";if(change==1)bad["resolvedPackage"]["digest"]=std::string(64,'b');if(change==2)bad["requestedPackageId"]="foreign";if(change==4)bad["request"]["adoptModifiedInstall"]=true;bool refused=false;try{nh::ValidatePlanIntent(intent,bad,change==3?64:32);}catch(...){refused=true;}check(refused,"AUDIT-DESK01: changed action, identity, digest, architecture or added control refuses");}
 }

 {nh::Game known;known.title="DOOM Eternal";check(nh::RecommendedProxy(known)=="winmm.dll"&&nh::RecommendedProxyIndex(known)==1,"known Vulkan title recommends winmm");known.title="Dragon's Dogma 2";check(nh::RecommendedProxy(known)=="dxgi.dll","known DX12 title recommends dxgi");known.title="Red Dead Redemption 2";check(nh::RecommendedProxy(known).empty(),"multi-API title does not guess a renderer");known.title="Unrecognized game";check(nh::RecommendedProxy(known).empty(),"unknown title does not invent a recommendation");}

 {nh::HubModel choices;choices.games={Game(fixture,"proxy-choice")};choices.games[0].title="DOOM Eternal";choices.selected=0;
  const Json uninstalled={{"status","Inspected"},{"inspection",{{"state",nullptr},{"settings",Json::array()}}}};
  choices.RefreshSelected();Feed(choices,uninstalled);check(choices.proxy==1&&choices.SelectedProxyName()=="winmm.dll","fresh Vulkan inspection selects winmm automatically");
  choices.Plan("Install");check(Read(choices.installer.request).value("proxyName",std::string{})=="winmm.dll","displayed Vulkan recommendation is the filename sent to Install");Stop(choices.installer);
  choices.ChooseProxy(2);choices.RefreshSelected();Feed(choices,uninstalled);check(choices.proxy==2,"manual proxy choice survives a fresh inspection");
  choices.Plan("Install");check(Read(choices.installer.request).value("proxyName",std::string{})=="version.dll","explicit proxy preference remains pinned in Install");Stop(choices.installer);
  nh::HubModel existing;existing.games=choices.games;existing.selected=0;existing.RefreshSelected();auto installed=uninstalled;installed["inspection"]["state"]={{"status","Installed"},{"proxy","dbghelp.dll"}};Feed(existing,installed);check(existing.proxy==3,"installed explicit proxy overrides API recommendation");
  existing.Plan("Install");check(Read(existing.installer.request).value("proxyName",std::string{})=="dbghelp.dll","update retains the installed receipt proxy");Stop(existing.installer);
 }
 {nh::HubModel unresolved;unresolved.games={Game(fixture,"proxy-unresolved")};unresolved.selected=0;
  const Json uninstalled={{"status","Inspected"},{"inspection",{{"state",nullptr},{"settings",Json::array()}}}};
  unresolved.RefreshSelected();Feed(unresolved,uninstalled);unresolved.Plan("Install");check(unresolved.SelectedProxyName().empty()&&!Read(unresolved.installer.request).contains("proxyName"),"unknown profile leaves integration unresolved rather than guessing dxgi");
  Feed(unresolved,{{"status","NeedsDecision"},{"decisionKind","ProxyRequired"}});check(unresolved.showIssue&&!unresolved.installer.busy,"unresolved integration reaches the proxy decision");
  unresolved.ChooseProxy(0);unresolved.Plan("Install");check(unresolved.SelectedProxyName()=="dxgi.dll"&&Read(unresolved.installer.request).value("proxyName",std::string{})=="dxgi.dll"&&!unresolved.showIssue,"selecting the displayed default in the proxy decision pins it for retry");Stop(unresolved.installer);
  unresolved.RefreshSelected();Feed(unresolved,uninstalled);unresolved.Plan("Install");check(Read(unresolved.installer.request).value("proxyName",std::string{})=="dxgi.dll","explicit dxgi choice survives inspection refresh");Stop(unresolved.installer);
  nh::HubModel ambiguous;ambiguous.games={Game(fixture,"proxy-ambiguous")};ambiguous.games[0].title="Red Dead Redemption 2";ambiguous.selected=0;ambiguous.Plan("Install");check(ambiguous.SelectedProxyName().empty()&&!Read(ambiguous.installer.request).contains("proxyName"),"multi-API profile requires an explicit proxy choice");Stop(ambiguous.installer);
  auto x86Root=fixture/L"proxy-x86";std::filesystem::create_directories(x86Root);auto x86Exe=x86Root/L"Unknown.exe";wchar_t windows[MAX_PATH];GetWindowsDirectoryW(windows,MAX_PATH);std::filesystem::copy_file(std::filesystem::path(windows)/L"SysWOW64/cmd.exe",x86Exe,std::filesystem::copy_options::overwrite_existing);
  nh::HubModel x86;x86.games={unresolved.games[0]};x86.games[0].title="Unknown x86";x86.games[0].target.path=nh::Utf8(x86Exe.wstring());x86.games[0].target.bitness=32;x86.selected=0;x86.Plan("Install");check(!x86.installer.busy&&!x86.showIssue&&x86.message.find("NR Anything")!=std::string::npos&&!std::filesystem::exists(x86Root/L"dxgi.dll"),"unknown x86 executable rejects incompatible integration before any proxy decision or write");
 }
 {nh::HubModel unavailable;unavailable.games.resize(1);unavailable.selected=0;unavailable.proxy=3;unavailable.games[0].title="DOOM Eternal";unavailable.games[0].target.suitable=false;unavailable.inspection={{"state",{{"proxy","winmm.dll"}}}};
  const auto inspectionBefore=unavailable.inspection;const auto messageBefore=unavailable.message;
  for(const auto* path:{"","relative/invalid-game.exe"}){unavailable.games[0].target.path=path;bool empty=false;try{empty=unavailable.SelectedProxyName().empty();}catch(...){}
   check(empty,path[0]?"passive proxy resolution tolerates an unsuitable malformed executable path":"passive proxy resolution tolerates a discovered game without an executable");
   check(unavailable.selected==0&&unavailable.proxy==3&&unavailable.inspection==inspectionBefore&&unavailable.message==messageBefore&&unavailable.games[0].target.path==path&&!unavailable.installer.busy&&unavailable.installer.request.empty(),"passive unavailable proxy lookup preserves model state and starts no operation");
  }
 }
 for(auto operation:{"Install"}){
  model.Plan(operation);auto request=Read(model.installer.request);Feed(model,Planned(request));
  check(model.installer.busy&&!model.showReview&&Read(model.installer.request).value("kind","")=="Execute","explicit normal action proceeds directly from Plan to Execute");check(model.installer.action==operation,"processing action remains attached to its button through execution");Stop(model.installer);
 }
 for(auto operation:{"Update","Repair","ChangeProxy","Restore","CompactHistory","Sanitize","DumbfireInstall","DumbfireRollback"}){model.Plan(operation);check(!model.installer.busy,"retired operation cannot launch a desktop worker");}
 model.PlanHistory(3);check(!model.installer.busy,"restore history cannot launch a worker");
 model.freshInstall=true;model.Plan("Install");auto freshRequest=Read(model.installer.request);check(freshRequest.value("freshInstall",false)&&!freshRequest.contains("includeInspector")&&!freshRequest.contains("uninstallMode"),"Fresh Install uses simple request; mandatory Inspector has no optional control");Stop(model.installer);model.freshInstall=false;
 model.Plan("Uninstall");check(model.showUninstallConfirm&&!model.installer.busy,"uninstall asks for confirmation before starting any installer work");model.CancelUninstall();model.ConfirmUninstall();check(!model.installer.busy&&!model.showUninstallConfirm,"cancelled uninstall cannot start later");
 model.Plan("Uninstall");model.selected=1;model.ConfirmUninstall();check(!model.installer.busy&&!model.showUninstallConfirm,"uninstall confirmation cannot cross game targets");model.selected=0;
 model.Plan("Uninstall");model.ConfirmUninstall();auto request=Read(model.installer.request);auto removal=Planned(request);removal["antiCheat"]=nullptr;Feed(model,removal);check(model.installer.busy&&!model.showReview&&Read(model.installer.request).value("kind","")=="Execute","confirmed uninstall proceeds with no second inventory review and accepts null anti-cheat");Stop(model.installer);model.Execute();check(!model.installer.busy,"uninstall confirmation is consumed by execution");
 model.Plan("Uninstall");model.ConfirmUninstall();request=Read(model.installer.request);auto changedRemoval=Planned(request);changedRemoval["request"]["uninstallMode"]="RemoveSettings";Feed(model,changedRemoval);check(!model.installer.busy&&!model.showReview,"uninstall receipt cannot substitute removal mode");
 model.Plan("Uninstall");model.ConfirmUninstall();request=Read(model.installer.request);check(request.size()==4&&request["operation"]=="Uninstall","uninstall sends no cleanup modes or backup choices");Stop(model.installer);model.CancelUninstall();
 {nh::HubModel maintenance;std::filesystem::create_directories(nh::UserRoot()/L"profiles");Write(nh::UserRoot()/L"profiles/maintenance.json","cached");maintenance.installer.busy=true;maintenance.RequestDataMaintenance(1);check(!maintenance.showDataMaintenance,"maintenance refuses active installer");maintenance.installer.busy=false;maintenance.RequestDataMaintenance(1);check(maintenance.showDataMaintenance&&std::filesystem::exists(nh::UserRoot()/L"profiles/maintenance.json"),"profile clear confirmation precedes deletion");maintenance.CancelDataMaintenance();maintenance.ConfirmDataMaintenance();check(std::filesystem::exists(nh::UserRoot()/L"profiles/maintenance.json"),"cancelled clear cannot run later");maintenance.RequestDataMaintenance(1);maintenance.ConfirmDataMaintenance();check(!std::filesystem::exists(nh::UserRoot()/L"profiles/maintenance.json")&&!maintenance.restartForMaintenance,"profile cache clears without restart");int pauses=0,resumes=0;maintenance.maintenanceQuiesce=[&](bool pause){if(pause)++pauses;else ++resumes;};maintenance.RequestDataMaintenance(0);check(pauses==1,"whole clear quiesces writers before inventory");maintenance.CancelDataMaintenance();check(resumes==1,"cancelled whole clear resumes services");maintenance.RequestDataMaintenance(1);maintenance.dataMaintenanceAction=0;maintenance.ConfirmDataMaintenance();check(!maintenance.restartForMaintenance,"changed maintenance scope cannot acquire whole-clear authority");maintenance.CancelDataMaintenance();maintenance.Save();maintenance.RequestDataMaintenance(0);auto library=nh::UserRoot()/L"library.json";auto before=Read(library);maintenance.light=!maintenance.light;maintenance.Save();check(Read(library)==before,"pending maintenance never rewrites library cache");maintenance.ConfirmDataMaintenance();maintenance.Save();check(Read(library)==before,"confirmed whole clear suppresses stale saves");check(maintenance.restartForMaintenance&&std::filesystem::exists(nh::UserRoot()),"whole clear defers mutations until owner shutdown");}
 {nh::HubModel runtime;check(!runtime.CanDownloadRuntime(),"unknown runtime status cannot launch a download");runtime.preflight={{"runtime",{{"ready",true}}}};check(!runtime.CanDownloadRuntime(),"installed runtime disables download");runtime.preflight["runtime"]["ready"]=false;check(runtime.CanDownloadRuntime(),"missing required runtime enables its information page");runtime.readiness.busy=true;check(!runtime.CanDownloadRuntime(),"runtime refresh disables download until the result arrives");}
 {nh::HubModel ini;ini.games=model.games;ini.selected=0;check(!ini.CanOpenSelectedIni(),"missing INI shortcut stays disabled");auto path=std::filesystem::path(nh::Wide(ini.games[0].target.path)).parent_path()/L"OptiScaler.ini";Write(path,"[Menu]\n");check(ini.CanOpenSelectedIni(),"existing selected-game INI enables shortcut");ini.selected=1;check(!ini.CanOpenSelectedIni(),"INI availability follows selected game");ini.selected=0;ini.installer.busy=true;check(!ini.CanOpenSelectedIni(),"INI editor cannot be opened during installer work");ini.installer.busy=false;ini.games[0].target.bitness=32;check(!ini.CanOpenSelectedIni(),"x86 ignores unrelated game-root INI");auto helper=path.parent_path()/L"NeuRotic/Prepared/NeuRotic.GpuHost/OptiScaler.ini";std::filesystem::create_directories(helper.parent_path());Write(helper,"[DlssNr]\nEnabled=false\n");check(ini.CanOpenSelectedIni(),"x86 opens actual helper INI");std::filesystem::remove(helper);std::filesystem::remove(path);}
 model.Plan("Install");request=Read(model.installer.request);Feed(model,Planned(request,true));check(model.showIssue&&!model.showReview&&!model.installer.busy,"anti-cheat findings stop automatic execution");model.AcknowledgeRisk();check(model.installer.busy&&!model.showReview,"explicit target-bound risk acknowledgement resumes normal action");Stop(model.installer);
 model.Plan("Install");request=Read(model.installer.request);auto wrong=Planned(request);wrong["target"]["executable"]=model.games[1].target.path;Feed(model,wrong);check(!model.installer.busy&&!model.showReview,"plan for another executable cannot execute or become reviewable");
 model.Plan("Install");request=Read(model.installer.request);wrong=Planned(request);wrong["request"]["operation"]="Uninstall";Feed(model,wrong);check(!model.installer.busy&&!model.showReview,"receipt cannot substitute an operation for the clicked action");
 model.Plan("Install");request=Read(model.installer.request);Feed(model,{{"status","NeedsDecision"},{"decisionKind","RuntimeMissing"}});check(model.showIssue&&!model.installer.busy,"missing runtime requires a warning decision by default");model.ProceedWithoutRuntime();check(model.installer.busy&&Read(model.installer.request).value("continueWithoutRuntime",false)&&!model.continueWithoutRuntime,"one-time runtime choice retries the exact action without saving global consent");Stop(model.installer);
 model.Plan("Install");Feed(model,{{"status","NeedsDecision"},{"decisionKind","RuntimeMissing"}});model.selected=1;model.ProceedWithoutRuntime();check(!model.installer.busy,"one-time runtime acknowledgement cannot cross targets");model.selected=0;
 model.Select(-1);model.Select(0);model.Select(1);auto end=GetTickCount64()+5000;while(model.selected!=1&&GetTickCount64()<end){model.Poll();Sleep(1);}check(model.selected==1,"switching games cancels and drains the old read-only check");Stop(model.installer);
 model.installer.busy=true;model.installer.readOnlyInstaller=false;model.Select(0);check(model.selected==1,"active mutation keeps target selection locked");model.installer.busy=false;
 {nh::HubModel merging;merging.games=model.games;merging.selected=0;merging.games[0].manualTarget=true;const auto pinned=merging.games[0];merging.installer.busy=true;merging.installer.readOnlyInstaller=false;
  Json discovery={{"protocolVersion",2},{"kind","DiscoveryResult"},{"errors",Json::array()},{"games",Json::array({
   {{"id",pinned.id},{"title","Moved discovery metadata"},{"store","Steam"},{"installRoot",nh::Utf8((fixture/L"moved").wstring())},{"storeId","123"}},
   {{"id",merging.games[1].id},{"title","Other game refreshed"},{"store","Custom"},{"installRoot",merging.games[1].root}}
  })}};
  merging.MergeDiscovery(discovery);const auto& actual=merging.games[0];check(actual.target.path==pinned.target.path&&actual.target.suitable==pinned.target.suitable&&actual.root==pinned.root&&actual.title==pinned.title&&actual.store==pinned.store&&actual.manualTarget&&merging.selected==0,"discovery preserves the complete selected manual-game snapshot during mutation");check(merging.games[1].title=="Other game refreshed","discovery still merges other games while a mutation owns the selection");merging.installer.busy=false;
 }
 check(!nh::HubModel().continueWithoutRuntime,"global missing-runtime preference defaults off");model.continueWithoutRuntime=true;model.Save();check(Read(nh::UserRoot()/L"library.json").value("ignoreMissingVCRuntime",false),"global missing-runtime preference persists explicitly");
 {nh::HubModel reopened;reopened.Load();check(reopened.continueWithoutRuntime,"global missing-runtime preference loads on reopening");Stop(reopened.readiness);}
 {auto library=Read(nh::UserRoot()/L"library.json");library["ignoreMissingVCRuntime"]="malformed";Write(nh::UserRoot()/L"library.json",library.dump());nh::HubModel reopened;reopened.Load();check(!reopened.continueWithoutRuntime&&reopened.games.size()==2,"malformed optional runtime preference defaults off without losing the library");Stop(reopened.readiness);}
 {nh::HubModel longValue;longValue.games=model.games;longValue.Select(0);const std::string original="0."+std::string(180,'0')+"1";
  Json inspection={{"target",{{"executable",longValue.games[0].target.path}}},{"state",{{"status","Installed"},{"proxy","dxgi.dll"}}},{"configRevision","fixture-config"},{"settings",Json::array({
   {{"section","Menu"},{"key","Brightness"},{"type","float"},{"available",true},{"value",original},{"values",Json::array()},{"minimum",0},{"maximum",1}},
   {{"section","DlssNr"},{"key","Enabled"},{"type","boolean"},{"available",true},{"value","false"},{"values",Json::array({"true","false"})}}
  })}};
  Feed(longValue,{{"status","Inspected"},{"inspection",inspection}});check(longValue.settings.size()==2&&!longValue.settings[0].available&&longValue.settings[0].original==original,"oversized recognized saved value stays complete and read-only");
  strcpy_s(longValue.settings[1].draft.data(),longValue.settings[1].draft.size(),"true");longValue.PlanSettings();auto save=Read(longValue.installer.request);
  check(save.value("operation","")=="SaveSettings"&&save["settings"].size()==1&&save["settings"][0]["section"]=="DlssNr"&&save["settings"][0]["key"]=="Enabled"&&longValue.settings[0].original==original,"editing another setting never sends the truncated long value for writing");Stop(longValue.installer);
 }
 // NR-WO-0033: a conflict receipt must not destroy the user's uncommitted edits.
 {nh::HubModel concurrent;concurrent.games=model.games;concurrent.Select(0);
  const std::string oldProfile="profile-before",localProfile="profile-local",outsideProfile="profile-outside";
  Json baseline={{"target",{{"executable",concurrent.games[0].target.path}}},{"state",{{"status","Installed"},{"proxy","dxgi.dll"}}},{"configRevision",std::string(64,'a')},{"settings",Json::array({
   {{"section","DlssNr"},{"key","Enabled"},{"type","boolean"},{"available",true},{"value","false"},{"values",Json::array({"auto","true","false"})}},
   {{"section","DlssNr"},{"key","ObjectRulesProfile"},{"type","objectrules"},{"available",true},{"value",oldProfile},{"values",Json::array()}},
   {{"section","Future"},{"key","Unknown"},{"type","text"},{"available",false},{"value",std::string(220,'u')},{"values",Json::array()}}
  })}};
  auto outside=baseline;outside["configRevision"]=std::string(64,'b');outside["settings"][0]["value"]="auto";outside["settings"][1]["value"]=outsideProfile;outside["state"]["note"]="fresh outside metadata";
  Feed(concurrent,{{"status","Inspected"},{"inspection",baseline}});
  strcpy_s(concurrent.settings[0].draft.data(),concurrent.settings[0].draft.size(),"true");concurrent.settings[1].profileDraft=localProfile;
  concurrent.PlanSettings();auto request=Read(concurrent.installer.request);Feed(concurrent,Planned(request));
  check(concurrent.installer.busy&&Read(concurrent.installer.request).value("kind","")=="Execute","0033 conflict reproduction reaches actual SaveSettings Execute");
  Feed(concurrent,{{"status","PreconditionChanged"},{"reason","Settings changed externally; outside edit preserved. Reload before saving."},{"inspection",outside}});
  check(concurrent.showIssue&&!concurrent.installer.busy&&concurrent.lastResult["inspection"]["configRevision"]==std::string(64,'b'),"0033 rejected save retains actual conflict and fresh evidence without autosaving");
  check(std::string(concurrent.settings[0].draft.data())=="true"&&concurrent.settings[0].original=="false"&&concurrent.settings[1].profileDraft==localProfile&&concurrent.settings[1].original==oldProfile,"0033 conflict receipt preserves scalar and applied profile drafts with their originals");
  check(concurrent.inspection["configRevision"]==std::string(64,'a')&&concurrent.settings[2].original==std::string(220,'u')&&concurrent.inspection["state"]["note"]=="fresh outside metadata","0033 retained draft uses its old revision, preserves unknown values and accepts fresh install metadata");
  concurrent.PlanSettings();request=Read(concurrent.installer.request);
  check(request["configRevision"]==std::string(64,'a')&&request["settings"].size()==2&&request["settings"][0]["value"]=="true"&&request["settings"][1]["value"]==localProfile,"0033 retry cannot silently overwrite an outside edit by advancing the revision");Stop(concurrent.installer);
  concurrent.RefreshSelected();Feed(concurrent,{{"status","Inspected"},{"inspection",outside}});
  check(std::string(concurrent.settings[0].draft.data())=="true"&&concurrent.settings[1].profileDraft==localProfile&&concurrent.inspection["configRevision"]==std::string(64,'a'),"0033 explicit refresh retains dirty drafts and their write precondition");
  concurrent.RefreshSelected();Feed(concurrent,{{"status","Failed"},{"reason","fixture refresh failed"}});
  concurrent.RefreshSelected();Feed(concurrent,{{"status","Cancelled"}});
  check(std::string(concurrent.settings[0].draft.data())=="true"&&concurrent.settings[1].profileDraft==localProfile,"0033 failed and cancelled refresh keep the dirty snapshot");
  concurrent.Select(-1);concurrent.Select(0);
  check(concurrent.installer.busy&&Read(concurrent.installer.request).value("kind","")=="Inspect","0033 cached reselection checks current files before using the saved status");
  if(!concurrent.installer.busy)concurrent.RefreshSelected();Feed(concurrent,{{"status","Inspected"},{"inspection",outside}});
  check(std::string(concurrent.settings[0].draft.data())=="true"&&concurrent.settings[1].profileDraft==localProfile,"0033 cached reselection refresh preserves the same game's dirty draft");
  strcpy_s(concurrent.settings[0].draft.data(),concurrent.settings[0].draft.size(),concurrent.settings[0].original.c_str());concurrent.settings[1].profileDraft=concurrent.settings[1].original;
  concurrent.RefreshSelected();Feed(concurrent,{{"status","Inspected"},{"inspection",outside}});
  check(std::string(concurrent.settings[0].draft.data())=="auto"&&concurrent.settings[1].profileDraft==outsideProfile&&concurrent.inspection["configRevision"]==std::string(64,'b'),"0033 undo followed by clean refresh safely adopts outside settings");
  auto editor=std::make_shared<Neurotic::Semantic::Rules::Editor>();editor->dirty=true;concurrent.settings[1].ruleEditor=editor;
  concurrent.RefreshSelected();Feed(concurrent,{{"status","Inspected"},{"inspection",baseline}});
  check(concurrent.settings[1].ruleEditor==editor&&editor->dirty&&concurrent.inspection["configRevision"]==std::string(64,'b'),"0033 unfinished object-rule editor survives refresh without a committed profile change");
  concurrent.settings[1].ruleEditor.reset();strcpy_s(concurrent.settings[0].draft.data(),concurrent.settings[0].draft.size(),"true");
  concurrent.PlanSettings();request=Read(concurrent.installer.request);Feed(concurrent,Planned(request));auto committed=outside;committed["settings"][0]["value"]="true";committed["configRevision"]=std::string(64,'c');Feed(concurrent,{{"status","Succeeded"},{"inspection",committed}});
  check(concurrent.settings[0].original=="true"&&std::string(concurrent.settings[0].draft.data())=="true"&&concurrent.inspection["configRevision"]==std::string(64,'c'),"0033 successful save accepts the committed baseline instead of retaining a stale draft");
  concurrent.Select(1);auto other=baseline;other["target"]["executable"]=concurrent.games[1].target.path;Feed(concurrent,{{"status","Inspected"},{"inspection",other}});
  check(concurrent.selected==1&&concurrent.settings[0].original=="false"&&std::string(concurrent.settings[0].draft.data())=="false","0033 another selected target never inherits the previous game's drafts");
 }
 {nh::HubModel defaults;defaults.games=model.games;defaults.Select(0);
  Json inspection={{"target",{{"executable",defaults.games[0].target.path},{"fileIdentity","fixture-exe"}}},{"state",{{"status","Installed"}}},{"configRevision","defaults-before"},{"settings",Json::array({{{"section","DlssNr"},{"key","Enabled"},{"type","boolean"},{"available",true},{"value","true"},{"values",Json::array({"auto","true","false"})}}})}};
  Feed(defaults,{{"status","Inspected"},{"inspection",inspection}});defaults.RestoreSettingsDefaults();auto request=Read(defaults.installer.request);
  check(request.value("kind","")=="RestoreDefaults"&&request.value("configRevision","")=="defaults-before"&&!request.contains("gameRoot"),"defaults request binds the selected game's current config without unrelated request fields");
  Json resolved={{"status","DefaultsResolved"},{"inspection",inspection},{"defaultSettings",Json::array({{{"section","DlssNr"},{"key","Enabled"},{"value","auto"}}})},{"defaultsSource",{{"packageId","flagship-approved"},{"packageDigest",std::string(64,'a')},{"executable",defaults.games[0].target.path},{"executableFileIdentity","fixture-exe"}}}};
  Feed(defaults,resolved);check(!defaults.installer.busy&&!defaults.showReview&&defaults.settings[0].original=="true"&&std::string(defaults.settings[0].draft.data())=="auto","restoring defaults stages game-rule auto while preserving the original until explicit Save Changes");
  defaults.PlanSettings();auto save=Read(defaults.installer.request);check(save.value("operation","")=="SaveSettings"&&save.value("configRevision","")=="defaults-before"&&save["settings"][0]["value"]=="auto","saving restored defaults uses ordinary revision-checked settings operation");Stop(defaults.installer);
  defaults.RestoreSettingsDefaults();auto changed=resolved;changed["inspection"]["configRevision"]="unexpected-new-config";Feed(defaults,changed);check(defaults.settings.empty()||std::string(defaults.settings[0].draft.data())!="auto","changed defaults receipt cannot replace editor drafts silently");
  defaults.RefreshSelected();Feed(defaults,{{"status","Inspected"},{"inspection",inspection}});defaults.RestoreSettingsDefaults();changed=resolved;changed["defaultsSource"]["executable"]=defaults.games[1].target.path;Feed(defaults,changed);check(defaults.settings.empty()||std::string(defaults.settings[0].draft.data())!="auto","defaults from another game cannot be staged");
 }
 {nh::HubModel bulk;bulk.games=model.games;bulk.selected=0;bulk.BeginBulkUninstall();
  check(bulk.showBulkUninstall&&bulk.installer.busy&&Read(bulk.installer.request).value("kind","")=="Inspect","bulk opening only starts read-only inventory and shows confirmation window");
  auto inspected=[&](int index){return Json{{"status","Inspected"},{"inspection",{{"target",{{"executable",bulk.games[index].target.path},{"fileIdentity",std::string(64,'a')}}},{"revision",std::string(64,'b')},{"configRevision",std::string(64,'e')},{"recoveryRequired",false},{"state",{{"kind","neurotic-public-install"},{"schema_version",2},{"status","Installed"}}}}}};};
  bulk.ConfirmBulkUninstall();check(Read(bulk.installer.request).value("kind","")=="Inspect","bulk cannot confirm before inventory completes");
  while(bulk.bulkUninstall.GetPhase()==nh::BulkUninstall::Phase::Inventory&&bulk.installer.busy){
   const auto current=bulk.bulkUninstall.CurrentExecutable();int index=current==bulk.games[0].target.path?0:current==bulk.games[1].target.path?1:-1;
   if(index>=0)Feed(bulk,inspected(index));else Feed(bulk,{{"status","Inspected"},{"inspection",{{"target",{{"executable",current},{"fileIdentity","fixture-stat"}}},{"state",nullptr}}}});
  }
  check(!bulk.installer.busy&&bulk.bulkUninstall.ReadyCount()==2&&bulk.bulkUninstall.GetPhase()==nh::BulkUninstall::Phase::Review,"bulk inventory stops at review before any mutation");
  bulk.Plan("Uninstall");bulk.Select(1);check(!bulk.showUninstallConfirm&&!bulk.installer.busy&&bulk.selected==0,"single-game actions and target selection cannot compete with bulk confirmation");
  bulk.ConfirmBulkUninstall();const int first=bulk.bulkUninstall.CurrentExecutable()==bulk.games[0].target.path?0:1;Feed(bulk,inspected(first));auto plannedRequest=Read(bulk.installer.request);check(plannedRequest.value("operation","")=="Uninstall"&&!plannedRequest.contains("uninstallMode"),"confirmed bulk removal starts bound simple uninstall plan");
  auto receipt=Planned(plannedRequest);receipt["target"]["fileIdentity"]=std::string(64,'a');receipt["configRevision"]=std::string(64,'e');receipt["planId"]=std::string(32,'c');receipt["planFingerprint"]=std::string(64,'d');Feed(bulk,receipt);
  check(Read(bulk.installer.request).value("kind","")=="Execute"&&!bulk.showReview,"bulk validated plan proceeds through sole installer without another inventory dialog");
  bulk.CancelBulkUninstall();check(bulk.installer.busy&&WaitForSingleObject(bulk.installer.process,0)==WAIT_TIMEOUT,"bulk stop does not kill active file mutation");
  auto done=inspected(first);done["status"]="Succeeded";done["planId"]=std::string(32,'c');done["inspection"]["state"]=nullptr;Feed(bulk,done);
  auto counts=[&](const char* status){return std::count_if(bulk.bulkUninstall.Rows().begin(),bulk.bulkUninstall.Rows().end(),[&](const auto& row){return row.status==status;});};
  check(!bulk.installer.busy&&bulk.bulkUninstall.GetPhase()==nh::BulkUninstall::Phase::Finished&&counts("Uninstalled")==1&&counts("Not attempted")==1,"bulk stop records completed game and preserves next target without executing it");
  bulk.CloseBulkUninstall();Stop(bulk.installer);
 }
 {nh::HubModel simple;simple.games=model.games;simple.selected=0;simple.sanitizeRevealed=true;simple.dumbfireInstall=true;simple.OpenSanitize();simple.PlanSanitize();simple.ConfirmSanitize();simple.Plan("DumbfireRollback");check(!simple.installer.busy&&!simple.showSanitize,"retired APIs cannot invoke a legacy service even from stale state");
  simple.Plan("Install");auto requested=Read(simple.installer.request);check(requested["operation"]=="Install","stale Dumbfire flag cannot substitute the requested Install");Feed(simple,Planned(requested));
  auto inspection=Json{{"target",{{"executable",simple.games[0].target.path}}},{"state",{{"schemaVersion",3},{"kind","neurotic-game-install"},{"status","Partial"},{"proxy","dxgi.dll"}}},{"settings",Json::array()}};
  Feed(simple,{{"status","Partial"},{"inspection",inspection},{"notes",Json::array({"Required file skipped."})}});check(!simple.installer.busy&&simple.showIssue&&simple.inspection["state"]["status"]=="Partial"&&simple.message.find("skipped")!=std::string::npos,"partial install retains truthful state and paths without a recovery gate");simple.Plan("Install");check(simple.installer.busy,"partial install permits immediate retry");Stop(simple.installer);
 }
 nh::ProcessJob progress;progress.busy=progress.readOnlyInstaller=true;progress.started=1000;progress.lastProgressAt=121000;
 check(!progress.ReadOnlyTimedOut(122000),"real recent progress keeps a long read-only check alive");check(progress.ReadOnlyTimedOut(241000),"stalled check has a bounded inactivity deadline");progress.lastProgressAt=600000;check(!progress.ReadOnlyTimedOut(601000),"advancing read-only work is not stopped by total elapsed time");progress.readOnlyInstaller=false;check(!progress.ReadOnlyTimedOut(99999999),"mutation is never killed by read-only deadlines");
 progress.ReadProgress("[NR-PROGRESS] {\"stage\":\"Checking files\",\"done\":1,\"total\":5}\n");progress.lastProgressAt=1000;progress.ReadProgress("[NR-PROGRESS] {\"stage\":\"Checking files\",\"done\":1,\"total\":5}\n");check(progress.lastProgressAt==1000&&progress.progress==.2f,"repeated progress does not postpone a stall");progress.ReadProgress("[NR-PROGRESS] {\"stage\":\"Checking files\",\"done\":2,\"total\":5}\n");check(progress.lastProgressAt!=1000&&progress.progress==.4f,"advancing file count renews progress deadline");progress.busy=false;
 progress.ReadProgress("[NR-PROGRESS] {\"stage\":\"Checking installation inventory\",\"done\":3,\"total\":0}\n");check(progress.progress<0&&progress.progressStage=="Checking installation inventory","unknown inventory total remains indeterminate while accepting completed work");progress.lastProgressAt=1000;progress.ReadProgress("[NR-PROGRESS] {\"stage\":\"Checking installation inventory\",\"done\":4,\"total\":0}\n");check(progress.lastProgressAt!=1000&&progress.progress<0,"advancing unbounded inventory counts renew the stall deadline");progress.lastProgressAt=1000;progress.ReadProgress("[NR-PROGRESS] {\"stage\":\"Invalid count\",\"done\":4,\"total\":2}\n");check(progress.lastProgressAt==1000&&progress.progressStage=="Checking installation inventory","impossible bounded counts cannot fabricate progress");
 {nh::ProcessJob writer;writer.StartInstaller({{"kind","Execute"},{"planId","fixture"},{"planFingerprint","fixture"}});writer.Cancel();check(writer.busy&&WaitForSingleObject(writer.process,0)==WAIT_TIMEOUT&&writer.error.empty(),"cancel cannot terminate the active mutation worker");Stop(writer);}
 {nh::ProcessJob reader;reader.StartInstaller({{"kind","Inspect"},{"gameExecutable",model.games[0].target.path}});reader.logPath=fixture/L"progress.log";Write(reader.logPath,"[NR-PROGRESS] {\"stage\":\"Checking files\",\"done\":1,");Json result;reader.Poll(result);check(reader.progress<0,"partial log write does not invent progress");{std::ofstream file(reader.logPath,std::ios::app);file<<"\"total\":5}\n";}reader.progressReadAt=0;reader.Poll(result);check(reader.progress==.2f,"split log record becomes measurable after its newline arrives");auto progressAt=reader.lastProgressAt;reader.progressReadAt=0;reader.Poll(result);check(reader.lastProgressAt==progressAt,"polling unchanged log bytes does not renew progress");Stop(reader);}
 return failed;
}

#ifdef NEUROTIC_OPERATION_FLOW_STANDALONE
int main(){try{return RunOperationFlowTests()?1:0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
