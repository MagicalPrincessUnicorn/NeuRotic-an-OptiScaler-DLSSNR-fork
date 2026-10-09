#include "VulkanNrSession.h"
#include "VulkanNrFlightRecorder.h"
#include "VulkanNrRuntime.h"
#include "VulkanNrCompletion.h"

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
    const bool needsReset=resetPending_ || observedEpoch_!=runtime_.Epoch() || !runtime_.IsActive(route_);
    resetPending_=true;
    const auto resetRevision=resetRevision_;
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
        resetPending_ = true;
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
    if (result.status == VkRecordStatus::Complete)
    {
        ++recorded_;
        observedEpoch_ = runtime_.Epoch();
        resetPending_ = resetRevision_ != resetRevision;
        if (frame.use && Recordings().RetainUse(frame.use)) pendingCompletion_.push_back(frame.use);
    }
    return result;
}

void VulkanNrSession::PollCompletions()
{
    PollVkNrCompletions();
    for (auto use = pendingCompletion_.begin(); use != pendingCompletion_.end();) {
        if (Recordings().GpuComplete(*use)) { ObserveCompleted(); Recordings().ReleaseUse(*use); use = pendingCompletion_.erase(use); }
        else if (Recordings().Reusable(*use)) { RequestReset(); Recordings().ReleaseUse(*use); use = pendingCompletion_.erase(use); }
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
        --recorded_;
        if (!pendingCompletion_.empty()) { Recordings().ReleaseUse(pendingCompletion_.back()); pendingCompletion_.pop_back(); }
    }
    RequestReset();
}

} // namespace DlssNr
