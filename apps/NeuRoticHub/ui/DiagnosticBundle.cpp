#include <menu/Localization.h>
#include "DiagnosticBundle.h"
#include "ManualLibrary.h"
#include <objbase.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace nh {
Json RedactAntiCheatForExport(const Json& inspection){
 auto copy=inspection;if(!copy.is_object()||!copy.contains(Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")))return copy;
 Json report=Json::object();auto& source=copy[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")];
 if(source.is_object())for(auto key:{"schema_version","ruleset_version","rules_digest","game_state","headline",Neurotic::UiLiteral("desktop.hubshell.scan_status_cc2cdbf3", "scan_status"),"findings",Neurotic::UiLiteral("desktop.hubshell.stale_findings_ead79e28", "stale_findings"),"system_context","system_status","issues","safety_verified","protection_active","block_for_anticheat","requires_ack"})if(source.contains(key))report[key]=source[key];
 copy[Neurotic::UiLiteral("desktop.hubshell.anticheat_1eec58ab", "antiCheat")]=std::move(report);return copy;
}
namespace {
struct Handle {
 HANDLE value=nullptr;
 Handle()=default;
 explicit Handle(HANDLE h):value(h){if(h==INVALID_HANDLE_VALUE||!h)throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_export_could_not_open_an_ordinary_loc_7db013ad", "Diagnostic export could not open an ordinary local path or process handle."));}
 Handle(const Handle&)=delete;
 Handle(Handle&& other) noexcept:value(other.value){other.value=nullptr;}
 ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
void Ordinary(HANDLE handle,bool directory){
 BY_HANDLE_FILE_INFORMATION info{};
 if(!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)!=directory)
  throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.linked_or_unexpected_paths_are_unavailable_for_d_2071e469", "Linked or unexpected paths are unavailable for diagnostic export."));
}
void Local(const std::filesystem::path& path){
 auto text=path.wstring();
 if(!path.is_absolute()||text.starts_with(L"\\\\")||text.find(L':',2)!=std::wstring::npos||text.find(L'\0')!=std::wstring::npos||path.lexically_normal()!=path)
  throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.choose_an_ordinary_local_absolute_path_for_diagn_e933f564", "Choose an ordinary local absolute path for diagnostic export."));
}
struct Pins {
 std::vector<Handle> handles;
 void Directory(const std::filesystem::path& path){
  Local(path);auto current=path.root_path();
  auto pin=[&]{Handle h(CreateFileW(current.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Ordinary(h.value,true);handles.push_back(std::move(h));};
  pin();for(auto& part:path.relative_path()){current/=part;pin();}
 }
 void File(const std::filesystem::path& path){
  Directory(path.parent_path());Handle h(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Ordinary(h.value,false);handles.push_back(std::move(h));
 }
};
std::vector<wchar_t> Environment(const std::filesystem::path& shell){
 auto raw=GetEnvironmentStringsW();if(!raw)throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.worker_environment_unavailable_d7e63892", "Worker environment unavailable."));
 std::vector<std::wstring> entries;for(auto item=raw;*item;item+=wcslen(item)+1)if(_wcsnicmp(item,L"PSModulePath=",13)!=0)entries.emplace_back(item);FreeEnvironmentStringsW(raw);
 entries.push_back(L"PSModulePath="+(shell.parent_path()/L"Modules").wstring());
 std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
 std::vector<wchar_t> block;for(auto& entry:entries){block.insert(block.end(),entry.begin(),entry.end());block.push_back(0);}block.push_back(0);return block;
}
Json ReadReceipt(const std::filesystem::path& path){
 Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Ordinary(file.value,false);
 LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size)||size.QuadPart<2||size.QuadPart>65536)throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_collector_returned_no_valid_receipt_4862e22e", "Diagnostic collector returned no valid receipt."));
 std::string bytes((size_t)size.QuadPart,'\0');DWORD read=0;
 if(!ReadFile(file.value,bytes.data(),(DWORD)bytes.size(),&read,nullptr)||read!=bytes.size())throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_receipt_could_not_be_read_5e0ba896", "Diagnostic receipt could not be read."));return Json::parse(bytes);
}
struct Scratch {
 std::filesystem::path path;
 ~Scratch(){
  // No recursive cleanup: only our three known files, and never follow links.
  if(path.empty())return;
  try{Pins pins;pins.Directory(path);for(auto name:{L"inspection.json",L"result.json"}){auto file=path/name;auto attributes=GetFileAttributesW(file.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))DeleteFileW(file.c_str());}pins.handles.clear();RemoveDirectoryW(path.c_str());}catch(...){}
 }
};
}
DiagnosticBundleResult ExportDiagnosticBundle(const std::string& executable,const std::filesystem::path& outputDirectory,const Json& inspection){
 DiagnosticBundleResult result;
 try{
  auto target=std::filesystem::path(Wide(executable));Local(target);Local(outputDirectory);
  Pins pins;pins.File(target);pins.Directory(outputDirectory);
  auto script=AppRoot()/L"support"/L"Collect-NR-Review.ps1";pins.File(script);
  GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_export_identifier_unavailable_9515548f", "Diagnostic export identifier unavailable."));wchar_t guid[40]{};StringFromGUID2(id,guid,40);
  Scratch scratch;auto scratchPath=outputDirectory/(std::wstring(L".nr-export-")+guid);
  if(!CreateDirectoryW(scratchPath.c_str(),nullptr))throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.cannot_create_diagnostic_export_working_director_7afa01fb", "Cannot create diagnostic export working directory."));
  scratch.path=std::move(scratchPath);
  Pins scratchPins;scratchPins.Directory(scratch.path);
  auto snapshot=scratch.path/L"inspection.json",receipt=scratch.path/L"result.json";
  Json context={{"kind","CachedHubInspection"},{"gameExecutable",executable},{"inspection",RedactAntiCheatForExport(inspection)},{"runtimeExecution","NotAttempted"}};
  // ObjectRules may occupy 4 MiB in the settings projection plus an escaped
  // config copy. Match the bounded installer receipt budget, not the raw rules.
  auto bytes=context.dump();if(bytes.size()>16777216)throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.hub_inspection_exceeds_the_16_mib_export_snapsho_78638fd1", "Hub inspection exceeds the 16 MiB export snapshot limit."));
  {Handle file(CreateFileW(snapshot.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));DWORD written=0;if(!WriteFile(file.value,bytes.data(),(DWORD)bytes.size(),&written,nullptr)||written!=bytes.size())throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.cannot_save_hub_inspection_snapshot_4c7d5024", "Cannot save Hub inspection snapshot."));}
  wchar_t system[32768]{};auto length=GetSystemDirectoryW(system,32768);if(!length||length>=32768)throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.windows_powershell_unavailable_86ab6b5d", "Windows PowerShell unavailable."));
  auto shell=std::filesystem::path(system)/L"WindowsPowerShell"/L"v1.0"/L"powershell.exe";
  auto command=Quote(shell.wstring())+L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "+Quote(script.wstring())+
   L" -GameDirectory "+Quote(target.parent_path().wstring())+L" -GameExecutable "+Quote(target.wstring())+L" -OutputDirectory "+Quote(outputDirectory.wstring())+
   L" -HubSnapshotPath "+Quote(snapshot.wstring())+L" -ResultPath "+Quote(receipt.wstring());
  Handle job(CreateJobObjectW(nullptr,nullptr));JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.cannot_own_diagnostic_collector_process_bcfbab55", "Cannot own diagnostic collector process."));
  STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION info{};auto environment=Environment(shell);
  if(!CreateProcessW(shell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT,environment.data(),AppRoot().c_str(),&start,&info))throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.cannot_start_the_packaged_diagnostic_collector_dbf98181", "Cannot start the packaged diagnostic collector."));
  Handle process(info.hProcess),thread(info.hThread);
  if(!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,3);WaitForSingleObject(process.value,5000);throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.cannot_own_diagnostic_collector_process_bcfbab55", "Cannot own diagnostic collector process."));}
  if(ResumeThread(thread.value)==(DWORD)-1)throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.cannot_resume_diagnostic_collector_8d4bf90e", "Cannot resume diagnostic collector."));
  if(WaitForSingleObject(process.value,180000)!=WAIT_OBJECT_0){TerminateJobObject(job.value,3);WaitForSingleObject(process.value,5000);throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_export_timed_out_after_three_minutes__b34c1557", "Diagnostic export timed out after three minutes. Partial collection files may remain in the chosen folder."));}
  DWORD code=1;if(!GetExitCodeProcess(process.value,&code))throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_collector_exit_status_unavailable_2a707fc1", "Diagnostic collector exit status unavailable."));
  auto report=ReadReceipt(receipt);
  if(code||!report.value("success",false))throw std::runtime_error(report.value("status",std::string(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_export_failed_partial_collection_file_f5a4fe42", "Diagnostic export failed. Partial collection files may remain in the chosen folder."))));
  auto archive=std::filesystem::path(Wide(report.at("path").get<std::string>()));Local(archive);
  if(archive.parent_path()!=outputDirectory||archive.extension()!=L".zip"||!archive.filename().wstring().starts_with(L"nr-game-"))throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_archive_receipt_named_an_unexpected_p_48b085a0", "Diagnostic archive receipt named an unexpected path."));
  pins.File(archive);if(std::filesystem::file_size(archive)==0)throw std::runtime_error(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_archive_is_empty_2708a3bc", "Diagnostic archive is empty."));
  result.path=Utf8(archive.wstring());result.status=report.value("status",std::string(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_zip_exported_4f0b6cc8", "Diagnostic ZIP exported.")));result.success=true;
 }catch(const std::exception& error){result.status=std::string(Neurotic::UiMessage("desktop.diagnosticbundle.diagnostic_zip_unavailable_5fa19a86", "Diagnostic ZIP unavailable: "))+error.what();}
 return result;
}
}
