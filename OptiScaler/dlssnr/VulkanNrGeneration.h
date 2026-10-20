#pragma once
#include "VulkanNrFrameContract.h"
#include "VulkanNrRecording.h"
#include "VulkanNrTuning.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace DlssNr
{
struct VkNrGenerationKey
{
    VkNrFrameContract frame;
    VkFormat colorFormat=VK_FORMAT_UNDEFINED,depthFormat=VK_FORMAT_UNDEFINED,
             motionFormat=VK_FORMAT_UNDEFINED,targetFormat=VK_FORMAT_UNDEFINED;
    bool hold=false;
    bool gameHdr=false;
    bool privateDlaa=false;
    VkExtent2D srOutput{};
    uint32_t srQuality=0;
    uint64_t preSrEpoch=0;
    uint32_t creationFlags=0;
    uint32_t passIndex=0,upFilter=0,downFilter=0;
    VkTuning::Settings tuning;
    bool operator==(const VkNrGenerationKey&) const;
};
bool CompatibleVkNrGeneration(const VkNrGenerationKey&,const VkNrGenerationKey&);
struct VkNrGenerationPayload
{
    // Only actual private Vulkan allocation requirements; opaque vendor allocations are unknown.
    uint64_t privateBytes=0;
    virtual void Abandon() {}
    virtual ~VkNrGenerationPayload()=default;
};
struct VkNrGenerationCreation
{
    std::unique_ptr<VkNrGenerationPayload> payload;
    bool ready=false,recorded=false;
    std::string reason;
};
struct VkNrGenerationResult
{
    bool ready=false,pending=false,failed=false;
    uint64_t generation=0;
    std::string reason;
    std::string diagnostic;
};
// Diagnostic projection only. No owner/resource/eligibility capability.
struct VkNrKeyDifference {const char* field=nullptr;uint64_t before=0,after=0;};
VkNrKeyDifference FirstVkNrGenerationDifference(const VkNrGenerationKey&,const VkNrGenerationKey&)noexcept;
enum class VkNrGenerationLossCause {None,PriorResume,PreparationReplacement,InitializationUnready,RetainedUseUnready,ExplicitDisable};
struct VkNrGenerationLoss {
    VkNrGenerationLossCause cause=VkNrGenerationLossCause::None;
    const char* origin=nullptr;const char* caller=nullptr;
    uint64_t oldGeneration=0,nextCandidate=0,allocatedReplacement=0;uint32_t thread=0;
    VkNrRoute route=VkNrRoute::Native;uint32_t pass=0;
    uint64_t deviceGeneration=0,routeEpoch=0,resumeGeneration=0,swapchainGeneration=0;
    VkNrKeyDifference difference;VkNrUseId use;
    bool reusableObserved=false,reusable=false,gpuCompleteObserved=false,gpuComplete=false;
    bool snapshotUnavailable=false;
    std::optional<VkNrRecordingOwner::UseRecordingSnapshot> postPredicate;
};
class VkNrGenerationOwner
{
  public:
    using Create=std::function<VkNrGenerationCreation(const VkNrGenerationKey&,VkNrUseId,uint64_t)>;
    using Release=std::function<bool(VkNrGenerationPayload&)>;
    VkNrGenerationOwner(VkNrRecordingOwner&,Create,Release,size_t perPassCapacity=8,
                        uint64_t privateBudget=1536ull*1024*1024);
    ~VkNrGenerationOwner();
    // The callback may wait on an actual submitted predecessor. Readiness is still
    // established by the recording owner afterwards, never by callback return.
    // Preparation observes existing metadata without retaining a history-writing
    // use. New creations still retain their initialization. Evaluation may set
    // existingOnly to prohibit creation on an inference command buffer.
    VkNrGenerationResult Prepare(const VkNrGenerationKey&,VkNrUseId,
                                 const std::function<void(VkNrUseId)>& wait = {},bool preparationOnly=false,
                                 bool existingOnly=false,VkNrGenerationLoss* diagnostic=nullptr);
    bool Commit(uint64_t readyGeneration);
    bool Retain(uint64_t generation,VkNrUseId);
    void RetireCompleted(const VkNrRecordingOwner&,VkNrGenerationLoss* diagnostic=nullptr);
    std::optional<VkNrGenerationKey> AppliedKey() const;
    bool CanApply(const VkNrGenerationKey&) const;
    bool ReadyForUse(uint64_t,VkNrUseId,VkNrUseId* firstBlocked=nullptr) const;
    VkNrGenerationPayload* Payload(uint64_t) const;
    uint64_t AppliedGeneration() const { return applied_; }
    uint64_t PrivateBytes() const { return bytes_; }
    void SetPrivateBudget(uint64_t budget) { budget_=budget; }
    size_t Count() const { return entries_.size(); }
    void Disable(const char* origin="direct_disable",VkNrGenerationLoss* diagnostic=nullptr);
    void RetryPreparation() { refusedKey_.reset();refusedReason_.clear(); }
    bool DrainReleased(VkNrGenerationLoss* diagnostic=nullptr);
    // No driver/provider calls on a dead device. Caller keeps unresolved diagnostics separately.
    void AbandonDevice();
  private:
    struct Entry {
        uint64_t id;VkNrGenerationKey key;std::unique_ptr<VkNrGenerationPayload> payload;
        std::vector<VkNrUseId> uses;VkNrUseId initialization;
        bool ready=false,initializationComplete=false;
    };
    void ObserveLoss(VkNrGenerationLoss*,VkNrGenerationLossCause,const Entry&,const char*,VkNrUseId={},
                     bool=false,bool=false,bool=false,bool=false,const VkNrGenerationKey* =nullptr)const noexcept;
    Entry* Find(uint64_t);
    const Entry* Find(uint64_t) const;
    VkNrRecordingOwner& recordings_;Create create_;Release release_;
    size_t capacity_;uint64_t budget_,bytes_=0,next_=1,applied_=0;
    std::vector<Entry> entries_;
    std::optional<VkNrGenerationKey> refusedKey_;
    std::string refusedReason_;
    unsigned completionDiagnostics_=0;
};
// Serialized by its existing model/session owner. A revision from another
// owner is not an acknowledgment token. Pending-only events deliberately keep
// the revision; only Request defeats an older captured acknowledgment.
class VkNrResetRevision
{
  public:
    explicit VkNrResetRevision(uint64_t initialRevision=1) noexcept:requested_(initialRevision) {}
    uint64_t Capture() const noexcept { return requested_; }
    bool Pending() const noexcept { return pending_; }
    void MarkPending() noexcept { pending_=true; }
    void Request() noexcept { pending_=true;++requested_; }
    bool Acknowledge(uint64_t captured) noexcept
    {
        if(captured!=requested_)return false;
        pending_=false;return true;
    }
    void Complete(uint64_t revision) noexcept { Acknowledge(revision); }
  private:
    uint64_t requested_;
    bool pending_=true;
};
}
