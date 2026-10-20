#include "dlssnr/VulkanPresentStatus.h"

#include <cassert>
#include <string>

using namespace DlssNr;
// The status fixture controls the coherent atomic-policy snapshot; no production
// ConnectionPolicy owner or external device/model is created.
static Connections::Mixed::Policy sourcePolicy;
static std::optional<VkNrPresentModelLifetime> liveModel;
namespace DlssNr {std::optional<VkNrPresentModelLifetime> CurrentVulkanPresentModelLifetimeVk(){return liveModel;}}
namespace DlssNr::Connections { Mixed::Policy SignalPreferences() noexcept { return sourcePolicy; } }

int main()
{
    {
        VulkanPresentStatus sources;
        sourcePolicy.revision=9;
        VkPresentSources::Context context{11,12,13,1,2,3,2,true};
        liveModel=VkNrPresentModelLifetime{11,1,3,0,0};context.modelLifetime=liveModel;
        sources.ObserveSourcePolicy(sourcePolicy,context,20);
        VkPresentSources::Receipt receipt;
        receipt.context=context;receipt.ticket=7;receipt.acquisition=4;receipt.image=14;receipt.imageIndex=0;
        receipt.signals=PresentSources::Choose(sourcePolicy,true).receipt;
        receipt.signals.capture=20;receipt.signals.generation=2;receipt.completeTarget=true;
        assert(sources.SourceCompleted(receipt,9));
        auto used=sources.Snapshot().sources;
        assert(used.usedFresh&&used.used.consumed&&used.used.capture==20);
        assert(!sources.SourceCompleted(receipt,9)); // exact completion is idempotent
        auto stale=receipt;stale.signals.capture=19;
        assert(!sources.SourceCompleted(stale,9));
        auto newer=receipt;newer.signals.capture=21;newer.completeTarget=false;
        assert(!sources.SourceCompleted(newer,9)); // preparation/partial/no target
        newer.completeTarget=true;newer.context.deviceGeneration++;
        assert(!sources.SourceCompleted(newer,9));
        newer=receipt;newer.signals.capture=21;newer.acquisition=0;
        assert(!sources.SourceCompleted(newer,9));
        newer=receipt;newer.signals.capture=21;newer.signals.generation=3;
        assert(!sources.SourceCompleted(newer,9)); // carrier/receipt generation mismatch
        sourcePolicy.revision=10;
        assert(!sources.Snapshot().sources.usedFresh); // immediate policy invalidation
        assert(sources.Snapshot().requestedSources.revision==10);
        assert(!sources.SourceCompleted(receipt,10));
        sources.ObserveSourcePolicy(sourcePolicy,context);
        assert(!sources.Snapshot().sources.usedFresh&&!sources.Snapshot().sources.used.capture);
        newer=receipt;newer.signals.revision=10;newer.signals.capture=22;
        assert(sources.SourceCompleted(newer,10));
        sources.InvalidateSources();
        assert(!sources.Snapshot().sources.usedFresh&&!sources.SourceCompleted(newer,10));
        sources.ObserveSourcePolicy(sourcePolicy,context,23);
        assert(!sources.SourceCompleted(newer,10)); // old pending completion after same-context reentry
        newer.signals.capture=23;
        assert(sources.SourceCompleted(newer,10));
    }
    {
        // VK003: selected refusal changes identity even without executor entry.
        sourcePolicy.revision=40;liveModel=VkNrPresentModelLifetime{11,1,3,0,0};
        VulkanPresentStatus s;VkPresentSources::Context c{11,12,13,1,2,3,2,true};c.modelLifetime=liveModel;
        s.ObserveSourcePolicy(sourcePolicy,c,30);VkPresentExecution a;
        a.sources.context=c;a.sources.ticket=7;a.sources.acquisition=4;a.sources.image=14;a.sources.imageIndex=0;
        a.sources.signals=PresentSources::Choose(sourcePolicy,true).receipt;
        a.sources.signals.capture=30;a.sources.signals.generation=2;a.sources.completeTarget=true;
        VkObservedPresent o;o.applicationFacing=true;o.device=reinterpret_cast<VkDevice>(11);o.queue=reinterpret_cast<VkQueue>(12);
        o.swapchain=reinterpret_cast<VkSwapchainKHR>(13);o.request.deviceGeneration=1;o.request.swapchainGeneration=2;o.request.route=2;
        s.Observe(&o,&a,true,VK_SUCCESS,"");assert(s.SourceCompleted(a.sources,40));
        assert(s.Snapshot().sources.fresh&&s.Snapshot().sources.usedFresh);
        // A generated child with different physical carrier must not own app history.
        auto child=o;child.applicationFacing=false;child.swapchain=reinterpret_cast<VkSwapchainKHR>(99);
        child.request.fgStateKnown=true;child.request.fgKnownActive=true;VkPresentExecution empty;
        s.Observe(&child,&empty,true,VK_SUCCESS,"generated child bypass");
        assert(s.Snapshot().sources.fresh&&s.Snapshot().sources.usedFresh);
        auto b=o;b.swapchain=reinterpret_cast<VkSwapchainKHR>(15);b.request.swapchainGeneration=8;
        s.Observe(&b,&empty,true,VK_SUCCESS,"proxy image unavailable before executor");
        assert(!s.Snapshot().sources.fresh&&!s.Snapshot().sources.usedFresh);
        auto delayed=a.sources;delayed.signals.capture=31;assert(!s.SourceCompleted(delayed,40));
        // Null execution uses the same invalidation rule.
        s.ObserveSourcePolicy(sourcePolicy,c,32);a.sources.signals.capture=32;s.Observe(&o,&a,true,VK_SUCCESS,"");
        assert(s.SourceCompleted(a.sources,40));s.Observe(&b,nullptr,true,VK_SUCCESS,"missing public tag");
        assert(!s.Snapshot().sources.fresh&&!s.Snapshot().sources.usedFresh&&!s.SourceCompleted(delayed,40));
        // Current lifetime changes independently of carrier/policy; no new Present is needed.
        s.ObserveSourcePolicy(sourcePolicy,c,33);a.sources.signals.capture=33;s.Observe(&o,&a,true,VK_SUCCESS,"");
        assert(s.SourceCompleted(a.sources,40));++liveModel->routeEpoch;
        assert(!s.Snapshot().sources.fresh&&!s.Snapshot().sources.usedFresh);delayed=a.sources;delayed.signals.capture=34;
        assert(!s.SourceCompleted(delayed,40));liveModel.reset();assert(!s.SourceCompleted(delayed,40));
    }
    VkPresentLogGate log;
    assert(log.ShouldLog(1, 7, 2, true, VK_SUCCESS, "queue ownership unavailable"));
    for (int i = 0; i < 1000; ++i)
        assert(!log.ShouldLog(1, 7, 2, true, VK_SUCCESS, "queue ownership unavailable"));
    assert(log.ShouldLog(1, 7, 3, true, VK_SUCCESS, "queue ownership unavailable"));
    assert(log.ShouldLog(1, 7, 3, true, VK_SUCCESS, "recording incomplete"));
    assert(log.ShouldLog(1, 7, 3, true, VK_ERROR_DEVICE_LOST, "recording incomplete"));
    assert(log.ShouldLog(2, 7, 2, true, VK_SUCCESS, "queue ownership unavailable"));
    assert(!log.ShouldLog(1, 7, 2, true, VK_SUCCESS, "queue ownership unavailable"));
    assert(log.ShouldLog(1, 8, 2, true, VK_SUCCESS, "queue ownership unavailable"));
    assert(log.ShouldLog(1, 8, 2, false, VK_SUCCESS, "queue ownership unavailable"));
    // A successful original Present remains observable without an NR submission.
    VulkanPresentStatus gameOnly;
    gameOnly.ObserveGamePresent(true,VK_SUCCESS);
    assert(gameOnly.Snapshot().gamePresents==1);
    assert(gameOnly.Snapshot().originalAccepted==0);
    gameOnly.ObserveGamePresent(false,VK_SUCCESS);
    gameOnly.ObserveGamePresent(true,VK_ERROR_DEVICE_LOST);
    assert(gameOnly.Snapshot().gamePresents==1);
    VulkanPresentStatus status;
    assert(status.PhysicalReports(7));
    status.ApplicationProvider(7);
    assert(!status.PhysicalReports(7)&&status.PhysicalReports(8)&&status.PhysicalReports(0));
    status.ApplicationProvider(0);assert(status.PhysicalReports(7));
    VkNrWorkloadStatus workload;
    workload.requestedOutput={3840,2160};workload.requestedWork={7680,4320};
    workload.appliedOutput={3840,2160};workload.appliedWork={1920,1080};
    workload.policy={PresentResolution::Manual,200};workload.pending=true;workload.reason="old recording still executable";
    workload.appliedGeneration=9;status.ObserveWorkload(workload);
    auto raster=status.Snapshot().workload;
    assert(raster.requestedWork.width==7680&&raster.appliedWork.width==1920&&raster.pending&&raster.appliedGeneration==9);
    VkObservedPresent observed {};
    observed.request.route = 2;
    observed.request.policy = PresentInput::Policy::AutoGuides;
    observed.request.format = VK_FORMAT_B8G8R8A8_SRGB;
    observed.request.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    observed.request.extent = { 1920, 1080 };
    VkPresentExecution accepted {};
    accepted.admission = { true, PresentInputDecision::InputClass::ImageOnly,
                           VkPresentRefusal::AutoImageOnly, false };
    accepted.record = { VkRecordStatus::Complete, "", true, true };
    accepted.handoff = { VkPresentHandoffState::Accepted, reinterpret_cast<VkSemaphore>(1), VK_SUCCESS };
    accepted.ticket = 7;
    status.Observe(&observed, &accepted, true, VK_SUCCESS, "");
    auto s = status.Snapshot();
    assert(s.requested && s.active && !s.failed && s.attempts == 1);
    assert(s.actualInputClass == PresentInputDecision::InputClass::ImageOnly);
    assert(s.requestedPolicy == PresentInput::Policy::AutoGuides);
    assert(s.format == VK_FORMAT_B8G8R8A8_SRGB && s.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    assert(s.submitted == 1 && s.completed == 0 && s.originalAccepted == 1);
    assert(s.reason.find("image-only") != std::string::npos);
    status.GpuCompleted();
    assert(status.Snapshot().completed == 1);
    {
        VulkanPresentStatus capturedStatus;auto completedCapture=accepted;
        completedCapture.capturedCompletionObserved=true;
        capturedStatus.Observe(&observed,&completedCapture,true,VK_SUCCESS,"");
        assert(capturedStatus.Snapshot().submitted==1&&capturedStatus.Snapshot().completed==1);
        completedCapture.ticket++;
        capturedStatus.Observe(&observed,&completedCapture,true,VK_SUCCESS,"");
        assert(capturedStatus.Snapshot().submitted==2&&capturedStatus.Snapshot().completed==2);
    }

    observed.request.policy = PresentInput::Policy::RequireGuides;
    VkPresentExecution refused {};
    refused.admission = { false, PresentInputDecision::InputClass::Refused,
                          VkPresentRefusal::GuidesRequired, false };
    refused.reason = VkPresentRefusalText(refused.admission.reason);
    status.Observe(&observed, &refused, true, VK_SUCCESS, refused.reason.c_str());
    s = status.Snapshot();
    assert(!s.active && s.failed && s.attempts == 2 && s.submitted == 1);
    assert(s.reason.find("Required Vulkan guides") != std::string::npos);

    refused.admission.reason = VkPresentRefusal::TransferNotEnabled;
    refused.admission.needsRecreate = true;
    refused.reason = VkPresentRefusalText(refused.admission.reason);
    status.Observe(&observed, &refused, true, VK_SUCCESS, refused.reason.c_str());
    s = status.Snapshot();
    assert(s.needsRecreate && s.reason.find("Recreate swapchain") != std::string::npos);

    observed.request.policy = PresentInput::Policy::ImageOnly;
    status.Observe(&observed, &accepted, true, VK_ERROR_DEVICE_LOST, "original Present failed");
    s = status.Snapshot();
    assert(!s.active && s.failed && s.possibleTargetWrite);
    assert(s.submitted == 2 && s.originalAccepted == 1);
    assert(s.completed == 1 && s.uncertain == 1);
    VkNrRecordingOwner recordings;auto device=reinterpret_cast<VkDevice>(81);auto cb=reinterpret_cast<VkCommandBuffer>(82);
    recordings.OnAllocate(device,reinterpret_cast<VkCommandPool>(83),cb,0);recordings.OnBegin(cb,0,VK_SUCCESS);auto use=*recordings.Reserve(cb,1);
    VkNrGpuStageSample stages;stages.use=use;stages.frame.route=VkNrRoute::Present;stages.frame.swapchainGeneration=s.generation;stages.milliseconds={1,2,3,4};
    assert(!status.ObserveGpuStages(recordings,stages));
    auto submission=*recordings.PrepareSubmission({reinterpret_cast<VkQueue>(84),1,{use}});recordings.SubmissionReturned(submission,VK_SUCCESS);recordings.CheckpointReturned(submission,VK_SUCCESS);
    assert(!status.ObserveGpuStages(recordings,stages));recordings.ObserveFence(submission,VK_SUCCESS);
    assert(status.ObserveGpuStages(recordings,stages));assert(status.Snapshot().gpuStages[0].use==use);
    for(unsigned i=1;i<=40;++i){auto token=status.BeginCall(1,double(i*16));status.FinishCall(token,1,2,3,false);}
    assert(status.Snapshot().pacing.frameInterval.samples==8);
    assert(status.Snapshot().pacing.originalPresentCpu.average==3);
    accepted.record.preparationOnly=true;accepted.record.targetWriteRecorded=false;
    status.Observe(&observed,&accepted,true,VK_SUCCESS,"Vulkan model chain preparation awaits GPU completion");
    s=status.Snapshot();
    assert(!s.active&&!s.failed&&!s.possibleTargetWrite&&s.reason.find("preparation")!=std::string::npos);
    return 0;
}
