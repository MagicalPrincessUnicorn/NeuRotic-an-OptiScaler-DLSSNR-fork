#pragma once
#include "VulkanPresentColor.h"
#include "VulkanDevicePreparation.h"
#include "VulkanWsiExtensions.h"

#include "DlssNr_PresentInputDecision.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <array>
#include <cstring>

#ifndef VK_KHR_win32_surface
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

namespace DlssNr
{

struct VkSwapchainChainDescription
{
    std::array<VkStructureType, 16> types {};
    size_t count = 0;
    bool cycle = false;
    bool truncated = false;
};

// Observes only the common Vulkan header. This never qualifies a chain for mutation.
// Vulkan requires the caller's chain pointers to remain valid during the call.
inline VkSwapchainChainDescription DescribeVkSwapchainChain(const void* chain) noexcept
{
    VkSwapchainChainDescription out {};
    std::array<const VkBaseInStructure*, 16> visited {};
    auto node = static_cast<const VkBaseInStructure*>(chain);
    while (node != nullptr)
    {
        for (size_t i = 0; i < out.count; ++i)
            if (visited[i] == node) { out.cycle = true; return out; }
        if (out.count == out.types.size()) { out.truncated = true; return out; }
        visited[out.count] = node;
        out.types[out.count++] = node->sType;
        node = node->pNext;
    }
    return out;
}

inline bool IsVkSwapchainUsageChainKnown(const void* chain) noexcept
{
    const auto description = DescribeVkSwapchainChain(chain);
    if (description.cycle || description.truncated) return false;
    bool fullscreen = false, monitor = false, latency = false;
    for (size_t i = 0; i < description.count; ++i)
    {
        // These two structures extend both swapchain creation and surface-capability
        // queries. They select fullscreen behavior/monitor, without overriding usage.
        switch (description.types[i])
        {
        case VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT:
            if (fullscreen) return false;
            fullscreen = true;
            break;
        case VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT:
            if (monitor) return false;
            monitor = true;
            break;
        case WsiExtensions::SwapchainLatencyCreate:
            // Creation-only latency policy does not override image usage.
            // Preserve it at CreateSwapchain, omit it from surface queries.
            if (latency) return false;
            latency = true;
            break;
        default: return false;
        }
    }
    return true;
}

// NR changes only the wait list, never the presented images or present count.
// NVIDIA metering therefore remains attached to the same original Present.
inline bool IsVkPresentWaitReplacementChainKnown(const void* chain) noexcept
{
    const auto description = DescribeVkSwapchainChain(chain);
    if (description.cycle || description.truncated) return false;
    if (description.count == 0) return true;
    if (description.count != 1 || description.types[0] != WsiExtensions::SetPresentConfig) return false;
    WsiExtensions::PresentConfig config {};
    std::memcpy(&config, chain, sizeof(config));
    return config.numFramesPerBatch <= 8;
}

inline VkImageUsageFlags QueryVkSwapchainSurfaceUsage(VkPhysicalDevice physical,
    const VkSwapchainCreateInfoKHR& create, PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getCapabilities,
    PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR getCapabilities2) noexcept
{
    if (create.pNext != nullptr && IsVkSwapchainUsageChainKnown(create.pNext))
    {
        const auto description = DescribeVkSwapchainChain(create.pNext);
        bool hasLatency = false, hasSelectors = false;
        for (size_t i = 0; i < description.count; ++i)
        {
            hasLatency |= description.types[i] == WsiExtensions::SwapchainLatencyCreate;
            hasSelectors |= description.types[i] != WsiExtensions::SwapchainLatencyCreate;
        }
        // Latency-only creation uses ordinary surface capabilities.
        if (!hasSelectors)
        {
            VkSurfaceCapabilitiesKHR capabilities {};
            return getCapabilities != nullptr &&
                getCapabilities(physical, create.surface, &capabilities) == VK_SUCCESS
                ? capabilities.supportedUsageFlags : 0;
        }
        // Fullscreen capabilities may differ from the default surface query.
        if (getCapabilities2 == nullptr) return 0;
        VkSurfaceFullScreenExclusiveInfoEXT fullscreen {};
        VkSurfaceFullScreenExclusiveWin32InfoEXT monitor {};
        const void* surfaceChain = create.pNext;
        if (hasLatency)
        {
            // Make a typed query-only projection. Never relink caller storage
            // or pass creation-only structures to SurfaceCapabilities2.
            surfaceChain = nullptr;
            for (auto node = static_cast<const VkBaseInStructure*>(create.pNext); node; node = node->pNext)
            {
                if (node->sType == VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT)
                {
                    std::memcpy(&fullscreen, node, sizeof(fullscreen));
                    fullscreen.pNext = nullptr;
                    if (surfaceChain == nullptr) surfaceChain = &fullscreen;
                    else monitor.pNext = &fullscreen;
                }
                else if (node->sType == VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT)
                {
                    std::memcpy(&monitor, node, sizeof(monitor));
                    monitor.pNext = nullptr;
                    if (surfaceChain == nullptr) surfaceChain = &monitor;
                    else fullscreen.pNext = &monitor;
                }
            }
        }
        VkPhysicalDeviceSurfaceInfo2KHR info {};
        info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
        info.surface = create.surface;
        info.pNext = surfaceChain;
        VkSurfaceCapabilities2KHR capabilities {};
        capabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
        return getCapabilities2(physical, &info, &capabilities) == VK_SUCCESS
                   ? capabilities.surfaceCapabilities.supportedUsageFlags : 0;
    }
    VkSurfaceCapabilitiesKHR capabilities {};
    return getCapabilities != nullptr &&
                   getCapabilities(physical, create.surface, &capabilities) == VK_SUCCESS
               ? capabilities.supportedUsageFlags : 0;
}

inline bool ShouldHookVulkanPresent(bool overlayEnabled, bool nrEnabled, uint32_t route) noexcept
{
    // Route/enable may change after device creation. Observe WSI so a late
    // request can report the missing device/swapchain prerequisites.
    (void) overlayEnabled; (void) nrEnabled; (void) route;
    return true;
}

enum class VkPresentRefusal
{
    None,
    AutoImageOnly,
    Disabled,
    NativeRoute,
    GuidesRequired,
    InvalidPolicy,
    FrameGenerationActive,
    FrameGenerationUnknown,
    DeviceUnavailable,
    DeviceRestart,
    ModelExtensionsMissing,
    ModelExtensionsUnsupported,
    StorageWriteUnsupported,
    StorageWriteUnsafeChain,
    TransferUnsupported,
    UnknownUsageChain,
    TransferNotEnabled,
    UnsupportedSdrRepresentation,
    ProtectedSwapchain,
    MultipleSwapchains,
    StaleImage,
    ImageNotAcquired,
    StaleGeneration,
    WrongQueue,
    ImageOwnershipUnproven,
    InvalidExtent,
    SwapchainUnavailable
};

struct VkPresentRequest
{
    bool enabled = false;
    uint32_t route = 0;
    PresentInput::Policy policy = PresentInput::Policy::AutoGuides;
    bool fgKnownActive = false;
    bool fgStateKnown = true;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D extent {};
    uint32_t arrayLayers = 0;
    VkImageUsageFlags imageUsage = 0;
    VkSwapchainCreateFlagsKHR flags = 0;
    uint32_t swapchainCount = 0;
    uint32_t imageIndex = UINT32_MAX;
    bool acquiredObserved = false;
    uint64_t deviceGeneration = 0;
    uint64_t swapchainGeneration = 0;
    uint64_t acquireGeneration = 0;
};

struct VkPresentCapabilities
{
    bool applicationBeforeProvider = false;
    bool guidesQualified = false;
    bool deviceObserved = false;
    bool modelExtensionsEnabled = false;
    bool modelExtensionsUnsupported = false;
    bool storageWriteUnsupported = false;
    bool storageWriteUnsafeChain = false;
    bool routePreparedAtDeviceCreation = false;
    bool surfaceUsageKnown = false;
    VkImageUsageFlags surfaceUsage = 0;
    bool swapchainPNextKnown = false;
    bool swapchainObserved = false;
    bool imagesObserved = false;
    uint32_t imageCount = 0;
    bool queueObserved = false;
    VkQueueFlags queueFlags = 0;
    bool queueFamilyMatches = false;
    bool presentQueueAccessQualified = false;
    uint64_t deviceGeneration = 0;
    uint64_t swapchainGeneration = 0;
};

struct VkPresentAdmission
{
    bool allowed = false;
    PresentInputDecision::InputClass actualInputClass = PresentInputDecision::InputClass::Refused;
    VkPresentRefusal reason = VkPresentRefusal::Disabled;
    bool needsRecreate = false;
};

struct VkSwapchainUsageDecision
{
    VkImageUsageFlags usage = 0;
    bool amended = false;
    VkPresentRefusal reason = VkPresentRefusal::Disabled;
};

inline VkSwapchainUsageDecision PrepareVkSwapchainUsage(VkImageUsageFlags original,
    VkImageUsageFlags supported, bool pNextKnown, bool requested) noexcept
{
    if (!requested) return { original, false, VkPresentRefusal::Disabled };
    if (!pNextKnown) return { original, false, VkPresentRefusal::UnknownUsageChain };
    constexpr VkImageUsageFlags required = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((supported & required) != required)
        return { original, false, VkPresentRefusal::TransferUnsupported };
    const auto usage = original | required;
    return { usage, usage != original, VkPresentRefusal::None };
}

inline VkPresentAdmission DecideVkPresent(const VkPresentRequest& r,
                                           const VkPresentCapabilities& c) noexcept
{
    const auto refuse = [](VkPresentRefusal why, bool recreate = false) noexcept {
        return VkPresentAdmission { false, PresentInputDecision::InputClass::Refused, why, recreate };
    };
    if (!r.enabled) return refuse(VkPresentRefusal::Disabled);
    if (r.route == 0) return refuse(VkPresentRefusal::NativeRoute);
    if (r.route != 1 && r.route != 2) return refuse(VkPresentRefusal::InvalidPolicy);
    if (r.policy == PresentInput::Policy::Invalid) return refuse(VkPresentRefusal::InvalidPolicy);
    if (r.policy == PresentInput::Policy::RequireGuides && !c.guidesQualified) return refuse(VkPresentRefusal::GuidesRequired);
    if (!r.fgStateKnown) return refuse(VkPresentRefusal::FrameGenerationUnknown);
    if (r.fgKnownActive && !c.applicationBeforeProvider) return refuse(VkPresentRefusal::FrameGenerationActive);
    if (!c.deviceObserved) return refuse(VkPresentRefusal::DeviceUnavailable);
    if (c.modelExtensionsUnsupported) return refuse(VkPresentRefusal::ModelExtensionsUnsupported);
    if (c.storageWriteUnsupported) return refuse(VkPresentRefusal::StorageWriteUnsupported);
    if (c.storageWriteUnsafeChain) return refuse(VkPresentRefusal::StorageWriteUnsafeChain);
    if (!c.routePreparedAtDeviceCreation) return refuse(VkPresentRefusal::DeviceRestart, true);
    if (!c.modelExtensionsEnabled) return refuse(VkPresentRefusal::ModelExtensionsMissing, true);
    if (!c.swapchainObserved || !c.imagesObserved) return refuse(VkPresentRefusal::SwapchainUnavailable);
    if (r.deviceGeneration != c.deviceGeneration ||
        r.swapchainGeneration != c.swapchainGeneration)
        return refuse(VkPresentRefusal::StaleGeneration);
    if (r.swapchainCount != 1) return refuse(VkPresentRefusal::MultipleSwapchains);
    if (r.flags & VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR)
        return refuse(VkPresentRefusal::ProtectedSwapchain);
    if (r.arrayLayers != 1 || r.extent.width < 8 || r.extent.height < 8 ||
        r.extent.width > 8192 || r.extent.height > 8192)
        return refuse(VkPresentRefusal::InvalidExtent);
    if (!SelectVkPresentColorRecipe(r.format,r.colorSpace).supported)
        return refuse(VkPresentRefusal::UnsupportedSdrRepresentation);
    if (!c.surfaceUsageKnown) return refuse(VkPresentRefusal::TransferUnsupported);
    if (!c.swapchainPNextKnown) return refuse(VkPresentRefusal::UnknownUsageChain);
    constexpr VkImageUsageFlags required = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((c.surfaceUsage & required) != required) return refuse(VkPresentRefusal::TransferUnsupported);
    if ((r.imageUsage & required) != required) return refuse(VkPresentRefusal::TransferNotEnabled, true);
    if (r.imageIndex >= c.imageCount) return refuse(VkPresentRefusal::StaleImage);
    if (!r.acquiredObserved) return refuse(VkPresentRefusal::ImageNotAcquired);
    if (!c.queueObserved || !c.queueFamilyMatches ||
        (c.queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) !=
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
        return refuse(VkPresentRefusal::WrongQueue);
    if (!c.presentQueueAccessQualified) return refuse(VkPresentRefusal::ImageOwnershipUnproven);
    const bool guided = c.guidesQualified && r.policy != PresentInput::Policy::ImageOnly;
    return { true, guided ? PresentInputDecision::InputClass::Guided : PresentInputDecision::InputClass::ImageOnly,
             !guided && r.route == 2 && r.policy == PresentInput::Policy::AutoGuides
                 ? VkPresentRefusal::AutoImageOnly : VkPresentRefusal::None,
             false };
}

inline const char* VkPresentRefusalText(VkPresentRefusal reason) noexcept
{
    switch (reason)
    {
    case VkPresentRefusal::None: return "";
    case VkPresentRefusal::AutoImageOnly: return "Vulkan guides unavailable; using image-only input";
    case VkPresentRefusal::Disabled: return "Neural Rendering is off";
    case VkPresentRefusal::NativeRoute: return "Native Temporal is selected";
    case VkPresentRefusal::GuidesRequired: return "Required Vulkan guides are unavailable";
    case VkPresentRefusal::InvalidPolicy: return "Unsupported Vulkan Present route or input policy";
    case VkPresentRefusal::FrameGenerationUnknown: return "Image unchanged. Vulkan frame generation state has not been observed successfully";
    case VkPresentRefusal::FrameGenerationActive: return "Vulkan Present with frame generation is unqualified";
    case VkPresentRefusal::DeviceUnavailable: return "Vulkan device facts unavailable";
    case VkPresentRefusal::DeviceRestart: return "Restart required to prepare Vulkan Present at device creation";
    case VkPresentRefusal::ModelExtensionsMissing: return "Restart required with Vulkan model extensions";
    case VkPresentRefusal::ModelExtensionsUnsupported: return "Vulkan driver lacks model extensions; see startup prerequisite names";
    case VkPresentRefusal::StorageWriteUnsupported: return "Vulkan device lacks shaderStorageImageWriteWithoutFormat for NR composition";
    case VkPresentRefusal::StorageWriteUnsafeChain: return "Vulkan caller feature chain cannot safely enable NR composition; restarting alone cannot repair this chain";
    case VkPresentRefusal::TransferUnsupported: return "Vulkan surface does not support required image transfers";
    case VkPresentRefusal::UnknownUsageChain: return "Vulkan swapchain extension chain cannot be amended safely";
    case VkPresentRefusal::TransferNotEnabled: return "Recreate swapchain with transfer usage for Vulkan Present";
    case VkPresentRefusal::UnsupportedSdrRepresentation: return "Vulkan Present format/color space has no qualified SDR/PQ/scRGB recipe";
    case VkPresentRefusal::ProtectedSwapchain: return "Protected Vulkan swapchain is unsupported";
    case VkPresentRefusal::MultipleSwapchains: return "Vulkan Present requires one swapchain";
    case VkPresentRefusal::StaleImage: return "Vulkan Present image index is not in the live swapchain";
    case VkPresentRefusal::ImageNotAcquired: return "Vulkan swapchain image acquisition was not observed";
    case VkPresentRefusal::StaleGeneration: return "Vulkan device or swapchain generation changed";
    case VkPresentRefusal::WrongQueue: return "Present queue cannot run the Vulkan NR work";
    case VkPresentRefusal::ImageOwnershipUnproven: return "Vulkan swapchain image ownership on the Present queue is unproved";
    case VkPresentRefusal::InvalidExtent: return "Vulkan swapchain image size or layers are unsupported";
    case VkPresentRefusal::SwapchainUnavailable: return "Vulkan swapchain images are not observed";
    }
    return "Unknown Vulkan Present refusal";
}

} // namespace DlssNr
