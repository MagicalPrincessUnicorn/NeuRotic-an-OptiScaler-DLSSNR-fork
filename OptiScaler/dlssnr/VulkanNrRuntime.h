#pragma once

#include "VulkanNrSession.h"
#include "VulkanNrRecording.h"

#include <functional>

namespace DlssNr
{

enum class VkRouteDeviceUse { FirstUse, Reuse, Conflict };
inline VkRouteDeviceUse ClassifyVkRouteDevice(VkDevice routeDevice, VkDevice runtimeDevice,
                                               VkDevice requested) noexcept
{
    if (requested == VK_NULL_HANDLE ||
        (runtimeDevice != VK_NULL_HANDLE && runtimeDevice != requested) ||
        (routeDevice != VK_NULL_HANDLE && routeDevice != requested))
        return VkRouteDeviceUse::Conflict;
    return routeDevice == VK_NULL_HANDLE ? VkRouteDeviceUse::FirstUse : VkRouteDeviceUse::Reuse;
}

struct VulkanNrExports
{
    void* module = nullptr;
    int (__cdecl* probe)(const wchar_t*) = nullptr;
    int (__cdecl* init)(const wchar_t*, const wchar_t*, void*, void*, void*, int) = nullptr;
    int (__cdecl* create)(void*, void*, void**) = nullptr;
    int (__cdecl* evaluate)(void*, void*, void*) = nullptr;
    int (__cdecl* release)(void*) = nullptr;
    int (__cdecl* shutdown)(int) = nullptr;
};

// The one device-scoped initialization and route-switch authority for Vulkan NR.
// The caller supplies the actual forwarder initializer; a failed init never marks
// a device ready or allows a second route to claim that it was initialized.
class VulkanNrRuntime
{
  public:
    bool EnsureDevice(VkDevice device, const std::function<bool()>& initialize);
    void Activate(VkNrRoute route);
    bool IsActive(VkNrRoute route) const { return hasActive_ && active_ == route; }
    uint64_t Epoch() const { return epoch_; }
    void ResetDevice() { device_ = VK_NULL_HANDLE; initialized_ = false; active_ = VkNrRoute::Native; ++epoch_; }
    VkDevice Device() const { return device_; }
    bool Initialized() const { return initialized_; }
    VkNrRecordingOwner& Recordings() { return VulkanNrRecordings(); }
    VulkanNrExports& Exports() { return exports_; }
    const VulkanNrExports& Exports() const { return exports_; }

  private:
    VkDevice device_ = VK_NULL_HANDLE;
    bool initialized_ = false;
    bool hasActive_ = false;
    VkNrRoute active_ = VkNrRoute::Native;
    uint64_t epoch_ = 0;
    VulkanNrExports exports_;
};

} // namespace DlssNr
