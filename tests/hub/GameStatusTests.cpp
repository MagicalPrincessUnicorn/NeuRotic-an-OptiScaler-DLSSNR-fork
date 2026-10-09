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
nh::Json Inspection(const std::filesystem::path& exe,const char* proxy="winmm.dll"){
 return {{"target",{{"executable",nh::Utf8(exe.wstring())}}},{"state",{{"kind","neurotic-game-install"},{"schemaVersion",3},{"status","Installed"},{"proxy",proxy}}},{"configRevision","fixture-config-revision"},{"runtime","NotAttempted"},{"recoveryRequired",false},{"conflicts",nh::Json::array()},{"missing",nh::Json::array()},
 {"settings",nh::Json::array({{{"section","Neural"},{"key","Enabled"},{"group","Neural"},{"available",true},{"value","true"},{"values",nh::Json::array({"true","false"})}}})}};
}
void StopInspect(nh::HubModel& model){
 if(model.installer.process){if(WaitForSingleObject(model.installer.process,0)!=WAIT_OBJECT_0){TerminateProcess(model.installer.process,0);WaitForSingleObject(model.installer.process,5000);}CloseHandle(model.installer.process);model.installer.process=nullptr;}model.installer.busy=false;
}
// Exercise ProcessJob::Poll's real installer-receipt branch with a completed
// harmless child. The installer protocol remains unchanged.
void Feed(nh::HubModel& model,const std::filesystem::path& receipt,const nh::Json& result){
 StopInspect(model);Write(receipt,result.dump());wchar_t system[MAX_PATH];GetSystemDirectoryW(system,MAX_PATH);auto exe=std::filesystem::path(system)/L"cmd.exe";auto command=nh::Quote(exe.wstring())+L" /d /c exit 0";
 STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION child{};if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child))throw std::runtime_error("Game status fixture child could not start");
 CloseHandle(child.hThread);WaitForSingleObject(child.hProcess,5000);model.installer.process=child.hProcess;model.installer.result=receipt;model.installer.discovery=model.installer.preflight=false;model.installer.busy=true;model.installer.error.clear();model.Poll();
}
bool Junction(const std::filesystem::path& path,const std::filesystem::path& target){
 CreateDirectoryW(path.c_str(),nullptr);auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(handle==INVALID_HANDLE_VALUE)return false;
 // A directory junction does not require symbolic-link privilege.
 auto substitute=L"\\??\\"+target.wstring();auto print=target.wstring();auto pathBytes=(substitute.size()+1+print.size()+1)*sizeof(wchar_t);std::vector<unsigned char> bytes(16+pathBytes,0);
 auto put16=[&](size_t at,USHORT value){memcpy(bytes.data()+at,&value,2);};DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;memcpy(bytes.data(),&tag,4);put16(4,(USHORT)(8+pathBytes));put16(8,0);put16(10,(USHORT)(substitute.size()*2));put16(12,(USHORT)((substitute.size()+1)*2));put16(14,(USHORT)(print.size()*2));
 memcpy(bytes.data()+16,substitute.c_str(),(substitute.size()+1)*2);memcpy(bytes.data()+16+(substitute.size()+1)*2,print.c_str(),(print.size()+1)*2);DWORD returned=0;bool ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,bytes.data(),(DWORD)bytes.size(),nullptr,0,&returned,nullptr)!=FALSE;CloseHandle(handle);return ok;
}
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
