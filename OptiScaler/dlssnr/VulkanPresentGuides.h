#pragma once
#include "VulkanNrSession.h"
#include "VulkanNrImageFacts.h"
#include <array>
#include <memory>
namespace DlssNr
{
struct VkNrGuideImages
{
    NVSDK_NGX_Resource_VK depth{},motion{};uint64_t bytes=0;
    std::vector<uint32_t> concurrentFamilies;
    virtual void Abandon() {}
    virtual ~VkNrGuideImages()=default;
};
struct VkNrGuideLease
{
    uint64_t slot=0;VkNrFrameContract contract;VkNrUseId sourceUse,consumerUse;
    VkNrSubmissionId producerSubmission;VkQueue producerQueue=VK_NULL_HANDLE;
    std::shared_ptr<VkNrGuideImages> images;VkCommandBuffer sameRecording=VK_NULL_HANDLE;
    VkImage outputImage=VK_NULL_HANDLE;VkCommandBuffer consumerCommand=VK_NULL_HANDLE;
    bool completedProducer=false,orderedProducer=false;
    std::shared_ptr<VkNrUseId> selectionHold;
};
struct VkNrGuideCreation { std::shared_ptr<VkNrGuideImages> images;bool recorded=false,ready=false;std::string reason; };
// Supplied by a successful explicit Backbuffer tag, or the current application's
// successful Present-marker scope plus its exact acquired image at Present.
// The target identity comes from the WSI registry,
// never a same-size image, submission interval or locally invented frame token.
struct VkNrPresentGuideTag
{
    uint64_t providerGeneration=0,frameToken=0;
    uint32_t viewport=UINT32_MAX;
    VkNrFrameContract target;
    VkImage image=VK_NULL_HANDLE,selectedOutput=VK_NULL_HANDLE;
};
class VulkanPresentGuides;
class VkNrGuideSelection
{
    friend class VulkanPresentGuides;
    VkNrRecordingOwner* owner_=nullptr;
    const VulkanPresentGuides* source_=nullptr;
    uint64_t captures_=0,candidate_=0;
    std::optional<VkNrFrameContract> metadata_,context_;
    VkNrUseId metadataUse_;
    std::optional<VkNrProducerProof> proof_;
    std::string reason_;
    struct Association { VkNrPresentGuideTag tag;VkNrGuideLease lease; };
    std::vector<Association> associations_;
  public:
    VkNrGuideSelection()=default;
    VkNrGuideSelection(const VkNrGuideSelection&)=delete;
    VkNrGuideSelection& operator=(const VkNrGuideSelection&)=delete;
    ~VkNrGuideSelection();
};
class VulkanPresentGuides
{
  public:
    using PresentTag=VkNrPresentGuideTag;
    using Copy=std::function<VkNrGuideCreation(const VkFrameRequest&,VkNrUseId,uint64_t)>;
    using Release=std::function<bool(VkNrGuideImages&)>;
    VulkanPresentGuides(VkNrRecordingOwner&,Copy,Release);
    ~VulkanPresentGuides();
    bool Capture(const VkFrameRequest&,VkNrUseId);
    void CompleteEvaluation(const VkNrEvaluationIdentity&,bool succeeded);
    std::optional<VkNrGuideLease> SelectRecording(const VkNrFrameContract&,VkCommandBuffer,VkImage output=VK_NULL_HANDLE);
    std::shared_ptr<const VkNrGuideSelection> BeginPresent(VkQueue queue=VK_NULL_HANDLE);
    bool ObservePresentTag(const VkNrPresentGuideTag&);
    // Zero provider revokes all providers; unknown viewport revokes every view.
    void RevokePresentTags(uint64_t providerGeneration,uint32_t viewport=UINT32_MAX);
    std::optional<VkNrGuideLease> Select(const VkNrFrameContract&,const VkNrGuideSelection* = nullptr,VkImage presentedImage=VK_NULL_HANDLE);
    std::optional<VkNrFrameContract> SelectMetadata(const VkNrFrameContract&,const VkNrGuideSelection* = nullptr);
    // Raster sizing only; excludes temporal/image binding rights and expires at Present.
    std::optional<VkNrFrameContract> SelectResolutionMetadata(const VkNrFrameContract&,const VkNrGuideSelection* = nullptr);
    uint64_t ContextGeneration(const VkNrFrameContract&,const VkNrGuideSelection* = nullptr) const;
    bool Bind(const VkNrGuideLease&,VkNrUseId,VkQueue);
    void RejectNative(const char*);
    void PresentObserved();
    // Revoke only this device's future publication rights. Independent selected
    // leases and actual recording/GPU owners still decide physical retirement.
    void RetireInactive(VkDevice);
    void RetireCompleted(const VkNrRecordingOwner&);
    void AbandonDevice();
    uint64_t PrivateBytes() const { return bytes_; }
    size_t Count() const { return slots_.size(); }
    std::string Reason() const { return reason_; }
    std::string SelectionReason() const { return selectionReason_; }
    std::string DescribeSelection(const VkNrFrameContract&,const VkNrGuideSelection* = nullptr) const;
  private:
    struct Slot { VkNrGuideLease lease;std::vector<VkNrUseId> consumers;bool ready=false,published=false,resolved=false,publicationHeld=false; };
    VkNrRecordingOwner& owner_;Copy copy_;Release release_;
    std::vector<Slot> slots_;uint64_t next_=1,bytes_=0,captures_=0,candidate_=0;
    std::string reason_,selectionReason_;
    std::optional<VkNrFrameContract> metadata_;VkNrUseId metadataUse_;
    std::optional<VkNrFrameContract> context_;
    struct Pending { VkNrFrameContract frame;VkNrUseId use;uint64_t candidate=0;bool succeeded=false; };
    std::vector<Pending> pending_;
    uint64_t rejections_=0;
    std::vector<VkNrPresentGuideTag> presentTags_;
    struct PresentSource { VkNrEvaluationIdentity evaluation;uint64_t deviceGeneration=0,provider=0,frame=0;uint32_t viewport=UINT32_MAX;VkImage output=VK_NULL_HANDLE; };
    std::vector<PresentSource> presentSources_;
    const Slot* AssociatedSource(const VkNrPresentGuideTag&) const;
    std::shared_ptr<VkNrUseId> HoldSelection(VkNrUseId);
    void ClearPending();
    void RevokePublication(Slot&);
};
}
