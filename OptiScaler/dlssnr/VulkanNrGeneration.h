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
                                 bool existingOnly=false);
    bool Commit(uint64_t readyGeneration);
    bool Retain(uint64_t generation,VkNrUseId);
    void RetireCompleted(const VkNrRecordingOwner&);
    std::optional<VkNrGenerationKey> AppliedKey() const;
    bool CanApply(const VkNrGenerationKey&) const;
    bool ReadyForUse(uint64_t,VkNrUseId) const;
    VkNrGenerationPayload* Payload(uint64_t) const;
    uint64_t AppliedGeneration() const { return applied_; }
    uint64_t PrivateBytes() const { return bytes_; }
    void SetPrivateBudget(uint64_t budget) { budget_=budget; }
    size_t Count() const { return entries_.size(); }
    void Disable() { applied_=0; }
    void RetryPreparation() { refusedKey_.reset();refusedReason_.clear(); }
    bool DrainReleased();
    // No driver/provider calls on a dead device. Caller keeps unresolved diagnostics separately.
    void AbandonDevice();
  private:
    struct Entry {
        uint64_t id;VkNrGenerationKey key;std::unique_ptr<VkNrGenerationPayload> payload;
        std::vector<VkNrUseId> uses;VkNrUseId initialization;
        bool ready=false,initializationComplete=false;
    };
    Entry* Find(uint64_t);
    const Entry* Find(uint64_t) const;
    VkNrRecordingOwner& recordings_;Create create_;Release release_;
    size_t capacity_;uint64_t budget_,bytes_=0,next_=1,applied_=0;
    std::vector<Entry> entries_;
    std::optional<VkNrGenerationKey> refusedKey_;
    std::string refusedReason_;
};
class VkNrResetRevision
{
  public:
    uint64_t Capture() const { return requested_; }
    void Request() { ++requested_; }
    void Complete(uint64_t revision) { if(revision==requested_)completed_=revision; }
    bool Pending() const { return requested_!=completed_; }
  private:
    uint64_t requested_=1,completed_=0;
};
}
