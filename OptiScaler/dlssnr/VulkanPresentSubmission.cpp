#include "VulkanPresentSubmission.h"
#include "VulkanNrFlightRecorder.h"

#include <vector>

namespace DlssNr
{

VkResult PollOrWaitVkPresentFence(VkDevice device, VkFence fence,
    PFN_vkGetFenceStatus poll, PFN_vkWaitForFences wait, uint64_t timeoutNs)
{
    const auto result = poll(device, fence);
    // Share the caller's remaining allowance with acquire and model waits.
    // A zero allowance still polls; timeout never grants resource reuse.
    return result == VK_NOT_READY ?
        VkFlight::Call(VkFlight::CallSite::PresentCompositionWait,reinterpret_cast<uintptr_t>(fence),
            [&]{return wait(device, 1, &fence, VK_TRUE, timeoutNs);}) : result;
}

VkPresentHandoff SubmitVkPresent(const VkPresentRecordResult& record, const VkPresentInfoKHR& original,
                                 VkQueue queue, VkCommandBuffer commandBuffer, VkSemaphore signal,
                                 VkFence fence, const VkSubmitCall& submit)
{
    if (record.status != VkRecordStatus::Complete || !record.modelRecorded ||
        (!record.targetWriteRecorded && !record.captureOnly && !record.preparationOnly) ||
        (record.preparationOnly && (record.targetWriteRecorded || record.chainDeliverable || record.completedPasses)) ||
        queue == VK_NULL_HANDLE || commandBuffer == VK_NULL_HANDLE || signal == VK_NULL_HANDLE ||
        fence == VK_NULL_HANDLE || !submit ||
        (original.waitSemaphoreCount != 0 && original.pWaitSemaphores == nullptr))
        return {};

    // Preserve the game's entire pre-Present dependency, including any queue
    // ownership acquire it submitted before this call, before our first barrier.
    std::vector<VkPipelineStageFlags> waitStages(original.waitSemaphoreCount,
                                                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo info { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    info.waitSemaphoreCount = original.waitSemaphoreCount;
    info.pWaitSemaphores = original.pWaitSemaphores;
    info.pWaitDstStageMask = waitStages.empty() ? nullptr : waitStages.data();
    info.commandBufferCount = 1;
    info.pCommandBuffers = &commandBuffer;
    info.signalSemaphoreCount = 1;
    info.pSignalSemaphores = &signal;
    const auto result = submit(queue, info, fence);
    if (result != VK_SUCCESS)
        return { VkPresentHandoffState::Uncertain, VK_NULL_HANDLE, result };
    return { VkPresentHandoffState::Accepted, signal, VK_SUCCESS };
}

bool ApplyVkPresentHandoff(VkPresentInfoKHR& local, VkPresentHandoff& handoff)
{
    if (handoff.state != VkPresentHandoffState::Accepted || handoff.signal == VK_NULL_HANDLE)
        return false;
    local.waitSemaphoreCount = 1;
    local.pWaitSemaphores = &handoff.signal;
    return true;
}

bool VkPresentSlotState::CanRecord(bool imageReacquired, bool fenceComplete, bool acquireConsumed) const
{
    if (!imageReacquired) return false;
    return state_ == State::Fresh || (state_ == State::Presented && fenceComplete && acquireConsumed);
}

void VkPresentSlotState::Submitted() { state_ = State::Submitted; }

void VkPresentSlotState::OriginalPresentReturned(VkResult result)
{
    if (state_ != State::Submitted) return;
    // OUT_OF_DATE still enqueues the Present wait. It invalidates this image's
    // reuse, not completion evidence for our preceding private submission.
    // Keep its semaphore owned until independent consumption proof arrives.
    state_ = result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR ? State::Presented :
        result == VK_ERROR_OUT_OF_DATE_KHR ? State::OutOfDate : State::Uncertain;
}

} // namespace DlssNr
