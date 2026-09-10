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
UINT64 TimestampFrequency(const Ticket& ticket);
CompletionSet Pending();
bool Reusable(const CompletionSet& tickets);
// Explicit NGX shutdown: wait only for already submitted work, never signal a guessed queue.
// The caller has stopped recording and promises no further submissions after shutdown.
bool Drain(unsigned int timeoutMs);
void NewSession();
}
