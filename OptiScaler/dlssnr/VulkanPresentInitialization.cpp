#include "VulkanPresentInitialization.h"
#include "VulkanNrCompletion.h"

namespace DlssNr
{
namespace
{
double Milliseconds(VkNrWaitBudget::Clock::time_point start)
{ return std::chrono::duration<double,std::milli>(VkNrWaitBudget::Clock::now()-start).count(); }
}

bool VkPresentInitializationOwner::Initialize(const VkPresentInitializationRequest& request)
{
    if (pool_) return device_ == request.device && queue_ == request.frame.queue &&
        family_ == request.frame.queueFamily && deviceGeneration_ == request.frame.deviceGeneration;
    if (!request.device || !request.frame.queue || request.frame.queueFamily == UINT32_MAX ||
        !request.frame.deviceGeneration || VkNrCompletionDeviceGeneration(request.device) != request.frame.deviceGeneration)
        return false;
    device_ = request.device; queue_ = request.frame.queue; family_ = request.frame.queueFamily;
    deviceGeneration_ = request.frame.deviceGeneration;
    VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; info.queueFamilyIndex = family_;
    {
        VkNrObservationScope scope(VkNrObservation::CreatePool,reinterpret_cast<uintptr_t>(&info),device_);
        const auto result = vkCreateCommandPool(device_,&info,nullptr,&pool_);
        if (scope.Observe()) VulkanNrRecordings().OnCreatePool(device_,pool_,info,result);
        if (result != VK_SUCCESS) return false;
    }
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = pool_; allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount = 1;
    VkNrObservationScope scope(VkNrObservation::Allocate,reinterpret_cast<uintptr_t>(&allocation),device_);
    const auto result = vkAllocateCommandBuffers(device_,&allocation,&command_);
    if (scope.Observe()) VulkanNrRecordings().OnAllocateBuffers(device_,allocation,&command_,result);
    if (result != VK_SUCCESS) { Release(true,{}); return false; }
    return true;
}

void VkPresentInitializationOwner::Maintain(const VkPresentPollModels& poll)
{
    PollVkNrCompletions();
    if (poll) poll(); // Latch generation initialization proof before releasing our observation.
    if (use_ && submitted_ && VulkanNrRecordings().GpuComplete(use_)) {
        VulkanNrRecordings().ReleaseUse(use_); use_ = {}; submitted_ = false;
    }
}

bool VkPresentInitializationOwner::Cancel(const VkPresentPollModels& poll)
{
    if (submitted_ || uncertain_) return false;
    VkNrObservationScope scope(VkNrObservation::ResetPool,reinterpret_cast<uintptr_t>(pool_),device_);
    const auto result = vkResetCommandPool(device_,pool_,0);
    if (scope.Observe()) VulkanNrRecordings().OnResetPool(device_,pool_,result);
    if (result != VK_SUCCESS) { uncertain_ = true; return false; }
    if (poll) poll(); // A canceled creation must be poisoned, never called completed.
    if (use_) VulkanNrRecordings().ReleaseUse(use_);
    use_ = {}; return true;
}

VkPresentInitializationResult VkPresentInitializationOwner::Prepare(const VkPresentInitializationRequest& request,
    const std::function<bool()>& current, const VkPresentPrepareModels& prepare, const VkPresentPollModels& poll)
{
    VkPresentInitializationResult result;
    const auto stop = [&](VkPresentInitializationStatus status, const char* reason) {
        result.status = status; result.reason = reason; return result;
    };
    if (!request.waitBudget || !current || !prepare || !current())
        return stop(VkPresentInitializationStatus::Refused,"Vulkan initialization frame is no longer current");
    Maintain(poll);
    if (uncertain_) return stop(VkPresentInitializationStatus::Uncertain,"Vulkan initialization ownership is uncertain");
    if (use_) {
        const auto remaining = request.waitBudget->RemainingNs();
        if (!remaining) return stop(VkPresentInitializationStatus::Pending,"Vulkan initialization deadline exhausted");
        const auto start = VkNrWaitBudget::Clock::now();
        const auto waited = WaitVkNrUse(use_,remaining); result.waitMs += Milliseconds(start);
        Maintain(poll);
        if (waited != VK_SUCCESS && waited != VK_TIMEOUT) {
            uncertain_ = true; return stop(VkPresentInitializationStatus::Uncertain,"Vulkan initialization wait failed");
        }
        if (use_) return stop(VkPresentInitializationStatus::Pending,"Vulkan initialization awaits owned completion");
    }
    if (!current()) return stop(VkPresentInitializationStatus::Refused,"Vulkan initialization frame changed during completion");
    if (pool_ && (device_ != request.device || queue_ != request.frame.queue ||
        family_ != request.frame.queueFamily || deviceGeneration_ != request.frame.deviceGeneration)) {
        if (!Release(true,poll)) return stop(VkPresentInitializationStatus::Pending,"Previous Vulkan initializer is still owned");
    }
    if (!Initialize(request)) return stop(VkPresentInitializationStatus::Refused,"Private Vulkan initialization command allocation failed");
    // At most ten new creations, then one final all-ready observation. No deadline renewal.
    for (unsigned attempt = 0; attempt <= 10; ++attempt) {
        if (!current()) return stop(VkPresentInitializationStatus::Refused,"Vulkan initialization frame changed");
        if (!request.waitBudget->RemainingNs())
            return stop(VkPresentInitializationStatus::Pending,"Vulkan initialization deadline exhausted");
        if (!Cancel(poll)) return stop(VkPresentInitializationStatus::Uncertain,"Vulkan initialization reset failed");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult begun;
        {
            VkNrObservationScope scope(VkNrObservation::Begin,reinterpret_cast<uintptr_t>(command_));
            begun = vkBeginCommandBuffer(command_,&begin);
            if (scope.Observe()) VulkanNrRecordings().OnBegin(command_,begin.flags,begun);
        }
        if (begun != VK_SUCCESS) return stop(VkPresentInitializationStatus::Refused,"Vulkan initialization begin failed");
        const auto reserved = VulkanNrRecordings().ReserveModel(command_,deviceGeneration_);
        if (!reserved || !VulkanNrRecordings().RetainUse(*reserved)) {
            if (!Cancel(poll)) return stop(VkPresentInitializationStatus::Uncertain,"Vulkan initialization reservation cancellation failed");
            return stop(VkPresentInitializationStatus::Refused,"Vulkan initialization use unavailable");
        }
        use_ = *reserved;
        if (!ReserveVkNrCompletion(device_,use_)) {
            if (!Cancel(poll)) return stop(VkPresentInitializationStatus::Uncertain,"Vulkan initialization completion cancellation failed");
            return stop(VkPresentInitializationStatus::Refused,"Vulkan initialization completion unavailable");
        }
        const auto start = VkNrWaitBudget::Clock::now();
        const auto step = prepare(request,command_,use_); result.creationMs += Milliseconds(start);
        if (!current() ||
            step.status != VkPresentInitializationStatus::Recorded || !step.recorded || attempt == 10) {
            const bool stillValid = current();
            const bool budget = request.waitBudget->RemainingNs() != 0;
            if (!Cancel(poll)) return stop(VkPresentInitializationStatus::Uncertain,"Vulkan initialization cancellation failed");
            if (!stillValid) return stop(VkPresentInitializationStatus::Refused,"Vulkan initialization frame changed during preparation");
            if (!budget) return stop(VkPresentInitializationStatus::Pending,"Vulkan initialization CPU preparation exceeded deadline");
            if (step.status == VkPresentInitializationStatus::Ready && !step.recorded)
                return stop(VkPresentInitializationStatus::Ready,"");
            result.status = step.status == VkPresentInitializationStatus::Recorded ?
                VkPresentInitializationStatus::Refused : step.status;
            result.reason = step.reason.empty() ? "Vulkan initialization did not produce a ready chain" : step.reason;
            return result;
        }
        VkResult ended;
        {
            VkNrObservationScope scope(VkNrObservation::End,reinterpret_cast<uintptr_t>(command_));
            ended = vkEndCommandBuffer(command_);
            if (scope.Observe()) VulkanNrRecordings().OnEnd(command_,ended);
        }
        if (ended != VK_SUCCESS) {
            if (!Cancel(poll)) return stop(VkPresentInitializationStatus::Uncertain,"Vulkan initialization end/cancel failed");
            return stop(VkPresentInitializationStatus::Refused,"Vulkan initialization end failed");
        }
        if (!current()) {
            if (!Cancel(poll)) return stop(VkPresentInitializationStatus::Uncertain,"Vulkan initialization stale cancellation failed");
            return stop(VkPresentInitializationStatus::Refused,"Vulkan initialization frame changed before submit");
        }
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command_;
        VkResult submitted;
        {
            VkNrObservationScope scope(VkNrObservation::Submit,reinterpret_cast<uintptr_t>(queue_));
            submitted = ObserveVkNrQueueSubmit(queue_,std::span<const VkCommandBuffer>(&command_,1),
                [&] { return vkQueueSubmit(queue_,1,&submit,VK_NULL_HANDLE); });
        }
        ++result.submissions;
        if (submitted != VK_SUCCESS) {
            // Keep the actual recorded/model owner; do not invent a rejected-submission rollback.
            submitted_ = true; uncertain_ = true;
            return stop(VkPresentInitializationStatus::Uncertain,"Vulkan initialization submission failed");
        }
        submitted_ = true;
        const auto remaining = request.waitBudget->RemainingNs();
        const auto waitStart = VkNrWaitBudget::Clock::now();
        const auto waited = WaitVkNrUse(use_,remaining); result.waitMs += Milliseconds(waitStart);
        Maintain(poll);
        if (waited != VK_SUCCESS && waited != VK_TIMEOUT) {
            uncertain_ = true; return stop(VkPresentInitializationStatus::Uncertain,"Vulkan initialization completion failed");
        }
        if (use_) return stop(VkPresentInitializationStatus::Pending,"Vulkan initialization awaits owned completion");
    }
    return stop(VkPresentInitializationStatus::Refused,"Vulkan initialization pass limit reached");
}

bool VkPresentInitializationOwner::Release(bool alive,const VkPresentPollModels& poll)
{
    if (alive) {
        Maintain(poll);
        if (Busy()) return false;
        if (pool_) {
            VkNrObservationScope scope(VkNrObservation::DestroyPool,reinterpret_cast<uintptr_t>(pool_),device_);
            vkDestroyCommandPool(device_,pool_,nullptr);
            if (scope.Observe()) VulkanNrRecordings().OnDestroyPool(device_,pool_);
        }
    } else if (use_) VulkanNrRecordings().ReleaseUse(use_);
    device_ = VK_NULL_HANDLE; queue_ = VK_NULL_HANDLE; family_ = UINT32_MAX; deviceGeneration_ = 0;
    pool_ = VK_NULL_HANDLE; command_ = VK_NULL_HANDLE; use_ = {}; submitted_ = uncertain_ = false;
    return true;
}
void VkPresentInitializationOwner::DeviceDestroyed(VkDevice device)
{ if (device_ == device) Release(false,{}); }
}
