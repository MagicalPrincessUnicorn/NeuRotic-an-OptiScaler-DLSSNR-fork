#include "dlssnr/VulkanPresentSubmission.h"
#include "dlssnr/VulkanNrFlightRecorder.h"

#include <cassert>
#include <vector>

using namespace DlssNr;

namespace
{
VkResult fencePoll = VK_NOT_READY, fenceWait = VK_SUCCESS;
int polls = 0, waitsCalled = 0;
uint64_t expectedAllowance = 100000000;
VkResult VKAPI_CALL PollFence(VkDevice device, VkFence fence)
{
    assert(device == reinterpret_cast<VkDevice>(10) && fence == reinterpret_cast<VkFence>(4));
    ++polls;
    return fencePoll;
}
VkResult VKAPI_CALL WaitFence(VkDevice device, uint32_t count, const VkFence* fences,
                             VkBool32 all, uint64_t timeout)
{
    assert(device == reinterpret_cast<VkDevice>(10) && count == 1 &&
           fences[0] == reinterpret_cast<VkFence>(4) && all == VK_TRUE && timeout == expectedAllowance);
    ++waitsCalled;
    return fenceWait;
}
}

int main()
{
    const auto queue = reinterpret_cast<VkQueue>(1);
    const auto cmd = reinterpret_cast<VkCommandBuffer>(2);
    const auto signal = reinterpret_cast<VkSemaphore>(3);
    const auto fence = reinterpret_cast<VkFence>(4);
    const VkSemaphore waits[] = { reinterpret_cast<VkSemaphore>(5), reinterpret_cast<VkSemaphore>(6) };
    VkPresentInfoKHR original { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    original.waitSemaphoreCount = 2;
    original.pWaitSemaphores = waits;
    int submits = 0;
    VkPresentRecordResult complete { VkRecordStatus::Complete, "", true, true };
    auto accepted = SubmitVkPresent(complete, original, queue, cmd, signal, fence,
        [&](VkQueue observedQueue, const VkSubmitInfo& info, VkFence observedFence) {
            ++submits;
            assert(observedQueue == queue && observedFence == fence);
            assert(info.commandBufferCount == 1 && info.pCommandBuffers[0] == cmd);
            assert(info.waitSemaphoreCount == 2 && info.pWaitSemaphores == waits);
            assert(info.pWaitDstStageMask[0] == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            assert(info.pWaitDstStageMask[1] == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            assert(info.signalSemaphoreCount == 1 && info.pSignalSemaphores[0] == signal);
            return VK_SUCCESS;
        });
    assert(accepted.state == VkPresentHandoffState::Accepted && accepted.signal == signal);
    assert(submits == 1 && original.pWaitSemaphores == waits);
    VkPresentInfoKHR local = original;
    assert(ApplyVkPresentHandoff(local, accepted));
    assert(local.waitSemaphoreCount == 1 && local.pWaitSemaphores[0] == signal);
    assert(original.waitSemaphoreCount == 2 && original.pWaitSemaphores == waits);

    auto partial = complete;
    partial.status = VkRecordStatus::Partial;
    auto noSubmit = SubmitVkPresent(partial, original, queue, cmd, signal, fence,
        [&](VkQueue, const VkSubmitInfo&, VkFence) { ++submits; return VK_SUCCESS; });
    assert(noSubmit.state == VkPresentHandoffState::NoSubmission && submits == 1);
    local = original;
    assert(!ApplyVkPresentHandoff(local, noSubmit) && local.pWaitSemaphores == waits);

    VkPresentInfoKHR zero { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    auto zeroWait = SubmitVkPresent(complete, zero, queue, cmd, signal, fence,
        [&](VkQueue, const VkSubmitInfo& info, VkFence) {
            assert(info.waitSemaphoreCount == 0 && info.pWaitSemaphores == nullptr &&
                   info.pWaitDstStageMask == nullptr);
            return VK_SUCCESS;
        });
    assert(zeroWait.state == VkPresentHandoffState::Accepted);

    auto uncertain = SubmitVkPresent(complete, original, queue, cmd, signal, fence,
        [&](VkQueue, const VkSubmitInfo&, VkFence) { ++submits; return VK_ERROR_DEVICE_LOST; });
    assert(uncertain.state == VkPresentHandoffState::Uncertain && submits == 2);

    VkPresentSlotState slot;
    assert(slot.CanRecord(true, false));
    slot.Submitted();
    assert(!slot.CanRecord(true, false));
    slot.OriginalPresentReturned(VK_SUCCESS);
    assert(!slot.CanRecord(true, false));
    assert(slot.CanRecord(true, true, true));
    assert(!slot.CanRecord(false, true)); // Render completion does not prove Present wait consumption.
    assert(slot.CanReleasePrivateFrame(true)); // Private images have no WSI wait use.
    assert(!slot.CanReleasePrivateFrame(false));
    slot.Submitted();
    slot.OriginalPresentReturned(VK_ERROR_OUT_OF_DATE_KHR);
    assert(!slot.CanRecord(true, true, true));
    assert(!slot.CanRecord(false, true));
    assert(slot.Pending() && !slot.Presented() && slot.PresentWaitEnqueued());
    assert(slot.CanReleasePrivateFrame(true)); // Completed private work is independent of the retained WSI wait.
    assert(!slot.CanReleasePrivateFrame(false)); // OUT_OF_DATE cannot replace private fence proof.
    assert(!slot.CanRecord(true, true, false)); // Even a completed private frame grants no replay right.
    // Actual submission/Present uncertainty remains quarantined even when a
    // fence argument is true; OUT_OF_DATE is a separate, released contract.
    for (const auto failed : { VK_ERROR_DEVICE_LOST, VK_ERROR_SURFACE_LOST_KHR,
                               VK_ERROR_INITIALIZATION_FAILED })
    {
        VkPresentSlotState failedSlot;
        failedSlot.Submitted();
        assert(!failedSlot.CanReleasePrivateFrame(true)); // Original Present outcome still unknown.
        failedSlot.OriginalPresentReturned(failed);
        assert(failedSlot.Pending() && !failedSlot.Presented() && !failedSlot.PresentWaitEnqueued());
        assert(!failedSlot.CanRecord(true, true, true));
        assert(!failedSlot.CanReleasePrivateFrame(true) && !failedSlot.CanReleasePrivateFrame(false));
    }
    slot.Uncertain(); // Original-not-called/provider ownership uncertainty has the same conservative boundary.
    assert(slot.Pending() && !slot.PresentWaitEnqueued());
    assert(!slot.CanReleasePrivateFrame(true) && !slot.CanRecord(true, true, true));
    VkPresentCompositionGate gate;
    assert(gate.CanRecord() && gate.Ticket() == 0);
    assert(gate.NeedsHistoryReset(8));
    gate.Submitted(17, 8);
    assert(!gate.NeedsHistoryReset(8));
    assert(!gate.CanRecord() && !gate.ObserveCompletion(17, false));
    assert(gate.ObserveCompletion(17, true) && gate.CanRecord());
    assert(!gate.NeedsHistoryReset(8) && gate.NeedsHistoryReset(9));
    gate.Submitted(18);
    assert(!gate.ObserveCompletion(17, true) && !gate.CanRecord());
    assert(gate.ObserveCompletion(18, true) && gate.CanRecord());
    gate.Submitted(19, 8);
    gate.MarkHistoryDiscontinuous();
    assert(gate.Ticket() == 19 && !gate.CanRecord() && gate.NeedsHistoryReset(8));
    assert(!gate.ObserveCompletion(19, false) && gate.NeedsHistoryReset(8));
    assert(gate.ObserveCompletion(19, true) && gate.CanRecord() && gate.NeedsHistoryReset(8));
    gate.MarkHistoryDiscontinuous();
    assert(gate.NeedsHistoryReset(8));
    gate.Submitted(20, 8);
    assert(!gate.NeedsHistoryReset(8));
    assert(gate.ObserveCompletion(20, true) && !gate.NeedsHistoryReset(8));
    const auto device = reinterpret_cast<VkDevice>(10);
    assert(PollOrWaitVkPresentFence(device, fence, PollFence, WaitFence) == VK_SUCCESS);
    assert(polls == 1 && waitsCalled == 1);
    fencePoll = VK_SUCCESS;
    assert(PollOrWaitVkPresentFence(device, fence, PollFence, WaitFence) == VK_SUCCESS);
    assert(polls == 2 && waitsCalled == 1);
    fencePoll = VK_NOT_READY;
    fenceWait = VK_TIMEOUT;
    assert(PollOrWaitVkPresentFence(device, fence, PollFence, WaitFence) == VK_TIMEOUT);
    assert(polls == 3 && waitsCalled == 2);
    fencePoll = VK_ERROR_DEVICE_LOST;
    assert(PollOrWaitVkPresentFence(device, fence, PollFence, WaitFence) == VK_ERROR_DEVICE_LOST);
    assert(polls == 4 && waitsCalled == 2);
    const auto trace=VkFlight::Current().Read();
    std::vector<VkFlight::Event> entries,returns;
    for(const auto& event:trace.events)if(event.command==reinterpret_cast<uintptr_t>(fence)) {
        if(event.kind==VkFlight::Kind::SpanEnter)entries.push_back(event);
        if(event.kind==VkFlight::Kind::SpanReturn)returns.push_back(event);
    }
    assert(entries.size()==2&&returns.size()==2);
    for(const auto& event:entries)assert(event.detail==static_cast<uint64_t>(VkFlight::CallSite::PresentCompositionWait));
    for(const auto& event:returns)assert(event.detail==static_cast<uint64_t>(VkFlight::CallSite::PresentCompositionWait));
    assert(entries[0].operation==returns[0].operation&&entries[1].operation==returns[1].operation);
    assert(returns[0].result==VK_SUCCESS&&returns[1].result==VK_TIMEOUT);
    expectedAllowance=0;fencePoll=VK_SUCCESS;
    assert(PollOrWaitVkPresentFence(device,fence,PollFence,WaitFence,0)==VK_SUCCESS);
    assert(polls==5&&waitsCalled==2); // Exhaustion still observes completed work.
    fencePoll=VK_NOT_READY;
    assert(PollOrWaitVkPresentFence(device,fence,PollFence,WaitFence,0)==VK_TIMEOUT);
    assert(polls==6&&waitsCalled==3); // No renewed blocking allowance.
    complete.preparationOnly=true;complete.targetWriteRecorded=false;
    unsigned preparedSubmits=0;
    auto prepareSubmit=[&](VkQueue,const VkSubmitInfo& info,VkFence){++preparedSubmits;
        assert(info.waitSemaphoreCount==2&&info.pWaitSemaphores==waits&&info.signalSemaphoreCount==1);
        return VK_SUCCESS;};
    auto preparation=SubmitVkPresent(complete,original,queue,cmd,signal,fence,prepareSubmit);
    assert(preparation.state==VkPresentHandoffState::Accepted&&preparedSubmits==1);
    complete.completedPasses=1;
    assert(SubmitVkPresent(complete,original,queue,cmd,signal,fence,prepareSubmit).state!=VkPresentHandoffState::Accepted&&preparedSubmits==1);
    return 0;
}
