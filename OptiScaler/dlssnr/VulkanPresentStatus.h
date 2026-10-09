#pragma once

#include "VulkanPresentExecutor.h"
#include "VulkanNrResolution.h"
#include "DlssNr_PresentPacing.h"

#include <mutex>
#include <atomic>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

namespace DlssNr
{

// Per-calling-thread bounded diagnostic history; does not control rendering.
class VkPresentLogGate
{
  public:
    bool ShouldLog(uint64_t generation, uintptr_t queue, uint32_t decision,
                   bool originalCalled, VkResult result, std::string_view reason)
    {
        for (size_t i = 0; i < count_; ++i)
        {
            const auto& e = entries_[i];
            if (e.generation == generation && e.queue == queue && e.decision == decision &&
                e.originalCalled == originalCalled && e.result == result && e.reason == reason)
                return false;
        }
        entries_[next_] = { generation, queue, decision, originalCalled, result, std::string(reason) };
        next_ = (next_ + 1) % entries_.size();
        if (count_ < entries_.size()) ++count_;
        return true;
    }

  private:
    struct Entry
    {
        uint64_t generation = 0;
        uintptr_t queue = 0;
        uint32_t decision = 0;
        bool originalCalled = false;
        VkResult result = VK_SUCCESS;
        std::string reason;
    };
    std::array<Entry, 64> entries_ {};
    size_t count_ = 0, next_ = 0;
};

struct VkNrGpuStageSample {
    VkNrUseId use;VkNrFrameContract frame;uint32_t pass=0;
    // Encode, rescale, model, resolve. Final copyback/WSI are separate boundaries.
    std::array<double,4> milliseconds{};
};
struct VkPresentStatusSnapshot
{
    bool requested = false;
    bool active = false;
    bool failed = false;
    bool possibleTargetWrite = false;
    bool needsRecreate = false;
    uint32_t route = 0;
    PresentInput::Policy requestedPolicy = PresentInput::Policy::AutoGuides;
    PresentInputDecision::InputClass actualInputClass = PresentInputDecision::InputClass::Refused;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D extent {};
    uint64_t generation = 0;
    unsigned long long attempts = 0;
    unsigned long long recorded = 0;
    unsigned long long composed = 0;
    unsigned long long submitted = 0;
    unsigned long long completed = 0;
    unsigned long long originalAccepted = 0;
    unsigned long long uncertain = 0;
    unsigned long long consecutiveFallbacks = 0;
    std::string reason;
    VkNrWorkloadStatus workload;
    uint32_t requestedPasses=0,completedPasses=0;
    uint64_t outputVersion=0;
    std::array<VkNrGpuStageSample,10> gpuStages{},nativeGpuStages{};
    PresentPacing::WindowSummary pacing;
    uint64_t realFrameContributions=0,retainedFgConsumers=0;
};

// Separate from the D3D Present owner: Vulkan has Vulkan formats and a fence-based completion
// boundary, and its counters must not be presented as DXGI state or scanout proof.
class VulkanPresentStatus
{
  public:
    // Once the app boundary is observed, generated physical Presents must not
    // overwrite its NR delivery result with an intentional bypass status.
    void ApplicationProvider(uint64_t provider){applicationProvider_.store(provider,std::memory_order_release);}
    bool PhysicalReports(uint64_t provider) const{return !provider||applicationProvider_.load(std::memory_order_acquire)!=provider;}
    void Observe(const VkObservedPresent* observed, const VkPresentExecution* execution,
                 bool originalCalled, VkResult originalResult, const char* reason);
    void GpuCompleted();
    void ObserveWorkload(const VkNrWorkloadStatus&);
    VkPresentStatusSnapshot Snapshot() const;
    void ObserveFgOwnership(uint64_t primary,uint64_t consumers);
    bool ObserveGpuStages(const VkNrRecordingOwner&,const VkNrGpuStageSample&);
    PresentPacing::CallToken BeginCall(uint32_t route,double startMs);
    void FinishCall(const PresentPacing::CallToken&,double adapterMs,double hookMs,double originalMs,bool failed);
    void ExpectGpu(const PresentPacing::CallToken&,VkNrUseId,uint32_t passes,double atMs);

  private:
    std::atomic<uint64_t> applicationProvider_{0};
    mutable std::mutex mutex_;
    VkPresentStatusSnapshot state_;
    PresentPacing::Window<> pacing_;
    uint64_t calls_=0;double priorMs_=0;
    std::unordered_map<uint64_t,double> callIntervals_;
    struct PendingGpu { PresentPacing::CallToken token;VkNrUseId use;uint32_t passes;double atMs;std::array<bool,10> ready{};std::array<double,10> values{}; };
    std::vector<PendingGpu> pendingGpu_;
};

VulkanPresentStatus& GetVulkanPresentStatus();

} // namespace DlssNr
