#include "control/ControlCore.h"
#include "control/FrameTimings.h"
#include "control/DiagnosticTrace.h"
#include "control/PerformanceRecord.h"
#include "control/WorkerWake.h"
#include "control/ComparisonRefresh.h"
#include "diagnostics/FrameProbe.h"
#include "guidance/dav2/Dav2Host.h"
#include <roapi.h>
#include <iostream>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>
#include <algorithm>

namespace nrw {
#ifdef NRW_CONTROL_FIXTURE
HWND ControlFixtureForeground();
#endif
namespace {
std::atomic<bool> interrupted=false;
uint64_t Qpc(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return uint64_t(value.QuadPart);}
double QpcMs(uint64_t value){LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);return double(value)*1000./double(frequency.QuadPart);}
BOOL WINAPI ConsoleSignal(DWORD type) {if(type==CTRL_C_EVENT || type==CTRL_BREAK_EVENT || type==CTRL_CLOSE_EVENT){interrupted=true;return TRUE;}return FALSE;}
void Emit(Json event) {std::cout<<event.dump(-1,' ',false,Json::error_handler_t::replace)<<'\n';std::cout.flush();}
NrProcessingOptions ProcessingOptions(const Json& j){return {j.value("transferStrength",1.f),j.value("colourStrength",1.f),j.value("nrScalePercent",100u),j.value("modelStyle",0)};}
Json ProcessingJson(const NrProcessingOptions& p){return {{"transferStrength",p.transferStrength},{"colourStrength",p.colourStrength},{"nrScalePercent",p.nrScalePercent},{"modelStyle",p.modelStyle}};}
std::wstring ExeFolder() {std::vector<wchar_t> buffer(32768);auto n=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!n || n>=buffer.size())throw std::runtime_error("Worker executable path unavailable");return std::filesystem::path(std::wstring(buffer.data(),n)).parent_path().wstring();}
struct Inbox {
    struct State {std::mutex mutex;std::deque<std::string> lines;std::atomic<bool> stop=false,done=false,overflow=false;WakeEvent ready;};
    std::shared_ptr<State> state=std::make_shared<State>();std::thread reader;
    Inbox() :reader([s=state] {
        auto push=[&](std::string line){std::lock_guard lock(s->mutex);if(s->lines.size()<16)s->lines.push_back(std::move(line));else s->overflow=true;s->ready.Signal();};
        HANDLE input=GetStdHandle(STD_INPUT_HANDLE);std::string line;bool oversized=false;char buffer[1024];DWORD bytes=0;
        while(!s->stop && ReadFile(input,buffer,sizeof(buffer),&bytes,nullptr) && bytes) {
            for(DWORD i=0;i<bytes;++i) {
                if(buffer[i]=='\n') {
                    if(oversized)push("{\"command\":\"stream-error\",\"reason\":\"Command exceeds 65536 bytes\"}");
                    else {if(!line.empty() && line.back()=='\r')line.pop_back();if(!line.empty())push(std::move(line));}
                    line.clear();oversized=false;
                } else if(!oversized) {if(line.size()==MaxCommandBytes){line.clear();oversized=true;}else line+=buffer[i];}
            }
        }
        if(!line.empty() && !oversized)push(std::move(line));
        if(oversized)push("{\"command\":\"stream-error\",\"reason\":\"Command exceeds 65536 bytes\"}");
        s->done=true;s->ready.Signal();
    }) {}
    ~Inbox() {
        state->stop=true;
        for(int n=0;n<100 && !state->done;++n){CancelSynchronousIo(reader.native_handle());Sleep(1);}
        if(reader.joinable()){if(state->done)reader.join();else reader.detach();}
    }
    bool Pop(std::string& line) {std::lock_guard lock(state->mutex);if(state->lines.empty())return false;line=std::move(state->lines.front());state->lines.pop_front();return true;}
    bool Done() {std::lock_guard lock(state->mutex);return state->done && state->lines.empty();}
};
class Worker {
    FrameProbe measurement; // outlives backend owners, including quarantined tails
    SelectionState selection;ModelInfo model;CaptureHost capture;NrHost nr;GuidanceHost guidance;
    uint64_t measurementDeadline=0,targetGeneration=0;
    ComparisonRefresh refresh;uint64_t reusedPublications=0,sourceReevaluations=0;
    uint32_t hostPid=0;
    Json pending;std::string phase="Locked",reason="Please provide your DLSS NR file.";
    uint64_t applied=0,session=0,captured=0,recorded=0,submitted=0,completed=0,presented=0;
    uint64_t lastStatus=0,lastSequence=0;uint32_t sourceWidth=0,sourceHeight=0,workWidth=0,workHeight=0;
    bool guidesConsumed=false;depth::Settings depthSettings;std::string depthReason;uint64_t depthPresentations=0,depthFresh=0,depthDropped=0;
    bool active=false,quit=false;float split=0;int stripes=0;uint64_t snapshotDeadline=0;Json snapshot=Json::object();GuideMode guideMode=GuideMode::Off;
    GuideResult lastGuides;double modelMs=0;WindowIdentity target;
    FrameTimings timings;bool overlay=true;
    DiagnosticTrace trace;
    Json lastFrame=nullptr,comparison={{"revision",0},{"publicationsAtApply",0},{"completedAtApply",0}};
    CaptureStatistics lastCapture;
    uint64_t gpuCompleted=0,processingRevision=0;
    NrProcessingOptions processingOptions;
    Json pendingProcessing,processing={{"state","idle"},{"appliedRevision",0}};
    int comparisonDirection=0;
    std::string stopTrigger,lastCommand;
    bool gpuTiming=true;uint32_t nrScalePercent=100;
    OptionalMs lastTraceMs;
    uint64_t waitCalls=0,frameWakes=0,commandWakes=0,messageWakes=0,maintenanceWakes=0,previousPublicationQpc=0;
    double waitMilliseconds=0,waitSinceFrameMs=0;
    void FrameScheduling(uint64_t publicationQpc) {
        lastFrame["scheduling"]={{"wait_since_previous_frame_ms",waitSinceFrameMs},
            {"completion_interval_ms",previousPublicationQpc?Json(QpcMs(publicationQpc-previousPublicationQpc)):Json(nullptr)}};
        waitSinceFrameMs=0;previousPublicationQpc=publicationQpc;
    }
    void Trace(Json record) {if(!trace.Path().empty()){auto begin=MeasureClock::now();trace.Push(record.dump(-1,' ',false,Json::error_handler_t::replace));lastTraceMs=ElapsedMs(begin);}}
    Json Performance()const {
        return {{"build","retained-one-frame-probe-20261007"},{"compiled",__DATE__ " " __TIME__},
            {"scheduling",{{"mode","capture-command-message-events"},{"waitCalls",waitCalls},{"waitMilliseconds",waitMilliseconds},
                {"frameWakes",frameWakes},{"commandWakes",commandWakes},{"messageWakes",messageWakes},{"maintenanceWakes",maintenanceWakes},{"maintenanceIntervalMs",50}}},
            {"configuration","Release x64"},{"gpuTimingEnabled",gpuTiming},
            {"lastFrame",lastFrame},{"sourceMediaFps",nullptr},{"knownUniqueContentFps",nullptr},{"displayedFps",nullptr},
            {"nrGpuCompleted",gpuCompleted},{"bypassPublications",0},{"reusedPublications",reusedPublications},{"sourceReevaluations",sourceReevaluations},
            {"coverageDenominator","admitted capture samples; source pictures unknown"},
            {"capture",CaptureStatisticsJson(active?capture.Statistics():lastCapture)},
            {"timingRetained",!active||capture.Paused()},{"comparison",comparison},{"comparisonRefresh",{{"state",refresh.Status()},{"revision",refresh.Revision()},{"attempts",refresh.Attempts()},{"completedPairRetained",refresh.Retained()}}},{"stopTrigger",stopTrigger},
            {"lastCommand",lastCommand},{"trace",{{"path",Utf8(trace.Path())},{"written",trace.Written()},
             {"dropped",trace.Dropped()},{"error",trace.Error()},{"queueCapacity",64},{"recordLimit",32768},{"lastEnqueueMs",Measurement(lastTraceMs)}}}};
    }
public:
    explicit Worker(uint32_t excludedHostPid=0,const std::wstring& tracePath={},bool enableGpuTiming=true):hostPid(excludedHostPid),trace(tracePath),gpuTiming(enableGpuTiming){}
    ~Worker(){Stop("Worker exit");}
    bool Quit() const {return quit;}
    bool Active() const {return active || selection.Pending();}
    bool Retained() const {return measurement.Quarantined() || nr.RequiresRestart() || guidance.RequiresRestart() || (!active && capture.Device()!=nullptr);}
    uint64_t Completed() const {return depthSettings.preview?depthFresh:completed;}
    void Wait(HANDLE commands=nullptr) {
        const auto begin=Qpc();
        auto wake=WaitForWorkerActivity(commands,active&&!capture.Paused()?capture.WakeHandle():nullptr,active&&guidance.DepthBusy()?2:50);
        const double elapsed=QpcMs(Qpc()-begin);++waitCalls;waitMilliseconds+=elapsed;if(active)waitSinceFrameMs+=elapsed;
        switch(wake){case WakeReason::Command:++commandWakes;break;case WakeReason::Capture:++frameWakes;break;
            case WakeReason::Message:++messageWakes;break;case WakeReason::Maintenance:++maintenanceWakes;break;}
    }
    Json Status(const char* event="status") const {
        const auto sample=timings.Last();auto measured=measurement.Status();if(measured["state"]=="complete")measured["modelSha256"]=ModelJson(model).value("sha256",std::string{});
        return {{"event",event},{"apiVersion",1},{"phase",phase},{"reason",reason},
            {"capabilities",{{"hostProcessExclusion",true},{"requestRevisionErrors",true},{"sourceFollowingOverlay",true},{"gpuOutputPublication",true},{"frameTimings",true},{"performanceTelemetry",true},{"nrResolutionScale",true},{"eventDrivenScheduling",true},{"comparisonStripes",true},{"comparisonDirections",true},{"liveProcessing",true},{"outputSnapshots",true},{"modelStyles",true},{"depthAnythingV2",false},{"depthPreviewWithoutNr",false},{"oneFrameMeasurement",true},{"spatialSuperResolution",false},{"optionalNeuralRendering",false},{"fsrFrameGeneration2x",false}}},
            {"performance",Performance()},
            {"outputMode",overlay?"overlay":"preview"},{"sourcePaused",capture.Paused()},
            {"interaction",{{"cursorCaptured",capture.CursorCaptured()},{"cursorMode",overlay?"native-source":"preview-capture"},{"disengageHotkey","Ctrl+Alt+F10"},{"disengageHotkeyAvailable",capture.DisengageHotkeyAvailable()}}},
            {"timing",{{"samples",timings.Count()},{"processedFps",timings.Fps()},{"lastCaptureMs",sample.captureMs},{"lastGuidanceMs",sample.guidanceMs},{"lastNrMs",sample.nrMs},{"lastPresentMs",sample.presentMs},{"lastTotalMs",sample.totalMs},{"sourceAgeKnown",sample.ageKnown&&timings.Count()>0},{"lastSourceAgeMs",sample.ageMs},{"p95SourceAgeMs",timings.P95Age()},{"clock","WGC compositor QPC to accepted Present host return; not media decode or scanout"},{"outputTransfer","shared-gpu"},{"capturePoolFrames",2},{"completedMediaBufferFrames",0}}},
            {"model",ModelJson(model)},{"requestedRevision",selection.Requested()},{"appliedRevision",applied},
            {"session",session},{"countdownRemainingMs",selection.Remaining(GetTickCount64())},
            {"restartRequired",Retained() || phase=="RestartRequired"},
            {"window",WindowJson(target)},{"sourceWidth",sourceWidth},{"sourceHeight",sourceHeight},
            {"captureWidth",sourceWidth},{"captureHeight",sourceHeight},{"outputWidth",sourceWidth},{"outputHeight",sourceHeight},
            {"workWidth",workWidth},{"workHeight",workHeight},{"nrScalePercent",nrScalePercent},
            {"sequence",lastSequence},{"captured",captured},{"nrRecorded",recorded},
            {"nrSubmitted",submitted},{"nrCompleted",completed},{"presentAccepted",presented},
            {"nrMilliseconds",modelMs},{"comparisonSplit",split},{"comparisonStripes",stripes},{"comparisonDirection",comparisonDirection},{"snapshot",snapshot},{"processing",processing},{"measurement",std::move(measured)},
            {"depth",{{"provider",depthSettings.provider==depth::Provider::Dav2?"dav2":"off"},{"profile",depthSettings.profile==depth::Profile::Reference?"reference":"fast"},{"requestedHz",depthSettings.hz},{"preview",depthSettings.preview},{"ready",guidance.DepthReady()},{"freshCaptureEstimates",depthFresh},{"pendingSuperseded",depthDropped},{"presentations",depthPresentations},{"nrAdapterQualified",false},{"reason",depthReason}}},
            {"guidance",{{"mode",guideMode==GuideMode::Off?"off":guideMode==GuideMode::Shadow?"shadow":"experimental"},
                {"estimated",true},{"nativeQualified",false},{"consumed",guidesConsumed},{"depthReady",lastGuides.depthCompleted},
                {"motionReady",lastGuides.motionCompleted},{"depthProvider",lastGuides.depthProvider},
                {"motionProvider",lastGuides.motionProvider},{"reason",lastGuides.reason}}}};
    }
    void Stop(const std::string& why,bool failure=false) {
        if(measurement.RequestId())measurement.Invalidate("Session stopped");measurementDeadline=0;
        refresh.Clear();pendingProcessing=Json();processing["state"]="stopped";
        if(snapshotDeadline){snapshotDeadline=0;snapshot={{"state","canceled"},{"reason","Session stopped before snapshot completed"}};}
        if(active)lastCapture=capture.Statistics();
        stopTrigger=why=="Stopped by parent"?"command:stop":why=="Selection canceled"?"command:cancel":why;
        Trace({{"record_type","stop"},{"trigger",stopTrigger},{"session",session},{"sequence",lastSequence},{"capture",CaptureStatisticsJson(lastCapture)}});
        depthFresh=guidance.DepthFresh();depthDropped=guidance.DepthDropped();selection.Cancel();capture.Disengage();nr.Stop();capture.Stop();guidance.Stop();measurement.Poll();active=false;
        const bool retained=measurement.Quarantined() || nr.RequiresRestart() || guidance.RequiresRestart() || capture.Device()!=nullptr;
        phase=retained?"RestartRequired":failure?"Error":model.valid?"Stopped":"Locked";
        reason=retained?why+"; GPU/provider retirement is unproved. Restart the worker process.":why;
    }
    void StartSession(const Json& request,const WindowIdentity& selected) {
        std::string error;
        if(hostPid && selected.pid==hostPid)throw std::runtime_error("NeuRotic application windows are excluded; select another program");
        if(!CaptureHost::Validate(selected,error))throw std::runtime_error(error);
        Stop("Starting capture");
        if(phase=="RestartRequired")throw std::runtime_error(reason);
        // One user-initiated handoff only. Never fight a later Alt-Tab or activate the output.
        if(hostPid&&request.value("mode",std::string{})=="selected"&&request.value("outputMode",std::string("overlay"))=="overlay") {
            DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
            if(foregroundPid==hostPid)SetForegroundWindow(reinterpret_cast<HWND>(selected.hwnd));
        }
        // Repeat static file validation at the actual start; NrHost independently holds its file lease.
        depthSettings=ParseDepthSettings(request);
        if(depth::NeedsNrModel(depthSettings)){model=VerifyModel(model.path);if(!model.valid)throw std::runtime_error("Please provide your DLSS NR file.");}
        phase="Starting";
        target=selected;if(session==UINT64_MAX||targetGeneration==UINT64_MAX)throw std::runtime_error("Session generation exhausted");++session;++targetGeneration;
        sourceWidth=sourceHeight=workWidth=workHeight=0;lastSequence=0;
        captured=recorded=submitted=completed=presented=gpuCompleted=reusedPublications=sourceReevaluations=0;modelMs=0;guidesConsumed=false;lastGuides={};timings.Clear();
        lastFrame=nullptr;lastCapture={};stopTrigger.clear();processingRevision=request["revision"].get<uint64_t>();
        waitCalls=frameWakes=commandWakes=messageWakes=maintenanceWakes=previousPublicationQpc=0;waitMilliseconds=waitSinceFrameMs=0;
        Emit(Status());
        CaptureOptions options;options.target=selected;options.session=session;options.controlHostPid=hostPid;options.cursor=request.value("cursor",true);overlay=options.overlay=request.value("outputMode",std::string("overlay"))=="overlay";
        if(!capture.Start(options,error))throw std::runtime_error(error);
        NrOptions settings;settings.modelPath=model.path;settings.gpuTiming=gpuTiming;
        settings.forwarderPath=Wide(request.value("forwarder",Utf8((std::filesystem::path(ExeFolder())/L"nvngx.dll_dlssnr.dll").wstring())));
        settings.modelStyle=request.value("modelStyle",0);settings.transferStrength=request.value("transferStrength",1.0f);settings.colourStrength=request.value("colourStrength",1.0f);
        nrScalePercent=settings.nrScalePercent=request.value("nrScalePercent",100u);
        processingOptions=ProcessingOptions(request);processing={{"state","awaiting-frame"},{"requestedRevision",processingRevision},{"appliedRevision",0},{"requested",ProcessingJson(processingOptions)}};
        settings.workWidth=request.value("workWidth",0u);settings.workHeight=request.value("workHeight",0u);
        auto guides=request.value("guides",std::string("off"));
        guideMode=guides=="experimental"?GuideMode::Experimental:guides=="shadow"?GuideMode::Shadow:GuideMode::Off;
        settings.guides=guideMode;
        if(depth::NeedsNrModel(depthSettings)&&!nr.Initialize(capture.Device(),capture.Context(),settings,error))throw std::runtime_error(error);
        GuidanceOptions guidanceOptions;guidanceOptions.mode=guideMode;
        guidanceOptions.depthBundle=Wide(request.value("depthBundle",std::string{}));
        guidanceOptions.dav2=depthSettings;guidanceOptions.dav2Root=Wide(request.value("depthRoot",Utf8((std::filesystem::path(ExeFolder())/L"depth-anything-v2"/(depthSettings.profile==depth::Profile::Reference?L"reference":L"fast")).wstring())));
        if(!guidance.Initialize(capture.Device(),capture.Context(),guidanceOptions,error))throw std::runtime_error(error);
        lastGuides={};lastGuides.reason=error;depthReason=depthSettings.provider==depth::Provider::Dav2?error:"Depth off";depthPresentations=depthFresh=depthDropped=0;
        Trace({{"record_type","session"},{"session",session},{"settingsRevision",processingRevision},{"modelSha256",ModelJson(model)["sha256"]},{"captureApi","WGC"},{"gpuTiming",gpuTiming},{"transferStrength",settings.transferStrength},{"colourStrength",settings.colourStrength},{"guides",guides},{"build",Performance()["build"]},{"compiled",Performance()["compiled"]}});
        active=true;phase="Running";reason=depthSettings.preview?"Depth preview initialized; waiting for a completed estimate":"Capture and NR initialized; waiting for a completed frame";
        applied=std::max(applied,request["revision"].get<uint64_t>());split=request.value("split",split);stripes=request.value("stripes",0);comparisonDirection=request.value("comparisonDirection",0);Emit(Status("started"));
    }
    void Command(const std::string& line) {
        uint64_t requestRevision=0;
        try {
            // The stream error is generated internally, and never dispatches an operation.
            auto raw=Json::parse(line);
            if(raw.is_object()&&raw.contains("revision")&&raw["revision"].is_number_unsigned())requestRevision=raw["revision"].get<uint64_t>();
            if(raw.is_object() && raw.value("command",std::string{})=="stream-error"){Emit({{"event","error"},{"reason",raw.value("reason",std::string("Invalid command stream"))}});return;}
            auto request=ParseCommand(line);auto command=request["command"].get<std::string>();std::string error;
            lastCommand=command;
            Trace({{"record_type","control"},{"command",command},{"revision",requestRevision},{"session",session},{"sequence",lastSequence},{"completed",completed},{"published",presented},{"split",request.value("split",split)}});
            if(command=="status"){Emit(Status());return;}
            if(command=="catalog") {
                auto windows=Json::array();auto search=Wide(request.value("search",std::string{}));
                std::transform(search.begin(),search.end(),search.begin(),towlower);
                for(const auto& window:CaptureHost::Enumerate()) {
                    if(hostPid && window.pid==hostPid)continue;
                    auto text=window.title+L" "+window.executable;std::transform(text.begin(),text.end(),text.begin(),towlower);
                    if(search.empty() || text.find(search)!=std::wstring::npos)windows.push_back(WindowJson(window));
                    if(windows.size()==256)break;
                }
                Emit({{"event","catalog"},{"windows",windows},{"maxCandidates",256}});return;
            }
            if(command=="verify-model"){Emit({{"event","model"},{"model",ModelJson(VerifyModel(Wide(request["path"].get<std::string>())))}});return;}
            if(command=="quit"){Stop("Parent requested shutdown");quit=true;Emit(Status("shutdown"));return;}
            if(command=="measure-frame"){
                const auto id=request["requestId"].get<uint64_t>();
                if(!measurement.Request(id)){auto rejected=Status("measurement-rejected");rejected["measurementRequestId"]=id;rejected["measurementError"]="A measurement is pending, stale, or its GPU ownership is unproved";Emit(std::move(rejected));return;}
                measurementDeadline=GetTickCount64()+5000;
                if(!active||phase!="Running"||capture.Paused())measurement.Refuse("Start an available rendering session before measuring");
                else if(depthSettings.preview||nrScalePercent!=100||split!=0||stripes!=0||snapshotDeadline)measurement.Refuse("Only stable NR-only scale100 SDR equal-extent enhanced-only frames without a pending snapshot are supported");
                auto accepted=Status("measurement");accepted["measurementRequestId"]=id;Emit(std::move(accepted));return;
            }
            auto revision=request["revision"].get<uint64_t>();
            if(command=="start") {
                if(Retained() || phase=="RestartRequired")throw std::runtime_error("Restart the worker process before starting another session");
                if(depth::NeedsNrModel(ParseDepthSettings(request))&&!model.valid)throw std::runtime_error("Please provide your DLSS NR file.");
                if(request.value("mode",std::string("countdown"))=="countdown") {
                    if(!selection.Countdown(revision,request.value("seconds",5u),GetTickCount64(),true,error))throw std::runtime_error(error);
                    if(active){if(measurement.RequestId())measurement.Invalidate("Target selection changed");refresh.Clear();capture.Disengage();nr.Stop();capture.Stop();guidance.Stop();measurement.Poll();active=false;
                        if(nr.RequiresRestart() || guidance.RequiresRestart() || capture.Device()!=nullptr){Stop("Unable to retire the previous session",true);throw std::runtime_error(reason);}}
                    pending=request;phase="Countdown";reason="Switch to the window you want to process; Escape cancels";Emit(Status("countdown"));
                } else {
                    auto window=ParseWindow(request["window"]);
                    if(!CaptureHost::Validate(window,error))throw std::runtime_error(error);
                    if(!selection.Request(revision,error))throw std::runtime_error(error);
                    StartSession(request,window);
                }return;
            }
            if(!selection.Request(revision,error))throw std::runtime_error(error);
            if(command=="stop" || command=="cancel"){Stop(command=="cancel"?"Selection canceled":"Stopped by parent");applied=revision;Emit(Status("stopped"));return;}
            if(command=="set-comparison"){if(measurement.RequestId())measurement.Invalidate("Comparison configuration changed");if(active)refresh.Request(revision,GetTickCount64());comparison={{"revision",revision},{"publicationsAtApply",presented},{"completedAtApply",completed}};split=request["split"].get<float>();stripes=request.value("stripes",0);comparisonDirection=request.value("comparisonDirection",0);if(selection.Pending()){pending["split"]=split;pending["stripes"]=stripes;pending["comparisonDirection"]=comparisonDirection;}applied=revision;Emit(Status("comparison"));return;}
            if(command=="set-processing"){
                if(measurement.RequestId())measurement.Invalidate("Processing configuration changed");
                if(depthSettings.preview)throw std::runtime_error("Stop depth preview before changing NR processing");
                if(!active&&!selection.Pending())throw std::runtime_error("Start a session before changing live processing");
                pendingProcessing=request;processing["state"]="pending";processing["requestedRevision"]=revision;processing["requested"]=ProcessingJson(ProcessingOptions(request));
                if(selection.Pending()){for(const char* field:{"transferStrength","colourStrength","nrScalePercent","modelStyle"})pending[field]=request[field];pending["revision"]=revision;}
                Emit(Status("processing-requested"));return;
            }
            if(command=="snapshot") {
                if(!active||capture.Paused())throw std::runtime_error("Start rendering an available window before taking a snapshot");
                if(snapshotDeadline)throw std::runtime_error("A snapshot is already pending");
                if(measurement.RequestId())measurement.Invalidate("Snapshot request changed the pending output operation");
                snapshotDeadline=GetTickCount64()+5000;snapshot={{"state","pending"}};applied=revision;Emit(Status("snapshot-requested"));return;
            }
            if(command=="set-model" || command=="import-model") {
                if(Retained())throw std::runtime_error("Restart the worker process before changing its model");
                if(Active())throw std::runtime_error("Stop the current session before changing its model");
                auto candidate=command=="import-model"?ImportModel(Wide(request["path"].get<std::string>()),Wide(request.value("modelsRoot",Utf8(DefaultModelsRoot())))):VerifyModel(Wide(request["path"].get<std::string>()));
                if(!candidate.valid){Emit({{"event","error"},{"reason",candidate.reason},{"model",ModelJson(candidate)},{"requestedRevision",selection.Requested()},{"appliedRevision",applied}});return;}
                model=std::move(candidate);applied=revision;phase="Ready";reason="NR file verified; window selection is available";Emit(Status("model"));return;
            }
        } catch(const std::exception& e){if(phase=="Starting")Stop(e.what(),true);Emit({{"event","error"},{"reason",e.what()},{"requestRevision",requestRevision},{"requestedRevision",selection.Requested()},{"appliedRevision",applied}});}
    }
    void Tick() {
        try {
            measurement.Poll();if(measurement.Pending()&&measurementDeadline&&GetTickCount64()>=measurementDeadline)measurement.Invalidate("No completed eligible frame before the measurement deadline");
            if(!active){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){quit=true;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}}
            if(selection.Pending() && (GetAsyncKeyState(VK_ESCAPE)&0x8000)){Stop("Selection canceled with Escape");Emit(Status("stopped"));}
            if(selection.Due(GetTickCount64())) {
                WindowIdentity foreground;std::string error;
#ifdef NRW_CONTROL_FIXTURE
                const auto selectedForeground=ControlFixtureForeground();
#else
                const auto selectedForeground=GetForegroundWindow();
#endif
                if(!CaptureHost::Inspect(selectedForeground,foreground,error))throw std::runtime_error(error);
                StartSession(pending,foreground);
            }
            if(snapshotDeadline&&GetTickCount64()>=snapshotDeadline){snapshotDeadline=0;snapshot={{"state","failed"},{"reason","No completed output arrived before snapshot timeout"}};Emit(Status("snapshot"));}
            if(active) {
                if(!capture.Pump()){Stop("Source or output window closed");Emit(Status("stopped"));return;}
                auto observed=capture.Statistics();
                if(observed.interactionReason!=lastCapture.interactionReason || observed.outputVisible!=lastCapture.outputVisible)
                    Trace({{"record_type","interaction"},{"session",session},{"sequence",lastSequence},{"capture",CaptureStatisticsJson(observed)}});
                lastCapture=observed;
                if(measurement.RequestId()&&measurement.Identity().requestId==measurement.RequestId()) {
                    const auto current=capture.ObservedIdentity();const auto& id=measurement.Identity();
                    if(id.frame.streamEpoch!=current.streamEpoch||id.frame.geometryEpoch!=current.geometryEpoch||id.outputEpoch!=capture.OutputEpoch()||id.targetGeneration!=targetGeneration||capture.Paused())measurement.Invalidate("Source, geometry or output epoch changed");
                }
                refresh.Validate(capture.ObservedIdentity(),targetGeneration,capture.OutputEpoch(),processingRevision,capture.Paused());
                if(capture.Paused()){timings.Pause();previousPublicationQpc=0;waitSinceFrameMs=0;}
                CapturedFrame frame;std::string error;
                const auto captureBegin=Qpc();
                const bool freshCapture=capture.Next(frame,error);
                if(freshCapture) {
                    refresh.DropPair();
                    const auto captureEnd=Qpc();
                    ++captured;frame.stamp.configEpoch=processingRevision;sourceWidth=frame.stamp.width;sourceHeight=frame.stamp.height;lastSequence=frame.stamp.sequence;
                    if(!pendingProcessing.is_null()){
                        auto next=ProcessingOptions(pendingProcessing);if(!nr.Reconfigure(next,error))throw std::runtime_error(error);
                        processingOptions=next;nrScalePercent=next.nrScalePercent;processingRevision=pendingProcessing["revision"].get<uint64_t>();pendingProcessing=Json();
                        frame.stamp.reset=true;frame.stamp.configEpoch=processingRevision;processing["state"]="awaiting-frame";
                    }
                    if(guidance.DepthReady())guidance.OfferDepth(frame,target.hwnd);
                    if(!depthSettings.preview){
                    lastGuides=guidance.Process(frame);
                    const auto guidanceEnd=Qpc();
                    if(guidance.RequiresRestart())throw std::runtime_error(lastGuides.reason.empty()?"Guidance retirement is unproved":lastGuides.reason);
                    FrameProbe* frameProbe=nullptr;
                    if(measurement.Pending()&&measurement.Identity().requestId!=measurement.RequestId()&&pendingProcessing.is_null()&&processing.value("state",std::string{})=="applied"&&selection.Requested()==applied) {
                        ProbeIdentity id;id.requestId=measurement.RequestId();id.frame=frame.stamp;id.targetGeneration=targetGeneration;id.outputEpoch=capture.OutputEpoch();
                        id.requestedRevision=selection.Requested();id.appliedRevision=applied;id.processingRevision=processingRevision;id.comparisonRevision=comparison.value("revision",uint64_t(0));
                        id.workWidth=workWidth;id.workHeight=workHeight;id.outputWidth=frame.stamp.width;id.outputHeight=frame.stamp.height;id.scalePercent=nrScalePercent;id.split=split;id.stripes=stripes;
                        id.transferStrength=processingOptions.transferStrength;id.colourStrength=processingOptions.colourStrength;id.modelStyle=processingOptions.modelStyle;id.comparisonDirection=comparisonDirection;
                        if(measurement.Bind(id))frameProbe=&measurement;
                    }
                    auto result=nr.Process(frame,guideMode==GuideMode::Off?nullptr:&lastGuides,frameProbe);
                    const auto nrEnd=Qpc();
                    recorded+=result.recorded;submitted+=result.submitted;completed+=result.completed;gpuCompleted+=result.measurements.gpuCompleted;modelMs=result.milliseconds;
                    workWidth=result.workWidth;workHeight=result.workHeight;guidesConsumed=result.estimatedGuidesUsed;
                    if(!result.completed || !result.texture){
                        lastFrame=PerformanceRecord(frame,result,processingRevision,ModelJson(model).value("sha256",std::string{}),QpcMs(captureEnd-captureBegin),QpcMs(guidanceEnd-captureEnd),{},{},false);FrameScheduling(nrEnd);Trace(lastFrame);
                        throw std::runtime_error(result.reason.empty()?"NR did not complete an output frame":result.reason);}
                    SnapshotResult saved;
                    const bool accepted=capture.Present(frame,result.texture.Get(),split,error,stripes,snapshotDeadline?&saved:nullptr,comparisonDirection,result.ownership,frameProbe);
                    if(frameProbe){measurement.Poll();Emit(Status("measurement-updated"));}
                    if(snapshotDeadline&&(!saved.path.empty()||!saved.error.empty())) {
                        snapshotDeadline=0;snapshot={{"state",saved.path.empty()?"failed":"saved"},{"path",Utf8(saved.path)},{"reason",saved.error},{"session",session},{"sequence",frame.stamp.sequence}};Emit(Status("snapshot"));
                    }
                    if(error.empty())refresh.Store(frame,result,targetGeneration,capture.OutputEpoch(),processingRevision);
                    if(accepted){++presented;refresh.AcceptedFresh();}
                    if(accepted&&processing.value("state",std::string{})=="awaiting-frame"){
                        processing["state"]="applied";processing["appliedRevision"]=processingRevision;processing["applied"]=ProcessingJson(processingOptions);processing["sequence"]=frame.stamp.sequence;
                        applied=std::max(applied,processingRevision);Emit(Status("processing-updated"));
                    }
                    const auto presentEnd=Qpc();
                    const bool ageKnown=accepted&&frame.acquiredQpc&&presentEnd>=frame.stamp.timestampQpc;
                    lastFrame=PerformanceRecord(frame,result,processingRevision,ModelJson(model).value("sha256",std::string{}),QpcMs(captureEnd-captureBegin),QpcMs(guidanceEnd-captureEnd),QpcMs(presentEnd-nrEnd),ageKnown?OptionalMs(QpcMs(presentEnd-frame.stamp.timestampQpc)):OptionalMs{},accepted);lastFrame["cpu"]["present_call_ms"]=Measurement(capture.Statistics().presentCallMs);FrameScheduling(presentEnd);Trace(lastFrame);
                    if(!error.empty())throw std::runtime_error(error);
                    timings.Add({QpcMs(presentEnd),QpcMs(captureEnd-captureBegin),QpcMs(guidanceEnd-captureEnd),QpcMs(nrEnd-guidanceEnd),QpcMs(presentEnd-nrEnd),QpcMs(presentEnd-captureBegin),ageKnown?QpcMs(presentEnd-frame.stamp.timestampQpc):0,accepted,ageKnown});
                    reason=result.reason.empty()?"NR completed; output presentation is tracked separately":result.reason;
                    }
                } else if(!error.empty())throw std::runtime_error(error);
                else if(capture.Paused())reason="Original application remains interactive; rendering resumes when the source is available and its menu or dialog is closed";
                if(!freshCapture&&!capture.Paused()) {
                    // Guide/depth histories cannot be silently repeated on an old
                    // source. Image-only processing may reset and evaluate it once.
                    if(!pendingProcessing.is_null()&&guideMode==GuideMode::Off&&!guidance.DepthReady()&&!depthSettings.preview&&
                       refresh.Validate(capture.ObservedIdentity(),targetGeneration,capture.OutputEpoch(),processingRevision,false)) {
                        auto held=refresh.TakeSource();
                        auto next=ProcessingOptions(pendingProcessing);if(!nr.Reconfigure(next,error))throw std::runtime_error(error);
                        processingOptions=next;nrScalePercent=next.nrScalePercent;processingRevision=pendingProcessing["revision"].get<uint64_t>();pendingProcessing=Json();
                        held->stamp.reset=true;held->stamp.configEpoch=processingRevision;processing["state"]="awaiting-frame";
                        const auto result=nr.Process(*held,nullptr);++sourceReevaluations;
                        recorded+=result.recorded;submitted+=result.submitted;completed+=result.completed;gpuCompleted+=result.measurements.gpuCompleted;
                        modelMs=result.milliseconds;workWidth=result.workWidth;workHeight=result.workHeight;guidesConsumed=false;
                        if(!result.completed||!result.texture)throw std::runtime_error(result.reason.empty()?"NR did not complete a held-source evaluation":result.reason);
                        if(refresh.Store(*held,result,targetGeneration,capture.OutputEpoch(),processingRevision))refresh.Request(comparison.value("revision",processingRevision),GetTickCount64());
                        Trace({{"record_type","held-source-evaluation"},{"session",session},{"sequence",held->stamp.sequence},{"timestampQpc",held->stamp.timestampQpc},{"processingRevision",processingRevision},{"recorded",result.recorded},{"submitted",result.submitted},{"completed",result.completed}});
                    }
                    if(refresh.Validate(capture.ObservedIdentity(),targetGeneration,capture.OutputEpoch(),processingRevision,false)) {
                        const bool accepted=refresh.Try(GetTickCount64(),[&](const ComparisonRefresh::Pair& pair){
                            return capture.Present(pair.source,pair.enhanced.texture.Get(),split,error,stripes,nullptr,comparisonDirection,pair.enhanced.ownership);
                        });
                        if(accepted){
                            ++presented;++reusedPublications;
                            if(processing.value("state",std::string{})=="awaiting-frame"){
                                processing["state"]="applied";processing["appliedRevision"]=processingRevision;processing["applied"]=ProcessingJson(processingOptions);processing["sequence"]=lastSequence;
                                applied=std::max(applied,processingRevision);Emit(Status("processing-updated"));
                            }
                            Trace({{"record_type","held-source-publication"},{"session",session},{"sequence",lastSequence},{"comparisonRevision",refresh.Revision()},{"processingRevision",processingRevision},{"presentAccepted",true},{"newCapture",false}});
                        }
                        if(!error.empty())throw std::runtime_error(error);
                    }
                }
                guidance.ObserveDepthEpochs(capture.ObservedIdentity(),target.hwnd,processingRevision);
                depth::DepthFrame depthFrame;std::string depthError;
                if(guidance.PollDepth(depthFrame,depthError)){
                    depthReason=depthFrame.reason;lastGuides.stamp=depthFrame.color.stamp;lastGuides.depthCompleted=true;lastGuides.depthProvider="depth-anything-v2-small-tensorrt-gpu-v1";lastGuides.reason=depthReason;
                    if(depthSettings.preview&&!capture.Paused()){SnapshotResult saved;const bool accepted=capture.Present(depthFrame.color,depthFrame.preview.Get(),split,error,stripes,snapshotDeadline?&saved:nullptr,comparisonDirection);if(accepted){++presented;++depthPresentations;if(processing.value("state",std::string{})=="awaiting-frame"){processing["state"]="applied";processing["appliedRevision"]=processingRevision;processing["sequence"]=depthFrame.color.stamp.sequence;processing["mode"]="depth-preview";applied=std::max(applied,processingRevision);Emit(Status("processing-updated"));}}if(snapshotDeadline&&(!saved.path.empty()||!saved.error.empty())){snapshotDeadline=0;snapshot={{"state",saved.path.empty()?"failed":"saved"},{"path",Utf8(saved.path)},{"reason",saved.error},{"session",session},{"sequence",depthFrame.color.stamp.sequence}};Emit(Status("snapshot"));}if(!error.empty())throw std::runtime_error(error);reason=depthReason;}
                }
                if(!guidance.DepthReady()&&!depthError.empty()){depthReason=depthError;if(depthSettings.preview)throw std::runtime_error(depthError);}
                depthFresh=guidance.DepthFresh();depthDropped=guidance.DepthDropped();
                if(guidance.RequiresRestart())throw std::runtime_error(depthError.empty()?"Depth retirement is unproved":depthError);
            }
        } catch(const std::exception& e){Stop(e.what(),true);Emit(Status("error"));}
        auto now=GetTickCount64();if(Active() && now-lastStatus>=100){lastStatus=now;Emit(Status());}
    }
};
void Help() {
    std::cout<<"NeuRotic Window Worker (Windows 11 x64)\n"
      "  --catalog [--search text]\n"
      "  --verify-model path\n"
      "  --import-model path [--models-root directory]\n"
      "  --run --model path [--window HWND | --countdown 5]\n"
      "        [--forwarder path] [--guides off|shadow|experimental] [--depth-bundle path]\n"
      "        [--duration seconds] [--max-frames count] [--trace absolute-file.jsonl]\n"
      "        [--no-gpu-timing] (paired diagnostic-overhead baseline)\n"
      "  --control   JSON lines on inherited stdin/stdout; no network listener\n"
      "  --host-pid PID   exclude hosting application from all window selection\n"
      "  --test-control\n"
      "NR start requires the byte-pinned compatible user-provided nvngx_dlssnr.dll.\n"
      "See INTEGRATION.md for selection identities, revisions, commands and status.\n";
}
}
int WorkerMain(int argc,wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);SetConsoleCtrlHandler(ConsoleSignal,TRUE);
    HRESULT apartment=RoInitialize(RO_INIT_MULTITHREADED);
    struct ApartmentExit {HRESULT hr;~ApartmentExit(){if(SUCCEEDED(hr))RoUninitialize();}} apartmentExit{apartment};
    auto argument=[&](const wchar_t* flag)->std::wstring {for(int i=1;i<argc;++i)if(std::wstring(argv[i])==flag){if(i+1==argc)throw std::runtime_error("Missing command-line argument");return argv[i+1];}return {};};
    auto has=[&](const wchar_t* flag){for(int i=1;i<argc;++i)if(std::wstring(argv[i])==flag)return true;return false;};
    try {
        if(argc==1 || has(L"--help")){Help();return 0;}
        if(has(L"--test-control"))return RunControlTests();
        if(has(L"--verify-model")){auto model=VerifyModel(argument(L"--verify-model"));Emit({{"event","model"},{"model",ModelJson(model)}});return model.valid?0:3;}
        if(has(L"--import-model")){auto root=argument(L"--models-root");auto model=ImportModel(argument(L"--import-model"),root.empty()?DefaultModelsRoot():root);Emit({{"event","model"},{"model",ModelJson(model)}});return model.valid?0:3;}
        auto hostArgument=argument(L"--host-pid");uint32_t hostPid=0;
        if(!hostArgument.empty()){size_t used=0;auto value=std::stoull(hostArgument,&used);if(used!=hostArgument.size()||value==0||value>UINT32_MAX)throw std::runtime_error("Invalid hosting application PID");hostPid=static_cast<uint32_t>(value);}
        auto tracePath=argument(L"--trace");
        if(tracePath.empty()) {
            wchar_t folder[32768]{};auto n=GetEnvironmentVariableW(L"NEUROTIC_ANYTHING_TRACE_DIR",folder,32768);
            if(n && n<32768){FILETIME now{};GetSystemTimeAsFileTime(&now);ULARGE_INTEGER id{};id.LowPart=now.dwLowDateTime;id.HighPart=now.dwHighDateTime;
                tracePath=(std::filesystem::path(folder)/(L"anything-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(id.QuadPart)+L".jsonl")).wstring();}
        }
        wchar_t gpuFlag[2]{};const bool disableGpuTiming=GetEnvironmentVariableW(L"NEUROTIC_ANYTHING_GPU_TIMING",gpuFlag,2)==1&&gpuFlag[0]==L'0';
        Worker worker(hostPid,tracePath,!has(L"--no-gpu-timing")&&!disableGpuTiming);
        if(has(L"--catalog")){auto search=argument(L"--search");worker.Command(Json{{"command","catalog"},{"search",Utf8(search)}}.dump());return 0;}
        if(has(L"--control")) {
            Inbox inbox;Emit(worker.Status());
            while(!worker.Quit() && !interrupted) {
                std::string line;for(int n=0;n<16 && inbox.Pop(line);++n){worker.Command(line);if(worker.Quit())break;}
                if(inbox.state->overflow.exchange(false))Emit({{"event","error"},{"reason","Too many pending commands; maximum is 16"}});
                if(worker.Quit())break;
                if(inbox.Done()){worker.Command("{\"command\":\"quit\"}");break;}
                worker.Tick();if(!worker.Quit()&&!interrupted)worker.Wait(inbox.state->ready.Handle());
            }
            if(!worker.Quit())worker.Command("{\"command\":\"quit\"}");return 0;
        }
        if(has(L"--run")) {
            auto model=argument(L"--model");if(!has(L"--depth-preview"))worker.Command(Json{{"command","set-model"},{"revision",uint64_t(1)},{"path",Utf8(model)}}.dump());
            Json start={{"command","start"},{"revision",uint64_t(2)},{"mode","countdown"}};
            auto window=argument(L"--window");if(!window.empty()){WindowIdentity id;std::string reason;if(!CaptureHost::Inspect(reinterpret_cast<HWND>(std::stoull(window,nullptr,0)),id,reason))throw std::runtime_error(reason);start["mode"]="selected";start["window"]=WindowJson(id);}
            auto countdown=argument(L"--countdown");if(!countdown.empty())start["seconds"]=std::stoull(countdown);
            for(const auto& [flag,key]:std::vector<std::pair<const wchar_t*,const char*>>{{L"--forwarder","forwarder"},{L"--guides","guides"},{L"--depth-bundle","depthBundle"},{L"--depth-provider","depthProvider"},{L"--depth-profile","depthProfile"},{L"--depth-root","depthRoot"}}){auto value=argument(flag);if(!value.empty())start[key]=Utf8(value);}
            if(has(L"--depth-preview"))start["depthPreview"]=true;
            if(has(L"--depth-hz"))start["depthHz"]=std::stoull(argument(L"--depth-hz"));
            worker.Command(start.dump());if(!worker.Active())return 3;
            auto duration=argument(L"--duration"),maxFrames=argument(L"--max-frames");auto begin=GetTickCount64();
            auto seconds=duration.empty()?0ull:std::stoull(duration);auto limit=maxFrames.empty()?0ull:std::stoull(maxFrames);
            while(worker.Active() && !interrupted){worker.Tick();if((seconds && (GetTickCount64()-begin)/1000>=seconds)||(limit && worker.Completed()>=limit))break;if(worker.Active()&&!worker.Quit()&&!interrupted)worker.Wait();}
            auto status=worker.Status();worker.Stop("CLI session finished");auto shutdown=worker.Status("shutdown");Emit(shutdown);
            return status["phase"]=="Error" || status["restartRequired"].get<bool>() || shutdown["restartRequired"].get<bool>()?1:0;
        }
        throw std::runtime_error("Unknown command-line mode; use --help");
    } catch(const std::exception& e){Emit({{"event","error"},{"reason",e.what()}});return 1;}
}
}
int wmain(int argc,wchar_t** argv){return nrw::WorkerMain(argc,argv);}
