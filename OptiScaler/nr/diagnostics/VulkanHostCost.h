#pragma once
#include "HostCost.h"
#include <Windows.h>

#define NR_VULKAN_HOST_COST_DETAILS 1
namespace Neurotic::HostCost
{
// Updated at the application Present boundary from the real logger level.
// It never activates/rearms the independently bounded HostCost session.
inline std::atomic<bool> vulkanDetail {false};
inline void SetVulkanDetail(bool enabled) noexcept {
    vulkanDetail.store(enabled,std::memory_order_relaxed);
}
inline bool VulkanDetailEnabled() noexcept {
    return Enabled() && vulkanDetail.load(std::memory_order_relaxed);
}
inline uint64_t VulkanDetailClock() noexcept {
    const auto error=GetLastError();
    const auto now=Now();
    SetLastError(error);
    return now;
}
class VulkanScope : public Scope
{
public:
    explicit VulkanScope(Kind kind) noexcept
        : Scope(kind,1,VulkanDetailClock,VulkanDetailEnabled()) {}
};
}
