#include "pch.h"
#include "UpscalerTime_Vk.h"
#include <mutex>

// The legacy global two-query pool had no device/recording completion identity.
// It also consumed unavailable/stale results. Keep these entry points inert
// until a completion-qualified timing owner can replace it. In particular,
// measuring an upscaler must not inject reset/timestamp dependencies into every
// game evaluation, including when NR is off, solely to populate an overlay.
void UpscalerTimeVk::Init(VkDevice, VkPhysicalDevice)
{
    static std::once_flag notice;
    std::call_once(notice, [] {
        LOG_INFO("Vulkan legacy upscaler GPU timing disabled: no query pool, resets, timestamps or host reads; "
                 "completion-qualified timing unavailable");
    });
}
void UpscalerTimeVk::UpscaleStart(VkCommandBuffer) {}
void UpscalerTimeVk::UpscaleEnd(VkCommandBuffer) {}
void UpscalerTimeVk::ReadUpscalingTime(VkDevice) {}
