#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace DlssNr
{
struct VkNrUseId { uint64_t value = 0; explicit operator bool() const { return value != 0; } bool operator==(const VkNrUseId&) const = default; };
struct VkNrSubmissionId { uint64_t value = 0; explicit operator bool() const { return value != 0; } bool operator==(const VkNrSubmissionId&) const = default; };
struct VkNrConsumerId { uint64_t value = 0; explicit operator bool() const { return value != 0; } bool operator==(const VkNrConsumerId&) const = default; };
struct VkNrSubmission { VkQueue queue = VK_NULL_HANDLE; uint64_t deviceGeneration = 0; std::vector<VkNrUseId> uses; };
struct VkNrProducerProof { VkNrSubmissionId submission;VkQueue queue=VK_NULL_HANDLE;uint64_t deviceGeneration=0; };
enum class VkNrObservation { CreatePool, Allocate, Begin, End, Reset, ResetPool, Free, DestroyPool, Execute, Submit, Submit2,
    CreateImage,CreateView,DestroyImage,DestroyView,Work,Present,Count };
// Nested export/proc/bridge aliases observe one real operation once.
class VkNrObservationScope
{
  public:
    VkNrObservationScope(VkNrObservation operation, uintptr_t identity, VkDevice device = VK_NULL_HANDLE);
    ~VkNrObservationScope();
    VkNrObservationScope(const VkNrObservationScope&) = delete;
    VkNrObservationScope& operator=(const VkNrObservationScope&) = delete;
    bool Observe() const { return observe_; }
    static uint32_t SubmissionDepth();
  private:
    bool observe_ = false;
};
class VkNrRecordingOwner
{
  public:
    // Scalar-only first-16 capture history. Survives use retirement without
    // retaining GPU resources or granting frame/submission eligibility.
    struct GuideTrace {
        uint64_t use=0,incarnation=0,thread=0,capture=0,end=0,present=0,selected=0,root=0,returned=0,closed=0;
        uint32_t submitDepth=0; VkResult result=VK_NOT_READY;
        const char* closeReason="open";
        const char* returnPath="none";
    };
    bool TraceGuideCapture(VkNrUseId);
    void TraceGuidePresent(VkNrUseId selected);
    std::vector<GuideTrace> GuideTraces() const;
    void OnCreatePool(VkDevice, VkCommandPool, const VkCommandPoolCreateInfo&, VkResult);
    void OnAllocateBuffers(VkDevice, const VkCommandBufferAllocateInfo&, const VkCommandBuffer*, VkResult);
    void OnAllocate(VkDevice, VkCommandPool, VkCommandBuffer, uint32_t family,
                    VkCommandPoolCreateFlags flags = 0, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    void OnBegin(VkCommandBuffer, VkCommandBufferUsageFlags, VkResult);
    void OnEnd(VkCommandBuffer, VkResult);
    void OnReset(VkCommandBuffer, VkResult);
    void OnResetPool(VkDevice, VkCommandPool, VkResult);
    void OnFree(VkCommandBuffer);
    void OnDestroyPool(VkDevice, VkCommandPool);
    void OnExecute(VkCommandBuffer, std::span<const VkCommandBuffer>);
    void OnRendering(VkCommandBuffer, bool active, VkRenderingFlags flags = 0);
    // Bookkeeping reservation does not authorize private provider execution.
    std::optional<VkNrUseId> Reserve(VkCommandBuffer, uint64_t generation);
    // Initial provider envelope: one-time primary only. Replay/secondary require
    // independent provider qualification, not merely Vulkan legality.
    const char* ModelRecordingReason(VkCommandBuffer) const;
    std::optional<VkNrUseId> ReserveModel(VkCommandBuffer, uint64_t generation);
    // Transfer commands preserve shader bindings; all lifetime/recording gates still apply.
    std::optional<VkNrUseId> ReserveCopy(VkCommandBuffer, uint64_t generation);
    std::optional<VkNrUseId> ReserveExecutionDependency(VkCommandBuffer,uint64_t generation);
    // A lease for an observed queue dependency, independent of command recording.
    // The returned use owns one observer reference; the caller releases it.
    std::optional<VkNrUseId> ReserveSubmissionDependency(VkDevice,uint64_t generation,uint32_t family);
    void CancelBeforeRecording(VkNrUseId);
    bool RetainUse(VkNrUseId);
    void ReleaseUse(VkNrUseId);
    bool RecordingReleased(VkNrUseId) const;
    // Profile observation only: a wait still needs an accepted owned submission,
    // and reuse still needs actual completion plus release of every consumer.
    bool SingleUsePrimary(VkNrUseId) const;
    uint64_t Incarnation(VkCommandBuffer) const;
    std::optional<std::vector<VkNrUseId>> SnapshotUses(VkCommandBuffer) const;
    // Submission proves known work in an observed executable root even when an
    // unrelated secondary was opaque. Invalid known links still refuse the root.
    std::optional<std::vector<VkNrUseId>> SnapshotSubmissionUses(VkCommandBuffer);
    std::optional<VkNrSubmissionId> PrepareSubmission(const VkNrSubmission&);
    void SubmissionReturned(VkNrSubmissionId, VkResult);
    void CheckpointReturned(VkNrSubmissionId, VkResult);
    void ObserveFence(VkNrSubmissionId, VkResult);
    void UntrackedSubmission(std::span<const VkNrUseId>, VkResult);
    bool GpuComplete(VkNrUseId) const;
    bool Reusable(VkNrUseId) const;
    // Read-only aggregate proof after new renderer admission is suppressed.
    // Includes recorded-but-unsubmitted work and independent observer/consumer leases.
    bool CanYieldOutput(std::string& reason) const;
    bool SafeToWriteAfter(VkNrUseId) const;
    bool AddConsumerHold(VkNrUseId);
    void RemoveConsumerHold(VkNrUseId);
    bool UseOnRecording(VkNrUseId,VkCommandBuffer) const;
    void OnWork(VkCommandBuffer);
    void OnUnsupportedBindings(VkCommandBuffer);
    bool BindingsQualified(VkCommandBuffer) const;
    // Freshness token for this active incarnation, not an absolute draw count.
    // Sampling starts observation; later work invalidates the sampled token.
    uint64_t CommandSerial(VkCommandBuffer) const;
    VkDevice UseDevice(VkNrUseId) const;
    uint32_t UseFamily(VkNrUseId) const;
    uint32_t CommandFamily(VkCommandBuffer) const;
    VkDevice CommandDevice(VkCommandBuffer) const;
    std::optional<VkNrProducerProof> ProducerProof(VkNrUseId) const;
    struct ProducerState { std::optional<VkNrProducerProof> proof;bool recordingReleased=false; };
    std::vector<ProducerState> ProducerStates(std::span<const VkNrUseId>) const;
    // Bounded, handle-free diagnostic snapshot; does not grant submission rights.
    std::string DescribeProducer(VkNrUseId) const;
    void OnDeviceDestroyed(VkDevice);
  private:
    std::optional<VkNrUseId> ReserveRecording(VkCommandBuffer, uint64_t, bool requireBindings);
    struct Link { VkCommandBuffer buffer; uint64_t incarnation; };
    struct Recording
    {
        VkDevice device = VK_NULL_HANDLE; VkCommandPool pool = VK_NULL_HANDLE;
        uint32_t family = UINT32_MAX; VkCommandPoolCreateFlags poolFlags = 0;
        VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        uint64_t incarnation = 0; bool recording = false, executable = false, knownLinks = true, validKnownLinks = true;
        bool rendering = false, suspendsRendering = false;
        VkCommandBufferUsageFlags flags = 0;
        uint64_t workSerial=0;bool unsupportedBindings=false;
        mutable bool workObserved=false;
        uint64_t submissionQueries=0;
        const char* submissionGraph="not_observed";
        std::vector<VkNrUseId> uses; std::vector<Link> children;
    };
    struct Use { uint64_t generation; VkDevice device; VkCommandBuffer buffer; uint64_t incarnation;
                 bool recordingReference = true; uint32_t pending = 0, observers = 0; bool submitted = false, uncertain = false;
                 uint32_t submissionCount=0,consumerHolds=0;VkNrProducerProof producer;uint32_t family=UINT32_MAX;
                 uint64_t snapshotSequenceAtReserve=0; };
    struct Submission { VkNrSubmission value; bool submitted = false, checkpoint = false; };
    void Close(Recording&,const char* reason);
    void ObserveWork(const Recording&) const;
    void StopObservingWork(Recording&);
    GuideTrace* FindGuideTrace(VkNrUseId);
    std::array<GuideTrace,16> guideTraces_{};
    size_t guideTraceCount_=0;
    uint64_t guideTraceSequence_=0;
    bool Collect(VkCommandBuffer, uint64_t, std::vector<VkNrUseId>&, std::vector<VkCommandBuffer>&, bool allowOpaque=false,
                 const char** refusal=nullptr) const;
    mutable std::mutex mutex_;
    mutable std::atomic<uint32_t> workObservers_{0};
    uint64_t nextIncarnation_ = 1, nextUse_ = 1, nextSubmission_ = 1;
    uint64_t snapshotSequence_=0,unknownSubmissionRoots_=0,rejectedSubmissionRoots_=0;
    std::unordered_map<VkCommandBuffer, Recording> recordings_;
    std::unordered_map<uint64_t, Use> uses_;
    std::unordered_map<uint64_t, Submission> submissions_;
    struct Pool { VkDevice device; uint32_t family; VkCommandPoolCreateFlags flags; };
    std::map<std::pair<VkDevice, VkCommandPool>, Pool> pools_;
};
VkNrRecordingOwner& VulkanNrRecordings();
} // namespace DlssNr
