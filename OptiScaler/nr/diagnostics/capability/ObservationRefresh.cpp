#include "ObservationRefresh.h"
#include "ObservationRefreshControl.h"
#include "../candidate/Observer.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <algorithm>
#include <tuple>
#ifndef NR_OBSERVATION_REFRESH_TEST
#include "../../../Config.h"
#include "../../../dlssnr/DlssNrFeature_Dx12.h"
#include "../../../dlssnr/DlssNrFeature_Vk.h"
#include "../../../dlssnr/DlssNr_Present.h"
#include "../../../dlssnr/PreFg.h"
#include "../../../dlssnr/NativeVulkanGuides.h"
#endif
namespace DlssNr::Capability::ObservationRefresh {
namespace {
struct Service {
    ObservationRefreshControl control;
    std::atomic<std::shared_ptr<const ObservationReportBundle>> completed;
};
std::atomic<Service*> serviceSlot{nullptr};
Service& Instance() {static auto* service=[] {auto* p=new Service;serviceSlot.store(p);return p;}();return *service;}
struct WorkerConfig {Service* service;ObservationRefreshTicket ticket;bool vulkan;HMODULE module=nullptr;};
BackendSample ReadBackend(bool vulkan) {
#ifdef NR_OBSERVATION_REFRESH_TEST
    return ReadTestBackend(vulkan);
#else
    BackendSample sample;sample.vulkan=vulkan;
    const auto settings=Config::Instance()->GetDlssNrConfigSnapshot();
    const auto present=DlssNr::PresentTelemetry();const auto fg=DlssNr::PreFg::Provider();
    sample.vulkan=ObservationUsesVulkan(settings.DlssNrRoute.value_or_default(),vulkan,
        present.api!=PresentApi::Unknown,present.api==PresentApi::Vulkan);
    const auto native=sample.vulkan?DlssNr::NativeTelemetryVk():DlssNr::Telemetry();
    sample.state=MakeRefreshState(settings.DlssNrEnabled.value_or_default(),settings.DlssNrRoute.value_or_default(),
        NrConfigSynchronization::ProfileGeneration(),native,present,fg,settings.ObservationRevision());
    sample.state.hdrChangeRevision=HdrChangeRevision();
    auto& input=sample.evidence;input.requestedEnabled=sample.state.requestedEnabled;
    input.nativeRoute=sample.state.requestedRoute==0;
    input.ownerRunning=input.nativeRoute?native.running:present.active;
    input.ownerFailed=input.nativeRoute?(native.failed || native.outputQuarantined || native.transitionPending):
        (present.failed || present.policyBlocked);
    input.resetPending=input.nativeRoute?(native.resetPending || native.historyResetRequested):present.historyResetPending;
    input.lifecycleOpen=native.lifecycleOpen;input.lifecycle=native.lifecycleGeneration;
    input.resources=present.resourceGeneration;
    input.presentGuided=present.actualInputClass==PresentInputDecision::InputClass::Guided;
    input.completedAfter=input.nativeRoute?native.gpuCompletedOutputEvaluations:present.modelSubmissions;
    input.parametersAfter=native.nativeInputs.observations;
    input.finiteJitter=native.nativeInputs.jitterSupplied && native.nativeInputs.jitter.has_value();
    input.finitePreExposure=native.nativeInputs.preExposureSupplied && native.nativeInputs.preExposure.has_value();
    return sample;
#endif
}
CollectionResult CollectBackend(bool vulkan) {
#ifdef NR_OBSERVATION_REFRESH_TEST
    return CollectTestBackend(vulkan);
#else
    return CollectStatus(vulkan);
#endif
}
// Compare only known identity/generation bindings, not per-frame sample counters.
bool SameOwnerIdentities(const Snapshot& before,const Snapshot& after) {
    using Bindings=std::vector<std::tuple<uint64_t,uint8_t,std::string,std::string,uint64_t>>;
    auto identities=[](const Snapshot& value) {
        Bindings result;
        for(const auto& stream:value.streams)for(unsigned i=0;i<stream.batch.scopeCount;i++) {
            const auto& scope=stream.batch.At<Scope>(stream.batch.scopes[i]);
            for(const auto& binding:stream.batch.List<OwnerStamp>(scope.bindings)) {
                if(binding.dimension!=Dimension::Device && binding.dimension!=Dimension::Swapchain &&
                   binding.dimension!=Dimension::Lifecycle && binding.dimension!=Dimension::Configuration &&
                   binding.dimension!=Dimension::ResourceSet)continue;
                if(binding.status==StampStatus::Known)result.emplace_back(stream.writer,static_cast<uint8_t>(binding.dimension),
                    std::string(stream.batch.Text(binding.owner)),std::string(stream.batch.Text(binding.instance)),binding.generation);
            }
        }
        std::sort(result.begin(),result.end());return result;
    };
    return identities(before)==identities(after);
}
DWORD WINAPI Worker(void* argument) noexcept {
    std::unique_ptr<WorkerConfig> cfg(static_cast<WorkerConfig*>(argument));
    auto* service=cfg->service;auto token=cfg->ticket;const auto vulkan=cfg->vulkan;const auto module=cfg->module;cfg.reset();
    std::shared_ptr<ObservationReportBundle> report;bool manualOpened=false,available=false;
    try {
        const auto before=ReadBackend(vulkan);auto initial=CollectBackend(vulkan);
        service->control.Observe(before.state,before.vulkan,false);
        if(!service->control.Cancelled(token)) {
            if(token.manual && !vulkan)manualOpened=CandidateObserver::BeginManual(token.identity);
            token.started=GetTickCount64();
            while(!ObservationRefreshControl::CollectionDone(token,GetTickCount64()) && !service->control.Cancelled(token)) {
                if(manualOpened)CandidateObserver::AdvanceManual(GetTickCount64()-token.started);
                const auto current=ReadBackend(vulkan);service->control.Observe(current.state,current.vulkan,false);Sleep(16);
            }
            report=std::make_shared<ObservationReportBundle>();
            if(manualOpened) {
                CandidateObserver::AdvanceManual(2000);
                while(!CandidateObserver::EndManual(report->discovery))Sleep(1);
                report->hasDiscovery=true;manualOpened=false;
            }
            const auto after=ReadBackend(vulkan);service->control.Observe(after.state,after.vulkan,false);
            report->observation=CollectBackend(vulkan);
#ifndef NR_OBSERVATION_REFRESH_TEST
            // Freeze external diagnostics with this capture; export never re-queries.
            report->preparedExternal=PreparedGuides::QueryStatus();
            report->renderingOutput=RenderingOutput::Query();
            report->renderingOutputCapturedTickMs=GetTickCount64();
            report->preparedBuiltInCapturedTickMs=GetTickCount64();
            report->preparedBuiltIn=PreparedGuides::QueryStatusV2(report->preparedBuiltInCapturedTickMs);
            const auto native=NativeVulkanGuides::Status();
            report->nativeCaptureSelected=native.selected && after.vulkan;
            report->nativeCaptures=native.captured;report->nativeDeliveries=native.delivered;
            report->nativeScopes=native.scopesSeen;report->nativeDepthCopies=native.depthCopies;
            report->nativeEstimatedAssociations=native.estimatedAssociations;
            report->nativeCaptureReason=native.reason;
#endif
            // Check once more after collection; configuration/owner changes cannot be relabeled current.
            const auto final=ReadBackend(vulkan);service->control.Observe(final.state,final.vulkan,false);
            auto use=final.evidence;use.completedBefore=before.evidence.completedAfter;
            use.parametersBefore=before.evidence.parametersAfter;
            use.contextMatched=!service->control.Cancelled(token) && initial.capture.snapshot && report->observation.capture.snapshot &&
                initial.capture.state==CaptureState::Complete && report->observation.capture.state==CaptureState::Complete &&
                SameOwnerIdentities(*initial.capture.snapshot,*report->observation.capture.snapshot);
            report->owner=NormalizeUseEvidence(use);report->operation=token.identity;report->context=token.context;
            report->manual=token.manual;report->started=token.started;report->completed=GetTickCount64();
            report->completedUtc=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            available=!service->control.Cancelled(token) && PrepareObservationReport(*report);
        }
    }catch(...) {}
    if(manualOpened) {
        CandidateObserver::Snapshot discarded{};
        while(!CandidateObserver::EndManual(discarded))Sleep(1);
    }
    if(!available)report.reset();
    service->control.Finish(token,GetTickCount64(),[&] {service->completed.store(std::move(report),std::memory_order_release);},available);
    FreeLibraryAndExitThread(module,0);
}
}
void Update(const RefreshState& state,bool vulkan,bool manual,bool activationHeld,bool allowAutomatic) noexcept {
    try {
        auto& service=Instance();service.control.Observe(state,vulkan);service.control.Activation(activationHeld);
        if(!manual && !allowAutomatic)return;
        const auto token=service.control.Request(manual,GetTickCount64());if(!token)return;
        HMODULE module=nullptr;
        try {
            auto cfg=std::make_unique<WorkerConfig>();cfg->service=&service;cfg->ticket=*token;cfg->vulkan=vulkan;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&Update),&module))throw 1;
            cfg->module=module;const auto thread=CreateThread(nullptr,1024*1024,Worker,cfg.get(),0,nullptr);
            if(!thread)throw 1;
            cfg.release();module=nullptr;CloseHandle(thread);
        }catch(...) {service.control.Finish(*token,GetTickCount64(),[]{},false);if(module)FreeLibrary(module);}
    }catch(...) {}
}
bool Busy() noexcept {auto* service=serviceSlot.load();return service && service->control.Busy();}
std::shared_ptr<const ObservationReportBundle> Completed() noexcept {
    auto* service=serviceSlot.load();return service?service->completed.load(std::memory_order_acquire):nullptr;
}
OwnerContribution CurrentOwner(bool finiteJitter,bool finitePreExposure) noexcept {
    auto* service=serviceSlot.load();auto report=Completed();
    auto owner=service && report && service->control.Context()==report->context?report->owner:OwnerContribution{};
    // Current scalar indicators use the UI's already-sampled latest owner values.
    // Historical report bytes remain immutable and Copy never samples an owner.
    owner.jitter=owner.jitter && finiteJitter;
    owner.preExposure=owner.preExposure && finitePreExposure;
    return owner;
}
void RequestClose() noexcept {if(auto* service=serviceSlot.load())service->control.Close();}
}
