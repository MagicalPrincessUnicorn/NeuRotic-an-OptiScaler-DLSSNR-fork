#pragma once

#include "VulkanPresentRegistry.h"
#include "VulkanPresentSubmission.h"

#include <memory>
#include <mutex>
#include <vector>

namespace DlssNr
{
class VkNrGuideSelection;
struct VkNrAcquireProof;
struct VkPresentConsumer;

struct VkPresentExecution
{
    VkPresentAdmission admission {};
    VkPresentRecordResult record {};
    VkPresentHandoff handoff {};
    uint64_t ticket = 0;
    std::string reason;
    bool capturedCompletionObserved = false;
};

class VulkanPresentExecutor
{
  public:
    ~VulkanPresentExecutor() { Shutdown(false); }
    VkPresentExecution BeforePresent(const VkObservedPresent& observed, VkInstance instance,
                                     const VkPresentInfoKHR& original,
                                     std::shared_ptr<const NrConfigSnapshot<Config>> settings = {},
                                     std::shared_ptr<const VkNrGuideSelection> guideSelection = {},
                                     std::shared_ptr<const VkNrWaitBudget> waitBudget = {},
                                     const std::function<bool()>& prepareProvider = {},
                                     std::shared_ptr<const VkCapturedGuideInput> capturedGuides = {});
    bool CompleteCapturedGuides(uint64_t ticket);
    void OriginalPresentReturned(uint64_t ticket, VkResult result, uint64_t registryPresentSerial = 0);
    void MarkUncertain(uint64_t ticket);
    void Shutdown(bool deviceAlive);
    void DeviceDestroyed(VkDevice);
    void NotifyDeviceInit();
    // Poll completion at a render boundary, without waiting for GPU idle.
    void Maintain(const VkObservedPresent&, bool inactive);
    // Read-only; inactive render-boundary maintenance owns actual retirement.
    bool CanYieldOutput(std::string& reason);

  private:
    struct Slot
    {
        bool applicationFacing = false;
        std::shared_ptr<VkPresentConsumer> consumer;
        uint64_t ticket = 0;
        uint64_t generation = 0;
        uint64_t deviceGeneration = 0;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        uint32_t imageIndex = UINT32_MAX;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VkSemaphore signal = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkPresentSlotState state;
        bool retirementProven = false;
        bool completionObserved = false;
        bool fenceNeedsReset = false;
        uint64_t presentSerial = 0;
        uint64_t registryPresentSerial = 0;
        uint64_t acquisition = 0;
        std::shared_ptr<const VkNrAcquireProof> acquireProof;
        std::unique_ptr<VkPresentPrivateFrame> frame;
        uint64_t estimatedBytes = 0;
    };
    Slot* Find(const VkObservedPresent& observed);
    Slot* Create(const VkObservedPresent& observed, VkInstance instance);
    void Destroy(Slot& slot, bool deviceAlive);
    void ReleaseCompletedSupersededFrames(const VkObservedPresent&);
    void RetirePredecessors(const VkObservedPresent&, uint64_t successorPresentSerial);

    std::mutex mutex_;
    std::vector<std::unique_ptr<Slot>> slots_;
    uint64_t nextTicket_ = 0;
    uint64_t nextPresentSerial_ = 0;
    uint64_t retainedBytes_ = 0;
    bool closed_ = false;
    std::optional<bool> lastApplicationFacing_;
    VkQueue modelQueue_ = VK_NULL_HANDLE;
    VkPresentCompositionGate compositionGate_;
    const char* createRefusal_ = "Vulkan Present private frame allocation or format features unavailable";
};

VulkanPresentExecutor& GetVulkanPresentExecutor();

} // namespace DlssNr
