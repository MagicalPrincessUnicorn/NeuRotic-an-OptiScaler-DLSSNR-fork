#include "ui/HubViewModel.h"
#include "ui/GameScreenshots.h"
#include "ui/GameDiagnostics.h"
#include "ui/ProfileCache.h"
#include "storage/UserFile.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <winioctl.h>
namespace {
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(bytes.data(),bytes.size());if(!file)throw std::runtime_error("Game status fixture could not be written");}
void MakeExecutable(const std::filesystem::path& path){
 IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=sizeof(dos);IMAGE_FILE_HEADER header{};header.Machine=IMAGE_FILE_MACHINE_AMD64;header.NumberOfSections=1;header.Characteristics=IMAGE_FILE_EXECUTABLE_IMAGE;header.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);
 IMAGE_OPTIONAL_HEADER64 optional{};optional.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;IMAGE_SECTION_HEADER section{};
 std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write((char*)&dos,sizeof(dos));DWORD signature=IMAGE_NT_SIGNATURE;file.write((char*)&signature,sizeof(signature));file.write((char*)&header,sizeof(header));file.write((char*)&optional,sizeof(optional));file.write((char*)&section,sizeof(section));
}
void MakeExecutable32(const std::filesystem::path& path){
 IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=sizeof(dos);IMAGE_FILE_HEADER header{};header.Machine=IMAGE_FILE_MACHINE_I386;header.NumberOfSections=1;header.Characteristics=IMAGE_FILE_EXECUTABLE_IMAGE;header.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER32);
 IMAGE_OPTIONAL_HEADER32 optional{};optional.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;IMAGE_SECTION_HEADER section{};
 std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write((char*)&dos,sizeof(dos));DWORD signature=IMAGE_NT_SIGNATURE;file.write((char*)&signature,sizeof(signature));file.write((char*)&header,sizeof(header));file.write((char*)&optional,sizeof(optional));file.write((char*)&section,sizeof(section));
}
nh::Json Inspection(const std::filesystem::path& exe,const char* proxy="winmm.dll"){
 return {{"target",{{"executable",nh::Utf8(exe.wstring())}}},{"state",{{"kind","neurotic-game-install"},{"schemaVersion",3},{"status","Installed"},{"proxy",proxy}}},{"configRevision","fixture-config-revision"},{"runtime","NotAttempted"},{"recoveryRequired",false},{"conflicts",nh::Json::array()},{"missing",nh::Json::array()},
 {"settings",nh::Json::array({{{"section","Neural"},{"key","Enabled"},{"group","Neural"},{"available",true},{"value","true"},{"values",nh::Json::array({"true","false"})}}})}};
}
void StopInspect(nh::HubModel& model){
 if(model.installer.process){if(WaitForSingleObject(model.installer.process,0)!=WAIT_OBJECT_0){TerminateProcess(model.installer.process,0);WaitForSingleObject(model.installer.process,5000);}CloseHandle(model.installer.process);model.installer.process=nullptr;}model.installer.busy=false;
}
// Exercise ProcessJob::Poll's real installer-receipt branch with a completed
// harmless child. The installer protocol remains unchanged.
void FeedBytes(nh::HubModel& model,const std::filesystem::path& receipt,const std::string& bytes,bool discovery=false,bool writeReceipt=true){
 StopInspect(model);if(writeReceipt)Write(receipt,bytes);else std::filesystem::remove(receipt);wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);auto exe=std::filesystem::path(system)/L"cmd.exe";auto command=nh::Quote(exe.wstring())+L" /d /c exit 0";
 STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION child{};if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child))throw std::runtime_error("Game status fixture child could not start");
 CloseHandle(child.hThread);WaitForSingleObject(child.hProcess,5000);auto& job=discovery?model.discovery:model.installer;job.process=child.hProcess;job.result=receipt;job.discovery=discovery;job.preflight=false;job.busy=true;job.started=GetTickCount64();job.error.clear();if(discovery)job.text=bytes;model.Poll();
}
void Feed(nh::HubModel& model,const std::filesystem::path& receipt,const nh::Json& result,bool discovery=false){FeedBytes(model,receipt,result.dump(),discovery);}
bool Junction(const std::filesystem::path& path,const std::filesystem::path& target){
 CreateDirectoryW(path.c_str(),nullptr);auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(handle==INVALID_HANDLE_VALUE)return false;
 // A directory junction does not require symbolic-link privilege.
 auto substitute=L"\\??\\"+target.wstring();auto print=target.wstring();auto pathBytes=(substitute.size()+1+print.size()+1)*sizeof(wchar_t);std::vector<unsigned char> bytes(16+pathBytes,0);
 auto put16=[&](size_t at,USHORT value){memcpy(bytes.data()+at,&value,2);};DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;memcpy(bytes.data(),&tag,4);put16(4,(USHORT)(8+pathBytes));put16(8,0);put16(10,(USHORT)(substitute.size()*2));put16(12,(USHORT)((substitute.size()+1)*2));put16(14,(USHORT)(print.size()*2));
 memcpy(bytes.data()+16,substitute.c_str(),(substitute.size()+1)*2);memcpy(bytes.data()+16+(substitute.size()+1)*2,print.c_str(),(print.size()+1)*2);DWORD returned=0;bool ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,bytes.data(),(DWORD)bytes.size(),nullptr,0,&returned,nullptr)!=FALSE;CloseHandle(handle);return ok;
}
}
int RunFreshOperationInspectionTests(){
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<"NH-FRESH-OPERATION-INSPECTION: "<<label<<'\n';failed+=!ok;};
 auto fixture=nh::UserRoot()/L"fresh-operation-inspection";std::filesystem::create_directories(fixture);
 auto one=fixture/L"Game.exe",two=fixture/L"Other.exe",receipt=fixture/L"receipt.json";MakeExecutable(one);MakeExecutable(two);
 nh::Json genuine;{std::ifstream file("tests/hub/fixtures/control-install-cd94d8e1.json");genuine=nh::Json::parse(file);}
 check(genuine.at("inspection").at("settings").size()==421,"preserved producer fixture contains all 421 settings rows");
 for(int kind=0;kind<5;++kind){
  nh::HubModel model;model.games={{"one","One","Manual",nh::Utf8(fixture.wstring()),nh::InspectExecutable(one)}};model.selected=0;
  model.installer.action="Inspect";Feed(model,receipt,{{"status","FailedWithoutChanges"},{"reason","Fixture prior check failed"}});
  check(model.SelectedInspectionFailure()=="Fixture prior check failed","real ProcessJob establishes target-bound failed check");
  auto result=genuine;result["inspection"]["target"]["executable"]=nh::Utf8(one.wstring());
  if(kind==1)result.erase("inspection");
  if(kind==2)result["inspection"]["settings"]=nh::Json::object();
  if(kind==3)result["inspection"]["target"]["executable"]=nh::Utf8(two.wstring());
  if(kind==4)result["inspection"].erase("target");
  model.installer.action="Execute";Feed(model,receipt,result);
  if(kind==0)check(model.settings.size()==421&&!model.inspectionCached&&model.SelectedInspectionFailure().empty(),"accepted same-target Execute inspection resolves previous failed check");
  else check(model.SelectedInspectionFailure()=="Fixture prior check failed"&&(kind==1||model.inspection.is_null()),
    kind==1?"success without inspection does not resolve failed check":kind==2?"malformed operation inspection does not resolve failed check":kind==3?"foreign-target operation inspection does not resolve failed check":"targetless operation inspection does not resolve failed check");
  StopInspect(model);
 }
 return failed;
}
int RunInspectionPresentationTests(){
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<"NH-INSPECTION-PRESENTATION: "<<label<<'\n';failed+=!ok;};
 auto fixture=nh::UserRoot()/L"inspection-presentation";std::filesystem::create_directories(fixture/L"one");std::filesystem::create_directories(fixture/L"two");
 auto one=fixture/L"one/Game.exe",two=fixture/L"two/Game.exe",receipt=fixture/L"receipt.json";MakeExecutable(one);MakeExecutable(two);
 nh::HubModel model;model.games={{"one","One","Manual",nh::Utf8(one.parent_path().wstring()),nh::InspectExecutable(one)},{"two","Two","Manual",nh::Utf8(two.parent_path().wstring()),nh::InspectExecutable(two)}};model.selected=0;
 {
  nh::HubModel background;background.games=model.games;background.selected=0;
  Write(one.parent_path()/L"OptiScaler.ini","[Fixture]\nEnabled=true\n");Write(one.parent_path()/L"OptiScaler.log","background scan log\n");
  background.installer.busy=background.installer.readOnlyInstaller=true;background.installer.action="Inspect";background.inspectionRefreshing=true;
  check(background.CanCollectGameDiagnostics()&&background.CanOpenSelectedIni()&&background.CanStartGame(),"background Inspect leaves known-path diagnostics, INI and Play available before a snapshot");
  auto preview=nh::ReadGameDiagnostics(nh::Utf8(one.wstring()),nh::Json());
  check(preview.text.find("background scan log")!=std::string::npos,"quick preview reads the ordinary game log without installation metadata");
  nh::SettingsField draft;draft.available=true;draft.original="true";strcpy_s(draft.draft.data(),draft.draft.size(),"false");background.settings.push_back(draft);background.proxy=2;
  auto oldMessage=background.message;background.Select(0);
  check(background.installer.error.empty()&&background.message==oldMessage,"same-game selection does not cancel its running inspection");
  background.installer.busy=false;background.inspectionRefreshing=false;background.Select(0);
  check(!background.installer.busy&&background.settings.size()==1&&std::string(background.settings[0].draft.data())=="false"&&background.proxy==2,"idle same-game selection preserves draft and proxy without restarting inspection");
  StopInspect(background);
  background.CollectGameDiagnostics();const bool started=background.gameDiagnosticsBusy;auto deadline=GetTickCount64()+5000;
  while(background.gameDiagnosticsBusy&&GetTickCount64()<deadline){background.Poll();Sleep(1);}
  check(started&&!background.gameDiagnosticsBusy&&background.gameDiagnosticsText.find("background scan log")!=std::string::npos,"asynchronous first log preview owns its target session without an inspection snapshot");
  background.installer.busy=true;background.installer.readOnlyInstaller=false;background.installer.action="Execute";
  check(!background.CanCollectGameDiagnostics()&&!background.CanOpenSelectedIni()&&!background.CanStartGame(),"mutation ownership still protects selected-game actions");background.installer.busy=false;
  background.SetExecutable(0,two);
  const bool reloadedTarget=nh::GamePathKey(background.games[0].target.path)==nh::GamePathKey(nh::Utf8(two.wstring()));
  if(!reloadedTarget||!background.installer.busy||background.installer.action!="Inspect")std::cout<<"NH-SCAN-RELOAD facts: actual="<<background.games[0].target.path<<" expected="<<nh::Utf8(two.wstring())<<" busy="<<background.installer.busy<<" action="<<background.installer.action<<" message="<<background.message<<" targetReason="<<background.games[0].target.reason<<'\n';
  check(reloadedTarget&&background.installer.busy&&background.installer.action=="Inspect","explicit executable change reloads the same row despite selection idempotence");StopInspect(background);
 }
 // A real discovery can observe changed executable bitness before its old
 // Inspect child drains. Clicking that row must preserve mandatory reload.
 {
  nh::HubModel queued;queued.games=model.games;queued.selected=0;queued.RefreshSelected();
  MakeExecutable32(one);
  queued.MergeDiscovery({{"protocolVersion",2},{"kind","DiscoveryResult"},{"games",nh::Json::array({{{"id","one"},{"title","One"},{"store","Manual"},{"installRoot",nh::Utf8(one.parent_path().wstring())},{"executable",nh::Utf8(one.wstring())}}})},{"errors",nh::Json::array()}});
  std::cout<<"NH-P2-RELOAD "<<nh::Json{{"target",queued.games[0].target.path},{"bitness",queued.games[0].target.bitness},{"busy",queued.installer.busy},{"action",queued.installer.action},{"error",queued.installer.error},{"message",queued.message}}.dump()<<'\n';
  check(nh::GamePathKey(queued.games[0].target.path)==nh::GamePathKey(nh::Utf8(one.wstring()))&&queued.games[0].target.bitness==32&&queued.installer.busy&&!queued.installer.error.empty(),"discovery observes real changed target bitness while the old Inspect remains owned");
  queued.Select(0);Feed(queued,receipt,{{"status","Cancelled"}});
  nh::Json next;if(queued.installer.busy){std::ifstream file(queued.installer.request);file>>next;}
  check(queued.selected==0&&queued.games[0].target.bitness==32&&queued.installer.busy&&queued.installer.action=="Inspect"&&next.value("gameExecutable","")==queued.games[0].target.path,"same-row click preserves mandatory changed-target reload until the old child drains");StopInspect(queued);MakeExecutable(one);
 }
 {
  nh::HubModel optional;optional.games=model.games;optional.selected=0;optional.RefreshSelected();optional.Select(1);optional.Select(0);Feed(optional,receipt,{{"status","Cancelled"}});
  check(optional.selected==0&&!optional.installer.busy,"same-row click still cancels an optional pending different-game selection");StopInspect(optional);
 }
 for(bool pending:{false,true})for(bool changedBitness:{false,true}){
  nh::HubModel previewOnly;previewOnly.games=model.games;previewOnly.selected=0;
  Write(one.parent_path()/L"OptiScaler.log",pending?"pending preview survives reselection\n":"completed preview survives reselection\n");
  previewOnly.CollectGameDiagnostics();const bool started=previewOnly.gameDiagnosticsBusy;
  if(!pending){auto until=GetTickCount64()+5000;while(previewOnly.gameDiagnosticsBusy&&GetTickCount64()<until){previewOnly.Poll();Sleep(1);}}
  // Without Poll, even a ready future remains an authentic unconsumed result.
  previewOnly.Select(-1);if(changedBitness)MakeExecutable32(one);previewOnly.Select(0);
  const bool freshInspect=previewOnly.installer.busy&&previewOnly.installer.action=="Inspect"&&!previewOnly.inspectionCached&&previewOnly.inspection.is_null();
  StopInspect(previewOnly);auto until=GetTickCount64()+5000;while(previewOnly.gameDiagnosticsBusy&&GetTickCount64()<until){previewOnly.Poll();Sleep(1);}
  const auto expected=pending?"pending preview survives reselection":"completed preview survives reselection";
  if(changedBitness)check(started&&freshInspect&&!previewOnly.gameDiagnosticsBusy&&previewOnly.gameDiagnosticsText.empty(),pending?"changed executable bitness invalidates a pending diagnostics-only result":"changed executable bitness invalidates a completed diagnostics-only preview");
  else check(started&&freshInspect&&!previewOnly.gameDiagnosticsBusy&&previewOnly.gameDiagnosticsText.find(expected)!=std::string::npos,pending?"pending diagnostics-only preview retains its generation across reselection and fresh Inspect":"completed diagnostics-only preview survives reselection without fabricating cached inspection");
  MakeExecutable(one);
 }
 model.installer.action="Inspect";Feed(model,receipt,{{"status","FailedWithoutChanges"},{"reason","Initial inspection refused"}});
 check(model.inspection.is_null()&&!model.inspectionCached&&model.SelectedInspectionFailure()=="Initial inspection refused","first failure has target-bound error without inventing a snapshot");
 model.selected=1;check(model.SelectedInspectionFailure().empty(),"another selected target cannot inherit the first inspection failure");model.selected=0;
 auto first=Inspection(one);first["state"]=nullptr;first["settings"]=nh::Json::array();
 for(const char* status:{"Failed","FailedWithoutChanges","Cancelled"}){
  model.installer.action="Inspect";Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});
  check(model.SelectedInspectionFailure().empty()&&!model.inspectionCached,"accepted inspection clears prior failure");
  model.RefreshSelected();check(model.SelectedInspectionFailure().empty()&&model.inspectionCached,"starting a refresh clears failure and retains advisory snapshot");
  Feed(model,receipt,{{"status",status},{"reason","Fixture check did not complete"}});
  check(model.inspection==first&&model.inspectionCached&&(model.SelectedInspectionFailure().empty()==(std::string(status)=="Cancelled")),"failed and cancelled inspections retain snapshot with distinct error semantics");
 }
 model.installer.action="Inspect";Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});
 Feed(model,receipt,{{"games",nh::Json::array({nh::Json::object()})}},true);
 const bool unrelatedDiscovery=model.inspection.is_null()&&model.SelectedInspectionFailure().empty()&&model.message.find("Operation receipt could not be read")!=std::string::npos;
 if(!unrelatedDiscovery)std::cout<<"NH-INSPECTION-PRESENTATION facts: inspectionNull="<<model.inspection.is_null()<<" failure="<<model.SelectedInspectionFailure()<<" message="<<model.message<<" discoveryError="<<model.discovery.error<<'\n';
 check(unrelatedDiscovery,"unrelated malformed discovery after completed Inspect never becomes an inspection failure");
 model.installer.action="Inspect";Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});
 model.installer.action="Install";Feed(model,receipt,{{"status","FailedWithoutChanges"},{"reason","Install refused"}});
 check(model.SelectedInspectionFailure().empty(),"unrelated operation failure is not an inspection failure");
 for(int kind=0;kind<3;++kind){
  model.installer.action="Inspect";Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});model.RefreshSelected();
  nh::Json request;{std::ifstream file(model.installer.request);request=nh::Json::parse(file);}
  const bool realInspectRequest=request.value("kind","")=="Inspect"&&model.installer.action=="Inspect";
  if(kind==2)model.installer.request=fixture/L"missing-inspect-request.json";
  FeedBytes(model,receipt,"{",false,kind!=1);
  const auto expected=kind==2?"RecoveryRequired":"FailedWithoutMutation";
  const bool failure=realInspectRequest&&model.lastResult.value("status","")==expected&&model.inspection==first&&model.inspectionCached&&!model.SelectedInspectionFailure().empty();
  if(!failure)std::cout<<"NH-INSPECTION-PRESENTATION transport facts: kind="<<kind<<" status="<<model.lastResult.value("status","")<<" cached="<<model.inspectionCached<<" failure="<<model.SelectedInspectionFailure()<<'\n';
  check(failure,kind==0?"actual invalid JSON reply preserves advisory snapshot with check failure":kind==1?"actual missing reply file preserves advisory snapshot with check failure":"unreadable original request retains RecoveryRequired classification and check failure");
 }
 auto malformedSettings=first;malformedSettings["settings"]=nh::Json::object();
 auto malformedField=Inspection(one);malformedField["settings"][0]["section"]=false;
 const std::vector<nh::Json> badProjections={{{"status","Inspected"}},{{"status","Inspected"},{"inspection",nullptr}},{{"status","Inspected"},{"inspection",nh::Json::object()}},{{"status","Inspected"},{"inspection",nh::Json::array()}},{{"status","Inspected"},{"inspection",malformedSettings}},{{"status","Inspected"},{"inspection",malformedField}}};
 for(size_t kind=0;kind<badProjections.size();++kind){
  const bool drafted=kind+1==badProjections.size();auto prior=drafted?Inspection(one):first;
  model.installer.action="Inspect";Feed(model,receipt,{{"status","Inspected"},{"inspection",prior}});
  if(drafted){strcpy_s(model.settings[0].draft.data(),model.settings[0].draft.size(),"false");model.proxy=2;}
  model.RefreshSelected();Feed(model,receipt,badProjections[kind]);
  const bool draftPreserved=!drafted||(model.settings.size()==1&&model.settings[0].available&&model.settings[0].original=="true"&&std::string(model.settings[0].draft.data())=="false"&&model.proxy==2&&model.inspection.value("configRevision","")=="fixture-config-revision");
  const bool failure=model.inspection==prior&&model.inspectionCached&&!model.SelectedInspectionFailure().empty()&&draftPreserved;
  if(!failure)std::cout<<"NH-INSPECTION-PRESENTATION projection facts: kind="<<kind<<" inspectionNull="<<model.inspection.is_null()<<" cached="<<model.inspectionCached<<" failure="<<model.SelectedInspectionFailure()<<'\n';
  check(failure,"missing/null/malformed inspected projection cannot replace the prior advisory snapshot or clear failure");
 }
 for(int kind=0;kind<2;++kind){
  auto prior=Inspection(one);model.installer.action="Inspect";Feed(model,receipt,{{"status","Inspected"},{"inspection",prior}});
  strcpy_s(model.settings[0].draft.data(),model.settings[0].draft.size(),"false");model.proxy=2;model.RefreshSelected();
  auto malformed=prior;if(kind==0)malformed["configRevision"]=false;else malformed["state"]["status"]=false;
  Feed(model,receipt,{{"status","Inspected"},{"inspection",malformed}});
  auto saved=nh::LoadGameProfile(model.games[0].target.path);
  const bool preserved=model.inspection==prior&&model.inspectionCached&&!model.SelectedInspectionFailure().empty()&&model.settings.size()==1&&model.settings[0].available&&model.settings[0].original=="true"&&std::string(model.settings[0].draft.data())=="false"&&model.proxy==2&&saved&&saved->inspection==prior;
  if(!preserved)std::cout<<"NH-INSPECTION-PRESENTATION consumed-type facts: kind="<<kind<<" inspectionNull="<<model.inspection.is_null()<<" fields="<<model.settings.size()<<" cachedProfile="<<bool(saved)<<" failure="<<model.SelectedInspectionFailure()<<'\n';
  check(preserved,kind==0?"wrong-type revision preserves unsaved draft, original revision, proxy and saved profile":"wrong-type installation status fails before session or profile publication");
 }
 // Keep the independent stale-target controls on their original draft-free
 // snapshot, after proving that a failed same-target parse retained the draft.
 for(auto& field:model.settings)strncpy_s(field.draft.data(),field.draft.size(),field.original.c_str(),_TRUNCATE);
 model.installer.action="Inspect";Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});
 auto unknown=first;unknown["state"]={{"status","Future observed status"}};
 model.installer.action="Inspect";Feed(model,receipt,{{"status","Inspected"},{"inspection",unknown}});
 check(model.inspection==unknown&&!model.inspectionCached&&model.SelectedInspectionFailure().empty(),"valid unknown state remains a fresh observed unknown without a state allowlist");
 model.RefreshSelected();const bool queued=model.TrySelectLibraryGame(1);Feed(model,receipt,{{"status","FailedWithoutChanges"},{"reason","Old target check failed"}});
 check(queued&&model.selected==1&&model.SelectedInspectionFailure().empty(),"late inspection result drains before queued selection without contaminating new target");StopInspect(model);
 model.Select(0);StopInspect(model);model.installer.action="Inspect";
 auto foreign=Inspection(two);Feed(model,receipt,{{"status","Inspected"},{"inspection",foreign}});
 check(model.inspection.is_null()&&!model.SelectedInspectionFailure().empty(),"foreign successful-looking inspection is refused and never displayed as current");
 model.Select(-1);check(model.SelectedInspectionFailure().empty(),"leaving selected game clears inspection failure");StopInspect(model);
 return failed;
}
int RunGameStatusTests(){
 int failed=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failed;};
 auto fixture=nh::UserRoot()/L"game-status";std::filesystem::create_directories(fixture/L"one");std::filesystem::create_directories(fixture/L"two");auto one=fixture/L"one"/L"Game.exe",two=fixture/L"two"/L"Game.exe";MakeExecutable(one);MakeExecutable(two);
 auto receipt=fixture/L"inspection.json";auto first=Inspection(one),second=Inspection(two,"version.dll");first["settings"][0]["label"]="Neural rendering";first["settings"][0]["type"]="integer";first["settings"][0]["minimum"]=0;first["settings"][0]["maximum"]=8;first["settings"][0]["description"]="Fixture metadata";first["settings"][0]["valueLabels"]={{"true","Enabled"},{"false","Disabled"}};
 nh::HubModel model;model.games={{"one","One","Manual",nh::Utf8(one.parent_path().wstring()),nh::InspectExecutable(one)},{"two","Two","Manual",nh::Utf8(two.parent_path().wstring()),nh::InspectExecutable(two)}};
 model.selected=0;Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});check(model.CanCollectGameDiagnostics()&&model.proxy==1,"NH-STATUS: owner inspection enables selected managed diagnostics");
 check(model.settings[0].label=="Neural rendering"&&model.settings[0].hasRange&&model.settings[0].maximum==8&&model.settings[0].description=="Fixture metadata"&&model.settings[0].valueLabels["true"]=="Enabled","NH-SETTINGS-SCHEMA: friendly labels, range and choices flow from the installer projection");
 {nh::HubModel reopened;reopened.games=model.games;reopened.Select(0);check(reopened.inspectionCached&&reopened.inspectionRefreshing&&reopened.installer.busy&&!reopened.inspection.is_null()&&reopened.settings.size()==1&&std::string(reopened.settings[0].draft.data())=="true"&&!reopened.profileCheckedUtc.empty(),"NH-PROFILE-UI: reopened profile displays observed values immediately while fresh inspection runs");StopInspect(reopened);} {auto malformed=first;malformed["settings"]=nh::Json{{"bad",nh::Json::object()}};nh::SaveGameProfile(nh::Utf8(one.wstring()),malformed);nh::HubModel reopened;reopened.games=model.games;reopened.Select(0);check(!reopened.inspectionCached&&reopened.inspection.is_null()&&reopened.installer.busy,"NH-PROFILE-UI: malformed cached projection is discarded and schedules fresh inspection");StopInspect(reopened);nh::SaveGameProfile(nh::Utf8(one.wstring()),first);} strcpy_s(model.settings[0].draft.data(),model.settings[0].draft.size(),"false");model.proxy=2;model.Select(-1);model.selected=1;Feed(model,receipt,{{"status","Inspected"},{"inspection",second}});
 auto requestBefore=model.installer.request;model.Select(0);check(!model.installer.busy&&model.installer.request==requestBefore&&model.settings.size()==1&&std::string(model.settings[0].draft.data())=="false"&&model.proxy==2,"NH-STATUS-CACHE: switch away and back preserves inspection, draft and proxy without launching an owner check");
 model.Select(1);check(!model.installer.busy&&model.proxy==2&&model.inspection==second,"NH-STATUS-CACHE: each target keeps its own inspection");model.Select(0);
 Write(one.parent_path()/L"OptiScaler.log","fixture selected log\n");model.CollectGameDiagnostics();check(model.gameDiagnosticsBusy,"NH-GAME-DIAGNOSTICS: selected log read runs asynchronously");
 model.Select(1);auto until=GetTickCount64()+5000;while(GetTickCount64()<until){model.Poll();Sleep(1);model.Select(0);if(!model.gameDiagnosticsBusy)break;model.Select(1);}
 check(!model.gameDiagnosticsBusy&&model.gameDiagnosticsText.find("fixture selected log")!=std::string::npos,"NH-GAME-DIAGNOSTICS: background completion returns to its original target");
 model.Select(1);check(model.gameDiagnosticsText.empty(),"NH-GAME-DIAGNOSTICS: another game does not inherit selected log text");model.Select(0);
 model.RefreshSelected();auto request=model.installer.request;nh::Json requested;{std::ifstream file(request);requested=nh::Json::parse(file);}check(model.installer.busy&&model.inspection.is_object()&&model.inspectionCached&&!model.CanCollectGameDiagnostics()&&requested.value("kind","")=="Inspect"&&!requested.contains("verifyFiles"),"NH-STATUS-REFRESH: explicit refresh checks current files while retaining an advisory snapshot");
 Feed(model,receipt,{{"status","Failed"},{"reason","Fixture read unavailable"}});check(model.inspection.is_object()&&model.inspectionCached&&!model.CanCollectGameDiagnostics()&&model.message.find("previous status")!=std::string::npos,"NH-STATUS-REFRESH: failed read retains lastgood status without verified authority");
 model.RefreshSelected();
 Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});check(model.CanCollectGameDiagnostics()&&std::string(model.settings[0].draft.data())=="true"&&model.proxy==1,"NH-STATUS-REFRESH: refreshed owner state replaces stale settings and proxy");
 Feed(model,receipt,{{"status","Succeeded"},{"inspection",first}});check(!model.installer.busy&&model.inspection==first,"NH-STATUS-MUTATION: successful operation reuses the owner's completed inspection without another scan");
 model.RefreshSelected();{std::ifstream file(model.installer.request);requested=nh::Json::parse(file);}check(model.installer.busy&&requested.value("kind","")=="Inspect"&&!requested.contains("verifyFiles"),"NH-STATUS-VERIFY: explicit refresh requests metadata without a payload check");Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});
 Feed(model,receipt,{{"status","PreconditionChanged"},{"reason","External change"}});check(model.inspection.is_object()&&model.showIssue,"NH-STATUS-PRECONDITION: external-change receipt keeps advisory status and requests retry");Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});
 Feed(model,receipt,{{"status","Partial"},{"reason","File locked"},{"inspection",first}});check(model.inspection.is_object()&&model.showIssue,"NH-STATUS-PARTIAL: unresolved paths keep actionable selected status");Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});
 Feed(model,receipt,{{"status","NeedsDecision"},{"conflicts",nh::Json::array({"dxgi.dll"})}});check(model.inspection.is_object()&&model.showIssue,"NH-STATUS-CONFLICTS: unfamiliar files get choices without discarding status");Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});
 model.inspection["state"]["status"]="restored";check(!model.CanCollectGameDiagnostics(),"NH-GAME-DIAGNOSTICS: restored installations cannot collect managed logs");model.inspection=first;model.inspection["conflicts"]=nh::Json::array({"dxgi.dll"});check(model.CanCollectGameDiagnostics(),"NH-GAME-DIAGNOSTICS: file conflicts do not block bounded logs");model.inspection=first;
 Write(one.parent_path()/L"OptiScaler.log","old generation log\n");model.CollectGameDiagnostics();check(model.gameDiagnosticsBusy,"NH-GAME-DIAGNOSTICS-GENERATION: old diagnostics worker started");model.RefreshSelected();Feed(model,receipt,{{"status","Inspected"},{"inspection",first}});
 until=GetTickCount64()+5000;while(model.gameDiagnosticsBusy&&GetTickCount64()<until){model.Poll();Sleep(1);}check(!model.gameDiagnosticsBusy&&model.gameDiagnosticsText.empty(),"NH-GAME-DIAGNOSTICS-GENERATION: explicit refresh discards a previous worker's stale snapshot");
 model.SetExecutable(0,two);check(!model.installer.busy&&model.inspection==second&&model.games[0].target.path==nh::Utf8(two.wstring()),"NH-STATUS-TARGET: executable change selects the destination's own cached status");StopInspect(model);
 // All log tests read fixed OptiScaler.log; arbitrary receipt paths are data.
 auto malicious=first;malicious["state"]["logPath"]=nh::Utf8((two.parent_path()/L"secret.log").wstring());Write(two.parent_path()/L"secret.log","DO NOT READ");auto diagnostics=nh::ReadGameDiagnostics(nh::Utf8(one.wstring()),malicious);
 check(diagnostics.text.find("old generation log")!=std::string::npos&&diagnostics.text.find("DO NOT READ")==std::string::npos,"NH-GAME-DIAGNOSTICS: receipt paths never select diagnostic files");
 std::string unicode="\xff\xfe";for(wchar_t c:std::wstring(L"UTF16 fixture Ω\n")){unicode.push_back((char)(c&255));unicode.push_back((char)(c>>8));}Write(one.parent_path()/L"OptiScaler.log",unicode);diagnostics=nh::ReadGameDiagnostics(nh::Utf8(one.wstring()),first);check(diagnostics.text.find(nh::Utf8(L"UTF16 fixture Ω"))!=std::string::npos,"NH-GAME-DIAGNOSTICS: UTF-16LE log is safely decoded");
 std::string big(nh::GameLogByteLimit+1024,'Q');Write(one.parent_path()/L"OptiScaler.log",big);diagnostics=nh::ReadGameDiagnostics(nh::Utf8(one.wstring()),first);check(diagnostics.status.find("limited")!=std::string::npos&&diagnostics.text.size()<nh::GameLogByteLimit+4096,"NH-GAME-DIAGNOSTICS: oversized logs have bounded bytes and visible truncation");
 big[nh::GameLogByteLimit-1]='\xff';Write(one.parent_path()/L"OptiScaler.log",big);diagnostics=nh::ReadGameDiagnostics(nh::Utf8(one.wstring()),first);check(diagnostics.status.find("valid UTF-8")!=std::string::npos,"NH-GAME-DIAGNOSTICS: truncation never hides an invalid trailing UTF-8 byte");
 big.assign(nh::GameLogByteLimit-2,'Q');big+="\xe2\x82\xac";Write(one.parent_path()/L"OptiScaler.log",big);diagnostics=nh::ReadGameDiagnostics(nh::Utf8(one.wstring()),first);check(diagnostics.status.find("limited")!=std::string::npos,"NH-GAME-DIAGNOSTICS: a valid UTF-8 sequence cut by the byte limit is trimmed safely");
 Write(one.parent_path()/L"OptiScaler.log",std::string("invalid\xff",8));diagnostics=nh::ReadGameDiagnostics(nh::Utf8(one.wstring()),first);check(diagnostics.status.find("valid UTF-8")!=std::string::npos,"NH-GAME-DIAGNOSTICS: invalid text encoding is refused");
 DeleteFileW((one.parent_path()/L"OptiScaler.log").c_str());diagnostics=nh::ReadGameDiagnostics(nh::Utf8(one.wstring()),first);check(diagnostics.status=="No game log is present yet.","NH-GAME-DIAGNOSTICS: missing log has an actionable empty state");
 auto linked=fixture/L"linked";RemoveDirectoryW(linked.c_str());bool junction=Junction(linked,two.parent_path());check(junction,"NH-GAME-DIAGNOSTICS: reparse fixture created");if(junction){diagnostics=nh::ReadGameDiagnostics(nh::Utf8((linked/L"Game.exe").wstring()),first);check(diagnostics.status.find("Linked")!=std::string::npos,"NH-GAME-DIAGNOSTICS: linked ancestor cannot escape the selected folder");RemoveDirectoryW(linked.c_str());}
 check(!GetModuleHandleW(L"nvngx_dlssnr.dll")&&!GetModuleHandleW(L"OptiScaler.dll"),"NH-GAME-DIAGNOSTICS: collection loads no game or model runtime");
 junction=Junction(linked,two.parent_path());check(junction,"NH-SCREENSHOTS: linked game fixture created");if(junction){nh::Target target;target.path=nh::Utf8((linked/L"Game.exe").wstring());target.suitable=true;target.bitness=64;bool refused=false;try{nh::EnsureSelectedGameScreenshots(target);}catch(...){refused=true;}check(refused&&!std::filesystem::exists(two.parent_path()/L"NeuroticScreenshots"),"NH-SCREENSHOTS: linked ancestor cannot create captures in another game");RemoveDirectoryW(linked.c_str());}
 auto saved=fixture/L"preferences"/L"cache.json";nh::SaveUserFile(saved,"original complete file");
 auto read=[](const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),{});};
 check(read(saved)=="original complete file","NH-USER-FILE: creates missing ordinary preference directories");
 auto alias=fixture/L"outside-alias.json";DeleteFileW(alias.c_str());bool hardlink=CreateHardLinkW(alias.c_str(),saved.c_str(),nullptr)!=FALSE;
 check(hardlink,"NH-USER-FILE: destination hardlink fixture created");
 nh::SaveUserFile(saved,"updated complete file");check(read(saved)=="updated complete file"&&read(alias)=="original complete file","NH-USER-FILE: destination replacement never modifies a hardlink referent");
 junction=Junction(linked,saved.parent_path());check(junction,"NH-USER-FILE: linked parent fixture created");
 if(junction){bool refused=false;try{nh::SaveUserFile(linked/L"cache.json","must not escape");}catch(...){refused=true;}RemoveDirectoryW(linked.c_str());check(refused&&read(saved)=="updated complete file","NH-USER-FILE: pinned parent rejects junction redirection without writes");}
 bool bounded=false;try{nh::SaveUserFile(saved,std::string(16777217,'x'));}catch(...){bounded=true;}
 check(bounded&&read(saved)=="updated complete file","NH-USER-FILE: oversized save preserves the previous complete file");
 bool pending=false;for(auto& entry:std::filesystem::directory_iterator(saved.parent_path()))pending|=entry.path().extension()==L".pending";
 check(!pending,"NH-USER-FILE: successful and refused writes leave no owned temporary files");
 DeleteFileW(alias.c_str());
 return failed;
}
