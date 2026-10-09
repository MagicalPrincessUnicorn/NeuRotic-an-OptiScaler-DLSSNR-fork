#pragma once
#include <vulkan/vulkan.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace DlssNr
{
enum class VkNrRoute { Native, Present };
enum class VkNrPlacement { AfterSR, BeforeSR, AfterRR };
struct VkNrRect
{
    uint32_t x = 0, y = 0, width = 0, height = 0;
    bool operator==(const VkNrRect&) const = default;
};
struct VkNrTemporalMetadata
{
    VkNrRect color, depth, motion;
    float motionScaleX = 1, motionScaleY = 1, jitterX = 0, jitterY = 0;
    uint32_t featureFlags = 0;
    bool depthInverted = false, reset = false;
    uint64_t featureGeneration = 0, frameToken = 0;
    bool providerFrameKnown = false;
    uint64_t providerGeneration = 0; uint32_t viewport = UINT32_MAX;
};
struct VkNrRepresentation
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    uint64_t recipeRevision = 0;
    bool operator==(const VkNrRepresentation&) const = default;
};
// Identity of an observed synchronous NGX evaluation, not a guessed game frame.
// The invocation is assigned at the selected ingress and never inferred from
// raster size, Present count or past queue activity.
struct VkNrEvaluationIdentity
{
    VkCommandBuffer commandBuffer=VK_NULL_HANDLE;
    uint64_t incarnation=0, invocation=0, featureGeneration=0;
    explicit operator bool() const {return commandBuffer&&incarnation&&invocation&&featureGeneration;}
    bool operator==(const VkNrEvaluationIdentity&) const = default;
};
struct VkNrFrameContract
{
    VkNrRoute route = VkNrRoute::Native;
    VkNrPlacement placement = VkNrPlacement::AfterSR;
    uint64_t deviceGeneration = 0, routeEpoch = 0, swapchainGeneration = 0, settingsRevision = 0;
    VkQueue queue = VK_NULL_HANDLE;
    // Recording family is known before submission. A null queue is never a
    // claim about which queue will execute a Native command buffer.
    uint32_t queueFamily = UINT32_MAX;
    uint64_t resumeGeneration = 0, guideGeneration = 0, inputInterruptionEpoch = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    uint32_t swapchainImageIndex = UINT32_MAX;
    uint64_t acquireGeneration = 0;
    VkExtent2D output{}, work{};
    VkNrRepresentation representation;
    VkNrEvaluationIdentity evaluation;
    std::optional<VkNrTemporalMetadata> temporal;
};
inline bool ValidVkNrRect(const VkNrRect& r) noexcept
{
    return r.width && r.height && r.width <= 8192 && r.height <= 8192 &&
           r.x <= UINT32_MAX - r.width && r.y <= UINT32_MAX - r.height;
}
inline bool ValidVkNrTemporal(const VkNrTemporalMetadata& v) noexcept
{
    return ValidVkNrRect(v.color) && ValidVkNrRect(v.depth) && ValidVkNrRect(v.motion) &&
           std::isfinite(v.motionScaleX) && std::isfinite(v.motionScaleY) &&
           std::isfinite(v.jitterX) && std::isfinite(v.jitterY);
}
inline bool ValidateVkNrFrameContract(const VkNrFrameContract& v) noexcept
{
    const auto size = [](VkExtent2D e) { return e.width >= 8 && e.height >= 8 && e.width <= 8192 && e.height <= 8192; };
    return size(v.output) && size(v.work) && v.representation.format != VK_FORMAT_UNDEFINED &&
           (!v.temporal || ValidVkNrTemporal(*v.temporal));
}
// Guide rectangles still refer to original images. Scale only model-space motion/jitter.
inline VkNrTemporalMetadata ScaleVkNrTemporal(const VkNrTemporalMetadata& v, VkExtent2D source, VkExtent2D work) noexcept
{
    auto result = v;
    if (!source.width || !source.height || !work.width || !work.height)
    {
        result.motionScaleX = result.motionScaleY = result.jitterX = result.jitterY =
            std::numeric_limits<float>::quiet_NaN();
        return result;
    }
    const float x = float(work.width) / float(source.width), y = float(work.height) / float(source.height);
    result.motionScaleX *= x; result.motionScaleY *= y;
    result.jitterX *= x; result.jitterY *= y;
    return result;
}
}
