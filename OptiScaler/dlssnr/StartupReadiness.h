#pragma once

#include "NrGpuSafety.h"
#include <wrl/client.h>
#include <cstdint>

namespace DlssNr::PreFg
{
// Deliberately excludes frame token and rotating backbuffer index. Those belong
// to the single-use dependency, and every output still needs a fresh frame claim.
struct ReadinessIdentity
{
    uintptr_t swapchain = 0, device = 0, queue = 0;
    uint64_t provider = 0, nativeGeneration = 0, nativeInstance = 0;
    uint64_t configuration = 0, resume = 0, resources = 0, model = 0, invalidation = 0;
    unsigned int route = 0, width = 0, height = 0, format = 0, samples = 0, quality = 0;
    unsigned int workWidth = 0, workHeight = 0, colorSpace = 0;
    uint64_t modelLifecycle = 0;
    bool operator==(const ReadinessIdentity&) const = default;
};

class StartupReadiness
{
  public:
    enum class Phase { AwaitingFrame, ProbeSubmitted, AwaitingProof, Ready };
  private:
    Phase phase = Phase::AwaitingFrame;
    ReadinessIdentity identity {};
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    std::shared_ptr<GpuSafety::ExternalWaitStatus> handoff;
    uint64_t value = 0, sequence = 0;
  public:
    Phase State() const { return phase; }
    bool Ready() const { return phase == Phase::Ready && handoff && !handoff->failed.load(); }
    bool Pending() const { return phase == Phase::ProbeSubmitted || phase == Phase::AwaitingProof; }
    const char* Reason() const
    {
        if (phase == Phase::AwaitingFrame) return "waiting for coherent frame";
        if (phase == Phase::ProbeSubmitted) return "waiting for original Present";
        if (!handoff || handoff->failed.load()) return "native FG handoff failed";
        if (phase == Phase::Ready) return "ready";
        if (!fence || fence->GetCompletedValue() < value) return "waiting for probe GPU completion";
        if (!handoff->bound.load()) return "waiting for native FG claim and wait binding";
        if (!handoff->applied.load()) return "waiting for provider queue wait";
        return "waiting for next coherent frame";
    }
    void CheckEpoch(uint64_t current)
    {
        if (phase != Phase::AwaitingFrame && identity.invalidation != current) Reset();
    }
    void Reset()
    {
        // The completion ledger and command-list cookie retain outstanding GPU
        // ownership independently. Revoking permission never signals a fence.
        phase = Phase::AwaitingFrame; identity = {}; fence.Reset(); handoff.reset();
        value = sequence = 0;
    }
    void CheckIdentity(const ReadinessIdentity& current)
    {
        if (phase != Phase::AwaitingFrame &&
            (identity != current || !handoff || handoff->failed.load())) Reset();
    }
    bool Submit(const ReadinessIdentity& current, ID3D12Fence* submittedFence,
                uint64_t submittedValue, uint64_t submittedSequence,
                std::shared_ptr<GpuSafety::ExternalWaitStatus> status)
    {
        if (phase != Phase::AwaitingFrame || !submittedFence || !submittedValue ||
            submittedValue == UINT64_MAX || !submittedSequence || !status || status->failed.load()) return false;
        identity = current; fence = submittedFence; value = submittedValue;
        sequence = submittedSequence; handoff = std::move(status); phase = Phase::ProbeSubmitted;
        return true;
    }
    void Presented(uint64_t presentedSequence, bool succeeded)
    {
        if (!succeeded) { Reset(); return; }
        if (phase == Phase::ProbeSubmitted && sequence == presentedSequence) phase = Phase::AwaitingProof;
    }
    bool Poll(const ReadinessIdentity& current, ID3D12Device* device, uint64_t currentSequence)
    {
        CheckIdentity(current);
        if (phase == Phase::AwaitingFrame) return false;
        if (!device || device->GetDeviceRemovedReason() != S_OK) { Reset(); return false; }
        const auto completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX) { Reset(); return false; }
        // Both native-hook binding and the actual provider queue Wait must be
        // observed. Neither CPU frame count nor provider statistics can pass this.
        if (phase == Phase::AwaitingProof && currentSequence > sequence && completed >= value &&
            handoff->bound.load() && handoff->applied.load() && !handoff->failed.load())
            phase = Phase::Ready;
        return Ready();
    }
};
}
