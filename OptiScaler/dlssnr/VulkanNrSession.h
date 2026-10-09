#pragma once
#include <chrono>
#include <array>

#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include "VulkanNrFrameContract.h"
#include "VulkanNrRecording.h"
#include "VulkanNrReservations.h"
#include "DlssNr_PresentInputDecision.h"

#include <cstdint>
#include <functional>
#include <string>
#include <memory>
class Config;
template<class Source> struct NrConfigSnapshot;

namespace DlssNr
{

class VulkanNrRuntime;
class VkNrRecordingOwner;

enum class VkRecordStatus { NoWork, Complete, Partial };

// Shared by every pass of one selected evaluation. Waiting never grows with
// pass count; expiry does not release or acknowledge outstanding GPU work.
class VkNrWaitBudget
{
public:
    using Clock=std::chrono::steady_clock;
    explicit VkNrWaitBudget(Clock::time_point start=Clock::now(),std::function<Clock::time_point()> now={})
        :deadline_(start+std::chrono::milliseconds(100)),now_(std::move(now)){}
    uint64_t RemainingNs() const { return RemainingNs(now_?now_():Clock::now()); }
    uint64_t RemainingNs(Clock::time_point now) const {
        return now>=deadline_?0:static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(deadline_-now).count());
    }
private:
    Clock::time_point deadline_;
    std::function<Clock::time_point()> now_;
};

struct VkFrameRequest
{
    std::shared_ptr<const VkNrWaitBudget> waitBudget;
    VkNrFrameContract contract;
    VkNrUseId use;
    uint64_t privateAllocationBudget = 1536ull*1024*1024;
    VkNrRoute route = VkNrRoute::Native;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    NVSDK_NGX_Resource_VK* color = nullptr;
    NVSDK_NGX_Resource_VK* targetColor = nullptr;
    NVSDK_NGX_Resource_VK* depth = nullptr;
    NVSDK_NGX_Resource_VK* motion = nullptr;
    // CPU wrapper lifetime only. GPU storage remains owned by the captured
    // frame lease until its actual consumer recording completes.
    std::shared_ptr<std::array<NVSDK_NGX_Resource_VK,2>> capturedGuideWrappers;
    NVSDK_NGX_Resource_VK* exposure = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t guideWidth = 0;
    uint32_t guideHeight = 0;
    float preExposure = 1.0f;
    bool creationReset = false;
    bool gameHdr = false;
    bool depthInverted = false;
    bool gameReset = false;
    VkImageLayout colorLayout = VK_IMAGE_LAYOUT_GENERAL;
    // Set only by the authenticated game NGX SuperSampling evaluation hook.
    bool ngxSrInputContract = false;
    // Authenticated NGX SR/RR wrapper provenance; does not establish image layout.
    bool ngxGuideInputIdentity = false;
    // Readable input convention only at authenticated SR or public Streamline RR entry.
    bool ngxGuideInputReadContract = false;
    std::function<bool(VkNrUseId)> bindGuides;
    std::shared_ptr<const NrConfigSnapshot<Config>> settings;
    std::optional<uint32_t> effectiveResolutionMode;
    uint32_t effectiveResolutionScale = 0;
    std::optional<VkNrTemporalMetadata> resolutionMetadata;
    std::optional<VkExtent2D> observedRenderSize;
    std::optional<PresentInputDecision::Decision> presentInput;
    uint32_t passIndex=0;
    bool privateChain=false;
    bool ownedPresentInputs=false;
    bool captureOnly=false;
    bool finalColorOnly=false;
    std::optional<VkNrTemporalMetadata> providerTemporal;
    std::optional<VkNrReservation> composition;
    std::optional<VkExtent2D> workOverride;
    std::optional<int> selectedSrQuality;
};

struct VkNrRecordedOutput {
    VkNrFrameContract frame;VkNrUseId use;NVSDK_NGX_Resource_VK resource{};VkDeviceMemory memory=VK_NULL_HANDLE;
    VkImage gameInput=VK_NULL_HANDLE;uint64_t contentRevision=0,commandSerial=0;
    uint32_t requestedPasses=0,completedPasses=0;
};
struct VkRecordResult
{
    VkRecordStatus status = VkRecordStatus::NoWork;
    std::string reason;
    bool modelRecorded = false;
    bool outputWriteRecorded = false;
    VkNrUseId use;
    NVSDK_NGX_Resource_VK* preSrColor = nullptr;
    VkNrFrameContract preSrContract;
    bool preSrReady = false, forceSrReset = false;
    uint64_t preSrGeneration = 0;
    uint32_t requestedPasses=1,completedPasses=0;
    uint64_t outputVersion=0;
    bool chainDeliverable=false;
    std::optional<VkNrRecordedOutput> finalOutput;
    const char* inputLayoutBasis = "not-applicable";
    // Creation commands need real submission/completion, but do not deliver an NR image.
    bool preparationOnly = false;
};

// Admission describes the latest selected SR result, independently of retained history.
inline bool VkNrNativeDeliveryAccepted(const VkRecordResult& result, bool before,
                                      bool bound, bool srSucceeded) noexcept
{
    return srSucceeded && (!before || bound) && result.status == VkRecordStatus::Complete &&
           result.modelRecorded && result.outputWriteRecorded && result.reason.empty();
}

class VulkanNrSession
{
  public:
    using Recorder = std::function<VkRecordResult(const VkFrameRequest&, bool reset)>;

    VulkanNrSession(VkNrRoute route, VulkanNrRuntime& runtime);
    VkRecordResult Record(const VkFrameRequest& request, const Recorder& recorder);
    unsigned long long RecordedCount() const { return recorded_; }
    unsigned long long CompletedCount() const { return completed_; }
    bool ResetPending() const { return resetPending_; }
    void RequestReset() { resetPending_ = true; ++resetRevision_; }
    void ObserveCompleted(unsigned long long count = 1);
    void AbandonLatestRecording(bool currentCounted = true);
    void PollCompletions();
    VkNrRecordingOwner& Recordings();

  private:
    VkNrRoute route_;
    VulkanNrRuntime& runtime_;
    uint64_t observedEpoch_ = 0;
    bool resetPending_ = true;
    uint64_t resetRevision_ = 0;
    unsigned long long recorded_ = 0;
    unsigned long long completed_ = 0;
    std::vector<VkNrUseId> pendingCompletion_;
};

} // namespace DlssNr
