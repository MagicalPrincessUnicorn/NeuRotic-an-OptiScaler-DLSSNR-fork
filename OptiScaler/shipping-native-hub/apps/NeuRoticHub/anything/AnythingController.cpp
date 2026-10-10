#include <menu/Localization.h>
#include "AnythingController.h"
#include "WindowPreferences.h"
#include "storage/UserFile.h"
#include "../../common/ModelFolderPolicy.h"
#include <windows.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace nh {
using AJson=nlohmann::json;
namespace {
struct OwnedHandle {
 HANDLE value=nullptr;OwnedHandle()=default;explicit OwnedHandle(HANDLE h):value(h){}
 ~OwnedHandle(){Reset();}OwnedHandle(const OwnedHandle&)=delete;
 void Reset(HANDLE h=nullptr){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=h;}
};
std::wstring CommandQuote(const std::wstring& value){std::wstring out=L"\"";size_t slashes=0;for(auto c:value){if(c==L'\\'){++slashes;continue;}if(c==L'"')out.append(slashes*2+1,L'\\');else out.append(slashes,L'\\');out+=c;slashes=0;}out.append(slashes*2,L'\\');return out+L"\"";}
bool Ordinary(const std::filesystem::path& path){auto a=GetFileAttributesW(path.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));}
void SafeNamespace(std::filesystem::path path){for(;;){auto a=GetFileAttributesW(path.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.linked_model_preferences_are_refused_07648984", "Linked model preferences are refused"));auto parent=path.parent_path();if(parent.empty()||parent==path)break;path=parent;}}
std::string ModelStamp(const std::filesystem::path& path){
 try{SafeNamespace(path);OwnedHandle file(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
  BY_HANDLE_FILE_INFORMATION i{};if(file.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(file.value,&i)||(i.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return {};
  return std::to_string(i.dwVolumeSerialNumber)+":"+std::to_string(i.nFileIndexHigh)+":"+std::to_string(i.nFileIndexLow)+":"+std::to_string(i.nFileSizeHigh)+":"+std::to_string(i.nFileSizeLow)+":"+std::to_string(i.ftLastWriteTime.dwHighDateTime)+":"+std::to_string(i.ftLastWriteTime.dwLowDateTime);
 }catch(...){return {};}
}
bool ActivePhase(const std::string& phase){return phase==Neurotic::UiLiteral("desktop.anythingview.countdown_58c4583f", "Countdown")||phase==Neurotic::UiLiteral("desktop.anythingview.starting_7e573dfb", "Starting")||phase==Neurotic::UiLiteral("desktop.anythingview.running_b9a06cc6", "Running");}
bool Identity(const AJson& w){
 if(!w.is_object())return false;
 for(auto name:{"hwnd","pid","processCreation"})if(!w.contains(name)||!w[name].is_number_unsigned()||w[name].get<uint64_t>()==0)return false;
 for(auto name:{Neurotic::UiLiteral("desktop.anythingview.title_07bed14a", "title"),"executable","windowClass"})if(!w.contains(name)||!w[name].is_string())return false;
 return true;
}
bool SameWindow(const AJson& a,const AJson& b){if(!Identity(a)||!Identity(b))return false;for(auto key:{"hwnd","pid","processCreation","executable","windowClass"})if(a.at(key)!=b.at(key))return false;return true;}
}
struct AnythingController::Impl {
 std::filesystem::path executable,data;
 const SessionPolicy policy;
 mutable std::mutex mutex;AnythingSnapshot view;
 std::thread supervisor;std::atomic<bool> finish=false;
 std::atomic<bool> processExitProven=true;
 bool connectWanted=false,restartWanted=false,stopWanted=false;
 AJson queued,queuedComparison,queuedProcessing,queuedGeneration;uint64_t revision=0,measurementId=0,pendingMeasurementId=0;
 bool measurementCanceled=false; // guarded by mutex, including status projection
 OwnedHandle job,process,input,output,stderrRead,writeEvent;
 DWORD workerPid=0; // guarded by mutex; cleared before process handle teardown
 OVERLAPPED write{};bool writing=false;std::string wire,stdoutBuffer,stderrTail;
 AJson pending;uint64_t pendingDeadline=0,lastResponse=0,lastQuery=0;
 std::string savedModel;
 bool depositSelected=false,depositBlocked=false;
 std::string observedDeposit,verifiedDeposit;
 uint64_t lastDepositCheck=0;
 std::filesystem::path Folder()const{return neurotic::model::Folder(data);}
 std::filesystem::path Deposit()const{return Folder()/neurotic::model::FileName;}
 static constexpr size_t MaxLine=512*1024;
 Impl(std::filesystem::path exe,std::filesystem::path root,SessionPolicy sessionPolicy):executable(std::move(exe)),data(std::move(root)),policy(sessionPolicy){
  if(policy.persistPreferences)try{SafeNamespace(data);auto path=data/L"anything.json";if(!Ordinary(path))path=data.parent_path()/L"Anything/anything.json";SafeNamespace(path);if(Ordinary(path)&&std::filesystem::file_size(path)<65536){std::ifstream in(path);auto preference=AJson::parse(in);if(preference.is_object()&&preference.contains("modelPath")&&preference["modelPath"].is_string())savedModel=preference["modelPath"].get<std::string>();}view.modelPath=savedModel;depositSelected=std::filesystem::path(Wide(savedModel))==Deposit();depositBlocked=depositSelected;}catch(...){view.message=Neurotic::UiMessage("desktop.anythingcontroller.saved_model_preference_could_not_be_read_place_y_5d277bf6", "Saved model preference could not be read; place your compatible file in the model folder.");}
  supervisor=std::thread([this]{Loop();});
 }
 ~Impl(){finish=true;if(supervisor.joinable())supervisor.join();}
 bool ShutdownAndWait(unsigned stopTimeoutMs){
  if(supervisor.joinable()){
   const auto deadline=GetTickCount64()+std::min(stopTimeoutMs,30000u);
   while(!finish&&GetTickCount64()<deadline){
    {std::lock_guard lock(mutex);if(!view.connected||(!view.stopping&&!view.busy&&!view.active))break;}
    Sleep(10);
   }
   finish=true;supervisor.join();
  }
  return processExitProven.load(std::memory_order_acquire);
 }
 void SaveModel(const std::string& path){
  try{if(policy.persistPreferences)SaveUserFile(data/L"anything.json",AJson{{"schemaVersion",1},{"modelPath",path}}.dump(2));savedModel=path;std::lock_guard lock(mutex);++view.modelRevision;
  }catch(...){std::lock_guard lock(mutex);view.message+=Neurotic::UiMessage("desktop.anythingcontroller.model_preference_could_not_be_saved_220591eb", " Model preference could not be saved.");}
 }
 void ProjectMeasurement(){ // caller holds mutex; worker ownership is never inferred here
  if(measurementCanceled){
   auto measurement=view.status.value("measurement",AJson::object());
   if(measurement.is_object()&&!measurement.empty()){
    measurement["state"]="invalidated";measurement["summaries"]=nullptr;
    measurement["reason"]="Measurement canceled by a session change.";
    measurement.erase("appAcknowledgementPending");view.status["measurement"]=std::move(measurement);
   }
  }else if(pendingMeasurementId){
   view.status["measurement"]={{"state","pending"},{"requestId",pendingMeasurementId},{"summaries",nullptr},{"ownershipRetired",false},{"appAcknowledgementPending",true},{"reason","Waiting for the worker to acknowledge this measurement."}};
  }
 }
 void Kill(const std::string& reason,bool forced,bool error){
  {std::lock_guard lock(mutex);workerPid=0;}
  if(job.value)TerminateJobObject(job.value,forced?4:0);
  if(process.value)processExitProven.store(WaitForSingleObject(process.value,1000)==WAIT_OBJECT_0,std::memory_order_release);
  if(writing){CancelIoEx(input.value,&write);DWORD bytes=0;GetOverlappedResult(input.value,&write,&bytes,TRUE);writing=false;}
  input.Reset();output.Reset();stderrRead.Reset();process.Reset();job.Reset();writeEvent.Reset();
  pending=AJson();wire.clear();stdoutBuffer.clear();
  std::lock_guard lock(mutex);pendingMeasurementId=0;measurementCanceled=false;view.connected=false;view.busy=false;view.ready=false;view.active=false;view.stopping=false;
  view.forcedTermination=forced;view.phase=error?Neurotic::UiLiteral("desktop.anythingview.error_eab1d8bf", "Error"):Neurotic::UiLiteral("desktop.anythingview.stopped_6c16cb30", "Stopped");view.message=reason;view.lastError=error?reason:"";view.status=AJson::object();view.windows.clear();queued=AJson();queuedComparison=AJson();queuedProcessing=AJson();queuedGeneration=AJson();
 }
 bool Launch(){
  try{
   // A failed exit proof must never be hidden by launching another child.
   if(!processExitProven.load(std::memory_order_acquire))return false;
   if(!executable.is_absolute()||!Ordinary(executable))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.window_worker_is_missing_use_a_complete_neurotic_4d32a485", "Window Worker is missing. Use a complete NeuRotic application package."));
   SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};OwnedHandle outWrite,inRead,errWrite;
   HANDLE read=nullptr,writer=nullptr;if(!CreatePipe(&read,&writer,&sa,65536))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.output_pipe_unavailable_6a5f85aa", "Output pipe unavailable"));output.Reset(read);outWrite.Reset(writer);SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
   if(!CreatePipe(&read,&writer,&sa,65536))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.diagnostic_pipe_unavailable_9e215b5e", "Diagnostic pipe unavailable"));stderrRead.Reset(read);errWrite.Reset(writer);SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
   static std::atomic<uint64_t> pipeId=0;auto name=L"\\\\.\\pipe\\NeuRoticAnything-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++pipeId);
   input.Reset(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_OUTBOUND|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_WAIT,1,65536,65536,0,nullptr));
   if(input.value==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.input_pipe_unavailable_4bc64542", "Input pipe unavailable"));
   inRead.Reset(CreateFileW(name.c_str(),GENERIC_READ,0,&sa,OPEN_EXISTING,0,nullptr));if(inRead.value==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.input_client_unavailable_c4252b1a", "Input client unavailable"));
   OVERLAPPED connection{};if(!ConnectNamedPipe(input.value,&connection)&&GetLastError()!=ERROR_PIPE_CONNECTED)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.input_pipe_connection_unavailable_63ac71ed", "Input pipe connection unavailable"));
   writeEvent.Reset(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!writeEvent.value)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.input_event_unavailable_daa264dd", "Input event unavailable"));
   job.Reset(CreateJobObjectW(nullptr,nullptr));if(!job.value)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.worker_supervision_unavailable_86fad86f", "Worker supervision unavailable"));
   JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
   if(!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.worker_lifetime_policy_unavailable_806d7731", "Worker lifetime policy unavailable"));
   STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof(start);start.StartupInfo.dwFlags=STARTF_USESTDHANDLES;start.StartupInfo.hStdInput=inRead.value;start.StartupInfo.hStdOutput=outWrite.value;start.StartupInfo.hStdError=errWrite.value;
   SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> attributes(bytes);
   start.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
   if(!InitializeProcThreadAttributeList(start.lpAttributeList,1,0,&bytes))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.handle_inheritance_policy_unavailable_ea050c28", "Handle inheritance policy unavailable"));
   struct AttributesExit{LPPROC_THREAD_ATTRIBUTE_LIST p;~AttributesExit(){DeleteProcThreadAttributeList(p);}} attrs{start.lpAttributeList};
   HANDLE inherited[]={inRead.value,outWrite.value,errWrite.value};
   if(!UpdateProcThreadAttribute(start.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.handle_allowlist_unavailable_c7e6d871", "Handle allowlist unavailable"));
   auto command=CommandQuote(executable.wstring())+L" --control";if(policy.excludeHostWindow)command+=L" --host-pid "+std::to_wstring(GetCurrentProcessId());PROCESS_INFORMATION pi{};
   if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,executable.parent_path().c_str(),&start.StartupInfo,&pi))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.window_worker_could_not_start_a7d6d7e6", "Window Worker could not start."));
   process.Reset(pi.hProcess);processExitProven.store(false,std::memory_order_release);OwnedHandle thread(pi.hThread);
   if(!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,4);throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.window_worker_could_not_be_contained_a79e3b24", "Window Worker could not be contained."));}
   if(ResumeThread(thread.value)==DWORD(-1))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.window_worker_could_not_resume_602c2ffe", "Window Worker could not resume."));
   revision=0;lastResponse=lastQuery=GetTickCount64();pending=AJson();stderrTail.clear();
   {std::lock_guard lock(mutex);workerPid=pi.dwProcessId;view.phase="Connecting";view.busy=true;view.ready=false;view.active=false;view.forcedTermination=false;view.message=Neurotic::UiMessage("desktop.anythingcontroller.connecting_to_window_worker_a05cb292", "Connecting to Window Worker...");view.status=AJson::object();}
   return true;
  }catch(const std::exception& e){Kill(e.what(),true,true);return false;}
 }
 void Send(AJson request){
  if(request.value("command","")!="catalog"&&request.value("command","")!="status"&&request.value("command","")!="measure-frame")request["revision"]=++revision;
  auto payload=request;payload.erase("_depositStamp");wire=payload.dump()+"\n";if(wire.size()>65536)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.request_exceeds_worker_command_limit_a116c79e", "Request exceeds worker command limit"));
  pending=request;auto command=request.value("command","");pendingDeadline=GetTickCount64()+(command=="stop"||command=="cancel"?2000:command=="catalog"||command=="status"||command=="measure-frame"?5000:30000);
  write={};write.hEvent=writeEvent.value;ResetEvent(write.hEvent);DWORD written=0;
  if(!WriteFile(input.value,wire.data(),DWORD(wire.size()),&written,&write)){if(GetLastError()!=ERROR_IO_PENDING)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.window_worker_input_closed_79cf33d1", "Window Worker input closed"));writing=true;}
  else if(written!=wire.size())throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.incomplete_worker_request_c9d4ef35", "Incomplete worker request"));
 }
 void Receive(const std::string& line){
  auto event=AJson::parse(line);if(!event.is_object()||!event.contains("event")||!event["event"].is_string())throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.invalid_worker_event_123d2ab8", "Invalid worker event"));
  auto kind=event["event"].get<std::string>();bool acknowledged=false;std::string modelToSave;
  {std::lock_guard lock(mutex);
   if(kind=="catalog"){
    if(!event.contains("windows")||!event["windows"].is_array()||event["windows"].size()>256)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.invalid_window_catalog_7f022b02", "Invalid window catalog"));
    view.windows.clear();for(auto& window:event["windows"])if(Identity(window)&&(!policy.excludeHostWindow||window["pid"].get<uint64_t>()!=GetCurrentProcessId()))view.windows.push_back(window);
    acknowledged=!pending.is_null()&&pending.value("command","")=="catalog";
    if(acknowledged)++view.catalogRevision;
   }
   if(event.contains("phase")){
    auto phase=event.at("phase").get<std::string>();
    if(!event.contains("capabilities")||!event["capabilities"].is_object()||!event["capabilities"].value("hostProcessExclusion",false)||!event["capabilities"].value("requestRevisionErrors",false))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.window_worker_build_is_incompatible_with_this_ap_f673914f", "Window Worker build is incompatible with this App. Update both together."));
    if(event.value("apiVersion",0)!=1||!(phase=="Locked"||phase=="Ready"||phase==Neurotic::UiLiteral("desktop.anythingview.countdown_58c4583f", "Countdown")||phase==Neurotic::UiLiteral("desktop.anythingview.starting_7e573dfb", "Starting")||phase==Neurotic::UiLiteral("desktop.anythingview.running_b9a06cc6", "Running")||phase==Neurotic::UiLiteral("desktop.anythingview.stopped_6c16cb30", "Stopped")||phase==Neurotic::UiLiteral("desktop.anythingview.error_eab1d8bf", "Error")||phase=="RestartRequired"))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.unsupported_worker_state_058dfbbf", "Unsupported worker state"));
    if(!event.contains("model")||!event["model"].is_object()||!event["model"].contains("ready")||!event["model"]["ready"].is_boolean())throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.invalid_model_readiness_514ca717", "Invalid model readiness"));
    view.connected=true;view.status=event;view.status["connection"]=ProjectWorkerStatus(event);if(!view.stopping){view.phase=phase;view.active=ActivePhase(phase);if(!view.active){queuedGeneration=AJson();queuedProcessing=AJson();queuedComparison=AJson();}}
    view.modelVerified=event["model"]["ready"].get<bool>();view.ready=view.modelVerified&&!event.value("restartRequired",false);
    if(event["model"].contains(Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"))){auto path=event["model"][Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path")].get<std::string>();if(!path.empty())view.modelPath=path;}
    if(event.contains(Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason")))view.message=event[Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason")].get<std::string>();
    if(event.contains("requestedRevision"))revision=std::max(revision,event["requestedRevision"].get<uint64_t>());
    if(pending.is_null())view.busy=!queued.is_null();
    else{auto command=pending.value("command","");auto expected=pending.value("revision",0ull),applied=event.value("appliedRevision",0ull),requested=event.value("requestedRevision",0ull);
     acknowledged=command=="status"||(command=="start"&&requested>=expected&&(phase==Neurotic::UiLiteral("desktop.anythingview.countdown_58c4583f", "Countdown")||phase==Neurotic::UiLiteral("desktop.anythingview.running_b9a06cc6", "Running")))||((command=="stop"||command=="cancel")&&requested>=expected&&(phase==Neurotic::UiLiteral("desktop.anythingview.stopped_6c16cb30", "Stopped")||phase=="Locked"||phase==Neurotic::UiLiteral("desktop.anythingview.error_eab1d8bf", "Error")||phase=="RestartRequired"))||(command=="snapshot"&&kind=="snapshot-requested"&&applied>=expected)||(command=="set-processing"&&kind=="processing-requested"&&requested>=expected)||(command=="set-comparison"&&kind=="comparison"&&applied>=expected)||(command=="set-frame-generation"&&kind=="frame-generation"&&applied>=expected)||((command=="set-model"||command=="import-model")&&kind=="model"&&applied>=expected);
     if(acknowledged&&(command=="set-model"||command=="import-model")&&view.ready){
      depositSelected=policy.persistPreferences&&std::filesystem::path(Wide(view.modelPath))==Deposit();
      if(depositSelected){auto stamp=ModelStamp(Deposit());depositBlocked=stamp.empty()||(pending.contains("_depositStamp")&&stamp!=pending["_depositStamp"].get<std::string>());
       if(!depositBlocked){verifiedDeposit=stamp;observedDeposit=stamp;}
       else{verifiedDeposit.clear();observedDeposit.clear();}
      }
      if(!depositSelected||!depositBlocked)modelToSave=view.modelPath;
     }
     if(command=="measure-frame"){
      acknowledged=(kind=="measurement"||kind=="measurement-rejected")&&event.contains("measurementRequestId")&&event["measurementRequestId"].is_number_unsigned()&&event["measurementRequestId"].get<uint64_t>()==pending.value("requestId",0ull);
      if(acknowledged&&kind=="measurement-rejected")view.lastError=event.value("measurementError",std::string("Measurement unavailable"));
     }
    }
   }
   if(kind=="error"){
    view.message=event.value(Neurotic::UiLiteral("desktop.anythingview.reason_adbde5fa", "reason"),std::string(Neurotic::UiMessage("desktop.anythingcontroller.window_worker_reported_an_error_8d61b524", "Window Worker reported an error.")));
    view.lastError=view.message;
    if(event.contains("model")&&!view.modelVerified){view.ready=false;view.phase="Locked";}
    else if(event.contains("model")){view.ready=true;view.phase="Ready";}
    if(!pending.is_null()){
     auto expected=pending.value("revision",0ull);
     acknowledged=expected==0||(event.contains("requestRevision")&&event["requestRevision"].is_number_unsigned()&&event["requestRevision"].get<uint64_t>()==expected)||(event.contains("requestedRevision")&&event["requestedRevision"].is_number_unsigned()&&event["requestedRevision"].get<uint64_t>()>=expected);
     if(pending.value("command","")=="measure-frame")acknowledged=event.contains("measurementRequestId")&&event["measurementRequestId"].is_number_unsigned()&&event["measurementRequestId"].get<uint64_t>()==pending.value("requestId",0ull);
     if(acknowledged&&pending.value("command","")=="start"){view.active=false;view.phase=Neurotic::UiLiteral("desktop.anythingview.error_eab1d8bf", "Error");}
     if(acknowledged&&(pending.value("command","")=="stop"||pending.value("command","")=="cancel"))throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.worker_could_not_acknowledge_safe_stopping_78abe516", "Worker could not acknowledge safe stopping: ")+view.message);
    }
   }
   if(acknowledged){
    if(pending.value("command","")=="measure-frame"){
     pendingMeasurementId=0;
     if(kind=="error")view.status["measurement"]={{"state","failed"},{"requestId",pending.value("requestId",0ull)},{"summaries",nullptr},{"ownershipRetired",false},{"reason",view.lastError}};
    }
    pending=AJson();view.busy=!queued.is_null();if(view.stopping){view.stopping=false;view.active=false;view.phase=event.value("phase",std::string(Neurotic::UiLiteral("desktop.anythingview.stopped_6c16cb30", "Stopped")));
    if(view.phase=="RestartRequired"&&!savedModel.empty()){restartWanted=true;view.busy=true;view.message=Neurotic::UiMessage("desktop.anythingcontroller.rendering_stopped_preparing_the_worker_for_your__317e5dc1", "Rendering stopped. Preparing the worker for your next session...");}
   }}
   if(depositSelected&&depositBlocked){view.ready=false;view.modelVerified=false;if(!view.active&&!view.stopping)view.phase="Locked";}
   ProjectMeasurement();
  }
  lastResponse=GetTickCount64();if(!modelToSave.empty())SaveModel(modelToSave);
 }
 void Drain(HANDLE pipe,bool protocol){
  size_t budget=512*1024;char buffer[8192];
  while(budget){DWORD available=0;if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)){if(GetLastError()==ERROR_BROKEN_PIPE)return;throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.worker_pipe_could_not_be_read_ee5357ce", "Worker pipe could not be read"));}if(!available)return;
   DWORD read=0;if(!ReadFile(pipe,buffer,DWORD(std::min({size_t(available),sizeof(buffer),budget})),&read,nullptr)||!read)return;budget-=read;
   if(!protocol){stderrTail.append(buffer,read);if(stderrTail.size()>16384)stderrTail.erase(0,stderrTail.size()-16384);continue;}
   for(DWORD i=0;i<read;++i){if(buffer[i]=='\n'){if(!stdoutBuffer.empty()&&stdoutBuffer.back()=='\r')stdoutBuffer.pop_back();if(!stdoutBuffer.empty())Receive(stdoutBuffer);stdoutBuffer.clear();}else{if(stdoutBuffer.size()>=MaxLine)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.worker_output_exceeds_the_bounded_protocol_limit_06d33412", "Worker output exceeds the bounded protocol limit"));stdoutBuffer+=buffer[i];}}
  }
 }
 void Loop(){
  while(!finish){
   try{
    bool reconnect=false,connect=false,stop=false;{
     std::lock_guard lock(mutex);reconnect=restartWanted;restartWanted=false;connect=connectWanted;connectWanted=false;stop=stopWanted;stopWanted=false;
    }
    if(reconnect){Kill(Neurotic::UiLiteral("desktop.anythingcontroller.restarting_window_worker_717d591d", "Restarting Window Worker."),true,false);if(Launch())RestoreModel();}
    else if(connect&&!process.value){if(Launch())RestoreModel();}
    if(process.value){
     if(writing){DWORD written=0;if(GetOverlappedResult(input.value,&write,&written,FALSE)){writing=false;if(written!=wire.size())throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.incomplete_worker_request_c9d4ef35", "Incomplete worker request"));}else if(GetLastError()!=ERROR_IO_INCOMPLETE)throw std::runtime_error(Neurotic::UiMessage("desktop.anythingcontroller.worker_input_failed_7e7dddfe", "Worker input failed"));}
     if(stop){if(writing){Kill(Neurotic::UiLiteral("desktop.anythingcontroller.stopped_the_unresponsive_window_worker_process_g_b9f7c95e", "Stopped the unresponsive Window Worker process; GPU retirement was not proved."),true,false);}
      else{pending=AJson();Send({{"command","stop"}});}}
     if(!process.value){Sleep(10);continue;}
     Drain(output.value,true);Drain(stderrRead.value,false);
     if(WaitForSingleObject(process.value,0)==WAIT_OBJECT_0){DWORD code=0;GetExitCodeProcess(process.value,&code);Kill(Neurotic::UiLiteral("desktop.anythingcontroller.window_worker_exited_519763c8", "Window Worker exited (")+std::to_string(code)+Neurotic::UiLiteral("desktop.anythingcontroller.restart_to_reconnect_b5b7f609", "). Restart to reconnect."),false,true);}
     else if(!pending.is_null()&&GetTickCount64()>=pendingDeadline){bool wasStop=pending.value("command","")=="stop";Kill(wasStop?Neurotic::UiLiteral("desktop.anythingcontroller.stopped_the_unresponsive_window_worker_process_g_b9f7c95e", "Stopped the unresponsive Window Worker process; GPU retirement was not proved."):Neurotic::UiLiteral("desktop.anythingcontroller.window_worker_did_not_acknowledge_the_operation__a7e18077", "Window Worker did not acknowledge the operation; its process was stopped. Restart to reconnect."),true,!wasStop);}
     else if(pending.is_null()&&GetTickCount64()-lastResponse>15000){Kill(Neurotic::UiLiteral("desktop.anythingcontroller.window_worker_stopped_responding_its_process_was_0e8ae789", "Window Worker stopped responding; its process was stopped. Restart to reconnect."),true,true);}
     else if(!writing&&pending.is_null()){
      AJson request;{
       std::lock_guard lock(mutex);
       if(policy.persistPreferences&&GetTickCount64()-lastDepositCheck>=250&&!view.active&&!view.stopping&&!view.busy&&queued.is_null()){
        lastDepositCheck=GetTickCount64();auto stamp=ModelStamp(Deposit());
        if(!stamp.empty()&&(!depositSelected||stamp!=observedDeposit)){
         depositSelected=true;depositBlocked=true;observedDeposit=stamp;view.ready=false;view.modelVerified=false;view.busy=true;view.lastError.clear();view.message=Neurotic::UiMessage("desktop.anythingcontroller.verifying_your_deposited_nr_file_009a280c", "Verifying your deposited NR file...");
         queued={{"command","set-model"},{Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),Utf8(Deposit().wstring())},{"_depositStamp",stamp}};
        }else if(depositSelected&&(stamp.empty()||stamp!=verifiedDeposit)){
         depositBlocked=true;view.ready=false;view.modelVerified=false;view.phase="Locked";
         if(stamp.empty()){observedDeposit.clear();view.message=Neurotic::UiMessage("desktop.anythingcontroller.place_your_compatible_nvngx_dlssnr_dll_in_the_mo_96e731a9", "Place your compatible nvngx_dlssnr.dll in the model folder.");}
        }
       }
       if(!queuedProcessing.is_null()&&view.active&&!view.stopping&&(queued.is_null()||queued.value("command",std::string{})=="snapshot"||queued.value("command",std::string{})=="measure-frame")){request=std::move(queuedProcessing);queuedProcessing=AJson();}
       else if(!queued.is_null()&&(queued.value("command",std::string{})=="snapshot"||queued.value("command",std::string{})=="measure-frame")&&!queuedComparison.is_null()){request=std::move(queuedComparison);queuedComparison=AJson();}
       else if(!queued.is_null()&&queued.value("command",std::string{})=="measure-frame"&&!queuedGeneration.is_null()){request=std::move(queuedGeneration);queuedGeneration=AJson();}
       else if(!queued.is_null()){request=std::move(queued);queued=AJson();}
       else if(!queuedComparison.is_null()&&view.active&&!view.stopping){request=std::move(queuedComparison);queuedComparison=AJson();}
       else if(!queuedGeneration.is_null()&&view.active&&!view.stopping){request=std::move(queuedGeneration);queuedGeneration=AJson();}
      }
      if(!request.is_null())Send(std::move(request));
      else if(GetTickCount64()-lastQuery>1000){lastQuery=GetTickCount64();Send({{"command","status"}});}
     }
    }
   }catch(const std::exception& e){Kill(std::string(Neurotic::UiLiteral("desktop.anythingcontroller.window_worker_connection_failed_53f4f361", "Window Worker connection failed: "))+e.what(),true,true);}
   Sleep(10);
  }
  Kill(Neurotic::UiLiteral("desktop.anythingcontroller.application_closed_window_worker_process_stopped_28c1585a", "Application closed; Window Worker process stopped."),true,false);
 }
 void RestoreModel(){
  if(!policy.persistPreferences){std::lock_guard lock(mutex);if(!savedModel.empty())queued={{"command","set-model"},{"path",savedModel}};return;}
  auto stamp=ModelStamp(Deposit());std::lock_guard lock(mutex);verifiedDeposit.clear();observedDeposit.clear();
  if(!stamp.empty()){depositSelected=true;depositBlocked=true;observedDeposit=stamp;queued={{"command","set-model"},{Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),Utf8(Deposit().wstring())},{"_depositStamp",stamp}};}
  else if(!savedModel.empty()){
   auto source=std::filesystem::path(Wide(savedModel));
   if(source==Deposit()){depositSelected=true;depositBlocked=true;return;}
   // Only verified worker import may publish legacy private bytes. It refuses
   // existing destinations and never modifies the source or follows links.
   queued={{"command",Ordinary(source)?"import-model":"set-model"},{Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),savedModel}};
   if(queued["command"]=="import-model")queued["modelsRoot"]=Utf8(Folder().wstring());
  }
 }
};
AnythingController::AnythingController(const std::filesystem::path& worker,const std::filesystem::path& userData):AnythingController(worker,userData,SessionPolicy{}){}
AnythingController::AnythingController(const std::filesystem::path& worker,const std::filesystem::path& userData,SessionPolicy policy):impl(std::make_unique<Impl>(worker,userData,policy)){}
AnythingController::~AnythingController()=default;
std::filesystem::path AnythingController::ModelFolder() const{return neurotic::model::Folder(impl->data);}
bool AnythingController::EnsureModelFolder(){if(!impl->policy.persistPreferences)return false;try{neurotic::model::Prepare(ModelFolder());return true;}catch(const std::exception& e){std::lock_guard lock(impl->mutex);impl->view.lastError=impl->view.message=e.what();return false;}}
AnythingSnapshot AnythingController::Snapshot() const{std::lock_guard lock(impl->mutex);return impl->view;}
void AnythingController::Connect(){std::lock_guard lock(impl->mutex);impl->connectWanted=true;}
void AnythingController::Restart(){std::lock_guard lock(impl->mutex);if(impl->view.connected&&impl->view.busy&&!impl->view.active)return;impl->pendingMeasurementId=0;impl->measurementCanceled=true;impl->ProjectMeasurement();impl->restartWanted=true;impl->view.ready=false;impl->view.active=false;impl->view.busy=true;impl->view.lastError.clear();impl->view.message=Neurotic::UiMessage("desktop.anythingcontroller.restarting_window_worker_73a74625", "Restarting Window Worker...");}
void AnythingController::Stop(){std::lock_guard lock(impl->mutex);if(!impl->view.connected)return;impl->pendingMeasurementId=0;impl->measurementCanceled=true;impl->ProjectMeasurement();impl->queued=AJson();impl->queuedComparison=AJson();impl->queuedProcessing=AJson();impl->queuedGeneration=AJson();impl->stopWanted=true;impl->view.busy=true;impl->view.stopping=true;impl->view.active=false;impl->view.phase="Stopping";impl->view.message=Neurotic::UiMessage("desktop.anythingcontroller.stopping_rendering_70263e0c", "Stopping rendering...");}
bool AnythingController::ShutdownAndWait(unsigned stopTimeoutMs){Stop();return impl->ShutdownAndWait(stopTimeoutMs);}
bool AnythingController::SelectModel(const std::filesystem::path& file,bool import){
 if(import&&!impl->policy.persistPreferences)return false;
 auto path=Utf8(file.wstring());if(path.empty()||path.find('\0')!=std::string::npos||path.size()>32768)return false;
 std::lock_guard lock(impl->mutex);if(!impl->view.connected||impl->view.busy||impl->view.active||impl->view.stopping)return false;
 impl->queued={{"command",import?"import-model":"set-model"},{Neurotic::UiLiteral("desktop.anythingview.path_aaaf4056", "path"),path}};if(import)impl->queued["modelsRoot"]=Utf8(impl->Folder().wstring());impl->view.busy=true;impl->view.ready=false;impl->view.lastError.clear();impl->view.message=import?Neurotic::UiMessage("desktop.anythingcontroller.verifying_and_importing_your_nr_file_672870cd", "Verifying and importing your NR file..."):Neurotic::UiMessage("desktop.anythingcontroller.verifying_your_nr_file_e9254f4c", "Verifying your NR file...");return true;
}
bool AnythingController::RefreshWindows(const std::string& search){std::lock_guard lock(impl->mutex);if(!impl->view.connected||impl->view.busy||impl->view.stopping||search.size()>1024)return false;impl->queued={{"command","catalog"},{"search",search}};if(impl->policy.excludeHostWindow)impl->queued["hostPid"]=GetCurrentProcessId();impl->view.busy=true;impl->view.lastError.clear();return true;}
bool AnythingController::Start(AJson options){
 std::lock_guard lock(impl->mutex);if(!options.is_object()||!impl->view.connected||impl->view.busy||impl->view.active||impl->view.stopping)return false;
 const auto capabilities=impl->view.status.value("capabilities",AJson::object());
 if(options.contains("neuralRendering")&&!options["neuralRendering"].is_boolean())return false;
 if(options.contains("superResolution")&&!options["superResolution"].is_string())return false;
 const bool neuralRendering=options.value("neuralRendering",true);
 const auto sr=options.value("superResolution",std::string("off"));
 if(sr!="off"&&sr!="fsr1")return false;
 if((sr!="off"&&!capabilities.value("spatialSuperResolution",false))||(!neuralRendering&&!capabilities.value("optionalNeuralRendering",false))){
  impl->view.lastError=impl->view.message=Neurotic::UiMessage("desktop.anything.sr_update_worker","Update the App and worker together to use these stages.");return false;
 }
 if(options.contains("outputMode")&&!options["outputMode"].is_string())return false;
 const auto output=options.value("outputMode",std::string("overlay"));
 if(output!="overlay"&&output!="preview"&&output!="fullscreen")return false;
 if(output=="fullscreen"&&(sr!="fsr1"||!capabilities.value("fullscreenSuperResolution",false)))return false;
 // Older workers retain their exact default request shape.
 if(!capabilities.value("spatialSuperResolution",false))options.erase("superResolution");
 if(!capabilities.value("optionalNeuralRendering",false))options.erase("neuralRendering");
 if(options.contains("frameGeneration")&&options["frameGeneration"]!="off"&&options["frameGeneration"]!="2x")return false;
 if(!impl->view.status.value("capabilities",AJson::object()).value("fsrFrameGeneration2x",false))options.erase("frameGeneration");
 impl->queuedGeneration=AJson();
 if(options.value("depthProvider",std::string("off"))!="off"||options.value("depthPreview",false)){
  impl->view.lastError=impl->view.message=Neurotic::UiMessage("desktop.anythingcontroller.depth_estimation_is_unavailable_in_nr_anything_d1547de6", "Depth estimation is unavailable in NR Anything.");return false;
 }
 for(const auto* key:{"depthProvider","depthProfile","depthHz","depthPreview","depthRoot"})options.erase(key);
 if(neuralRendering&&impl->depositSelected){auto stamp=ModelStamp(impl->Deposit());if(impl->depositBlocked||stamp.empty()||stamp!=impl->verifiedDeposit){impl->depositBlocked=true;impl->view.ready=false;impl->view.modelVerified=false;return false;}}
 if(!AnythingReadyForStages(impl->view,neuralRendering))return false;
 if(options.contains("nrScalePercent")) {
  const auto& scale=options["nrScalePercent"];
  if(!scale.is_number_integer() || scale.is_boolean() || scale.get<int64_t>()<25 || scale.get<int64_t>()>100 || options.contains(Neurotic::UiLiteral("desktop.anythingview.workwidth_d258bdc6", "workWidth")) || options.contains(Neurotic::UiLiteral("desktop.anythingview.workheight_6220a8cc", "workHeight")))return false;
  if(!impl->view.status.value("capabilities",AJson::object()).value("nrResolutionScale",false)) {
   impl->view.lastError=impl->view.message=Neurotic::UiMessage("desktop.anythingcontroller.update_the_app_and_window_worker_together_to_use_d0e572ea", "Update the App and Window Worker together to use NR resolution presets.");return false;
  }
 }
 if(options.value("modelStyle",0)!=0&&!impl->view.status.value("capabilities",AJson::object()).value("modelStyles",false))return false;
 if(options.value("stripes",0)!=0&&!impl->view.status.value("capabilities",AJson::object()).value(Neurotic::UiLiteral("desktop.anythingview.comparisonstripes_8c9c62e7", "comparisonStripes"),false))return false;
 if(options.contains("comparisonDirection")){
  const auto& direction=options["comparisonDirection"];
  if(!direction.is_number_integer()||direction.get<int64_t>()<0||direction.get<int64_t>()>7)return false;
  if(direction.get<int>()&&!impl->view.status.value("capabilities",AJson::object()).value(Neurotic::UiLiteral("desktop.anythingview.comparisondirections_d1a35c7e", "comparisonDirections"),false)){impl->view.lastError=impl->view.message=Neurotic::UiMessage("desktop.anythingcontroller.update_the_window_worker_to_use_this_comparison__762566a4", "Update the Window Worker to use this comparison direction.");return false;}
 }
 NrPreferences preferences;
 preferences.transferStrength=options.value("transferStrength",1.f);preferences.colourStrength=options.value("colourStrength",1.f);
 preferences.comparison=options.value("split",0.f);preferences.nrScalePercent=options.value("nrScalePercent",100u);
 preferences.overlay=options.value("outputMode",std::string("overlay"))=="overlay";
 preferences.requestsDepth=options.value("guides",std::string("off"))!="off";
 const auto mapped=MapNrPreferences(preferences,{impl->view.status.value("capabilities",AJson::object()).value("nrResolutionScale",false)});
 if(!mapped.accepted){impl->view.lastError=impl->view.message=mapped.reason;return false;}
 for(auto it=mapped.options.begin();it!=mapped.options.end();++it)options[it.key()]=it.value();
 options["outputMode"]=output;
 auto mode=options.value("mode",std::string("countdown"));if(mode!="selected"&&mode!="countdown")return false;
 if(mode=="selected"){
  if(!options.contains("window")||!Identity(options["window"]))return false;
  bool found=false;for(auto& candidate:impl->view.windows)if(SameWindow(candidate,options["window"])){options["window"]=candidate;found=true;break;}if(!found)return false;
 }else{auto seconds=options.value("seconds",5u);if(seconds<1||seconds>30)return false;options["seconds"]=seconds;}
 // Called by the foreground App's Start action; grant only the contained child, never ASFW_ANY.
 if(mode=="selected"&&options.value("outputMode",std::string("overlay"))!="preview"&&impl->workerPid)AllowSetForegroundWindow(impl->workerPid);
 impl->queuedProcessing=AJson();impl->queuedComparison=AJson();options["command"]="start";options.erase("revision");impl->queued=std::move(options);impl->view.busy=true;impl->view.active=true;impl->view.lastError.clear();impl->view.status=AJson::object();impl->view.message=Neurotic::UiMessage("desktop.anythingcontroller.requesting_rendering_10de6ca9", "Requesting rendering...");return true;
}
void AnythingController::SetComparison(float split){SetComparison(split,0);}
bool AnythingController::SetComparison(float split,int stripes,int direction){
 if(!std::isfinite(split)||stripes<0||stripes==1||stripes>32||direction<0||direction>7)return false;
 std::lock_guard lock(impl->mutex);if(!impl->view.connected||!impl->view.active||impl->view.stopping)return false;
 if(stripes&&!impl->view.status.value("capabilities",AJson::object()).value(Neurotic::UiLiteral("desktop.anythingview.comparisonstripes_8c9c62e7", "comparisonStripes"),false))return false;
 if(direction&&!impl->view.status.value("capabilities",AJson::object()).value(Neurotic::UiLiteral("desktop.anythingview.comparisondirections_d1a35c7e", "comparisonDirections"),false))return false;
 impl->queuedComparison={{"command","set-comparison"},{"split",std::clamp(split,0.f,1.f)},{"stripes",stripes},{"comparisonDirection",direction}};return true;
}
bool AnythingController::SetProcessing(AJson settings){
 if(!settings.is_object()||settings.size()!=4)return false;
 try{for(const char* key:{"transferStrength","colourStrength"}){if(!settings.at(key).is_number())return false;auto value=settings[key].get<double>();if(!std::isfinite(value)||value<0||value>2)return false;}
  if(!settings.at("modelStyle").is_number_integer()||settings["modelStyle"].get<int>()<0||settings["modelStyle"].get<int>()>2||!settings.at("nrScalePercent").is_number_integer()||settings["nrScalePercent"].get<int>()<25||settings["nrScalePercent"].get<int>()>100)return false;
 }catch(...){return false;}
 std::lock_guard lock(impl->mutex);
 if(!impl->view.connected||!impl->view.active||impl->view.stopping||!impl->view.status.value("capabilities",AJson::object()).value(Neurotic::UiLiteral("desktop.anythingview.liveprocessing_ac48aaa6", "liveProcessing"),false))return false;
 settings["command"]="set-processing";impl->queuedProcessing=std::move(settings);return true;
}
bool AnythingController::SetFrameGeneration(bool enabled){
 std::lock_guard lock(impl->mutex);
 if(!impl->view.connected||!impl->view.active||impl->view.stopping||!impl->view.status.value("capabilities",AJson::object()).value("fsrFrameGeneration2x",false))return false;
 impl->queuedGeneration={{"command","set-frame-generation"},{"frameGeneration",enabled?"2x":"off"}};return true;
}
bool AnythingController::TakeSnapshot(){
 std::lock_guard lock(impl->mutex);const auto& v=impl->view;
 if(!v.connected||!v.active||v.stopping||v.busy||v.phase!=Neurotic::UiLiteral("desktop.anythingview.running_b9a06cc6", "Running")||v.status.value(Neurotic::UiLiteral("desktop.anythingview.sourcepaused_96a59408", "sourcePaused"),true)||
    !v.status.value("capabilities",AJson::object()).value("outputSnapshots",false)||v.status.value("snapshot",AJson::object()).value("state",std::string{})==Neurotic::UiLiteral("desktop.anythingview.pending_399dd91d", "pending"))return false;
 impl->queued={{"command","snapshot"}};impl->view.busy=true;return true;
}
bool AnythingController::MeasureOneFrame(){
 std::lock_guard lock(impl->mutex);
 if(!AnythingCanMeasureFrame(impl->view)||!impl->queued.is_null()||!impl->queuedProcessing.is_null()||!impl->queuedComparison.is_null()||!impl->queuedGeneration.is_null()||impl->measurementId==UINT64_MAX)return false;
 impl->pendingMeasurementId=++impl->measurementId;impl->measurementCanceled=false;
 impl->queued={{"command","measure-frame"},{"requestId",impl->pendingMeasurementId}};impl->view.busy=true;impl->ProjectMeasurement();return true;
}
std::filesystem::path AnythingController::FindWorker(const std::filesystem::path& appRoot){
 for(auto path:{appRoot/L"Tools"/L"WindowWorker"/L"NeuRotic.WindowWorker.exe",appRoot/L"window-worker"/L"NeuRotic.WindowWorker.exe",appRoot.parent_path()/L"WindowWorker"/L"NeuRotic.WindowWorker.exe"})if(Ordinary(path))return std::filesystem::absolute(path);
 return std::filesystem::absolute(appRoot/L"window-worker"/L"NeuRotic.WindowWorker.exe");
}
}
