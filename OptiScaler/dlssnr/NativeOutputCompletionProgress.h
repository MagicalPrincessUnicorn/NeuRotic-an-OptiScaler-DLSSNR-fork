#pragma once
#include "NrGpuSafety.h"
#include <array>
#include <cstdint>

namespace DlssNr
{
// Diagnostic publication only. Dropping a bounded observation never releases
// recording ownership from GpuSafety or authorizes resource reuse.
class NativeOutputCompletionProgress
{
    struct Entry { GpuSafety::Ticket ticket; uint64_t outputs = 0; };
    std::array<Entry, 64> entries {};
    size_t next = 0;
    uint64_t completed = 0, omitted = 0;
public:
    void Poll()
    {
        for (auto& entry : entries)
        {
            if (!entry.ticket) continue;
            const auto proof = GpuSafety::InspectRecording(entry.ticket);
            if (proof.valid && proof.submitted && proof.completed)
            {
                completed += entry.outputs;
                entry = {};
            }
            else if (!proof.valid || (proof.reusable && !proof.submitted))
                entry = {}; // Unknown/cancelled recording is never delivered.
        }
    }
    void Record(const GpuSafety::Ticket& ticket)
    {
        Poll();
        if (!ticket) { ++omitted; return; }
        for (auto& entry : entries)
            if (entry.ticket == ticket) { ++entry.outputs; return; }
        for (auto& entry : entries)
            if (!entry.ticket) { entry = {ticket, 1}; return; }
        omitted += entries[next].outputs;
        entries[next] = {ticket, 1};
        next = (next + 1) % entries.size();
    }
    uint64_t Completed() const { return completed; }
    uint64_t Omitted() const { return omitted; }
    void Reset() { entries = {}; next = 0; completed = omitted = 0; }
};
}
