#include <menu/Localization.h>
#include "OperationController.h"
#include "ManualLibrary.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <algorithm>
namespace nh {
std::filesystem::path AppRoot(){wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.application_path_unavailable_279cf33f", "Application path unavailable"));return std::filesystem::path(path).parent_path();}
std::filesystem::path LegacyUserRoot(){
 wchar_t fixture[32768];if(GetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture,32768))return fixture;
 PWSTR raw=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw)))throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.user_data_directory_unavailable_69622bef", "User data directory unavailable"));
 auto root=std::filesystem::path(raw)/L"NeuRotic"/L"Hub";CoTaskMemFree(raw);return root;
}
std::filesystem::path UserRoot(){wchar_t fixture[32768];if(GetEnvironmentVariableW(L"NEUROTIC_HUB_FIXTURE_ROOT",fixture,32768))return fixture;return LegacyUserRoot().parent_path()/L"HubData-v2";}
std::filesystem::path SharedRuntimeRoot(){return LegacyUserRoot()/L"Runtime";}
std::wstring Quote(const std::wstring& value){std::wstring quoted=L"\"";unsigned slashes=0;for(wchar_t c:value){if(c==L'\\'){++slashes;continue;}if(c==L'"'){quoted.append(slashes*2+1,L'\\');quoted+=c;}else{quoted.append(slashes,L'\\');quoted+=c;}slashes=0;}quoted.append(slashes*2,L'\\');return quoted+L"\"";}
static std::wstring Id(){GUID id;CoCreateGuid(&id);wchar_t value[40];StringFromGUID2(id,value,40);return value;}
static std::vector<wchar_t> PowerShellEnvironment(){
 // The bound Windows PowerShell 5.1 worker must find its own built-in modules,
 // even when the Hub inherits a newer PowerShell host's PSModulePath.
 auto raw=GetEnvironmentStringsW();if(!raw)throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.worker_environment_unavailable_3053efa7", "Worker environment unavailable"));std::vector<std::wstring> entries;
 for(auto item=raw;*item;item+=wcslen(item)+1)if(_wcsnicmp(item,L"PSModulePath=",13)!=0)entries.emplace_back(item);FreeEnvironmentStringsW(raw);
 wchar_t windows[32768];if(!GetWindowsDirectoryW(windows,32768))throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.windows_directory_unavailable_3050321a", "Windows directory unavailable"));
 entries.push_back(L"PSModulePath="+(std::filesystem::path(windows)/L"System32"/L"WindowsPowerShell"/L"v1.0"/L"Modules").wstring());
 std::sort(entries.begin(),entries.end(),[](auto& a,auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});std::vector<wchar_t> block;
 for(auto& item:entries){block.insert(block.end(),item.begin(),item.end());block.push_back(0);}block.push_back(0);return block;
}
ProcessJob::~ProcessJob(){if(process)CloseHandle(process);if(output)CloseHandle(output);}
void ProcessJob::AcknowledgeAntiCheat(const Json& plan){
 ClearAntiCheatApproval();
 if(busy||plan.value("status","")!="Planned"||!plan.contains(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat"))||!plan[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")].is_object())throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.review_the_current_plan_before_proceeding_b234d928", "Review the current plan before proceeding"));
 const auto& risk=plan[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")];
 if(risk.value(Neurotic::UiLiteral("desktop.hubshell.scan_status_cc2cdbf3", "scan_status"),"")==Neurotic::UiLiteral("desktop.hubshell.cancelled_b89ba77e", "cancelled"))throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.the_cancelled_scan_cannot_be_approved_28278aa1", "The cancelled scan cannot be approved"));
 antiCheatApproval="NRAC1\n"+plan.at("planId").get<std::string>()+"\n"+plan.at("planFingerprint").get<std::string>()+"\n"+risk.at("fingerprint").get<std::string>()+"\n";
}
void ProcessJob::StartInstaller(const Json& payload){
 if(busy)throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.an_operation_is_already_running_ea0d93d6", "An operation is already running"));
 auto dir=UserRoot()/L"requests";std::filesystem::create_directories(dir);auto id=Id();request=dir/(id+L".request.json");result=dir/(id+L".result.json");auto log=dir/(id+L".log");
 std::ofstream file(request,std::ios::binary);Json message=payload;message["protocolVersion"]=1;file<<message.dump();file.close();if(!file)throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.cannot_save_request_5dba6d6c", "Cannot save request"));
 wchar_t system[32768];GetSystemDirectoryW(system,32768);auto executable=std::filesystem::path(system)/L"WindowsPowerShell"/L"v1.0"/L"powershell.exe";
 auto command=Quote(executable.wstring())+L" -NoProfile -ExecutionPolicy Bypass -File "+Quote((AppRoot()/L"support"/L"NeuRotic-Setup-Engine.ps1").wstring())+L" -HubNonInteractive -HubRequestPath "+Quote(request.wstring())+L" -HubResultPath "+Quote(result.wstring());
 SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE logFile=CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(logFile==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.cannot_open_operation_log_6fb66ae9", "Cannot open operation log"));
 HANDLE nullInput=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
 HANDLE approvalRead=nullptr,approvalWrite=nullptr;
 std::string approval;
 if(payload.value("kind","")=="Execute"){
  if(!antiCheatApproval.empty()){
   const std::string prefix="NRAC1\n"+payload.value("planId","")+"\n"+payload.value("planFingerprint","")+"\n";
   if(antiCheatApproval.rfind(prefix,0)==0)approval=antiCheatApproval;
  }
  ClearAntiCheatApproval();
 }else if(payload.value("kind","")=="Plan")ClearAntiCheatApproval();
 if(!approval.empty()){
  if(!CreatePipe(&approvalRead,&approvalWrite,&security,0)){CloseHandle(logFile);CloseHandle(nullInput);throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.private_approval_pipe_unavailable_7a39196a", "Private approval pipe unavailable"));}
  if(!SetHandleInformation(approvalWrite,HANDLE_FLAG_INHERIT,0)){CloseHandle(approvalRead);CloseHandle(approvalWrite);CloseHandle(logFile);CloseHandle(nullInput);throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.private_approval_pipe_inheritance_could_not_be_r_45db1c62", "Private approval pipe inheritance could not be restricted"));}
 }
 STARTUPINFOW start{sizeof(start)};start.dwFlags=STARTF_USESTDHANDLES;start.hStdOutput=logFile;start.hStdError=logFile;start.hStdInput=nullInput;PROCESS_INFORMATION info{};
 if(approvalRead)start.hStdInput=approvalRead;
 auto environment=PowerShellEnvironment();BOOL ok=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,environment.data(),AppRoot().c_str(),&start,&info);CloseHandle(logFile);CloseHandle(nullInput);
 if(approvalRead)CloseHandle(approvalRead);
 if(approvalWrite){DWORD sent=0;BOOL written=WriteFile(approvalWrite,approval.data(),static_cast<DWORD>(approval.size()),&sent,nullptr);CloseHandle(approvalWrite);if(ok&&(!written||sent!=approval.size())){TerminateProcess(info.hProcess,3);CloseHandle(info.hProcess);CloseHandle(info.hThread);throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.private_approval_could_not_be_sent_ca350699", "Private approval could not be sent"));}}
 if(!ok)throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.could_not_start_the_bound_installer_0ee922b7", "Could not start the bound installer"));process=info.hProcess;CloseHandle(info.hThread);busy=true;discovery=false;preflight=payload.value("kind","")=="Preflight";readOnlyInstaller=payload.value("kind","")!="Execute";started=GetTickCount64();text.clear();error.clear();logPath=log;progress=-1;progressStage.clear();progressReadAt=0;lastProgressAt=started;progressOffset=0;progressPartial.clear();progressDone=progressTotal=-1;if(payload.value("kind","")!="Execute")action=payload.value("operation",payload.value("kind",""));
}
void ProcessJob::StartDiscovery(const Json& payload){
 if(busy)throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.a_helper_is_already_running_f0651f87", "A helper is already running"));auto executable=AppRoot()/L"discovery"/L"NeuRotic.Discovery.exe";
 if(!std::filesystem::exists(executable))throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.game_discovery_helper_is_unavailable_in_this_bui_b3e27a1d", "Game discovery helper is unavailable in this build"));
 SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE read=nullptr,write=nullptr,inputRead=nullptr,inputWrite=nullptr;
 if(!CreatePipe(&read,&write,&security,0)||!CreatePipe(&inputRead,&inputWrite,&security,0))throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.discovery_pipe_unavailable_5d31e439", "Discovery pipe unavailable"));
 SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);SetHandleInformation(inputWrite,HANDLE_FLAG_INHERIT,0);
 auto dir=UserRoot()/L"requests";std::filesystem::create_directories(dir);auto id=Id();auto log=dir/(id+L".discovery.log");result=dir/(id+L".discovery.result.json");HANDLE stderrFile=CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 STARTUPINFOW start{sizeof(start)};start.dwFlags=STARTF_USESTDHANDLES;start.hStdInput=inputRead;start.hStdOutput=write;start.hStdError=stderrFile;PROCESS_INFORMATION info{};auto command=Quote(executable.wstring());
 BOOL ok=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,AppRoot().c_str(),&start,&info);CloseHandle(inputRead);CloseHandle(write);CloseHandle(stderrFile);
 if(!ok){CloseHandle(read);CloseHandle(inputWrite);throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.cannot_start_discovery_helper_d3a9f4d1", "Cannot start discovery helper"));}
 std::string requestText=payload.dump();DWORD count;bool sent=WriteFile(inputWrite,requestText.data(),(DWORD)requestText.size(),&count,nullptr)&&count==requestText.size();CloseHandle(inputWrite);
 process=info.hProcess;output=read;CloseHandle(info.hThread);discovery=true;preflight=false;readOnlyInstaller=false;busy=true;started=GetTickCount64();lastProgressAt=started;progress=-1;progressStage.clear();action="DiscoverGames";text.clear();error.clear();
 if(!sent){error=Neurotic::UiMessage("desktop.operationcontroller.game_discovery_request_could_not_be_sent_f31d4e9d", "Game discovery request could not be sent");TerminateProcess(process,3);}
}
void ProcessJob::Cancel(){ClearAntiCheatApproval();if(busy&&(discovery||readOnlyInstaller)){error=Neurotic::UiMessage("desktop.operationcontroller.read_only_scan_or_preview_cancelled_no_game_chan_4cec3efb", "Read-only scan or preview cancelled. No game changes were made.");TerminateProcess(process,3);}}
void ProcessJob::ReadProgress(const std::string& lines){
 std::istringstream stream(lines);std::string line;
 while(std::getline(stream,line)){constexpr auto marker="[NR-PROGRESS] ";auto pos=line.find(marker);if(pos==std::string::npos)continue;
  try{auto event=Json::parse(line.substr(pos+strlen(marker)));auto done=event.at("done").get<int64_t>(),total=event.at("total").get<int64_t>();auto stage=event.at("stage").get<std::string>();if(done<0||total<0||(total>0&&done>total)||stage.empty()||stage.size()>200)continue;
   if(stage!=progressStage||done>progressDone){lastProgressAt=GetTickCount64();}
   progressStage=stage;progressDone=done;progressTotal=total;progress=total?float(double(done)/double(total)):-1;
  }catch(...){} // Ignore incomplete log writes; they become readable on the next poll.
 }
}
bool ProcessJob::ReadOnlyTimedOut(ULONGLONG now) const{
 if(!busy||!readOnlyInstaller||discovery)return false;
 const ULONGLONG stall=preflight?60000:120000;
 return now-(lastProgressAt?lastProgressAt:started)>=stall;
}
bool ProcessJob::Poll(Json& value){
 if(!busy)return false;
 if(!discovery&&!logPath.empty()&&GetTickCount64()-progressReadAt>=200){
  progressReadAt=GetTickCount64();std::ifstream log(logPath,std::ios::binary|std::ios::ate);
  if(log){auto size=log.tellg();if(size>=0){if(uint64_t(size)<progressOffset){progressOffset=0;progressPartial.clear();}log.seekg(std::streamoff(progressOffset));
   char bytes[65536];log.read(bytes,sizeof(bytes));auto count=log.gcount();progressOffset+=uint64_t(count);progressPartial.append(bytes,size_t(count));
   auto end=progressPartial.find_last_of('\n');if(end!=std::string::npos){ReadProgress(progressPartial.substr(0,end+1));progressPartial.erase(0,end+1);}if(progressPartial.size()>65536)progressPartial.clear();
  }}
 }
 if(discovery){DWORD available=0;unsigned budget=65536;while(budget&&PeekNamedPipe(output,nullptr,0,nullptr,&available,nullptr)&&available){char buffer[8192];DWORD count=0;if(!ReadFile(output,buffer,std::min<DWORD>(available,std::min<DWORD>(sizeof(buffer),budget)),&count,nullptr))break;budget-=count;if(text.size()+count>1048576){error=Neurotic::UiMessage("desktop.operationcontroller.discovery_output_exceeded_its_limit_2be92424", "Discovery output exceeded its limit");break;}text.append(buffer,count);}if(error.empty()&&GetTickCount64()-started>50000)error=Neurotic::UiMessage("desktop.operationcontroller.game_discovery_timed_out_existing_games_remain_i_96b9ea57", "Game discovery timed out; existing games remain in the library");if(!error.empty())TerminateProcess(process,3);}
 if(ReadOnlyTimedOut(GetTickCount64())&&WaitForSingleObject(process,0)!=WAIT_OBJECT_0){error=Neurotic::UiMessage("desktop.operationcontroller.the_read_only_check_stopped_reporting_progress_n_4a191d69", "The read-only check stopped reporting progress. No game changes were made; retry or inspect Diagnostics.");TerminateProcess(process,3);}
 if(WaitForSingleObject(process,0)!=WAIT_OBJECT_0)return false;
 // The child must be finished BEFORE this final peek. Otherwise it can write
 // and exit between the peek and completion check, losing its final bytes.
 if(discovery&&error.empty()){DWORD remaining=0;if(PeekNamedPipe(output,nullptr,0,nullptr,&remaining,nullptr)&&remaining)return false;}
 DWORD code=1;GetExitCodeProcess(process,&code);CloseHandle(process);process=nullptr;if(output){CloseHandle(output);output=nullptr;}busy=false;
 try{
  if(discovery){if(!error.empty())throw std::runtime_error(error);if(code)throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.game_discovery_failed_see_local_diagnostics_log_bb68a891", "Game discovery failed; see local diagnostics log"));value=Json::parse(text);if(!result.empty()){std::ofstream receipt(result,std::ios::binary);receipt.write(text.data(),text.size());receipt.close();if(!receipt)value["diagnosticWarning"]=Neurotic::UiMessage("desktop.operationcontroller.the_full_scan_receipt_could_not_be_saved_to_the__dcfd082b", "The full scan receipt could not be saved to the local log folder.");}}
  else{if(!error.empty())throw std::runtime_error(error);std::ifstream stream(result,std::ios::binary);if(!stream)throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.helper_ended_without_a_receipt_check_again_or_in_a4094af1", "Helper ended without a receipt; check again or inspect recovery state before retrying"));std::string data(16*1048576+1,'\0');stream.read(data.data(),std::streamsize(data.size()));auto count=stream.gcount();if(count>16*1048576)throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.the_installer_response_is_too_large_to_display_u_0e294027", "The installer response is too large to display. Update NeuRotic and refresh this game; the complete result remains in the diagnostics folder."));if(stream.bad())throw std::runtime_error(Neurotic::UiMessage("desktop.operationcontroller.the_operation_result_could_not_be_read_refresh_t_9bde8a5f", "The operation result could not be read. Refresh the game and check Diagnostics."));data.resize(size_t(count));value=Json::parse(data);}
 }catch(const std::exception& e){std::string status="FailedWithoutMutation";if(!discovery){try{std::ifstream source(request);auto sent=Json::parse(source);if(sent.value("kind","")=="Execute")status="RecoveryRequired";}catch(...){status="RecoveryRequired";}}value={{"status",status},{Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),e.what()}};}
 return true;
}
}
