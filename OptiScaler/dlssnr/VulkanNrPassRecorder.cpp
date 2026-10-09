#include "VulkanNrPassRecorder.h"
namespace DlssNr {
VkNrChainResult RecordVkNrPassChain(const VkFrameRequest& frame,const VkNrPassPlan& plan,
    const VkNrReservation& reservation,const VkNrPassCallbacks& callbacks){
    VkNrChainResult result;result.requested=static_cast<uint32_t>(plan.passes.size());
    result.recording.use=frame.use;
    const auto demand=VkNrPassDemand(result.requested);
    if(!demand||!frame.use||reservation.use!=frame.use||reservation.slots.size()<demand||
        !callbacks.record||!callbacks.deliver) {
        result.recording.reason="complete Vulkan chain reservation or callbacks unavailable";return result;
    }
    auto input=frame;uint64_t version=0;VkNrPassStep last;
    // Admit every layer before advancing any existing neural history. One opaque
    // creation per lifecycle-only recording; its owned completion gates evaluation.
    if(callbacks.prepare) {
        NVSDK_NGX_Resource_VK preparedColor{};
        for(uint32_t i=0;i<result.requested;++i) {
            VkNrReservation local{reservation.use,{reservation.slots.begin()+i*8,reservation.slots.begin()+(i+1)*8}};
            auto step=callbacks.prepare(i,input,plan.passes[i],local,0);
            if(step.recording.modelRecorded||step.recording.status!=VkRecordStatus::Complete||(!step.preparedColor&&!step.output)) {
                result.recording=step.recording;
                result.recording.status=step.recording.modelRecorded?VkRecordStatus::Partial:VkRecordStatus::NoWork;
                result.recording.preparationOnly=step.recording.modelRecorded;
                result.recording.outputWriteRecorded=false;
                if(result.recording.reason.empty())result.recording.reason="Vulkan model chain preparation awaits GPU completion";
                return result;
            }
            // Only metadata crosses preparation visits; no pointer into an
            // unretained generation survives the pass owner's control lock.
            preparedColor=step.preparedColor?*step.preparedColor:*step.output;
            input.color=&preparedColor;
        }
        input=frame;
    }
    bool modelRecorded=false;
    for(uint32_t i=0;i<result.requested;++i) {
        VkNrReservation local{reservation.use,{reservation.slots.begin()+i*8,reservation.slots.begin()+(i+1)*8}};
        auto step=callbacks.record(i,input,plan.passes[i],local,version);
        modelRecorded|=step.recording.modelRecorded;
        if(step.recording.status!=VkRecordStatus::Complete||!step.recording.outputWriteRecorded||
            !step.output||step.output==input.color||step.outputVersion<=version) {
            result.recording.reason=step.recording.reason.empty()?"Vulkan model chain output is incomplete":step.recording.reason;break;
        }
        ++result.completed;step.passIndex=i;step.requestedPasses=result.requested;step.completedPasses=result.completed;version=step.outputVersion;input.color=step.output;last=std::move(step);
    }
    result.outputVersion=version;
    if(Multipass::CanDeliver(result.requested,result.completed,plan.requireComplete))
        result.deliverable=callbacks.deliver(last,reservation.slots[demand-2]);
    result.recording.modelRecorded=modelRecorded;
    result.recording.outputWriteRecorded=result.deliverable;
    result.recording.status=result.deliverable?VkRecordStatus::Complete:modelRecorded?VkRecordStatus::Partial:VkRecordStatus::NoWork;
    if(!result.deliverable&&result.recording.reason.empty())result.recording.reason="Vulkan chain final delivery unavailable";
    return result;
}
}
