#pragma once

#include <d3d12.h>
#include <memory>
#include <vector>

namespace DlssNr::GpuSafety
{
struct Recording;
using Ticket = std::shared_ptr<Recording>;
using CompletionSet = std::vector<Ticket>;

// Register BEFORE recording any NR commands. Null means no work may be recorded.
Ticket Record(ID3D12GraphicsCommandList* list);
// Only for an owned list guaranteed not to replay before Reset. GPU completion remains required.
bool SealOwnedRecording(ID3D12GraphicsCommandList* list);
// Reuse requires both GPU completion and Reset/destruction of the old recording, since a
// closed list may be replayed. A reset of an unsubmitted list cancels that recording safely.
bool Reusable(const Ticket& ticket);
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
bool Readable(const Ticket& ticket);
// GPU consumer ordering only, NOT permission to reuse/free. Exactly one observed submission,
// on the consumer's queue; no CPU wait and no inferred cross-queue dependency.
bool OrderedOn(const Ticket& ticket, ID3D12CommandQueue* queue);
// Explicit provider handoff only. Requires a sealed, uniquely submitted producer;
// a different queue must share its device and gets a GPU wait on its observed fence.
// never waits for an unsubmitted recording or grants permission to reuse resources.
bool OrderBefore(const Ticket& ticket, ID3D12CommandQueue* consumer);
// Attach an already-signaled producer fence to an external provider command
// list. Its first submission receives a GPU queue wait immediately before the
// provider list executes; Reset before submission cancels the dependency.
bool BindExternalWait(ID3D12GraphicsCommandList* list, ID3D12Fence* producerFence,
                      UINT64 producerValue, UINT64 token, UINT64 sequence);
UINT64 TimestampFrequency(const Ticket& ticket);
CompletionSet Pending();
bool Reusable(const CompletionSet& tickets);
// Explicit NGX shutdown: wait only for already submitted work, never signal a guessed queue.
// The caller has stopped recording and promises no further submissions after shutdown.
bool Drain(unsigned int timeoutMs);
void NewSession();
}
