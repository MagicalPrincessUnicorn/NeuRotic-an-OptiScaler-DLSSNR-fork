#pragma once
#include "VulkanNrSession.h"
#include "VulkanNrGeneration.h"
#include "DlssNr_PresentResolution.h"
#include <functional>

namespace DlssNr
{
// Metadata only: no game/guide image handles, wrappers, wait semaphores or provider callbacks.
struct VkPresentInitializationRequest
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkNrFrameContract frame;
    std::shared_ptr<const NrConfigSnapshot<Config>> settings;
    std::shared_ptr<const VkNrWaitBudget> waitBudget;
    std::optional<VkExtent2D> observedRenderSize;
    std::optional<VkNrTemporalMetadata> resolutionMetadata;
    PresentInputDecision::Decision input;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED, targetFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED, motionFormat = VK_FORMAT_UNDEFINED;
    bool gameHdr = false;
    uint64_t privateAllocationBudget = 0;
};

inline bool VkPresentInitializationEligible(bool enabled, VkNrRoute route, VkNrPlacement placement,
    uint32_t requestedMode, uint32_t effectiveMode, bool hold, bool privateDlaa, bool captureOnly)
{
    return enabled && route == VkNrRoute::Present && placement == VkNrPlacement::AfterSR &&
        requestedMode == PresentResolution::FollowNative && effectiveMode == PresentResolution::FollowNative &&
        !hold && !privateDlaa && !captureOnly;
}

// The normal Present evaluator and metadata-only initializer finish their keys identically.
inline void CompleteVkPresentGenerationKey(VkNrGenerationKey& key, VkFormat color, VkFormat target,
    VkFormat depth, VkFormat motion, bool hdr)
{
    key.colorFormat = color; key.targetFormat = target; key.depthFormat = depth; key.motionFormat = motion;
    key.gameHdr = hdr;
    const auto flags = key.frame.temporal ? key.frame.temporal->featureFlags : 0u;
    key.creationFlags = (flags & ~NVSDK_NGX_DLSS_Feature_Flags_IsHDR) |
        (hdr ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : 0u);
}

enum class VkPresentInitializationStatus { Ready, Recorded, Pending, Refused, Uncertain };
struct VkPresentInitializationStep
{
    VkPresentInitializationStatus status = VkPresentInitializationStatus::Refused;
    std::string reason;
    bool recorded = false;
    uint32_t requestedPasses = 0;
    std::optional<VkNrPresentModelLifetime> modelLifetime;
};
struct VkPresentInitializationResult
{
    VkPresentInitializationStatus status = VkPresentInitializationStatus::Refused;
    std::string reason;
    uint32_t submissions = 0;
    double creationMs = 0, waitMs = 0;
};
using VkPresentPrepareModels = std::function<VkPresentInitializationStep(
    const VkPresentInitializationRequest&, VkCommandBuffer, VkNrUseId)>;
using VkPresentPollModels = std::function<void()>;

// Owns only a private command pool/use. All model resources remain generation-owned.
// The caller serializes this object and supplies actual model-owner callbacks.
class VkPresentInitializationOwner
{
public:
    VkPresentInitializationResult Prepare(const VkPresentInitializationRequest&,
        const std::function<bool()>& stillCurrent, const VkPresentPrepareModels&, const VkPresentPollModels&);
    void Maintain(const VkPresentPollModels&);
    bool Release(bool deviceAlive, const VkPresentPollModels&);
    void DeviceDestroyed(VkDevice);
    bool Busy() const { return bool(use_) || uncertain_; }
    VkDevice Device() const { return device_; }
private:
    bool Initialize(const VkPresentInitializationRequest&);
    bool Cancel(const VkPresentPollModels&);
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = UINT32_MAX;
    uint64_t deviceGeneration_ = 0;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkNrUseId use_;
    bool submitted_ = false, uncertain_ = false;
};
}
