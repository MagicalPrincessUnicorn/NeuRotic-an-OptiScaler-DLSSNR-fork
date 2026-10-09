#include "../pch.h"
#include "RenderingOutput.h"
#include "RenderingOutputProjection.h"
#include "RenderingOutputStartupTrace.h"
#include "FinalFallbackControl.h"
#include "DlssNr_Present.h"
#include "DlssNr_PresentResolution.h"
#include "DlssNrFeature_Dx12.h"
#include "DlssNrFeature_Vk.h"
#include "NativeD3D11Guides.h"
#include "NativeD3D12Guides.h"
#include "NativeVulkanGuides.h"
#include "NativeGuideRoute.h"
#include "NativeFgVulkan.h"
#include "connections/InputSelectionPump.h"
#include "PreparedGuideStatusV2.h"
#include "NrGpuSafety.h"
#include "Config.h"
#include "Util.h"
#include "anything/AnythingController.h"
#include <atomic>
#include <mutex>
#include <memory>
#include <algorithm>

// Shared controller is compiled into the game DLL without the desktop library UI.
namespace nh {
std::wstring Wide(const std::string& value) {
    if(value.empty())return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    if(!count)return {};
    std::wstring out(count,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),count);return out;
}
std::string Utf8(const std::wstring& value) {
    if(value.empty())return {};
    const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(!count)return {};
    std::string out(count,0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),count,nullptr,nullptr);return out;
}
}
namespace DlssNr::RenderingOutput {
namespace {
using Json=nlohmann::json;
struct Shared {
    std::atomic<uintptr_t> window{0};
    std::atomic<uint64_t> heartbeat{0},frames{0};
    std::atomic<bool> menu{false},running{false},childExitUnproven{false},vulkan{false},supervisionFailed{false};
    std::atomic<DWORD> supervisorLaunchError{0};
    std::mutex mutex;
    Snapshot snapshot;
    StartupState startup;
    StartupRequestIdentity startupRequest;
    uint64_t startupSampledAt=0;
    bool startupFactsCurrent=false;
    std::optional<uint64_t> startupCaptured,startupNrCompleted,startupPresentAccepted;
};
// Intentionally process lifetime: a pinned supervisor never races static destruction.
Shared& Data(){static auto* data=new Shared;return *data;}
void Publish(Snapshot snapshot){auto& d=Data();std::lock_guard lock(d.mutex);d.snapshot=std::move(snapshot);}
bool SameProcessWindow(HWND window){DWORD pid=0;return window&&IsWindow(window)&&GetWindowThreadProcessId(window,&pid)&&pid==GetCurrentProcessId();}
bool LiveWindow(HWND window){return SameProcessWindow(window)&&IsWindowVisible(window)&&!IsIconic(window)&&GetAncestor(GetForegroundWindow(),GA_ROOT)==window;}
uint64_t Count(const Json& j,const char* key){auto it=j.find(key);return it!=j.end()&&it->is_number_unsigned()?it->get<uint64_t>():0;}
std::optional<uint64_t> ObservedCount(const Json& j,const char* key){
    const auto it=j.find(key);if(it!=j.end()&&it->is_number_unsigned())return it->get<uint64_t>();return {};
}
Json CountField(std::optional<uint64_t> count){return count?Json(*count):Json(nullptr);}
bool HardFailure(std::string reason){
    std::transform(reason.begin(),reason.end(),reason.begin(),[](unsigned char c){return char(std::tolower(c));});
    for(const char* token:{"device removed","device lost","completion unknown","quarantin","provider failed","model failed","model missing","model file","identity mismatch"})
        if(reason.find(token)!=std::string::npos)return true;
    return false;
}
Json Processing(const NrConfigSnapshot<Config>& config){
    const auto scale=std::clamp(config.DlssNrAnythingScale.value_or_default(),25u,100u);
    return {{"modelStyle",std::clamp(config.DlssNrStyle.value_or_default(),0u,2u)},
        {"transferStrength",std::clamp(config.DlssNrTransferStrength.value_or_default(),0.0f,2.0f)},
        {"colourStrength",std::clamp(config.DlssNrColourStrength.value_or_default(),0.0f,2.0f)},
        {"nrScalePercent",scale}};
}
bool YieldOutput(std::string& reason){
    if(FinalFallback::callbacks.load(std::memory_order_seq_cst)){reason="Waiting for in-game Present callbacks to finish";return false;}
    if(!GpuSafety::CanYieldOutput()){reason="Waiting for recorded GPU work to become reusable";return false;}
    return CanYieldPresentOutput(reason)&&NativeD3D11Guides::CanYieldOutput(reason)&&
        NativeD3D12Guides::CanYieldOutput(reason)&&NativeVulkanGuides::CanYieldOutput(reason)&&CanYieldVulkanOutput(reason);
}
bool InputPending(){const auto phase=Connections::QueryInputSelection().phase;
    return phase==Connections::InputPhase::Waiting||phase==Connections::InputPhase::Switching;}
bool AdvanceInputSession() noexcept {return Config::Instance()->RequestDlssNrSession();}
bool CanSwitchInput(std::string& reason){
    if(!GpuSafety::CanYieldOutput()){reason="Recorded GPU work is pending or quarantined";return false;}
    return CanYieldPresentOutput(reason)&&NativeD3D11Guides::CanYieldOutput(reason)&&
        NativeD3D12Guides::CanYieldOutput(reason)&&NativeVulkanGuides::CanSwitchInput(reason)&&
        NativeGuides::CanSwitchInput(reason)&&CanYieldVulkanOutput(reason);
}
Json StartupFields(const StartupState& s){
    return {{"context",s.context},{"route",s.route},{"requested",s.requested},{"stage",s.stage},
        {"recentCallback",s.recent},{"windowValid",s.windowValid},{"visible",s.visible},
        {"minimized",s.iconic},{"foreground",s.foreground},{"effectEnabled",s.effect},
        {"unsafe",s.unsafe},{"hardFailure",s.hardFailure},{"attempted",s.attempted},
        {"inGameOutputGated",s.gated},{"completedCallbacksReady",s.callbacksReady},
        {"controller",s.controller},{"connected",s.connected},{"busy",s.busy},{"ready",s.ready},
        {"workerActive",s.workerActive},{"workerPhase",s.workerPhase},{"modelRequestSent",s.modelSent},
        {"catalogRequestSent",s.catalogSent},{"startRequestSent",s.startSent},
        {"comparisonPaused",s.paused},{"childExitUnproven",s.childExitUnproven},
        {"ownershipWait",s.reason}};
}
void Run(){
    auto& data=Data();Health health;
    StartupTrace startupTrace;StartupState lastStartup;
    Connections::InputSelectionPump inputPump;
    std::unique_ptr<nh::AnythingController> controller;
    uint64_t context=0,sessionContext=0,phaseStart=0,gatedFrame=0,lastCompletion=0,lastProgress=0;
    bool attempted=false,modelSent=false,catalogSent=false,startSent=false;
    auto& childExitUnproven=data.childExitUnproven;
    std::string failure,failureStage;
    Json lastProcessing;
    float lastSplit=-1;
    StartupRequestIdentity sampledRequest;
    // Record observations only. No diagnostic call queries/retire-checks a producer.
    // Volatile totals are copied but excluded from the transition identity.
    auto recordStartup=[&](StartupState state,uint64_t now,std::optional<uint64_t> captured={},std::optional<uint64_t> nr={},std::optional<uint64_t> accepted={}){
        try{
            state.context=context;state.attempted=attempted;
            state.gated=FinalFallback::suppressInGame.load();state.controller=bool(controller);
            state.modelSent=modelSent;state.catalogSent=catalogSent;state.startSent=startSent;
            state.childExitUnproven=childExitUnproven.load();
            const auto callbacks=FinalFallback::completedCallbacks.load();
            state.callbacksReady=state.gated&&callbacks>=gatedFrame&&callbacks-gatedFrame>=2;
            const bool relevant=state.route==3||lastStartup.route==3;
            auto identity=state;
            // Routine status requests change busy after startup, without a new startup boundary.
            if(state.startSent&&state.workerPhase=="Running")identity.busy=false;
            const bool changed=startupTrace.Observe(identity);lastStartup=state;
            {std::lock_guard lock(data.mutex);data.startup=state;data.startupSampledAt=now;
                data.startupRequest=sampledRequest;
                data.startupFactsCurrent=state.stage!="config-snapshot-unavailable"&&state.stage!="input-switch";
                data.startupCaptured=captured;
                data.startupNrCompleted=nr;data.startupPresentAccepted=accepted;}
            if(changed&&relevant){auto receipt=StartupFields(state);
                receipt["api"]=data.vulkan.load()?"Vulkan":"DXGI";
                receipt["workerSnapshotTiming"]="before-this-iteration-commands";
                receipt["generation"]=sampledRequest.generation;receipt["sourceFrames"]=data.frames.load();
                receipt["completedCallbacks"]=callbacks;receipt["gatedCallbackBaseline"]=gatedFrame;
                receipt["callbacksInFlight"]=FinalFallback::callbacks.load();
                receipt["captured"]=CountField(captured);receipt["nrCompleted"]=CountField(nr);receipt["presentAccepted"]=CountField(accepted);
                LOG_INFO("NR Anything startup: {}",receipt.dump());}
        }catch(...){/* Diagnostic allocation/logging failure cannot change rendering admission. */}
    };
    auto stop=[&]{
        if(controller){
            if(!controller->ShutdownAndWait())childExitUnproven=true;
            controller.reset();
        }
        // Never resume a primary producer until the isolated child has actually exited.
        if(!childExitUnproven)FinalFallback::suppressInGame.store(false,std::memory_order_seq_cst);
        modelSent=catalogSent=startSent=false;lastCompletion=lastProgress=0;lastSplit=-1;
    };
    try {
        for(;;){
            const auto now=GetTickCount64(),beat=data.heartbeat.load(std::memory_order_acquire);
            if(FinalFallback::shutdownRequested.load()||now-beat>30000){
                auto ending=lastStartup;ending.stage=FinalFallback::shutdownRequested.load()?"shutdown-requested":"callback-expired";
                recordStartup(std::move(ending),now);break;}
            inputPump.Poll(now,[&](const Connections::InputSelectionSnapshot& selection){
                using Phase=Connections::InputPhase;Connections::InputRestriction result;
                if(controller||childExitUnproven||FinalFallback::suppressInGame.load())
                    return Connections::InputRestriction{Phase::Blocked,"Stop NR Anything before changing in-game input"};
                if((selection.requestedSource==0||selection.requestedSource==2)&&!NativeGuides::ObserveBuiltIn())
                    result.reason="Restart required: built-in capture was not prepared at startup";
                if(NativeFg::Selected())result.reason="Restart required: standalone FG owns the startup presentation path";
                return result;
            },CanSwitchInput,AdvanceInputSession);
            if(FinalFallback::inputSwitchPending.load()){
                Snapshot switching;{std::lock_guard lock(data.mutex);switching=data.snapshot;}switching.sampledAt=now;
                switching.active=switching.comparisonBypass=switching.fallbackEligible=false;
                switching.producer=Producer::None;
                switching.phase=Phase::Quiescing;switching.reason="Changing NR input";
                auto changing=lastStartup;changing.stage="input-switch";recordStartup(std::move(changing),now);
                Publish(std::move(switching));Sleep(25);continue;
            }
            auto captured=TryNrConfigSnapshot(*Config::Instance());
            if(!captured){auto waiting=lastStartup;waiting.stage="config-snapshot-unavailable";
                recordStartup(std::move(waiting),now);Sleep(100);continue;}
            const auto& config=*captured;
            const auto window=reinterpret_cast<HWND>(data.window.load());
            const bool requested=config.DlssNrEnabled.value_or_default();
            if(!requested){stop();Snapshot off;off.sampledAt=now;Publish(std::move(off));
                auto disabled=lastStartup;disabled.requested=false;disabled.stage="off";
                recordStartup(std::move(disabled),now);break;}
            const unsigned route=config.DlssNrRoute.value_or_default();
            const auto runtime=config.GetDlssNrRuntimeSnapshot();
            // Generation and target distinguish restarts. Internal admission is deliberately absent.
            const auto nextContext=runtime.resumeGeneration^(uint64_t(route)<<56)^(uint64_t(data.vulkan.load())<<55)^uint64_t(window);
            if(nextContext!=context||!requested){
                stop();context=nextContext;attempted=false;failure.clear();failureStage.clear();
                LOG_INFO("NR mode transition: route={} api={} requestedInput={} style={} captureScale={} enabled={}",
                    route,data.vulkan.load()?"Vulkan":"DXGI",config.DlssNrInputSource.value_or_default(),
                    config.DlssNrStyle.value_or_default(),config.DlssNrAnythingScale.value_or_default(),requested);
            }
            const bool recent=now>=beat&&now-beat<=5000;
            if(controller&&(!recent||!SameProcessWindow(window)||sessionContext!=context)){
                stop();failure="NR Anything stopped because its game window is unavailable";
                failureStage="game-window-unavailable";
            }
            const bool vulkan=data.vulkan.load();
            sampledRequest={runtime.resumeGeneration,uint64_t(window),route,requested,vulkan};
            const auto present=PresentTelemetryForApi(vulkan);
            const auto native=vulkan?NativeTelemetryVk():Telemetry();
            const auto prepared=PreparedGuides::QueryStatusV2(now);
            Input input;input.now=now;input.context=context;input.sourceFrames=data.frames.load();input.route=route;
            input.requested=requested;input.live=recent&&LiveWindow(window);
            input.effect=route==3||!(config.DlssNrMultipassEnabled.value_or_default()&&!config.DlssNrBasicMultipass.value_or_default().advanced&&BasicMultipass::Count(config.DlssNrBasicMultipass.value_or_default())==0);
            input.effect=input.effect&&config.DlssNrApplyModel.value_or_default()&&(config.DlssNrTransferStrength.value_or_default()>0||config.DlssNrColourStrength.value_or_default()>0);
            input.native={native.lifecycleGeneration,native.gpuCompletedOutputEvaluations,native.running&&!native.outputQuarantined&&!native.transitionPending&&(!native.runBeforeSr||native.preSrDisplayReady||native.nativeRayReconstructionActive)};
            input.present={present.resourceGeneration,present.acceptedOutputPresents,present.active};
            input.builtIn={prepared.status.generation,prepared.status.copybackCompletions,prepared.fresh&&prepared.status.outputValid!=0};
            input.unsafe=native.outputQuarantined||prepared.status.restartRequired!=0||present.vkUncertain!=0;
            input.hardFailure=HardFailure(present.failure)||(prepared.fresh&&HardFailure(prepared.status.reason));
            input.pending=present.pendingSlots!=0||(prepared.fresh&&prepared.status.modelPreparing!=0);
            const auto decision=health.Sample(input);
            StartupState startup;startup.route=route;startup.requested=requested;startup.stage="readiness";
            startup.recent=recent;startup.windowValid=SameProcessWindow(window);
            startup.visible=window&&IsWindowVisible(window);startup.iconic=window&&IsIconic(window);
            startup.foreground=window&&GetAncestor(GetForegroundWindow(),GA_ROOT)==window;
            startup.effect=input.effect;startup.unsafe=input.unsafe;startup.hardFailure=input.hardFailure;
            std::optional<uint64_t> workerCaptured,workerNr,workerAccepted;
            Snapshot view;view.sampledAt=now;view.sourceFrames=input.sourceFrames;view.requested=requested;view.route=route;
            view.producer=decision.producer;view.completed=decision.completed;view.active=decision.active;
            view.fallbackEligible=decision.fallbackEligible;
            view.phase=!requested?Phase::Off:decision.active?Phase::Active:Phase::Waiting;
            if(!decision.active){
                if(input.unsafe)view.reason="GPU completion is uncertain; restart the game before recovery";
                else if(input.hardFailure)view.reason="Recover the model or device before starting NR Anything";
                else if(!failure.empty())view.reason=failure;
                else if(!present.failure.empty())view.reason=present.failure;
                else if(!present.fallbackReason.empty())view.reason=present.fallbackReason;
                else if(prepared.fresh)view.reason=prepared.status.reason;
                if(input.unsafe||input.hardFailure||!failure.empty())view.phase=Phase::Blocked;
            }
            if(controller&&(!requested||route!=3||!input.effect||input.unsafe||input.hardFailure)){
                stop();failure="NR Anything stopped after the rendering request or safety state changed";
                failureStage="request-or-safety-changed";
            }
            if(!input.effect&&!controller){attempted=false;failure.clear();failureStage.clear();}
            if(decision.fallbackEligible&&!attempted&&!childExitUnproven){
                attempted=true;sessionContext=context;phaseStart=now;gatedFrame=FinalFallback::completedCallbacks.load();
                FinalFallback::suppressInGame.store(true,std::memory_order_seq_cst);
                failure.clear();failureStage.clear();
            }
            if(FinalFallback::suppressInGame.load()&&!controller&&!childExitUnproven){
                startup.stage=FinalFallback::completedCallbacks.load()-gatedFrame>=2?"output-ownership":"completed-callbacks";
                view.phase=Phase::Quiescing;view.active=false;view.producer=Producer::None;
                std::string reason="Waiting for in-game output ownership to settle";
                if(!input.live||!requested||route!=3||input.unsafe||input.hardFailure){
                    stop();failure="NR Anything could not start safely. Game output is unavailable.";
                    failureStage="handoff-unavailable";
                }else if(FinalFallback::completedCallbacks.load()-gatedFrame>=2&&YieldOutput(reason)){
                    const auto root=Util::DllPath().parent_path();
                    const auto worker=root/L"OptiScaler/NRAnything/NeuRotic.WindowWorker.exe";
                    if(!std::filesystem::is_regular_file(worker)){
                        startup.stage="worker-missing";
                        stop();failure="NR Anything is missing. Reinstall the complete NeuRotic package.";
                        failureStage="worker-missing";
                    }else{
                        controller=std::make_unique<nh::AnythingController>(worker,root,nh::AnythingController::SessionPolicy{false,false});
                        controller->Connect();phaseStart=now;view.phase=Phase::StartingFallback;
                        startup.stage="worker-connect";
                    }
                }else if(now-phaseStart>15000){
                    stop();failure="NR Anything could not obtain safe output ownership: "+reason;
                    failureStage="ownership-timeout";
                }
                view.reason=failure.empty()?reason:failure;
                // Ownership reasons come from fixed internal proof queries, never model paths.
                if(!controller)startup.reason=reason;
                if(!failure.empty())view.phase=Phase::Blocked;
            }
            if(controller){
                view.active=false;view.producer=Producer::CapturedImage;view.phase=Phase::StartingFallback;
                const auto worker=controller->Snapshot();
                startup.connected=worker.connected;startup.busy=worker.busy;startup.ready=worker.ready;
                startup.workerActive=worker.active;startup.workerPhase=worker.phase;
                workerCaptured=ObservedCount(worker.status,"captured");workerNr=ObservedCount(worker.status,"nrCompleted");
                workerAccepted=ObservedCount(worker.status,"presentAccepted");
                startup.stage=!worker.connected?"worker-connect":!modelSent?"model-selection":
                    !worker.ready?"model-readiness":!catalogSent?"capture-catalog":
                    !startSent?"capture-target":"start-acknowledgement";
                if(!worker.lastError.empty()||worker.status.value("restartRequired",false)){
                    startup.stage="worker-error";
                    failureStage="worker-error";
                    failure=worker.lastError.empty()?"Captured-image worker requires a restart":worker.lastError;stop();
                }else if(worker.connected&&!worker.busy&&!modelSent){
                    startup.stage="model-selection";
                    auto model=Util::FindFilePath(Util::DllPath().parent_path(),L"nvngx_dlssnr.dll");
                    if(!model)model=Util::FindFilePath(Util::ExePath().parent_path(),L"nvngx_dlssnr.dll");
                    if(!model||!controller->SelectModel(*model,false)){failure="NR Anything requires a verified DLSS NR model";failureStage="model-missing-or-rejected";stop();}
                    else modelSent=true;
                }else if(worker.ready&&!worker.busy&&!catalogSent){
                    startup.stage="capture-catalog";
                    catalogSent=controller->RefreshWindows();
                }else if(worker.ready&&!worker.busy&&catalogSent&&!startSent){
                    startup.stage="capture-target";
                    Json target;
                    for(const auto& candidate:worker.windows)
                        if(Count(candidate,"hwnd")==uint64_t(window)&&Count(candidate,"pid")==GetCurrentProcessId()){target=candidate;break;}
                    if(target.is_null()){failure="The game window is not available for Windows capture";failureStage="capture-target-missing";stop();}
                    else{
                        auto options=Processing(config);lastProcessing=options;
                        options["mode"]="selected";options["window"]=target;options["guides"]="off";
                        options["cursor"]=false;options["split"]=data.menu.load()?1.0f:0.0f;options["outputMode"]="overlay";
                        startSent=controller->Start(options);
                        if(!startSent){failure="NR Anything could not start with this game window";failureStage="start-rejected";stop();}
                    }
                }
                if(controller&&startSent){
                    const auto completed=(std::min)(Count(worker.status,"nrCompleted"),Count(worker.status,"presentAccepted"));
                    const bool paused=data.menu.load()||!input.live||worker.status.value("sourcePaused",false);
                    if(completed>lastCompletion){lastCompletion=completed;if(!paused)lastProgress=now;}
                    if(paused){lastProgress=0;phaseStart=now;}
                    view.comparisonBypass=paused;
                    startup.paused=paused;
                    view.completed=completed;view.active=worker.active&&!paused&&lastProgress&&now-lastProgress<=2000;
                    view.phase=view.active?Phase::FallbackActive:Phase::StartingFallback;
                    view.reason=worker.message;
                    if(paused){view.phase=Phase::FallbackPaused;view.reason="NR Anything is showing the original image while the menu is open or capture is paused";}
                    if(worker.phase!="Running")startup.stage="start-acknowledgement";
                    else if(paused)startup.stage="comparison-paused";
                    else if(view.active)startup.stage="active-output";
                    else startup.stage="awaiting-completed-output";
                    if(worker.active&&!worker.busy){
                        const auto processing=Processing(config);
                        if(processing!=lastProcessing&&controller->SetProcessing(processing))lastProcessing=processing;
                        const float split=data.menu.load()?1.0f:0.0f;
                        if(split!=lastSplit){controller->SetComparison(split);lastSplit=split;}
                    }
                }
                if(controller&&!view.active&&!view.comparisonBypass&&now-(lastProgress?lastProgress:phaseStart)>45000){
                    failure="NR Anything produced no fresh output. Toggle Neural Rendering to retry.";stop();
                    failureStage="output-timeout";
                }
                if(!controller){view.phase=Phase::Blocked;view.active=false;view.reason=failure;}
            }
            if(childExitUnproven){view.active=false;view.phase=Phase::Blocked;view.reason="Captured-image worker exit is unconfirmed; restart the game before rendering can resume";startup.stage="child-exit-unconfirmed";}
            if(!failure.empty()&&!controller&&startup.stage!="child-exit-unconfirmed")startup.stage=failureStage.empty()?"blocked":failureStage;
            recordStartup(std::move(startup),now,workerCaptured,workerNr,workerAccepted);
            Publish(std::move(view));
            Sleep(100);
        }
    }catch(const std::exception& e){data.supervisionFailed=true;Snapshot view;view.sampledAt=GetTickCount64();view.phase=Phase::Blocked;view.reason=e.what();Publish(std::move(view));
        auto fault=lastStartup;fault.stage="supervisor-failed";recordStartup(std::move(fault),GetTickCount64());}
    catch(...){data.supervisionFailed=true;Snapshot view;view.sampledAt=GetTickCount64();view.phase=Phase::Blocked;view.reason="Automatic output supervision stopped after an internal failure";Publish(std::move(view));
        auto fault=lastStartup;fault.stage="supervisor-failed";recordStartup(std::move(fault),GetTickCount64());}
    stop();
}
DWORD WINAPI Thread(void* parameter){
    const auto module=static_cast<HMODULE>(parameter);
    Run();Data().running.store(false,std::memory_order_release);
    FreeLibraryAndExitThread(module,0);
}
}
void Signal(uintptr_t window,bool menuOpen,bool vulkan) noexcept {
    try{
        if(FinalFallback::shutdownRequested.load())return;
        auto& d=Data();
        if(!Config::Instance()->DlssNrEnabled.value_or_default()){
            d.supervisionFailed=false;if(!InputPending())return;
        }
        const auto root=GetAncestor(reinterpret_cast<HWND>(window),GA_ROOT);
        if(!SameProcessWindow(root))return;
        d.window.store(reinterpret_cast<uintptr_t>(root));d.menu.store(menuOpen);d.vulkan.store(vulkan);
        d.heartbeat.store(GetTickCount64(),std::memory_order_release);d.frames.fetch_add(1);
        if(d.running.load()||d.supervisionFailed.load()||d.childExitUnproven.load())return;
        bool expected=false;if(!d.running.compare_exchange_strong(expected,true))return;
        HMODULE module=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Thread),&module)){d.supervisorLaunchError=GetLastError();d.running=false;return;}
        const auto thread=CreateThread(nullptr,1024*1024,&Thread,module,0,nullptr);
        if(thread){d.supervisorLaunchError=0;CloseHandle(thread);}else{d.supervisorLaunchError=GetLastError();d.running=false;FreeLibrary(module);}
    }catch(...){}
}
Snapshot Query(){
    // UI callers may already hold a settings transaction. Never acquire it
    // while holding the publication mutex (diagnostic/UI readers can overlap).
    const bool requested=Config::Instance()->DlssNrEnabled.value_or_default();
    const auto route=Config::Instance()->DlssNrRoute.value_or_default();
    auto& d=Data();std::lock_guard lock(d.mutex);
    return Project(d.snapshot,GetTickCount64(),requested,
        FinalFallback::suppressInGame.load(),d.childExitUnproven.load(),d.supervisionFailed.load(),route,d.supervisorLaunchError.load());
}
std::string StartupDiagnostics(){
    const auto config=TryNrConfigSnapshot(*Config::Instance());
    // Settings/runtime snapshots are captured before taking the publication lock.
    const bool requested=config&&config->DlssNrEnabled.value_or_default();
    const unsigned route=config?config->DlssNrRoute.value_or_default():2u;
    const uint64_t generation=config?config->GetDlssNrRuntimeSnapshot().resumeGeneration:0;
    auto& data=Data();const auto now=GetTickCount64(),beat=data.heartbeat.load();
    const StartupRequestIdentity currentRequest{generation,uint64_t(data.window.load()),route,requested,data.vulkan.load()};
    const bool running=data.running.load();const auto launchError=data.supervisorLaunchError.load();
    StartupObservation observation;observation.callbackSeen=beat!=0;observation.supervisorRunning=running;
    observation.requested=requested;observation.route=route;
    observation.supervisionFailed=data.supervisionFailed.load()||launchError!=0;
    observation.childExitUnproven=data.childExitUnproven.load();
    Json receipt={{"configurationAvailable",bool(config)},{"signalObserved",beat!=0},
        {"signalFrames",data.frames.load()},{"recentSignal",beat&&now>=beat&&now-beat<=5000},
        {"supervisorRunning",running},{"supervisorLaunchError",launchError},
        {"supervisionFailed",observation.supervisionFailed},{"childExitUnproven",observation.childExitUnproven}};
    {std::lock_guard lock(data.mutex);
        receipt["lastSample"]=data.startupSampledAt?StartupFields(data.startup):Json(nullptr);
        receipt["sampleAgeMs"]=data.startupSampledAt&&now>=data.startupSampledAt?Json(now-data.startupSampledAt):Json(nullptr);
        receipt["captured"]=CountField(data.startupCaptured);receipt["nrCompleted"]=CountField(data.startupNrCompleted);
        receipt["presentAccepted"]=CountField(data.startupPresentAccepted);
        receipt["sampleFactsCurrent"]=data.startupFactsCurrent;
        observation.sampleFresh=data.startupFactsCurrent&&data.startupSampledAt&&now>=data.startupSampledAt&&now-data.startupSampledAt<=2000;
        if(config){observation.sampleMatchesRequest=StartupRequestMatches(data.startupRequest,currentRequest);
            receipt["generation"]=generation;receipt["requested"]=requested;receipt["route"]=route;}
    }
    receipt["sampleFresh"]=observation.sampleFresh;receipt["sampleMatchesRequest"]=observation.sampleMatchesRequest;
    receipt["observation"]=config?StartupObservationStage(observation):"configuration-unavailable";
    return receipt.dump(2);
}
}
