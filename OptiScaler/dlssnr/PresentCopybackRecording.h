#pragma once
#include "NrGpuSafety.h"

namespace DlssNr
{
// Private Present copyback is submitted at most once. Terminal recording
// ownership must not depend on another Present/Reset or screenshot request.
class PresentCopybackRecording
{
    ID3D12GraphicsCommandList* list;
    ID3D12CommandAllocator* allocator;
    bool& unsafe;
    GpuSafety::Ticket ticket;
    bool submitted = false;
public:
    PresentCopybackRecording(ID3D12GraphicsCommandList* commandList,
                            ID3D12CommandAllocator* commandAllocator, bool& uncertain)
        : list(commandList), allocator(commandAllocator), unsafe(uncertain),
          ticket(GpuSafety::Record(commandList)) {}
    PresentCopybackRecording(const PresentCopybackRecording&) = delete;
    explicit operator bool() const { return bool(ticket); }
    bool Submitted(ID3D12CommandQueue* queue)
    {
        submitted = true;
        const bool ordered = GpuSafety::OrderedOn(ticket, queue);
        const bool sealed = GpuSafety::SealOwnedRecording(list);
        if (!ordered || !sealed) unsafe = true;
        return ordered && sealed;
    }
    ~PresentCopybackRecording()
    {
        // Only this private, never-submitted recording may be cancelled. A
        // submitted/unknown GPU use remains retained by its completion proof.
        if (ticket && !submitted && !GpuSafety::CancelOwnedUnsubmittedRecording(list, ticket, allocator))
            unsafe = true;
    }
};
}
