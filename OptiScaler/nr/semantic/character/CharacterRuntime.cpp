#ifndef CHARACTER_RUNTIME_TEST
#include "pch.h"
#include <Logger.h>
#else
#define NOMINMAX
#include <windows.h>
#define LOG_INFO(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#endif
#include "CharacterRuntime.h"
#include "CharacterCohort.h"
#include "CharacterInstallLayout.h"
#include "CharacterRealFrame.h"
#include "CharacterCaptureDx12.h"
#include "CharacterCaptureDx11.h"
#include "CharacterCaptureVk.h"
#ifndef CHARACTER_RUNTIME_TEST
#include "CharacterEarlyCaptureDx12.h"
#include <dlssnr/NativeIdentity.h>
#endif
#include "CharacterWire.h"
#include "CharacterTrackingWire.h"
#include "CharacterSourcePolicy.h"
#include "CharacterFgActivity.h"
#include <dlssnr/HdrObservation.h>
#include <bcrypt.h>
#include <sddl.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstring>
#include <cmath>
#pragma comment(lib,"bcrypt.lib")
#pragma comment(lib,"advapi32.lib")

namespace Neurotic::Semantic::Character {
namespace {
std::uint64_t Now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Handle {HANDLE value=nullptr;Handle()=default;explicit Handle(HANDLE v):value(v){};~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;};
struct Runtime {
    std::atomic<bool> requested{false},alive{false},qualified{false},enabled{false};
    std::atomic<WorkerState> worker{WorkerState::Stopped};
    std::atomic<std::uint64_t> generation{1},scene{1},sequence{0};
    std::atomic<unsigned> captures{0},dropped{0},results{0};
    std::atomic<unsigned> processed{0},noPose{0},expired{0},epochRejected{0},sceneResets{0},inferenceMs{0},replyAgeMs{0};
    std::atomic<bool> dxgiDefaultColor{false},awaitingReplacement{false};
    std::atomic<bool> fgActive{false},physicalOutputQualified{false};
    std::atomic<std::uint64_t> lastEarlyPublishedNs{0},lastQualifiedPresentNs{0};
    std::atomic<std::uint64_t> admittedEarlyFeature{0},lastAdmittedEarlyNs{0};
    std::atomic<std::uint64_t> lastSeenBoundEarlyNs{0};
    std::atomic<std::uint64_t> earlyAmbiguousUntilNs{0};
    std::atomic<unsigned> earlyCaptures{0},sourceSwitches{0};
    std::atomic<unsigned> fgCaptures{0};
    std::mutex mutex;
    Epoch current{};Snapshot snapshot{};bool hasSnapshot=false;
    BoxHold boxes;
    RealFrameClock realClock;RealFrameDisplay realDisplay;
    std::atomic<std::uintptr_t> applicationOwner{0};
    std::string reason="Start live inspection to load the local provider.";
    InspectorSettings settings{};
    DetectionStats stages{};
    TrackingStats motion{};
    CharacterCaptureDx12 capture;
    CharacterCaptureDx11 capture11;
#ifndef CHARACTER_RUNTIME_TEST
    CharacterEarlyCaptureDx12 earlyCapture;
    Microsoft::WRL::ComPtr<ID3D12Device> presentDevice;
#endif
    Detail::CaptureSourcePolicy sourcePolicy;
    std::atomic<unsigned> sourceApi{0};
    unsigned externalColorPolicy=0,displayColorPolicy=0;
    std::uint64_t externalDeviceIdentity=0;
    CpuFrame externalFrame;bool hasExternalFrame=false;
    unsigned presentWidth=0,presentHeight=0;
    GateTimer timer,earlyTimer;
};
// Process-lifetime scoped state prevents destructor-based release of unknown GPU
// submissions. The control thread holds a module lease until it has stopped.
Runtime& Get(){static auto* value=new Runtime;return *value;}
std::string Hex(const unsigned char* p,std::size_t n){const char digits[]="0123456789abcdef";std::string s;s.reserve(n*2);for(std::size_t i=0;i<n;++i){s+=digits[p[i]>>4];s+=digits[p[i]&15];}return s;}
std::string PixelHash(const std::vector<unsigned char>& pixels){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> result{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 unavailable");
    const auto created=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
    const bool ok=created>=0&&BCryptHashData(hash,const_cast<PUCHAR>(pixels.data()),static_cast<ULONG>(pixels.size()),0)>=0&&BCryptFinishHash(hash,result.data(),static_cast<ULONG>(result.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);if(!ok)throw std::runtime_error("SHA256 failed");return Hex(result.data(),result.size());
}
void SetReason(const std::string& text){auto& r=Get();std::lock_guard lock(r.mutex);r.reason=text;}
bool Transfer(HANDLE pipe,void* data,DWORD size,bool write,unsigned timeoutMs=2000){
    auto& r=Get();auto* at=static_cast<unsigned char*>(data);DWORD offset=0;
    const auto deadline=Now()+static_cast<std::uint64_t>(timeoutMs)*1'000'000;
    while(offset<size&&r.requested){
        Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event.value)return false;
        OVERLAPPED ov{};ov.hEvent=event.value;DWORD done=0;const DWORD part=std::min<DWORD>(size-offset,65536);
        BOOL ok=write?WriteFile(pipe,at+offset,part,&done,&ov):ReadFile(pipe,at+offset,part,&done,&ov);
        if(!ok&&GetLastError()!=ERROR_IO_PENDING)return false;
        if(!ok){
            while(r.requested&&Now()<deadline&&WaitForSingleObject(event.value,10)==WAIT_TIMEOUT){}
            if(!r.requested||Now()>=deadline){CancelIoEx(pipe,&ov);GetOverlappedResult(pipe,&ov,&done,TRUE);return false;}
            if(!GetOverlappedResult(pipe,&ov,&done,FALSE))return false;
        }
        if(!done)return false;offset+=done;
    }
    return offset==size;
}
void Send(HANDLE pipe,const Json& value){
    auto text=value.dump();if(text.empty()||text.size()>65536)throw std::runtime_error("request metadata bound");
    unsigned char prefix[4];const auto n=static_cast<unsigned>(text.size());for(unsigned i=0;i<4;++i)prefix[i]=static_cast<unsigned char>((n>>(8*i))&255);
    if(!Transfer(pipe,prefix,4,true)||!Transfer(pipe,text.data(),n,true))throw std::runtime_error("worker write failed or timed out");
}
Json Receive(HANDLE pipe,unsigned timeoutMs=2000){
    unsigned char prefix[4];if(!Transfer(pipe,prefix,4,false,timeoutMs))throw std::runtime_error("worker read failed or timed out");
    unsigned n=0;for(unsigned i=0;i<4;++i)n|=static_cast<unsigned>(prefix[i])<<(8*i);
    if(!n||n>1024*1024)throw std::runtime_error("result metadata bound");
    std::string text(n,'\0');if(!Transfer(pipe,text.data(),n,false))throw std::runtime_error("worker result truncated");
    return ParseWire(text);
}
struct Launch {std::filesystem::path root;HMODULE lease=nullptr;};
DWORD ControlBody(Launch* launch){
    auto& r=Get();bool fault=false;
    Handle process,pipe,client,nul;CharacterCohort cohort;PSECURITY_DESCRIPTOR security=nullptr;
    try {
        unsigned char nonceBytes[16];if(BCryptGenRandom(nullptr,nonceBytes,16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("nonce unavailable");
        const std::string nonce=Hex(nonceBytes,16);
        // Current-user ACL, local-only single endpoint, already connected in the
        // host before launch. Only its child endpoint is inherited by this child.
        Handle token;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value))throw std::runtime_error("token unavailable");
        DWORD needed=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&needed);if(!needed||needed>65536)throw std::runtime_error("token bound");
        std::vector<unsigned char> tokenData(needed);if(!GetTokenInformation(token.value,TokenUser,tokenData.data(),needed,&needed))throw std::runtime_error("token user unavailable");
        LPWSTR sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(tokenData.data())->User.Sid,&sid))throw std::runtime_error("SID unavailable");
        std::wstring acl=L"D:P(A;;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&security,nullptr))throw std::runtime_error("worker ACL unavailable");
        SECURITY_ATTRIBUTES sa{sizeof(sa),security,FALSE};const std::wstring name=L"\\\\.\\pipe\\NeuRotic.Character."+std::wstring(nonce.begin(),nonce.end());
        pipe.value=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&sa);
        if(pipe.value==INVALID_HANDLE_VALUE)throw std::runtime_error("local channel unavailable");
        sa.bInheritHandle=TRUE;client.value=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,&sa,OPEN_EXISTING,0,nullptr);
        if(client.value==INVALID_HANDLE_VALUE)throw std::runtime_error("child endpoint unavailable");
        Handle connectEvent(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED connect{};connect.hEvent=connectEvent.value;
        if(!ConnectNamedPipe(pipe.value,&connect)&&GetLastError()!=ERROR_PIPE_CONNECTED)throw std::runtime_error("child connection unavailable");
        nul.value=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        if(nul.value==INVALID_HANDLE_VALUE)throw std::runtime_error("stderr endpoint unavailable");
        const auto python=launch->root/L"runtime/python.exe",server=launch->root/L"server.py";
        if(!std::filesystem::is_regular_file(python)||!std::filesystem::is_regular_file(server))throw std::runtime_error("Install the Character Inspector worker package first.");
        // Authenticate code and native imports before executing Python. Keep all
        // file leases alive until this child exits, including model/runtime files.
        cohort.Verify(launch->root);
        SIZE_T attributeBytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&attributeBytes);std::vector<unsigned char> attributes(attributeBytes);
        auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());if(!InitializeProcThreadAttributeList(list,1,0,&attributeBytes))throw std::runtime_error("handle allowlist unavailable");
        HANDLE allowed[]{client.value,nul.value};STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=list;
        startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES;startup.StartupInfo.hStdInput=client.value;startup.StartupInfo.hStdOutput=client.value;startup.StartupInfo.hStdError=nul.value;
        if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowed,sizeof(allowed),nullptr,nullptr)){DeleteProcThreadAttributeList(list);throw std::runtime_error("handle allowlist failed");}
        std::wstring command=L"\""+python.wstring()+L"\" -I -B \""+server.wstring()+L"\"";PROCESS_INFORMATION info{};
        const bool created=CreateProcessW(python.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,launch->root.c_str(),&startup.StartupInfo,&info)!=FALSE;
        DeleteProcThreadAttributeList(list);if(!created)throw std::runtime_error("worker launch failed");
        process.value=info.hProcess;CloseHandle(info.hThread);CloseHandle(client.value);client.value=nullptr;
        // Cold model loading can exceed a frame reply's 2s fault deadline.
        // Only the startup header gets 8s; cancellation remains checked every 10ms.
        Send(pipe.value,{{"schema",3},{"nonce",nonce}});const auto hello=Receive(pipe.value,8000);
        if(!ExactFields(hello,{"schema","nonce","pid","models","provider"})||!ExactUnsigned(hello.at("schema"),3)||hello.at("nonce")!=nonce||!ExactUnsigned(hello.at("pid"),info.dwProcessId)||hello.at("models")!=ModelHashes()||hello.at("provider")!="opencv-zoo-cpu")throw std::runtime_error("worker identity mismatch");
        {std::lock_guard lock(r.mutex);std::memcpy(&r.current.sessionA,nonceBytes,8);std::memcpy(&r.current.sessionB,nonceBytes+8,8);r.current.provider=++r.generation;r.hasSnapshot=false;}
        r.worker=WorkerState::Ready;SetReason("Local CPU provider ready; waiting for a qualified game frame.");
        LOG_INFO("CharacterInspector worker ready pid={} provider=opencv-zoo-cpu",info.dwProcessId);
        OutstandingRequest ledger;SubmittedFrames history;
        auto nextDiagnostic=Now();std::uint64_t lastSubmittedSequence=0,lastSubmittedNs=0;
        while(r.requested){
            const auto diagnosticNow=Now();
            if(diagnosticNow>=nextDiagnostic){
                const auto view=TryCharacterRuntimeView();
                const auto gpu=r.capture.TryDiagnostics();
                const auto nativeFg=NativeFgWork().Read(diagnosticNow);
                LOG_INFO("CharacterInspector activity: nativeAvailable={} nativeRequested={} nativeRecentWork={} nativeObserved={} nativeInFlight={} maximumPersons={} lastPersons={} boxHoldMs={}",
                    nativeFg.available,nativeFg.requested,nativeFg.active,nativeFg.observed,nativeFg.inFlight,view.maximumPersons,view.lastPersons,view.boxHoldMs);
                LOG_INFO("CharacterInspector samples: qualified={} defaultSDR={} captures={} dropped={} processed={} fresh={} noPose={} expired={} epochRejected={} sceneResets={} inferenceMs={} replyAgeMs={} reason={}",
                    view.sourceQualified,view.dxgiDefaultColor,view.captures,view.dropped,view.processed,view.results,view.noPose,view.expired,view.epochRejected,view.sceneResets,view.inferenceMs,view.replyAgeMs,view.reason);
                LOG_INFO("CharacterInspector detection: candidates={} poseAttempts={} usablePoses={} returned={} detectorMs={} poseMs={}",view.detectorCandidates,view.poseAttempts,view.usablePoses,view.returnedDetections,view.detectorMs,view.poseMs);
                LOG_INFO("CharacterInspector capture: diagnosticAvailable={} format={} flags={} pending={} lastError={:X} operation={}",gpu.available,gpu.format,gpu.flags,gpu.pending,static_cast<unsigned>(gpu.lastError),gpu.operation);
                LOG_INFO("CharacterInspector motion: trackingMs={} detectorMs={} detectorUpdates={} poseMs={} poseUpdates={} poseFailures={}",view.trackingMs,view.detectorMs,view.detectionUpdates,view.poseMs,view.poseUpdates,view.poseFailures);
                LOG_INFO("CharacterInspector source: kind={} earlyCaptures={} sourceSwitches={} fgActive={} fgCaptures={} api={} generatedIdentity={}",view.earlySource?"pre-upscale":"present",view.earlyCaptures,view.sourceSwitches,view.fgActive,view.fgCaptures,view.sourceApi,view.realFrameSource?"application-image":"unknown");
#ifndef CHARACTER_RUNTIME_TEST
                LOG_INFO("CharacterInspector early source: {}",r.earlyCapture.Reason());
#endif
                nextDiagnostic=diagnosticNow+2'000'000'000;
            }
            r.capture.PrepareRequested();
            r.capture11.PrepareRequested();
            CpuFrame dx11Frame;if(r.capture11.TryTakeNewest(dx11Frame))CharacterPublishExternalFrame(std::move(dx11Frame));
            CpuFrame frame,presentFrame,earlyFrame;
            bool hasPresent=r.capture.TryTakeNewest(presentFrame),hasEarly=false;
            {std::unique_lock lock(r.mutex,std::try_to_lock);if(lock&&r.hasExternalFrame){
                if(!hasPresent||r.externalFrame.key.captureNs>presentFrame.key.captureNs){presentFrame=std::move(r.externalFrame);hasPresent=true;}
                r.hasExternalFrame=false;
            }}
#ifndef CHARACTER_RUNTIME_TEST
            r.earlyCapture.PrepareRequested();hasEarly=r.earlyCapture.TryTakeNewest(earlyFrame);
#endif
            if(!hasPresent&&!hasEarly){std::this_thread::sleep_for(std::chrono::milliseconds(5));continue;}
            if(!r.enabled||!r.qualified)continue;
            // Scene discontinuity is rejected by image-backed local motion in
            // the worker. A coarse screen-average cut test mistakes pans for cuts.
            {std::lock_guard lock(r.mutex);const auto now=Now();
                const auto valid=[&](const CpuFrame& candidate){
                    if(candidate.earlySource&&(!CharacterEarlyCaptureEnabled||r.fgActive)){++r.epochRejected;return false;}
                    if(candidate.key.epoch!=r.current||candidate.key.epoch.scene!=r.scene){++r.epochRejected;return false;}
                    if(candidate.key.captureNs>now||now-candidate.key.captureNs>DisplayTtlNs){++r.expired;return false;}
                    return candidate.key.sequence>lastSubmittedSequence&&candidate.key.captureNs>lastSubmittedNs;
                };
                hasPresent=hasPresent&&valid(presentFrame);hasEarly=hasEarly&&valid(earlyFrame);
                if(!hasPresent&&!hasEarly)continue;
                // A completed early image establishes health. Mere attempts or
                // pending GPU work must never suppress the Present fallback.
                using Choice=Detail::CaptureSourceChoice;
                const auto choice=r.sourcePolicy.Select(now,hasPresent,hasEarly?earlyFrame.key.captureNs:0,
                    earlyFrame.earlyFeatureGeneration,r.earlyAmbiguousUntilNs.load());
                r.lastEarlyPublishedNs=r.sourcePolicy.lastEarlyNs;
                if(choice==Choice::None)continue;
                if(choice==Choice::SwitchToEarly||choice==Choice::SwitchToPresent){
                    r.current.scene=++r.scene;r.hasSnapshot=false;r.boxes.Clear();++r.sourceSwitches;
                    continue; // A fresh capture must originate in this epoch.
                }
                frame=choice==Choice::Early?std::move(earlyFrame):std::move(presentFrame);
            }
            if(frame.externalVkFormat&&!ConvertCharacterVkPixels(frame)){++r.dropped;continue;}
            if(!ledger.Begin(frame.key,frame.width,frame.height))throw std::runtime_error("request ledger busy");
            lastSubmittedSequence=frame.key.sequence;lastSubmittedNs=frame.key.captureNs;
            r.awaitingReplacement=true;
            const auto inferenceStarted=Now();const auto hash=PixelHash(frame.pixels);history.Add(frame,hash);Send(pipe.value,MakeTrackingRequest(frame,nonce,frame.boxHoldMs));
            if(!Transfer(pipe.value,frame.pixels.data(),static_cast<DWORD>(frame.pixels.size()),true))throw std::runtime_error("image transfer failed or timed out");
            const auto reply=Receive(pipe.value);Snapshot snapshot;TrackingStats motion;
            if(!DecodeTrackingReply(reply,frame,nonce,info.dwProcessId,hash,history,snapshot,motion)||!ledger.Accept(frame.key,frame.width,frame.height))throw std::runtime_error("provider error or invalid tracking reply");
            r.awaitingReplacement=false;
            const auto arrived=Now();++r.processed;if(!snapshot.count)++r.noPose;
            r.inferenceMs=static_cast<unsigned>((arrived-inferenceStarted)/1'000'000);
            r.replyAgeMs=static_cast<unsigned>((arrived-frame.key.captureNs)/1'000'000);
            std::lock_guard lock(r.mutex);
            if(r.requested&&r.enabled&&r.qualified&&frame.key.epoch==r.current&&frame.key.epoch.scene==r.scene&&
                (!frame.earlySource||(CharacterEarlyCaptureEnabled&&!r.fgActive&&frame.key.captureNs>=r.earlyAmbiguousUntilNs.load()))){
                r.motion=motion;r.stages.candidates=motion.candidates;r.stages.returned=motion.returned;r.stages.detectorMs=motion.detectorMs;r.stages.poseMs=motion.poseMs;
                if(r.boxes.Update(snapshot,arrived,r.settings.boxHoldMs,r.settings.smartBoxHandoff,true)){r.snapshot=snapshot;r.hasSnapshot=true;++r.results;r.reason=snapshot.count?"Motion-tracked live detections":"Live tracking: no confirmed regions";}
                else {++r.expired;r.reason="Live sample arrived too late; try reducing Maximum persons.";}
            }else ++r.epochRejected;
        }
    }catch(const std::exception& e){fault=r.requested.load();SetReason(e.what());if(fault)LOG_ERROR("CharacterInspector worker fault: {}",e.what());}
    if(security)LocalFree(security);
    r.qualified=false;r.requested=false;
    if(process.value&&WaitForSingleObject(process.value,0)!=WAIT_OBJECT_0){
        TerminateProcess(process.value,0);
        // A timeout is not proof of process exit. This asynchronous owner keeps
        // its module and verified file leases until the child actually retires.
        while(WaitForSingleObject(process.value,1000)!=WAIT_OBJECT_0){r.worker=WorkerState::Stopping;SetReason("Waiting for the Inspector worker to exit; its files remain in use.");}
    }
    // Completed references retire; unresolved GPU submissions remain pinned in
    // the three-slot owner. No unknown-completion resource is freed on timeout.
    for(unsigned i=0;i<100;++i){r.capture.PollRetirement();
#ifndef CHARACTER_RUNTIME_TEST
        r.earlyCapture.RetireSources(0);
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    {std::lock_guard lock(r.mutex);r.hasSnapshot=false;}
    r.worker=fault?WorkerState::Fault:WorkerState::Stopped;r.alive=false;
    return 0;
}
DWORD WINAPI Control(void* argument){
    std::unique_ptr<Launch> launch(static_cast<Launch*>(argument));
    const auto result=ControlBody(launch.get());const auto lease=launch->lease;launch.reset();
    if(lease)FreeLibraryAndExitThread(lease,result);return result;
}
}
void StartCharacterWorker(const std::filesystem::path& root){
    auto& r=Get();bool expected=false;if(!r.alive.compare_exchange_strong(expected,true))return;
    auto launch=std::make_unique<Launch>();launch->root=root;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&StartCharacterWorker),&launch->lease)){
        r.alive=false;r.worker=WorkerState::Fault;SetReason("Worker module lease unavailable");return;
    }
    r.requested=true;r.worker=WorkerState::Starting;SetReason("Starting local CPU provider...");
    r.captures=0;r.dropped=0;r.results=0;r.processed=0;r.noPose=0;r.expired=0;r.epochRejected=0;r.sceneResets=0;r.inferenceMs=0;r.replyAgeMs=0;r.dxgiDefaultColor=false;r.awaitingReplacement=false;
    r.lastEarlyPublishedNs=0;r.lastQualifiedPresentNs=0;r.earlyCaptures=0;r.sourceSwitches=0;
    r.fgActive=false;r.physicalOutputQualified=false;r.fgCaptures=0;r.applicationOwner=0;r.realDisplay.Clear();
    r.admittedEarlyFeature=0;r.lastAdmittedEarlyNs=0;r.lastSeenBoundEarlyNs=0;r.earlyAmbiguousUntilNs=0;
    {std::lock_guard lock(r.mutex);r.sourcePolicy={};r.presentWidth=0;r.presentHeight=0;r.sourceApi=0;r.externalDeviceIdentity=0;r.hasExternalFrame=false;r.externalFrame={};
#ifndef CHARACTER_RUNTIME_TEST
        r.presentDevice.Reset();
#endif
    }
    Handle thread(CreateThread(nullptr,0,Control,launch.get(),0,nullptr));
    if(!thread.value){r.requested=false;r.alive=false;r.worker=WorkerState::Fault;SetReason("Worker control thread unavailable");if(launch->lease)FreeLibrary(launch->lease);return;}
    launch.release();
}
void StartCharacterWorkerAtDefaultLocation(){
#ifndef CHARACTER_RUNTIME_TEST
    HMODULE module=nullptr;wchar_t path[32768]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&StartCharacterWorkerAtDefaultLocation),&module))return;
    const auto count=GetModuleFileNameW(module,path,32768);if(!count||count>=32768)return;
    StartCharacterWorker(CharacterInspectorRootForModule(std::filesystem::path(path)));
#endif
}
void StopCharacterWorker() noexcept {auto& r=Get();r.requested=false;r.qualified=false;r.enabled=false;r.lastQualifiedPresentNs=0;r.lastEarlyPublishedNs=0;++r.generation;if(r.alive)r.worker=WorkerState::Stopping;}
bool CharacterWorkerRequested() noexcept{return Get().requested.load();}
RuntimeView TryCharacterRuntimeView(){auto& r=Get();RuntimeView v;v.worker=r.worker;v.sourceQualified=r.qualified;v.captures=r.captures;v.dropped=r.dropped;v.results=r.results;
    v.dxgiDefaultColor=r.dxgiDefaultColor;v.processed=r.processed;v.noPose=r.noPose;v.expired=r.expired;v.epochRejected=r.epochRejected;v.sceneResets=r.sceneResets;v.inferenceMs=r.inferenceMs;v.replyAgeMs=r.replyAgeMs;
    v.earlyCaptures=r.earlyCaptures;v.sourceSwitches=r.sourceSwitches;
    v.fgActive=r.fgActive;v.fgCaptures=r.fgCaptures;v.realFrameSource=r.applicationOwner!=0;
    v.sourceApi=r.sourceApi;
    std::unique_lock lock(r.mutex,std::try_to_lock);if(lock){v.earlySource=r.sourcePolicy.hasSource&&r.sourcePolicy.early;v.reason=r.reason;v.maximumPersons=r.settings.maximumPersons;v.lastPersons=static_cast<unsigned>(r.snapshot.count);v.boxHoldMs=r.settings.boxHoldMs;v.returnedDetections=r.stages.returned;v.detectorCandidates=r.stages.candidates;v.poseAttempts=r.stages.poseAttempts;v.usablePoses=r.stages.usablePoses;v.detectorMs=r.stages.detectorMs;v.poseMs=r.stages.poseMs;v.trackingMs=r.motion.trackingMs;v.detectionUpdates=r.motion.updates;v.poseUpdates=r.motion.poseUpdates;v.poseFailures=r.motion.poseFailures;}return v;}
void CharacterSourceInvalidated() noexcept {auto& r=Get();r.qualified=false;r.lastQualifiedPresentNs=0;r.lastEarlyPublishedNs=0;r.lastAdmittedEarlyNs=0;r.lastSeenBoundEarlyNs=0;r.earlyAmbiguousUntilNs=0;++r.generation;++r.scene;
#ifndef CHARACTER_RUNTIME_TEST
    try{r.earlyCapture.InvalidateWorkAdmission();}catch(...){}
#endif
}
void CharacterNativeFgStarted() noexcept {
    // Provider evaluation does not own our physical-output buffers. Preserve
    // Present epochs and snapshots; only the parked early-input path is revoked.
#ifndef CHARACTER_RUNTIME_TEST
    try{Get().earlyCapture.InvalidateWorkAdmission();}catch(...){}
#endif
}
#ifdef CHARACTER_RUNTIME_TEST
static void(*admissionTestHook)()=nullptr;
void SetCharacterAdmissionTestHook(void(*hook)()) noexcept {admissionTestHook=hook;}
#endif
void CharacterSwapchainReleased(IDXGISwapChain* chain,IUnknown* boundaryDevice) noexcept {try{
    auto& r=Get();
    const auto hdr=DlssNr::HdrObservation::Registry::Instance().Read(chain);
    Microsoft::WRL::ComPtr<ID3D11Device> device11;
    if(boundaryDevice)boundaryDevice->QueryInterface(IID_PPV_ARGS(&device11));
    std::lock_guard lock(r.mutex);
    const bool activeChain=hdr.registered&&r.current.swapchain==hdr.identityGeneration&&r.sourceApi!=2;
    const bool activeDx11Device=device11&&r.sourceApi==1&&
        r.externalDeviceIdentity==reinterpret_cast<std::uintptr_t>(device11.Get());
    // An auxiliary chain on the same device does not own the active stream.
    if(activeDx11Device&&!activeChain)return;
    // Capture can still retain an old DX11 device after the active API changed.
    // This only drops CPU references; D3D11 owns queued-command lifetimes.
    if(device11)r.capture11.ReleaseDevice(device11.Get());
    if(activeChain) {
        CharacterSourceInvalidated();r.hasExternalFrame=false;r.externalFrame={};
        r.applicationOwner=0;r.realDisplay.Clear();r.realClock={};
    }
}catch(...){} }
bool CharacterBeforeResize(IUnknown* boundaryDevice) noexcept {CharacterSourceInvalidated();try{
    auto& r=Get();r.capture.InvalidateAdmission();r.capture11.InvalidateAdmission();
#ifndef CHARACTER_RUNTIME_TEST
    r.earlyCapture.InvalidateAdmission();
#endif
    const auto started=Now();const bool presentRetired=r.capture.RetireSources(500);
    Microsoft::WRL::ComPtr<ID3D11Device> device11;
    const bool dx11Boundary=boundaryDevice&&SUCCEEDED(boundaryDevice->QueryInterface(IID_PPV_ARGS(&device11)));
    const auto elapsed11=static_cast<unsigned>((Now()-started)/1'000'000);
    const bool dx11Retired=!dx11Boundary||r.capture11.RetireSources(device11.Get(),elapsed11>=500?0:500-elapsed11);
#ifndef CHARACTER_RUNTIME_TEST
    const auto elapsed=static_cast<unsigned>((Now()-started)/1'000'000);
    const bool earlyRetired=r.earlyCapture.RetireSources(elapsed>=500?0:500-elapsed);
    return presentRetired&&dx11Retired&&earlyRetired;
#else
    return presentRetired&&dx11Retired;
#endif
}catch(...){return false;}}
void CharacterPresent(IDXGISwapChain3* chain,ID3D12CommandQueue* queue,bool fg,bool focused,const InspectorSettings& settings,bool physicalOutputQualified,std::uintptr_t applicationOwner,bool applicationReal,std::uint64_t frameToken) noexcept {
    auto& r=Get();r.enabled=settings.enabled;
    if(!settings.enabled){if(r.requested)StopCharacterWorker();return;}if(!r.requested||r.worker!=WorkerState::Ready)return;
    try {
        // A registered application owner supplies pre-generation images. Its
        // physical outputs only draw the published overlay, never feed inference.
        if((applicationOwner||r.applicationOwner)&&!applicationReal){
            if(!focused||!physicalOutputQualified||r.applicationOwner!=applicationOwner){
                if(r.qualified)CharacterSourceInvalidated();
            }
            return;
        }
        const auto admissionToken=r.capture.AdmissionToken();
        auto sourceGeneration=r.generation.load();const auto sourceScene=r.scene.load();
        fg=fg||NativeFgWork().Read(CharacterActivityNow()).active;
#ifdef CHARACTER_RUNTIME_TEST
        if(admissionTestHook)admissionTestHook();
#endif
        const auto observed=DlssNr::HdrObservation::Registry::Instance().TryRead(chain);
        const auto hdr=observed.value_or(DlssNr::HdrObservation::Snapshot{});
        const auto color=CharacterSourceColor(hdr);
        DXGI_SWAP_CHAIN_DESC1 description{};
#ifndef CHARACTER_RUNTIME_TEST
        Microsoft::WRL::ComPtr<ID3D12Device> device;
#endif
        const bool valid=chain&&queue&&focused&&(!applicationReal||(applicationOwner&&observed.has_value()))&&(!fg||physicalOutputQualified)&&color.has_value()&&
            SUCCEEDED(chain->GetDesc1(&description))&&description.Width&&description.Height&&
#ifndef CHARACTER_RUNTIME_TEST
            SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device)))&&device&&
#endif
            (settings.provider=="auto"||settings.provider=="opencv-zoo-cpu");
        if(!valid){r.lastQualifiedPresentNs=0;r.lastEarlyPublishedNs=0;if(r.qualified.exchange(false))CharacterSourceInvalidated();
            r.dxgiDefaultColor=false;
            std::unique_lock lock(r.mutex,std::try_to_lock);if(lock){r.hasSnapshot=false;r.reason=!focused?"Live inspection paused: game is not focused":fg&&!physicalOutputQualified?"Live inspection needs the physical D3D12 output queue during FG/MFG":"Live inspection needs a supported D3D12 color surface";}return;}
        r.dxgiDefaultColor=color->dxgiDefault;
        CpuFrame frame;frame.captureGeneration=admissionToken;frame.colorPolicy=color->conversion;const auto now=Now();
        {std::unique_lock lock(r.mutex,std::try_to_lock);if(!lock){++r.dropped;return;}
            if(sourceGeneration!=r.generation||sourceScene!=r.scene||admissionToken!=r.capture.AdmissionToken()){r.qualified=false;++r.dropped;return;}
            if(r.applicationOwner!=applicationOwner||r.sourceApi!=12||r.current.swapchain!=hdr.identityGeneration||r.current.descriptor!=hdr.resizeGeneration+1||r.settings.enabled!=settings.enabled||r.settings.provider!=settings.provider||r.settings.maximumPersons!=settings.maximumPersons||r.settings.boxHoldMs!=settings.boxHoldMs||r.settings.smartBoxHandoff!=settings.smartBoxHandoff||
               r.settings.torsoEstimate!=settings.torsoEstimate||r.settings.detectObjects!=settings.detectObjects||r.settings.updateIntervalMs!=settings.updateIntervalMs||
               r.presentWidth!=description.Width||r.presentHeight!=description.Height
#ifndef CHARACTER_RUNTIME_TEST
               ||(r.presentDevice&&!DlssNr::NativeIdentity::CompareDevices(r.presentDevice.Get(),device.Get()).equal)
#endif
               ){
                auto expected=sourceGeneration;
                if(!r.generation.compare_exchange_strong(expected,sourceGeneration+1)){r.qualified=false;++r.dropped;return;}
                ++sourceGeneration;r.hasSnapshot=false;r.boxes.Clear();r.lastEarlyPublishedNs=0;r.sourcePolicy={};
            }r.settings=settings;
            r.presentWidth=description.Width;r.presentHeight=description.Height;
            r.sourceApi=12;
            r.displayColorPolicy=color->conversion;
            r.fgActive=fg;r.physicalOutputQualified=physicalOutputQualified;
#ifndef CHARACTER_RUNTIME_TEST
            r.presentDevice=device;
#endif
            r.current.device=sourceGeneration;r.current.swapchain=hdr.identityGeneration;r.current.descriptor=hdr.resizeGeneration+1;r.current.scene=sourceScene;r.current.config=sourceGeneration;
            frame.key={r.current,++r.sequence,now};frame.maximumPersons=settings.maximumPersons;frame.poseRequested=settings.torsoEstimate;frame.detectObjects=settings.detectObjects;frame.boxHoldMs=settings.smartBoxHandoff?settings.boxHoldMs:0;r.qualified=true;
            if(sourceGeneration!=r.generation||sourceScene!=r.scene){r.qualified=false;r.lastQualifiedPresentNs=0;++r.dropped;return;}
            r.lastQualifiedPresentNs=now;
            r.applicationOwner=applicationOwner;
            if(applicationReal){
                if(!r.realClock.Advance(r.current,applicationOwner,frameToken,now))return;
                DisplayContext display{r.current,r.sequence,now,true,fg,physicalOutputQualified,r.displayColorPolicy,r.realClock.intervalNs};
                r.realDisplay.Publish(r.boxes,display,settings.boxHoldMs,settings.maximumPersons,settings.smartBoxHandoff,r.awaitingReplacement);
            }else r.realDisplay.Clear();}
        // Tracking needs intervening images even if an older INI requested a
        // detector-era 100-200ms polling interval. Model jobs stay single-flight.
        const auto earlyNs=r.lastEarlyPublishedNs.load();
        if(earlyNs&&now>=earlyNs&&now-earlyNs<=DisplayTtlNs)return;
        if(!applicationReal&&!r.timer.Due(true,now,std::min(settings.updateIntervalMs,50u)))return;
        if(r.capture.TrySubmitSwapchain(queue,chain,std::move(frame))){++r.captures;if(fg)++r.fgCaptures;}else ++r.dropped;
    }catch(...){r.qualified=false;r.lastQualifiedPresentNs=0;r.lastEarlyPublishedNs=0;++r.dropped;}
}
void CharacterPresentDx11(IDXGISwapChain* chain,ID3D11Device* device,bool fg,bool focused,const InspectorSettings& settings,bool physicalOutputQualified) noexcept {
    auto& r=Get();
    try {
        // Only this serialized rendering boundary polls the D3D11 immediate
        // context. The worker prepares private resources and moves CPU data.
        r.capture11.PollRetirement(device);
        const auto token=r.capture11.AdmissionToken();
        const auto observed=DlssNr::HdrObservation::Registry::Instance().TryRead(chain);
        const auto hdr=observed.value_or(DlssNr::HdrObservation::Snapshot{});
        const auto color=CharacterSourceColor(hdr);DXGI_SWAP_CHAIN_DESC desc{};
        const bool valid=chain&&device&&physicalOutputQualified&&color.has_value()&&SUCCEEDED(chain->GetDesc(&desc))&&
            !(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED);
        ExternalCharacterSource source;source.api=1;source.width=desc.BufferDesc.Width;source.height=desc.BufferDesc.Height;
        source.deviceIdentity=reinterpret_cast<std::uintptr_t>(device);source.swapchainGeneration=hdr.identityGeneration;source.resizeGeneration=hdr.resizeGeneration;
        source.colorPolicy=color?color->conversion:0;source.dxgiDefaultColor=color&&color->dxgiDefault;
        source.fgActive=fg;source.focused=focused;source.qualified=valid;
        if(auto frame=CharacterBeginExternalFrame(source,settings)){
            frame->captureGeneration=token;
            if(!r.capture11.TrySubmitSwapchain(device,chain,std::move(*frame)))++r.dropped;
        }
    }catch(...){CharacterSourceInvalidated();}
}
std::optional<CpuFrame> CharacterBeginExternalFrame(const ExternalCharacterSource& source,const InspectorSettings& settings) noexcept {
    auto& r=Get();r.enabled=settings.enabled;
    if(!settings.enabled){if(r.requested)StopCharacterWorker();return {};}
    if(!r.requested||r.worker!=WorkerState::Ready)return {};
    try {
        auto generation=r.generation.load();const auto scene=r.scene.load();
        if(!source.qualified||!source.focused||!source.deviceIdentity||!source.swapchainGeneration||
            (source.api!=1&&source.api!=2)||!source.width||!source.height||source.width>32768||source.height>32768||source.colorPolicy>2||
            (settings.provider!="auto"&&settings.provider!="opencv-zoo-cpu")){
            if(r.qualified.exchange(false))CharacterSourceInvalidated();
            std::unique_lock lock(r.mutex,std::try_to_lock);if(lock){r.hasSnapshot=false;r.hasExternalFrame=false;
                r.reason=source.focused?"Live inspection needs a supported physical output surface":"Live inspection paused: game is not focused";}
            return {};
        }
        const auto now=Now();std::unique_lock lock(r.mutex,std::try_to_lock);if(!lock){++r.dropped;return {};}
        if(generation!=r.generation||scene!=r.scene)return {};
        if(r.sourceApi!=source.api||r.externalDeviceIdentity!=source.deviceIdentity||r.externalColorPolicy!=source.colorPolicy||
            r.current.swapchain!=source.swapchainGeneration||r.current.descriptor!=source.resizeGeneration+1||r.displayColorPolicy!=(source.srgbAttachment?4u:source.colorPolicy)||
            r.settings.enabled!=settings.enabled||r.settings.provider!=settings.provider||r.settings.maximumPersons!=settings.maximumPersons||
            r.settings.boxHoldMs!=settings.boxHoldMs||r.settings.smartBoxHandoff!=settings.smartBoxHandoff||r.settings.torsoEstimate!=settings.torsoEstimate||
            r.settings.detectObjects!=settings.detectObjects||r.settings.updateIntervalMs!=settings.updateIntervalMs||
            r.presentWidth!=source.width||r.presentHeight!=source.height){
            auto expected=generation;if(!r.generation.compare_exchange_strong(expected,generation+1))return {};
            ++generation;r.hasSnapshot=false;r.hasExternalFrame=false;r.boxes.Clear();r.sourcePolicy={};r.lastEarlyPublishedNs=0;
        }
        r.applicationOwner=0;r.realDisplay.Clear();
        r.settings=settings;r.sourceApi=source.api;r.externalDeviceIdentity=source.deviceIdentity;r.externalColorPolicy=source.colorPolicy;
        r.displayColorPolicy=source.srgbAttachment?4:source.colorPolicy;
        r.presentWidth=source.width;r.presentHeight=source.height;r.fgActive=source.fgActive;r.physicalOutputQualified=true;
        r.dxgiDefaultColor=source.dxgiDefaultColor;
        r.current.device=generation;r.current.swapchain=source.swapchainGeneration;r.current.descriptor=source.resizeGeneration+1;
        r.current.scene=scene;r.current.config=generation;r.qualified=true;r.lastQualifiedPresentNs=now;
        if(generation!=r.generation||scene!=r.scene){r.qualified=false;return {};}
        if(!r.timer.Due(true,now,std::min(settings.updateIntervalMs,50u)))return {};
        CpuFrame frame;frame.key={r.current,++r.sequence,now};frame.colorPolicy=source.colorPolicy;
        frame.maximumPersons=settings.maximumPersons;frame.poseRequested=settings.torsoEstimate;frame.detectObjects=settings.detectObjects;
        frame.boxHoldMs=settings.smartBoxHandoff?settings.boxHoldMs:0;
        return frame;
    }catch(...){CharacterSourceInvalidated();return {};}
}
bool CharacterPublishExternalFrame(CpuFrame&& frame) noexcept {
    auto& r=Get();
    try {
        const auto now=Now();std::unique_lock lock(r.mutex,std::try_to_lock);
        if(!lock||!r.requested||!r.enabled||!r.qualified||r.worker!=WorkerState::Ready||
            (r.sourceApi!=1&&r.sourceApi!=2)||frame.colorPolicy!=r.externalColorPolicy||(frame.externalVkFormat&&r.sourceApi!=2)||
            frame.earlySource||frame.key.epoch!=r.current||r.current.scene!=r.scene||r.current.config!=r.generation||
            !frame.key.sequence||frame.key.sequence>r.sequence||frame.key.captureNs>now||now-frame.key.captureNs>DisplayTtlNs||
            !frame.width||!frame.height||frame.width>960||frame.height>960||
            (frame.externalVkFormat?!ValidCharacterVkPixels(frame):(frame.stride!=frame.width*4||
            frame.pixels.size()!=static_cast<std::size_t>(frame.stride)*frame.height))){++r.dropped;return false;}
        if(r.hasExternalFrame&&r.externalFrame.key.captureNs>=frame.key.captureNs){++r.dropped;return false;}
        r.externalFrame=std::move(frame);r.hasExternalFrame=true;++r.captures;if(r.fgActive)++r.fgCaptures;return true;
    }catch(...){++r.dropped;return false;}
}
bool CharacterBeforeUpscale(ID3D12GraphicsCommandList* list,ID3D12Resource* source,
    unsigned renderWidth,unsigned renderHeight,unsigned outputWidth,unsigned outputHeight,
    bool sceneLinear,float preExposure,std::uint64_t featureGeneration) noexcept {
#ifdef CHARACTER_RUNTIME_TEST
    (void)list;(void)source;(void)renderWidth;(void)renderHeight;(void)outputWidth;(void)outputHeight;
    (void)sceneLinear;(void)preExposure;(void)featureGeneration;return true;
#else
    auto& r=Get();
    if(r.earlyCapture.RestorationFailed())return false;
    if(!CharacterEarlyCaptureEnabled||!r.requested||!r.enabled||!r.qualified||r.fgActive||r.worker!=WorkerState::Ready)return true;
    try {
        const auto token=r.earlyCapture.AdmissionToken();
        const auto generation=r.generation.load(),scene=r.scene.load(),now=Now();
        const auto presentNs=r.lastQualifiedPresentNs.load();
        if(!list||!source||!renderWidth||!renderHeight||!outputWidth||!outputHeight||
            !presentNs||now<presentNs||now-presentNs>DisplayTtlNs||
            NativeFgWork().Read(CharacterActivityNow()).active||!std::isfinite(preExposure)||preExposure<=0)return true;
        // A differently shaped or differently sized NGX output is not evidence
        // that its input covers the current display surface.
        const auto inputAspect=static_cast<double>(renderWidth)/renderHeight;
        const auto outputAspect=static_cast<double>(outputWidth)/outputHeight;
        if(std::abs(inputAspect/outputAspect-1.)>.01)return true;
        Microsoft::WRL::ComPtr<ID3D12Device> device,sourceDevice;
        if(FAILED(list->GetDevice(IID_PPV_ARGS(&device)))||FAILED(source->GetDevice(IID_PPV_ARGS(&sourceDevice)))||
            !DlssNr::NativeIdentity::CompareDevices(device.Get(),sourceDevice.Get()).equal)return true;
        CpuFrame frame;
        {std::unique_lock lock(r.mutex,std::try_to_lock);if(!lock){++r.dropped;return true;}
            if(!r.requested||!r.enabled||!r.qualified||generation!=r.generation||scene!=r.scene||
                r.current.config!=generation||r.current.scene!=scene||token!=r.earlyCapture.AdmissionToken()||
                r.presentWidth!=outputWidth||r.presentHeight!=outputHeight||
                r.lastQualifiedPresentNs.load()!=presentNs||
                !DlssNr::NativeIdentity::CompareDevices(r.presentDevice.Get(),device.Get()).equal)return true;
            const auto admittedNs=r.lastAdmittedEarlyNs.load();
            const auto ambiguousUntil=r.earlyAmbiguousUntilNs.load();
            const auto boundFeature=r.admittedEarlyFeature.load();
            // Observe the bound stream even while captures are suppressed.
            // A surviving competitor alone cannot extend ambiguity forever.
            if(admittedNs&&boundFeature==featureGeneration)r.lastSeenBoundEarlyNs=now;
            if(Detail::EarlyFeatureCompetes(now,r.lastSeenBoundEarlyNs.load(),boundFeature,featureGeneration)){
                r.earlyAmbiguousUntilNs=now+DisplayTtlNs;r.lastEarlyPublishedNs=0;r.sourcePolicy.lastEarlyNs=0;
                if(now>=ambiguousUntil){r.hasSnapshot=false;r.boxes.Clear();}
                return true;
            }
            if(now<ambiguousUntil)return true;
            if(!r.earlyTimer.Due(true,now,std::min(r.settings.updateIntervalMs,50u)))return true;
            frame.key={r.current,++r.sequence,now};frame.captureGeneration=token;
            frame.maximumPersons=r.settings.maximumPersons;frame.poseRequested=r.settings.torsoEstimate;
            frame.detectObjects=r.settings.detectObjects;frame.boxHoldMs=r.settings.smartBoxHandoff?r.settings.boxHoldMs:0;
            frame.colorPolicy=sceneLinear?3u:0u;frame.preExposure=preExposure;frame.earlySource=true;
            frame.earlyFeatureGeneration=featureGeneration;
        }
        if(r.earlyCapture.TryRecord(list,source,std::move(frame))){
            r.admittedEarlyFeature=featureGeneration;r.lastAdmittedEarlyNs=now;r.lastSeenBoundEarlyNs=now;++r.captures;++r.earlyCaptures;
        }
        else ++r.dropped;
    }catch(...){++r.dropped;}
    return !r.earlyCapture.RestorationFailed();
#endif
}
bool TryCharacterDisplay(Snapshot& snapshot,DisplayContext& display) noexcept {
    auto& r=Get();if(!r.requested||!r.enabled||!r.qualified)return false;
    std::unique_lock lock(r.mutex,std::try_to_lock);if(!lock||!r.hasSnapshot||r.current.scene!=r.scene||r.current.config!=r.generation)return false;
    display={r.current,r.sequence,Now(),true,r.fgActive.load(),r.physicalOutputQualified.load(),r.displayColorPolicy};if(Admit(r.snapshot,display)!=Admission::Ready)return false;snapshot=r.snapshot;return true;
}
bool TryCharacterHeldDisplay(HeldSnapshot& snapshot,DisplayContext& display) noexcept {
    auto& r=Get();if(!r.requested||!r.enabled||!r.qualified)return false;
    if(r.applicationOwner){
        auto frozen=r.realDisplay.Read(r.generation,r.scene,Now());if(!frozen)return false;
        snapshot=frozen->held;display=frozen->display;
        return r.requested&&r.enabled&&r.qualified&&display.epoch.config==r.generation&&display.epoch.scene==r.scene;
    }
    std::unique_lock lock(r.mutex,std::try_to_lock);if(!lock||r.current.scene!=r.scene||r.current.config!=r.generation)return false;
    display={r.current,r.sequence,Now(),true,r.fgActive.load(),r.physicalOutputQualified.load(),r.displayColorPolicy};return r.boxes.Read(display,r.settings.boxHoldMs,snapshot,r.settings.maximumPersons,r.settings.smartBoxHandoff,r.awaitingReplacement);
}
}
