#pragma once
#include "VulkanNrRecording.h"
#include <functional>
namespace DlssNr
{
using VkNrApplicationSubmit = std::function<VkResult()>;
using VkNrCheckpointSubmit = std::function<VkResult(VkNrSubmissionId)>;
VkResult ObserveVkNrSubmission(VkNrRecordingOwner&, const VkNrSubmission&, const VkNrApplicationSubmit&,
                              const VkNrCheckpointSubmit&);
// GPU hooks register actual device/queue dispatch. Checkpoints use owned fences,
// never application fences or semaphore signals.
struct VkNrCompletionDispatch
{
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkGetFenceStatus getFenceStatus = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
};
void RegisterVkNrCompletionDevice(VkDevice, VkNrCompletionDispatch);
uint64_t VkNrCompletionDeviceGeneration(VkDevice);
std::optional<VkQueue> UniqueVkNrCompletionQueue(VkDevice,uint32_t family);
uint32_t VkNrCompletionQueueCount(VkDevice,uint32_t family);
struct VkNrQueueContext { VkQueue queue=VK_NULL_HANDLE;uint32_t family=UINT32_MAX; };
std::optional<VkNrQueueContext> VkNrRecordingQueueContext(VkDevice,uint32_t family,VkQueue explicitQueue=VK_NULL_HANDLE);
void RegisterVkNrCompletionQueue(VkDevice, VkQueue, uint32_t family, bool protectedQueue = false);
void DestroyVkNrCompletionDevice(VkDevice);
bool ObserveVkNrDeviceDestroy(VkDevice, const std::function<void()>& original);
bool ReserveVkNrCompletion(VkDevice, VkNrUseId);
void PollVkNrCompletions();
VkResult SubmitVkNrCheckpoint(VkQueue, VkFence);
VkResult ObserveVkNrQueueSubmit(VkQueue, std::span<const VkCommandBuffer>, const VkNrApplicationSubmit&,std::span<const VkSemaphore> waits = {});
VkResult WaitVkNrUse(VkNrUseId,uint64_t timeoutNs=100000000ull);
} // namespace DlssNr
