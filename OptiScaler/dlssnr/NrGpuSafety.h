#pragma once

#include <d3d12.h>
#include <memory>
#include <vector>
#include <atomic>
#include <optional>
#include <span>

namespace DlssNr::GpuSafety
{
// Shared by the exact completion packet and provider list, never by frame age.
struct ExternalWaitStatus
{
    std::atomic<bool> bound {false}, applied {false}, failed {false};
    std::atomic<uint64_t>* failureEpoch = nullptr; // process-lifetime registry
    // A fixed MFG input has one wait acknowledgement per actual evaluation.
    // Children retain this aggregate; the aggregate owns no child or GPU object.
    std::shared_ptr<ExternalWaitStatus> group;
    unsigned int expectedWaits = 1;
    std::atomic<unsigned int> boundWaits {0}, appliedWaits {0};
    void MarkBound()
    {
        if (!bound.exchange(true) && group && ++group->boundWaits == group->expectedWaits)
            group->bound = true;
    }
    void MarkApplied()
    {
        if (!applied.exchange(true) && group && ++group->appliedWaits == group->expectedWaits)
            group->applied = true;
    }
    void Fail()
    {
        if (!failed.exchange(true) && failureEpoch) ++*failureEpoch;
        if (group) group->Fail();
    }
};
struct Recording;
struct ExternalExecutionStatus
{
    std::atomic<bool> evaluated {false}, submitted {false}, failed {false};
    bool Ready() const { return evaluated.load() && submitted.load() && !failed.load(); }
};
// Observe an unmodified provider recording. No wait or NR work is introduced.
// Null means that this list/queue implementation cannot be safely observed.
std::shared_ptr<ExternalExecutionStatus> ObserveExternalExecution(ID3D12GraphicsCommandList* list,
                                                                 const char** reason = nullptr);
inline bool SupportedExternalType(D3D12_COMMAND_LIST_TYPE type)
{ return type == D3D12_COMMAND_LIST_TYPE_DIRECT || type == D3D12_COMMAND_LIST_TYPE_COMPUTE; }
using Ticket = std::shared_ptr<Recording>;
using CompletionSet = std::vector<Ticket>;
// Install the existing completion owner's callbacks before another owner pins
// their routes. This issues no ticket, recording identity, Reset or use rights.
bool PrepareRecordingHooks(ID3D12GraphicsCommandList* list);
enum class RecordingTerminalState { None, Reset, OwnerSeal, LifetimeEnded };
struct CompletionObservation
{
    uint64_t timeline=0, required=0, observed=0;
};
struct TerminalRecordingEvidence
{
    uint64_t incarnation=0, submissions=0;
    RecordingTerminalState state=RecordingTerminalState::None;
    std::vector<CompletionObservation> completion;
};
// Owner observation, available only after all observed work completes AND the
// exact recording cannot replay. An empty optional is never cancellation proof.
std::optional<TerminalRecordingEvidence> InspectTerminalRecording(const Ticket&);

// Register BEFORE recording any NR commands. Null means no work may be recorded.
Ticket Record(ID3D12GraphicsCommandList* list);
// Only for an owned list guaranteed not to replay before Reset. GPU completion remains required.
bool SealOwnedRecording(ID3D12GraphicsCommandList* list);
// Exact healthy current cookie only, before ANY submission and without a live
// recording borrow. Successful Reset cancels; failure preserves the old use.
#define NR_GPU_CANCEL_OWNED_UNSUBMITTED_V1 1
bool CancelOwnedUnsubmittedRecording(ID3D12GraphicsCommandList* list,const Ticket& expected,
                                    ID3D12CommandAllocator* allocator);
// Reuse requires both GPU completion and Reset/destruction of the old recording, since a
// closed list may be replayed. A reset of an unsubmitted list cancels that recording safely.
bool Reusable(const Ticket& ticket);
// Read-only handoff proof. Caller gates new work before querying; no tickets
// are removed and no resources are released by this observation.
bool CanYieldOutput();
// Read-only diagnostics; never grants reuse. Categories count slots, including duplicate
// tickets, are mutually exclusive, and sum to count. Failure inspection does not mutate state.
struct SlotSnapshot
{
    unsigned int reusable = 0;
    unsigned int gpuPending = 0;
    unsigned int completedUnsealed = 0;
    unsigned int unsubmittedUnsealed = 0;
    unsigned int failed = 0;
    bool registryFailed = false;
};
SlotSnapshot InspectSlots(const Ticket* tickets, unsigned int count);
struct RetainedRecordingSnapshot
{
    uint64_t incarnation = 0, submissions = 0, omittedPoints = 0;
    RecordingTerminalState state = RecordingTerminalState::None;
    bool sealed = false, failed = false;
    std::vector<CompletionObservation> completion;
};
struct PendingSnapshot
{
    SlotSnapshot slots;
    uint64_t omitted = 0;
    std::vector<RetainedRecordingSnapshot> recordings;
};
// Bounded data-only shutdown evidence from this owner; never removes a ticket,
// seals a recording, changes failure state, or grants reuse. Fence observations
// can advance during inspection. Completed reusable entries have counts only.
PendingSnapshot InspectPending(unsigned int maxRecordings = 16, unsigned int maxPoints = 4);

bool Readable(const Ticket& ticket);
// Atomic projection of this owner's existing recording state. Null is unavailable,
// unlike Reusable(null), which intentionally represents an empty legacy pool slot.
// No native handle is exported in this snapshot and no completion truth is retained.
struct RecordingSnapshot
{
    bool valid = false, registryHealthy = false, submitted = false, uniqueSubmission = false;
    bool completed = false, nonReplayable = false, reusable = false;
    bool queueKnown = false, supportedType = false, sameDevice = false, sameQueue = false;
    bool orderedForConsumer = false;
};
RecordingSnapshot InspectRecording(const Ticket& ticket, ID3D12CommandQueue* consumer = nullptr,
                                   bool establishCrossQueue = false);
bool MatchesRecording(const Ticket& ticket, ID3D12GraphicsCommandList* list, ID3D12CommandQueue* queue);
// Exact active unsubmitted recording and resource device. The eventual host
// queue is unknown; this grants no submission, completion or reuse proof.
bool MatchesLocalRecording(const Ticket& ticket, ID3D12GraphicsCommandList* list, ID3D12Resource* resource);
// A live owner borrow, not a snapshot. Open state must come from an observed
// successful Reset; first-seen lists remain unknown. Competing Close/Reset/
// Execute calls wait for this borrow; reentrant mutations invalidate Current.
// Does not grant resource rights, provider release, completion or replay safety.
#define NR_GPU_RECORDING_ACTION_V1 1
class LocalRecordingAction
{
    friend std::unique_ptr<LocalRecordingAction> BeginLocalAction(const Ticket&,
        ID3D12GraphicsCommandList*,ID3D12Resource*);
    friend std::unique_ptr<LocalRecordingAction> BeginLocalAction(const Ticket&,
        ID3D12GraphicsCommandList*,std::span<ID3D12Resource* const>);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit LocalRecordingAction(std::unique_ptr<Impl>);
  public:
    ~LocalRecordingAction();
    LocalRecordingAction(const LocalRecordingAction&)=delete;
    LocalRecordingAction& operator=(const LocalRecordingAction&)=delete;
    bool Current()const;
    // The recording borrow was opened for this exact native resource. Same
    // device or another resource on the list does not confer write authority.
    bool CurrentFor(ID3D12Resource* resource)const;
    // Exact retained recording identity; observing it grants no additional right.
    const Ticket& RecordingTicket()const noexcept;
    ID3D12GraphicsCommandList* CommandList()const noexcept;
};
std::unique_ptr<LocalRecordingAction> BeginLocalAction(const Ticket& ticket,
    ID3D12GraphicsCommandList* list,ID3D12Resource* resource);
// One list borrow retaining each exact resource participating in an owned pass.
std::unique_ptr<LocalRecordingAction> BeginLocalAction(const Ticket& ticket,
    ID3D12GraphicsCommandList* list,std::span<ID3D12Resource* const> resources);
// GPU consumer ordering only, NOT permission to reuse/free. Exactly one observed submission,
// on the consumer's queue; no CPU wait and no inferred cross-queue dependency.
bool OrderedOn(const Ticket& ticket, ID3D12CommandQueue* queue);
// Explicit provider handoff only. Requires a sealed, uniquely submitted producer;
// a different queue must share its device and gets a GPU wait on its observed fence.
// never waits for an unsubmitted recording or grants permission to reuse resources.
bool OrderBefore(const Ticket& ticket, ID3D12CommandQueue* consumer);
// Attach an already-signaled producer fence to a provider/derived-reader command
// list. Every submission receives a GPU queue wait immediately before the
// list executes; Reset before submission cancels the dependency.
bool BindExternalWait(ID3D12GraphicsCommandList* list, ID3D12Fence* producerFence,
                      UINT64 producerValue, UINT64 token, UINT64 sequence,
                      std::shared_ptr<ExternalWaitStatus> status = {});
UINT64 TimestampFrequency(const Ticket& ticket);
CompletionSet Pending();
bool Reusable(const CompletionSet& tickets);
// Explicit NGX shutdown: wait only for already submitted work, never signal a guessed queue.
// The caller has stopped recording and promises no further submissions after shutdown.
bool Drain(unsigned int timeoutMs);

// Private derived textures use the existing recording owner's retirement.
// Opaque artifact versions retain producer and all reader uses; raw ordering
// snapshots never authorize these immutable reads.
enum class DerivedArtifactKind {Anchor,Scratch};
enum class DerivedReadiness {CompletedImmutable,DependencyOrderedImmutable};
struct DerivedAllocation;
using DerivedAllocationRef=std::shared_ptr<DerivedAllocation>;
DerivedAllocationRef ReserveDerivedAllocation(DerivedArtifactKind,uint64_t bytes);
struct DerivedArtifact;
using DerivedArtifactRef=std::shared_ptr<DerivedArtifact>;
class DerivedReadLease
{
    friend std::unique_ptr<DerivedReadLease> ReserveDerivedRead(const DerivedArtifactRef&,
        const LocalRecordingAction&,ID3D12CommandQueue*);
    std::shared_ptr<void> backing_;
    DerivedReadiness readiness_;
    DerivedReadLease(std::shared_ptr<void> backing,DerivedReadiness readiness):backing_(std::move(backing)),readiness_(readiness){}
  public:
    DerivedReadiness Readiness()const noexcept{return readiness_;}
};
DerivedArtifactRef CreateDerivedArtifact(const LocalRecordingAction& producer,
    std::span<ID3D12Resource* const> resources,DerivedArtifactKind kind,
    std::shared_ptr<void> retainedPipeline={},uint64_t auxiliaryBytes=0,DerivedAllocationRef allocation={});
// A null recording-time queue permits pending immutable storage only with a
// successfully bound fence wait enforced on the actual consumer submission.
std::unique_ptr<DerivedReadLease> ReserveDerivedRead(const DerivedArtifactRef&,
    const LocalRecordingAction& consumer,ID3D12CommandQueue* queue);
bool DerivedArtifactReusable(const DerivedArtifactRef&);
bool RearmDerivedArtifact(const DerivedArtifactRef&,const LocalRecordingAction& producer);
struct DerivedSnapshotCopy {ID3D12Resource* source=nullptr;ID3D12Resource* destination=nullptr;};
// Both images arrive/leave in UAV state. Prepare a one-shot owned copy after the
// source's first actual submission. Source replay never rewrites the snapshot.
// The source action must retain access to every staging and destination image.
bool PrepareDerivedSnapshot(const LocalRecordingAction& source,std::span<const DerivedSnapshotCopy> copies,
    DerivedArtifactRef& snapshot,std::shared_ptr<void> retainedPipeline,DerivedAllocationRef allocation);
bool RetainDerivedUse(const LocalRecordingAction&,std::shared_ptr<void> use);
uint64_t DerivedAllocatedBytes();
void NewSession();
}
