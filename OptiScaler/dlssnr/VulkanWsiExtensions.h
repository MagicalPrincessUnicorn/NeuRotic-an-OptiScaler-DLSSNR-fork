#pragma once
#include <vulkan/vulkan.h>

namespace DlssNr::WsiExtensions
{
// Stable registry values. Preserved SDKs may hide the metering enum behind
// VK_ENABLE_BETA_EXTENSIONS even when its structure is available. Recognizing
// caller nodes must not require enabling beta APIs or changing shared headers.
inline constexpr auto SwapchainLatencyCreate = static_cast<VkStructureType>(1000505007);
inline constexpr auto SetPresentConfig = static_cast<VkStructureType>(1000613000);

#ifdef VK_NV_present_metering
using PresentConfig = VkSetPresentConfigNV;
#ifdef VK_ENABLE_BETA_EXTENSIONS
static_assert(VK_STRUCTURE_TYPE_SET_PRESENT_CONFIG_NV == SetPresentConfig);
#endif
#else
// VkSetPresentConfigNV, VK_NV_present_metering. Feedback is driver output:
// retain the original chain pointer so writes reach the caller's storage.
struct PresentConfig
{
    VkStructureType sType;
    const void* pNext;
    uint32_t numFramesPerBatch;
    uint32_t presentConfigFeedback;
};
#endif
#ifdef VK_NV_low_latency2
static_assert(VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV == SwapchainLatencyCreate);
#endif
}
