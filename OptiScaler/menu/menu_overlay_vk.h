#pragma once

#include "SysUtils.h"
#include <vulkan/vulkan.hpp>
#include <mutex>
namespace DlssNr { struct VkObservedPresent; class VulkanPresentRegistry; }

namespace MenuOverlayVk
{
struct BoundaryContext
{
    uint64_t generation = 0;
    DlssNr::VulkanPresentRegistry* registry = nullptr;
    PFN_vkGetSwapchainImagesKHR getImages = nullptr;
};
void CreateSwapchain(VkDevice device, VkPhysicalDevice pd, VkInstance instance, HWND hwnd,
                     const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                     VkSwapchainKHR* pSwapchain, BoundaryContext boundary = {});
enum class QueuePresentStatus { Bypassed, Accepted, FailedBeforeSubmit, SubmitUncertain };
// Keep the guard alive through downstream Present: its wait array belongs to
// this generation. Busy/reentrant operations bypass/defer rather than waiting
// on callbacks. Locked helpers never acquire the operation gate recursively.
using PresentLock = std::unique_lock<std::mutex>;
PresentLock LockPresent();
// Replay the finalized overlay on an extra physical image while PresentLock
// pins the backend. The caller joins its GPU submission before releasing it.
VkResult CompositeGenerated(const PresentLock& lock, VkSwapchainKHR swapchain, VkCommandBuffer command,
                            VkImage image, unsigned width, unsigned height);
void GeneratedPresentationFinished(const PresentLock& lock, VkResult result);
QueuePresentStatus QueuePresent(VkQueue queue, VkPresentInfoKHR* pPresentInfo, const PresentLock& lock,
                                const DlssNr::VkObservedPresent* observed, BoundaryContext boundary = {});
// Called after the forwarded Present and registry success publication, with
// the same guard that pins the submitted overlay wait array.
void Presented(const PresentLock& lock, bool accepted, BoundaryContext boundary = {});
void Unavailable();
void DestroyVulkanObjects(bool shutdown);
// Call before and after the actual device-destruction boundary respectively.
// The latter drops only CPU ownership; it never calls through the dead device.
void ShutdownDevice(VkDevice device);
// Before actual swapchain destruction; only retires the matching renderer.
void ShutdownSwapchain(VkDevice device, VkSwapchainKHR swapchain);
void NotifyDeviceDestroyed(VkDevice device);
} // namespace MenuOverlayVk
