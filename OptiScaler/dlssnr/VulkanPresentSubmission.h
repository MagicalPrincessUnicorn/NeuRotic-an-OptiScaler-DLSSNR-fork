#pragma once

#include "VulkanPresent.h"

#include <functional>

namespace DlssNr
{

enum class VkPresentHandoffState { NoSubmission, Accepted, Uncertain };

struct VkPresentHandoff
{
    VkPresentHandoffState state = VkPresentHandoffState::NoSubmission;
    VkSemaphore signal = VK_NULL_HANDLE;
    VkResult result = VK_SUCCESS;
};

using VkSubmitCall = std::function<VkResult(VkQueue, const VkSubmitInfo&, VkFence)>;

// A successful return transfers the game's binary waits to this submission exactly once.
// The caller must replace local Present waits with handoff.signal only after Accepted.
VkPresentHandoff SubmitVkPresent(const VkPresentRecordResult& record, const VkPresentInfoKHR& original,
                                 VkQueue queue, VkCommandBuffer commandBuffer, VkSemaphore signal,
                                 VkFence fence, const VkSubmitCall& submit);
bool ApplyVkPresentHandoff(VkPresentInfoKHR& local, VkPresentHandoff& handoff);

// Wait only for our already-submitted work, never for a future game submission.
// Timeout/error leaves its resources owned and unavailable for new recording.
VkResult PollOrWaitVkPresentFence(VkDevice device, VkFence fence,
    PFN_vkGetFenceStatus poll, PFN_vkWaitForFences wait, uint64_t timeoutNs = 100000000ull);

class VkPresentSlotState
{
  public:
    bool CanRecord(bool imageReacquired, bool fenceComplete, bool acquireConsumed = false) const;
    void Submitted();
    void OriginalPresentReturned(VkResult result);
    void Uncertain() { state_ = State::Uncertain; }
    bool Pending() const { return state_ != State::Fresh; }
    bool Presented() const { return state_ == State::Presented; }
    bool PresentWaitEnqueued() const { return state_ == State::Presented || state_ == State::OutOfDate; }
    // Private command/images are never consumed by WSI. Their submission fence
    // permits destruction independently of the still-owned Present semaphore.
    bool CanReleasePrivateFrame(bool fenceComplete) const
    { return state_ == State::Fresh || (PresentWaitEnqueued() && fenceComplete); }

  private:
    enum class State { Fresh, Submitted, Presented, OutOfDate, Uncertain };
    State state_ = State::Fresh;
};

// The composition pass owns one descriptor/constant ring across all image slots.
// A new record may update it only after the preceding Present submission's fence.
class VkPresentCompositionGate
{
  public:
    bool CanRecord() const { return ticket_ == 0; }
    uint64_t Ticket() const { return ticket_; }
    bool NeedsHistoryReset(uint64_t generation) const { return historyDiscontinuous_ || generation_ != generation; }
    void MarkHistoryDiscontinuous() { historyDiscontinuous_ = true; }
    void Submitted(uint64_t ticket, uint64_t generation = 0)
    { ticket_ = ticket; generation_ = generation; historyDiscontinuous_ = false; }
    bool ObserveCompletion(uint64_t ticket, bool gpuComplete)
    {
        if (ticket_ != ticket || !gpuComplete) return false;
        ticket_ = 0;
        return true;
    }

  private:
    uint64_t ticket_ = 0;
    uint64_t generation_ = 0;
    bool historyDiscontinuous_ = false;
};

} // namespace DlssNr
