#include "VulkanNrCompletion.h"
#include "VulkanNrFlightRecorder.h"
#include "VulkanPresentRegistry.h"
#include "VulkanPresentConsumer.h"
#include <algorithm>
#include <memory>
#include <atomic>
namespace DlssNr
{
VkResult ObserveVkNrSubmission(VkNrRecordingOwner& owner, const VkNrSubmission& submission, const VkNrApplicationSubmit& original,
                              const VkNrCheckpointSubmit& checkpoint)
{
    const auto receipt = owner.PrepareSubmission(submission);
    VkFlight::Current().Write(VkFlight::Kind::SubmitEnter,receipt?receipt->value:0,0,0,
        submission.uses.empty()?0:submission.uses.front().value,reinterpret_cast<uintptr_t>(submission.queue),submission.uses.size());
    const auto result = original();
    VkFlight::Current().Write(VkFlight::Kind::SubmitReturn,receipt?receipt->value:0,0,0,0,
        reinterpret_cast<uintptr_t>(submission.queue),submission.uses.size(),result,result!=VK_SUCCESS);
    if (!receipt) { owner.UntrackedSubmission(submission.uses, result); return result; }
    owner.SubmissionReturned(*receipt, result);
    if (result == VK_SUCCESS) owner.CheckpointReturned(*receipt, checkpoint(*receipt));
    return result;
}
namespace
{
struct Device {
    VkDevice handle; uint64_t generation; VkNrCompletionDispatch dispatch; std::atomic_bool alive{true};
    std::recursive_mutex operations;
    Device(VkDevice h,uint64_t g,VkNrCompletionDispatch d) : handle(h),generation(g),dispatch(d) {}
};
struct Queue { std::shared_ptr<Device> device; uint32_t family; bool protectedQueue; std::recursive_mutex mutex; };
struct Fence { std::shared_ptr<Device> device; VkFence handle; VkNrUseId use; VkNrSubmissionId receipt; bool submitted = false, claimed = false; };
std::mutex stateMutex;
std::mutex pollingMutex;
uint64_t nextDevice = 1;
std::unordered_map<VkDevice,std::shared_ptr<Device>> devices;
std::unordered_map<VkQueue,std::shared_ptr<Queue>> queues;
std::vector<std::shared_ptr<Fence>> fences;
thread_local bool checkpointBypass = false;
std::shared_ptr<Fence> CreateOwnedFence(const std::shared_ptr<Device>& device, VkNrUseId use)
{
    // Reserve capacity before leaving the control lock; Vulkan calls happen outside it.
    auto owned = std::make_shared<Fence>(Fence{device,VK_NULL_HANDLE,use,{}});
    { std::lock_guard lock(stateMutex); if (!device->alive || fences.size() >= 1024) return {}; fences.push_back(owned); }
    std::lock_guard operation(device->operations);
    if (!device->alive) { std::lock_guard lock(stateMutex); std::erase(fences,owned); return {}; }
    VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence handle = VK_NULL_HANDLE;
    const auto result = device->dispatch.createFence(device->handle,&info,nullptr,&handle);
    if (result != VK_SUCCESS) { std::lock_guard lock(stateMutex); std::erase(fences,owned); return {}; }
    { std::lock_guard lock(stateMutex); owned->handle = handle; }
    return owned;
}
std::shared_ptr<Queue> FindQueue(VkQueue queue)
{
    std::lock_guard lock(stateMutex); const auto found = queues.find(queue); return found == queues.end() ? nullptr : found->second;
}
}
void RegisterVkNrCompletionDevice(VkDevice handle, VkNrCompletionDispatch dispatch)
{
    if (!handle || !dispatch.createFence || !dispatch.destroyFence || !dispatch.getFenceStatus || !dispatch.queueSubmit) return;
    std::lock_guard lock(stateMutex);
    if (!devices.contains(handle)) devices.emplace(handle,std::make_shared<Device>(handle,nextDevice++,dispatch));
}
void RegisterVkNrCompletionQueue(VkDevice device, VkQueue handle, uint32_t family, bool protectedQueue)
{
    std::lock_guard lock(stateMutex); const auto found = devices.find(device); if (found == devices.end() || !handle) return;
    if (queues.contains(handle)) return;
    auto queue = std::make_shared<Queue>(); queue->device = found->second; queue->family = family; queue->protectedQueue = protectedQueue;
    queues.insert_or_assign(handle,queue);
}
uint64_t VkNrCompletionDeviceGeneration(VkDevice device)
{
    std::lock_guard lock(stateMutex);const auto found=devices.find(device);
    return found!=devices.end()&&found->second->alive ? found->second->generation : 0;
}
std::optional<VkQueue> UniqueVkNrCompletionQueue(VkDevice device,uint32_t family)
{
    std::lock_guard lock(stateMutex);std::optional<VkQueue> result;
    for(const auto& [handle,queue]:queues) {
        if(queue->device->handle!=device||queue->family!=family||queue->protectedQueue||!queue->device->alive)continue;
        if(result)return {};result=handle;
    }
    return result;
}
uint32_t VkNrCompletionQueueCount(VkDevice device,uint32_t family)
{
    std::lock_guard lock(stateMutex);uint32_t count=0;
    for(const auto& [handle,queue]:queues) {
        (void)handle;
        if(queue->device->handle==device&&queue->family==family&&!queue->protectedQueue&&queue->device->alive)++count;
    }
    return count;
}
std::optional<VkNrQueueContext> VkNrRecordingQueueContext(VkDevice device,uint32_t family,VkQueue explicitQueue)
{
    if(!device||family==UINT32_MAX)return {};
    std::lock_guard lock(stateMutex);VkQueue selected=VK_NULL_HANDLE;uint32_t count=0;
    for(const auto& [handle,queue]:queues) {
        if(queue->device->handle!=device||queue->family!=family||queue->protectedQueue||!queue->device->alive)continue;
        if(explicitQueue&&handle!=explicitQueue)continue;
        selected=handle;++count;
    }
    if(!count)return {};
    return VkNrQueueContext{count==1?selected:VK_NULL_HANDLE,family};
}
void DestroyVkNrCompletionDevice(VkDevice handle)
{
    { std::lock_guard lock(stateMutex); const auto found = devices.find(handle); if (found == devices.end()) return;
      found->second->alive = false; devices.erase(found);
      std::erase_if(queues,[&](const auto& value){return value.second->device->handle == handle;});
      // Called after vkDestroyDevice: its implicit cleanup owns these handles now.
      std::erase_if(fences,[&](const auto& value){return value->device->handle == handle;}); }
    VulkanPresentConsumers().DeviceDestroyed(handle);
    VulkanNrRecordings().OnDeviceDestroyed(handle);
}
bool ObserveVkNrDeviceDestroy(VkDevice handle, const std::function<void()>& original)
{
    std::shared_ptr<Device> device; std::vector<std::shared_ptr<Fence>> owned;
    { std::lock_guard lock(stateMutex); const auto found = devices.find(handle); if (found != devices.end()) device = found->second; }
    if (!device) { original(); VulkanNrRecordings().OnDeviceDestroyed(handle); return true; }
    std::lock_guard operation(device->operations);
    { std::lock_guard lock(stateMutex); device->alive = false;
      for (const auto& fence : fences) if (fence->device == device && fence->handle) owned.push_back(fence); }
    std::vector<VkFence> pending;
    for (const auto& fence : owned) if (fence->submitted) pending.push_back(fence->handle);
    VkResult waited = VK_SUCCESS;
    if (!pending.empty()) {
        waited = device->dispatch.waitForFences ? VkFlight::Call(VkFlight::CallSite::FenceWait,0,[&]{return device->dispatch.waitForFences(handle,static_cast<uint32_t>(pending.size()),pending.data(),VK_TRUE,100000000ull);}) : VK_NOT_READY;
    }
    bool drained = true;
    for (const auto& fence : owned) {
        const bool complete = !fence->submitted || waited == VK_SUCCESS ||
            device->dispatch.getFenceStatus(handle,fence->handle) == VK_SUCCESS;
        if (complete) device->dispatch.destroyFence(handle,fence->handle,nullptr); else drained = false;
    }
    // An unproved pending fence is kept through the original destruction boundary.
    // The caller reports the unsuccessful bounded drain; it is not called completion.
    original(); DestroyVkNrCompletionDevice(handle); return drained;
}
bool ReserveVkNrCompletion(VkDevice handle, VkNrUseId use)
{
    if (!use || VulkanNrRecordings().UseDevice(use) != handle) return false;
    PollVkNrCompletions(); std::shared_ptr<Device> device;
    { std::lock_guard lock(stateMutex); const auto found = devices.find(handle); if (found == devices.end()) return false;
      for (const auto& fence : fences) if (fence->use == use && !fence->receipt) return true;
      device = found->second; }
    return CreateOwnedFence(device,use) != nullptr;
}
void PollVkNrCompletions()
{
    std::unique_lock polling(pollingMutex,std::try_to_lock); if (!polling.owns_lock()) return;
    std::vector<std::shared_ptr<Fence>> snapshot;
    { std::lock_guard lock(stateMutex); snapshot = fences; }
    for (const auto& fence : snapshot) {
        std::lock_guard operation(fence->device->operations);
        VkFence handle; VkNrSubmissionId receipt; bool submitted,claimed;
        { std::lock_guard lock(stateMutex); handle = fence->handle; receipt = fence->receipt; submitted = fence->submitted; claimed = fence->claimed; }
        if (!fence->device->alive || !handle) continue;
        bool release = false;
        if (submitted) {
            const auto result = fence->device->dispatch.getFenceStatus(fence->device->handle,handle);
            VulkanNrRecordings().ObserveFence(receipt,result);if(result==VK_ERROR_DEVICE_LOST)fence->device->alive=false; release = result == VK_SUCCESS;
        } else if (!receipt && !claimed) release = VulkanNrRecordings().Reusable(fence->use);
        if (release) {
            bool removed = false;
            { std::lock_guard lock(stateMutex); const auto found = std::find(fences.begin(),fences.end(),fence);
              if (found != fences.end()) { fences.erase(found); removed = true; } }
            if (removed && fence->device->alive) fence->device->dispatch.destroyFence(fence->device->handle,handle,nullptr);
        }
    }
}
VkResult SubmitVkNrCheckpoint(VkQueue handle, VkFence fence)
{
    const auto queue = FindQueue(handle); if (!queue || queue->protectedQueue || !queue->device->alive || !fence) return VK_ERROR_INITIALIZATION_FAILED;
    std::lock_guard lock(queue->mutex);
    std::lock_guard operation(queue->device->operations);
    if (!queue->device->alive) return VK_ERROR_DEVICE_LOST;
    const bool previous = checkpointBypass; checkpointBypass = true;
    const VkSubmitInfo checkpoint{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    const auto result = queue->device->dispatch.queueSubmit(handle,1,&checkpoint,fence);
    checkpointBypass = previous; return result;
}
VkResult WaitVkNrUse(VkNrUseId use,uint64_t timeoutNs){
    if(VulkanNrRecordings().GpuComplete(use))return VK_SUCCESS;
    std::lock_guard polling(pollingMutex);const auto proof=VulkanNrRecordings().ProducerProof(use);if(!proof)return VK_NOT_READY;
    std::shared_ptr<Fence> owned;{std::lock_guard lock(stateMutex);for(auto& f:fences)if(f->receipt==proof->submission&&f->submitted){owned=f;break;}}
    if(!owned)return VK_NOT_READY;std::lock_guard operation(owned->device->operations);
    if(!owned->device->alive)return VK_ERROR_DEVICE_LOST;
    const auto result=owned->device->dispatch.waitForFences?VkFlight::Call(VkFlight::CallSite::FenceWait,0,[&]{return owned->device->dispatch.waitForFences(owned->device->handle,1,&owned->handle,VK_TRUE,timeoutNs);}):VK_NOT_READY;
    VulkanNrRecordings().ObserveFence(proof->submission,result);if(result==VK_ERROR_DEVICE_LOST)owned->device->alive=false;return result;
}
VkResult ObserveVkNrQueueSubmit(VkQueue handle, std::span<const VkCommandBuffer> buffers, const VkNrApplicationSubmit& original,std::span<const VkSemaphore> waits)
{
    if (checkpointBypass) return original();
    const auto queue = FindQueue(handle);
    VkNrSubmission submission{handle,queue ? queue->device->generation : 0,{}};
    std::vector<VkNrUseId> semanticAcquires,loaderAcquires,providerWaits;
    if(queue&&!queue->protectedQueue){
        semanticAcquires=GetVulkanPresentRegistry().PrepareAcquireWait(handle,waits,queue->device->generation);
        loaderAcquires=GetVulkanLoaderPresentRegistry().PrepareAcquireWait(handle,waits,queue->device->generation);
        providerWaits=VulkanPresentConsumers().Prepare(queue->device->handle,queue->device->generation,queue->family,waits);
        submission.uses=providerWaits;
        submission.uses.insert(submission.uses.end(),semanticAcquires.begin(),semanticAcquires.end());
        submission.uses.insert(submission.uses.end(),loaderAcquires.begin(),loaderAcquires.end());
    }
    for (auto buffer : buffers) {
        const auto uses = VulkanNrRecordings().SnapshotSubmissionUses(buffer); if (!uses) continue;
        for (auto use : *uses) if (std::find(submission.uses.begin(),submission.uses.end(),use) == submission.uses.end()) submission.uses.push_back(use);
    }
    if (submission.uses.empty()) return original();
    if (!queue) { const auto result = original(); VulkanNrRecordings().UntrackedSubmission(submission.uses,result); return result; }
    std::lock_guard queueLock(queue->mutex); PollVkNrCompletions();
    std::lock_guard operation(queue->device->operations);
    if (!queue->device->alive) return original();
    if (queue->protectedQueue || std::any_of(submission.uses.begin(),submission.uses.end(),[&](auto use){return VulkanNrRecordings().UseDevice(use) != queue->device->handle;})) {
        const auto result = original(); VulkanNrRecordings().UntrackedSubmission(submission.uses,result); return result;
    }
    std::shared_ptr<Fence> fence;
    { std::lock_guard lock(stateMutex); for (const auto& candidate : fences) {
        if (candidate->device == queue->device && candidate->handle && !candidate->receipt && !candidate->claimed &&
            std::find(submission.uses.begin(),submission.uses.end(),candidate->use) != submission.uses.end()) { fence = candidate; candidate->claimed = true; break; }
    } }
    if (!fence) { fence = CreateOwnedFence(queue->device,submission.uses.front()); if (fence) { std::lock_guard lock(stateMutex); fence->claimed = true; } }
    const auto applicationResult = ObserveVkNrSubmission(VulkanNrRecordings(),submission,original,[&](VkNrSubmissionId receipt) {
        if (!fence) return VK_ERROR_OUT_OF_HOST_MEMORY;
        { std::lock_guard lock(stateMutex); fence->receipt = receipt; }
        const auto result = SubmitVkNrCheckpoint(handle,fence->handle);
        { std::lock_guard lock(stateMutex); fence->submitted = result == VK_SUCCESS; }
        return result;
    });
    if (applicationResult != VK_SUCCESS && fence) { std::lock_guard lock(stateMutex); fence->claimed = false; }
    VulkanPresentConsumers().Returned(providerWaits,applicationResult);
    GetVulkanPresentRegistry().AcquireWaitSubmitted(handle,semanticAcquires,applicationResult);
    GetVulkanLoaderPresentRegistry().AcquireWaitSubmitted(handle,loaderAcquires,applicationResult);
    return applicationResult;
}
} // namespace DlssNr
