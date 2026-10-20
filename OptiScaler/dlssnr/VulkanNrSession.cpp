#include "VulkanNrSession.h"
#include "VulkanNrFlightRecorder.h"
#include "VulkanNrRuntime.h"
#include "VulkanNrCompletion.h"
#include <algorithm>

namespace DlssNr
{
VkNrRecordingOwner& VulkanNrSession::Recordings() { return runtime_.Recordings(); }

VulkanNrSession::VulkanNrSession(VkNrRoute route, VulkanNrRuntime& runtime) : route_(route), runtime_(runtime) {}

VkRecordResult VulkanNrSession::Record(const VkFrameRequest& request, const Recorder& recorder)
{
    PollCompletions();
    // Mark the selected attempt discontinuous until the whole recording succeeds.
    // This also covers early refusal and exceptions; completion of older work
    // cannot acknowledge a later gap.
    const bool needsReset=resetState_.Pending() || observedEpoch_!=runtime_.Epoch() || !runtime_.IsActive(route_);
    resetState_.MarkPending();
    const auto resetRevision=resetState_.Capture();
    VkFrameRequest frame = request;
    if(!frame.waitBudget)frame.waitBudget=std::make_shared<VkNrWaitBudget>();
    if (frame.commandBuffer) {
        if(const char* reason=Recordings().ModelRecordingReason(frame.commandBuffer)) {
            VkFlight::Current().Write(VkFlight::Kind::Refusal,Recordings().Incarnation(frame.commandBuffer),
                frame.contract.evaluation.invocation,reinterpret_cast<uintptr_t>(frame.commandBuffer));
            return {VkRecordStatus::NoWork,reason};
        }
        if(!Recordings().BindingsQualified(frame.commandBuffer))return {VkRecordStatus::NoWork,"Vulkan shader-object or descriptor-buffer binding restoration is unavailable"};
        const auto deviceGeneration=VkNrCompletionDeviceGeneration(frame.device);
        frame.contract.deviceGeneration=deviceGeneration?deviceGeneration:frame.contract.deviceGeneration;
        const auto use = Recordings().ReserveModel(frame.commandBuffer,frame.contract.deviceGeneration);
        if (!use || !ReserveVkNrCompletion(frame.device,*use)) {
            if (use) Recordings().CancelBeforeRecording(*use);
            return {VkRecordStatus::NoWork,"Vulkan recording or owned completion capacity unavailable"};
        }
        frame.use = *use;
        VkFlight::Current().Write(VkFlight::Kind::Record,Recordings().Incarnation(frame.commandBuffer),
            frame.contract.evaluation.invocation,reinterpret_cast<uintptr_t>(frame.commandBuffer),use->value,
            reinterpret_cast<uintptr_t>(frame.contract.queue),Recordings().UseFamily(*use));
    }
    if (observedEpoch_ != runtime_.Epoch() || !runtime_.IsActive(route_))
    {
        resetState_.MarkPending();
    }
    if(frame.bindGuides&&!frame.bindGuides(frame.use))
        return {VkRecordStatus::NoWork,"fresh guide consumer binding failed; no image-only retry"};
    auto result = recorder(frame, needsReset || frame.creationReset);
    result.use = frame.use;
    if (result.status == VkRecordStatus::Complete && (!result.modelRecorded || !result.outputWriteRecorded))
    {
        result.status = VkRecordStatus::Partial;
        result.reason = "incomplete model/output recording";
    }
    // An unselected route still receives game callbacks. NoWork must not switch
    // the active route or reset the other route's temporal history.
    if (result.modelRecorded) runtime_.Activate(route_);
    // Activate/initial initialization may advance the actual owner epoch. Export
    // that resulting identity, retaining the exact recording's resume/input facts.
    if(result.modelLifetime){
        if(route_!=VkNrRoute::Present||!result.modelRecorded||!runtime_.Initialized()||
           runtime_.Device()!=frame.device||!runtime_.IsActive(route_))result.modelLifetime.reset();
        else result.modelLifetime->routeEpoch=runtime_.Epoch();
    }
    if(result.nativeRecording){
        if(route_!=VkNrRoute::Native||!result.modelRecorded||!runtime_.Initialized()||
           runtime_.Device()!=frame.device||!runtime_.IsActive(route_))result.nativeRecording.reset();
        else result.nativeRecording->frame.routeEpoch=runtime_.Epoch();
    }
    if (result.status == VkRecordStatus::Complete)
    {
        ++recorded_;
        observedEpoch_ = runtime_.Epoch();
        resetState_.Acknowledge(resetRevision);
        if (frame.use && Recordings().RetainUse(frame.use)) pendingCompletion_.push_back({frame.use,{}});
    }
    return result;
}

void VulkanNrSession::PollCompletions()
{
    PollVkNrCompletions();
    for (auto use = pendingCompletion_.begin(); use != pendingCompletion_.end();) {
        if (Recordings().GpuComplete(use->use)) { ObserveCompleted(); Recordings().ReleaseUse(use->use); use = pendingCompletion_.erase(use); }
        // Reset releases the recording reference before exact abandonment can
        // reacquire Session authority. Keep the existing observer/membership so
        // another Record cannot prune that still-owned discard transaction.
        else if (!use->discard && Recordings().Reusable(use->use)) { RequestReset(); Recordings().ReleaseUse(use->use); use = pendingCompletion_.erase(use); }
        else ++use;
    }
}

void VulkanNrSession::ObserveCompleted(unsigned long long count)
{
    const auto available = recorded_ - completed_;
    completed_ += count < available ? count : available;
}

void VulkanNrSession::AbandonLatestRecording(bool currentCounted)
{
    if (currentCounted && recorded_ > completed_) {
        // A reserved entry can only leave through exact reset proof or genuine
        // GPU completion, never a legacy latest-entry pop.
        if (!pendingCompletion_.empty() && pendingCompletion_.back().discard) { RequestReset(); return; }
        --recorded_;
        if (!pendingCompletion_.empty()) { Recordings().ReleaseUse(pendingCompletion_.back().use); pendingCompletion_.pop_back(); }
    }
    RequestReset();
}

static bool SameDiscardRecording(const VkNrRecordingOwner::UseRecordingSnapshot& a,
                                 const VkNrRecordingOwner::UseRecordingSnapshot& b)
{
    return a.use==b.use&&a.device==b.device&&a.buffer==b.buffer&&a.pool==b.pool&&
        a.generation==b.generation&&a.incarnation==b.incarnation;
}

bool VulkanNrSession::BeginAbandonRecording(VkNrUseId use,const VkNrRecordingOwner::UseRecordingSnapshot& beforeReset)
{
    const auto found=std::find_if(pendingCompletion_.begin(),pendingCompletion_.end(),
        [=](const auto& entry){return entry.use==use;});
    if(!use||beforeReset.use!=use||!beforeReset.UnsubmittedRecording()||
       found==pendingCompletion_.end()||found->discard||recorded_<=completed_)return false;
    const auto current=Recordings().InspectUseRecording(use);
    if(!current||!current->UnsubmittedRecording()||!SameDiscardRecording(*current,beforeReset))return false;
    found->discard=*current;
    return true;
}

bool VulkanNrSession::AbandonRecording(VkNrUseId use,const VkNrRecordingOwner::UseRecordingSnapshot& beforeReset)
{
    const auto found=std::find_if(pendingCompletion_.begin(),pendingCompletion_.end(),
        [=](const auto& entry){return entry.use==use;});
    if(!use||beforeReset.use!=use||!beforeReset.UnsubmittedRecording()||found==pendingCompletion_.end()||
       !found->discard||!SameDiscardRecording(*found->discard,beforeReset)||recorded_<=completed_)return false;
    const auto after=Recordings().InspectUseRecording(use);
    if(!after||!after->DiscardedSameUse(*found->discard))return false;
    Recordings().ReleaseUse(use);
    pendingCompletion_.erase(found);--recorded_;RequestReset();return true;
}

} // namespace DlssNr
