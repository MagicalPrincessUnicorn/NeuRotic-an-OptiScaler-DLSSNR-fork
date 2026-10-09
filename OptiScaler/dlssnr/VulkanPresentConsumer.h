#pragma once
#include "VulkanNrRecording.h"
#include "VulkanPresentRegistry.h"
#include <memory>
#include <algorithm>
#include <atomic>
#include <functional>

namespace DlssNr {
inline std::function<bool()> MakeVkPhysicalPresentWait(const VkObservedPresent& image,uint64_t serial,VulkanPresentRegistry& registry)
{
    return [image,serial,&registry,proof=std::shared_ptr<const VkNrAcquireProof>{}]() mutable {
        if(!proof){
            auto candidate=registry.LatestAcquireProof(image.swapchain,image.request.imageIndex);
            if(candidate&&serial&&candidate->device==image.device&&candidate->deviceGeneration==image.request.deviceGeneration&&
               candidate->swapchainGeneration==image.request.swapchainGeneration&&candidate->acquireGeneration>image.request.acquireGeneration&&
               candidate->priorPresentSerial==serial&&candidate->priorPresentQueue==image.queue)proof=std::move(candidate);
        }
        return proof&&proof->Complete();
    };
}
// A receipt belongs to one signal/wait cycle. Only the real downstream wait
// submission and its owned checkpoint can complete it; never proxy reacquire.
struct VkPresentConsumer {
    explicit VkPresentConsumer(VkNrRecordingOwner& owner):owner(owner){}
    ~VkPresentConsumer(){if(use)owner.ReleaseUse(use);}
    bool Complete() const {std::lock_guard lock(mutex);return !abandoned&&submitted&&(physicalProof?physicalProof():(use&&owner.GpuComplete(use)));}
    VkNrRecordingOwner& owner;
    VkDevice device=VK_NULL_HANDLE;uint64_t generation=0;VkSemaphore semaphore=VK_NULL_HANDLE;
    std::function<bool()> physicalProof;
    mutable std::mutex mutex;VkNrUseId use;bool claimed=false,submitted=false,abandoned=false;
};
class VkPresentConsumers {
public:
    explicit VkPresentConsumers(VkNrRecordingOwner& owner):owner_(owner){}
    bool Interested() const{return interested_.load(std::memory_order_acquire);}
    std::shared_ptr<VkPresentConsumer> Register(VkDevice device,uint64_t generation,VkSemaphore semaphore){
        if(!device||!generation||!semaphore)return {};
        std::lock_guard lock(mutex_);std::erase_if(receipts_,[](const auto& r){return r.expired();});
        for(auto& weak:receipts_)if(auto r=weak.lock();r&&r->device==device&&r->semaphore==semaphore)return {};
        if(receipts_.size()>=128)return {};
        auto r=std::make_shared<VkPresentConsumer>(owner_);r->device=device;r->generation=generation;r->semaphore=semaphore;
        receipts_.push_back(r);interested_.store(true,std::memory_order_release);return r;
    }
    std::vector<VkNrUseId> Prepare(VkDevice device,uint64_t generation,uint32_t family,std::span<const VkSemaphore> waits){
        std::vector<VkNrUseId> result;if(waits.empty()||!interested_.load(std::memory_order_acquire))return result;
        std::lock_guard lock(mutex_);
        std::erase_if(receipts_,[](const auto& r){return r.expired();});
        if(receipts_.empty()){interested_.store(false,std::memory_order_release);return result;}
        for(auto& weak:receipts_)if(auto r=weak.lock();r&&r->device==device&&r->generation==generation&&
            std::find(waits.begin(),waits.end(),r->semaphore)!=waits.end()){
            std::lock_guard claim(r->mutex);if(r->claimed||r->abandoned)continue;
            auto use=owner_.ReserveSubmissionDependency(device,generation,family);if(!use)continue;
            r->use=*use;r->claimed=true;result.push_back(*use);
        }
        return result;
    }
    void Returned(std::span<const VkNrUseId> uses,VkResult result){
        std::lock_guard lock(mutex_);
        for(auto& weak:receipts_)if(auto r=weak.lock()){
            std::lock_guard claim(r->mutex);if(std::find(uses.begin(),uses.end(),r->use)==uses.end())continue;
            r->submitted=result==VK_SUCCESS;
            if(result==VK_ERROR_OUT_OF_HOST_MEMORY||result==VK_ERROR_OUT_OF_DEVICE_MEMORY){owner_.ReleaseUse(r->use);r->use={};r->claimed=false;}
        }
    }
    // Physical Present forwarding needs physical reacquire+completed acquire
    // synchronization. The caller binds that proof to the exact successful call.
    void PhysicalPresent(VkDevice device,uint64_t generation,std::span<const VkSemaphore> waits,VkResult result,
                         const std::function<bool()>& proof){
        if(waits.empty()||!interested_.load(std::memory_order_acquire))return;
        std::lock_guard lock(mutex_);
        for(auto& weak:receipts_)if(auto r=weak.lock();r&&r->device==device&&r->generation==generation&&
            std::find(waits.begin(),waits.end(),r->semaphore)!=waits.end()){
            std::lock_guard claim(r->mutex);if(r->claimed||r->abandoned)continue;
            r->claimed=true;r->submitted=result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR;
            if(r->submitted)r->physicalProof=proof;
        }
    }
    void RefreshPhysical(VkDevice device){
        if(!Interested())return;
        std::lock_guard lock(mutex_);
        for(auto& weak:receipts_)if(auto r=weak.lock();r&&r->device==device){
            std::lock_guard claim(r->mutex);
            // Retain the exact new acquisition before a later physical Present
            // replaces registry metadata. Completion itself may still be pending.
            if(!r->abandoned&&r->physicalProof)r->physicalProof();
        }
    }
    void DeviceDestroyed(VkDevice device){
        std::lock_guard lock(mutex_);
        for(auto& weak:receipts_)if(auto r=weak.lock();r&&r->device==device){std::lock_guard claim(r->mutex);r->abandoned=true;}
    }
private:
    VkNrRecordingOwner& owner_;std::atomic_bool interested_{false};std::mutex mutex_;std::vector<std::weak_ptr<VkPresentConsumer>> receipts_;
};
inline VkPresentConsumers& VulkanPresentConsumers(){static auto* owner=new VkPresentConsumers(VulkanNrRecordings());return *owner;}
}
