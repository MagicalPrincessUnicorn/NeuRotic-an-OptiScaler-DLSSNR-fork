#pragma once
#include "VulkanPresentRegistry.h"
#include <array>
#include <utility>

namespace DlssNr
{
struct VkAcquireFenceObservation
{
    VkFence fence = VK_NULL_HANDLE;
    std::array<std::shared_ptr<const VkNrAcquireProof>,2> proofs;
    bool Known() const { return proofs[0] || proofs[1]; }
    void Publish(VkResult result) const
    { for (const auto& proof : proofs) VulkanPresentRegistry::ObserveAcquireFence(proof,result); }
};
inline VkAcquireFenceObservation CaptureVkAcquireFence(VkDevice device,VkFence fence)
{
    return {fence,{GetVulkanPresentRegistry().AcquireFenceProof(device,fence),
                   GetVulkanLoaderPresentRegistry().AcquireFenceProof(device,fence)}};
}
inline void InvalidateVkAcquireFence(VkDevice device,VkFence fence)
{
    GetVulkanPresentRegistry().InvalidateAcquireFence(device,fence);
    GetVulkanLoaderPresentRegistry().InvalidateAcquireFence(device,fence);
}
template<class Call> VkResult ObserveVkAcquireFenceStatus(VkDevice device,VkFence fence,Call&& original)
{
    const auto observation=CaptureVkAcquireFence(device,fence);
    const auto result=original(); observation.Publish(result); return result;
}
template<class Call,class Poll> VkResult ObserveVkAcquireFenceWait(VkDevice device,uint32_t count,
    const VkFence* fences,VkBool32 waitAll,Call&& original,Poll&& poll)
{
    std::vector<VkAcquireFenceObservation> observations;
    if (fences) for(uint32_t i=0;i<count;++i) {
        auto observation=CaptureVkAcquireFence(device,fences[i]);
        if(observation.Known()) observations.push_back(std::move(observation));
    }
    const auto result=original();
    if(result==VK_SUCCESS) for(const auto& observation:observations)
        observation.Publish(waitAll||count==1?VK_SUCCESS:poll(observation.fence));
    return result;
}
template<class Call,class Poll> VkResult ObserveVkAcquireFenceReset(VkDevice device,uint32_t count,
    const VkFence* fences,Call&& original,Poll&& poll)
{
    // Capture signals before reset erases them, and only poll known acquisition
    // fences. Ordinary game/model fences acquire no new GPU-query overhead.
    if(fences) for(uint32_t i=0;i<count;++i) {
        const auto observation=CaptureVkAcquireFence(device,fences[i]);
        if(observation.Known()) observation.Publish(poll(fences[i]));
    }
    const auto result=original();
    if(result==VK_SUCCESS&&fences)for(uint32_t i=0;i<count;++i)InvalidateVkAcquireFence(device,fences[i]);
    return result;
}
template<class Call,class Poll> void ObserveVkAcquireFenceDestroy(VkDevice device,VkFence fence,Call&& original,Poll&& poll)
{
    const auto observation=CaptureVkAcquireFence(device,fence);
    if(observation.Known()) observation.Publish(poll(fence));
    InvalidateVkAcquireFence(device,fence); original();
}
}
