#pragma once
#include "VulkanNrSession.h"
#include "VulkanNrResolution.h"
#include "VulkanNrFrameParams.h"

namespace DlssNr
{
// CPU scalar observations only. This never retains parameters, wrappers or GPU storage.
struct VkNrNativeObservation
{
    NrPreflightSignals::NativeInputs inputs;
    VkExtent2D render{},output{};
    VkNrFrameContract frame;
    VkNrWorkloadStatus workload;
    VkDevice device=VK_NULL_HANDLE;
    bool rayReconstruction=false;
    // Checks the actual feature registry lifetime; not a provider/GPU ownership promise.
    std::function<bool()> sourceCurrent;
    bool Current(uint64_t inputEpoch) const {
        return device&&frame.evaluation.featureGeneration&&frame.evaluation.invocation&&
            frame.inputInterruptionEpoch==inputEpoch&&sourceCurrent&&sourceCurrent();
    }
};

template<class Settings>
VkNrNativeObservation CaptureVkNrNativeObservation(NVSDK_NGX_Parameter* params,
    const VkNrTemporalMetadata* temporal,const Settings& cfg,bool rr,
    const VkNrEvaluationIdentity& evaluation,VkDevice device,uint64_t inputEpoch,
    std::function<bool()> sourceCurrent)
{
    VkNrNativeObservation result;
    VkFrame::ObserveNativeInputs(result.inputs,params);
    result.render=temporal?VkExtent2D{temporal->color.width,temporal->color.height}:VkExtent2D{};
    NVSDK_NGX_Resource_VK* output=nullptr;VkFrame::Failure failure;
    if(params&&params->Get(NVSDK_NGX_Parameter_Output,reinterpret_cast<void**>(&output))==NVSDK_NGX_Result_Success&&
       VkFrame::ValidateImage("SR.Output",output,true,failure))
        result.output={output->Resource.ImageViewInfo.Width,output->Resource.ImageViewInfo.Height};
    result.device=device;result.rayReconstruction=rr;result.sourceCurrent=std::move(sourceCurrent);
    auto& frame=result.frame;
    frame.evaluation=evaluation;
    if(temporal)frame.temporal=*temporal;
    frame.placement=rr?VkNrPlacement::AfterRR:
        cfg.DlssNrRunBeforeSr.value_or_default()?VkNrPlacement::BeforeSR:VkNrPlacement::AfterSR;
    frame.output=frame.placement==VkNrPlacement::BeforeSR?result.render:result.output;
    frame.inputInterruptionEpoch=inputEpoch;
    frame.resumeGeneration=cfg.GetDlssNrRuntimeSnapshot().resumeGeneration;
    frame.settingsRevision=cfg.ObservationRevision().value_or(0);
    const auto work=ResolveVkNrWorkload(cfg,frame,frame.temporal);
    frame.work={work.width,work.height};
    result.workload.requestedOutput=frame.output;result.workload.requestedWork=frame.work;
    result.workload.policy=VkNrResolutionPolicy(cfg,frame);result.workload.pending=true;
    if(work.reason)result.workload.reason=work.reason;
    return result;
}

// Raw source-key progress remains available while Native NR is off or Present is selected.
// Publication and all reads are under the existing Vulkan state mutex.
struct VkNrNativeRawStatus
{
    std::optional<VkNrNativeObservation> observation;
    uint64_t latestInvocation=0,observations=0,generation=1;
    bool srSucceeded=false;
    bool Publish(const VkNrNativeObservation& value,bool succeeded,uint64_t inputEpoch) {
        if(!value.Current(inputEpoch)||value.frame.evaluation.invocation<=latestInvocation)return false;
        if(!observation||observation->device!=value.device||
           observation->frame.evaluation.featureGeneration!=value.frame.evaluation.featureGeneration||
           observation->frame.inputInterruptionEpoch!=value.frame.inputInterruptionEpoch) {
            ++generation;observations=0;
        }
        observation=value;latestInvocation=value.frame.evaluation.invocation;++observations;srSucceeded=succeeded;
        return true;
    }
    bool Current(uint64_t inputEpoch) const {return observation&&observation->Current(inputEpoch);}
    void InvalidateDevice(VkDevice device) {
        if(observation&&observation->device==device){observation.reset();observations=0;++generation;}
    }
};

template<class Settings> struct VkNrNativeSelectedStatus
{
    std::optional<VkNrNativeObservation> observation;
    std::optional<Settings> settings;
    std::optional<VkNrNativeRecordingReceipt> recording;
    VkNrWorkloadStatus workload;
    uint64_t observations=0,latestInvocation=0;
    bool accepted=false;
    uint32_t completedPasses=0;
    bool Publish(const VkNrNativeObservation& value,const Settings& cfg,const VkRecordResult& result,
                 bool before,bool bound,bool succeeded,bool selected,uint64_t inputEpoch) {
        if(!cfg.GetDlssNrRuntimeSnapshot().enabled||cfg.DlssNrRoute.value_or_default()!=0||
           cfg.IsPrivateDiagnostic()||(!selected&&succeeded)||!value.Current(inputEpoch)||
           value.frame.evaluation.invocation<=latestInvocation)return false;
        observation=value;settings=cfg;recording=result.nativeRecording;workload=value.workload;
        latestInvocation=value.frame.evaluation.invocation;++observations;completedPasses=result.completedPasses;
        const bool paired=recording&&recording->use&&recording->appliedGeneration&&
            recording->frame.route==VkNrRoute::Native&&
            recording->frame.evaluation==value.frame.evaluation&&
            recording->frame.placement==value.frame.placement&&
            recording->frame.resumeGeneration==value.frame.resumeGeneration&&
            recording->frame.inputInterruptionEpoch==value.frame.inputInterruptionEpoch;
        accepted=paired&&workload.reason.empty()&&VkNrNativeDeliveryAccepted(result,before,bound,succeeded);
        if(recording&&paired) {
            workload.appliedOutput=recording->frame.output;workload.appliedWork=recording->frame.work;
            workload.appliedGeneration=recording->appliedGeneration;
        }
        if(!succeeded)workload.reason="Selected Vulkan SR evaluation failed";
        else if(!result.reason.empty())workload.reason=result.reason;
        else if(before&&!bound)workload.reason="Native Performance carrier is not ready for the selected SR call";
        else if(!accepted&&workload.reason.empty())workload.reason="Waiting for a qualified Vulkan NR recording";
        workload.pending=!accepted;
        return true;
    }
    bool Matches(const Settings& cfg,uint64_t deviceGeneration,uint64_t routeEpoch,
                 uint64_t appliedGeneration,uint64_t inputEpoch) const {
        return accepted&&observation&&observation->Current(inputEpoch)&&settings&&settings->SameConfiguration(cfg)&&
            recording&&recording->frame.deviceGeneration&&recording->frame.deviceGeneration==deviceGeneration&&
            recording->frame.routeEpoch==routeEpoch&&recording->appliedGeneration==appliedGeneration&&
            workload.appliedWork.width==workload.requestedWork.width&&
            workload.appliedWork.height==workload.requestedWork.height;
    }
};
}
