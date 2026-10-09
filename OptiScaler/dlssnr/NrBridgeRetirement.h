#pragma once
#include "NrGpuSafety.h"
#include "NrBridgeOutcome.h"
#include <d3d11_4.h>
#include <wrl/client.h>

namespace DlssNr::Bridge
{
// The bridge supplies its D3D11 consumer observation; GpuSafety remains the
// sole authority for the exact D3D12 recording, submission and non-replayability.
struct Use
{
    GpuSafety::Ticket recording;
    std::uint64_t consumer=0;
    bool unknown=false;
    bool Reusable(std::uint64_t observed) const
    {
        return !unknown && FenceReached(observed,consumer) && GpuSafety::Reusable(recording);
    }
    // Completion assistance for this owner's submission only. Waking an event
    // grants nothing: the existing completion owner and live consumer are
    // consulted again before any resource/allocator reuse.
    bool WaitForReuse(ID3D11Fence* consumerFence,ID3D12Fence* producerFence,
                      std::uint64_t producerValue,ID3D12CommandQueue* queue,DWORD timeout,
                      const char** reason=nullptr) const
    {
        const auto refuse=[&](const char* why){if(reason)*reason=why;return false;};
        if (unknown || !consumerFence || consumer==UINT64_MAX ||
            consumerFence->GetCompletedValue()==UINT64_MAX ||
            (producerFence && producerFence->GetCompletedValue()==UINT64_MAX))
            return refuse("unknown-or-removed-completion");
        if (Reusable(consumerFence->GetCompletedValue())) return true;
        if (!GpuSafety::Reusable(recording))
        {
            const auto observed=GpuSafety::InspectRecording(recording,queue,false);
            if (!observed.valid || !observed.registryHealthy || !observed.submitted ||
                !observed.uniqueSubmission || !observed.nonReplayable || !observed.sameDevice ||
                !observed.sameQueue || !observed.supportedType || !producerFence ||
                !producerValue || producerValue==UINT64_MAX)
                return refuse("unsubmitted-unsealed-or-foreign-recording");
        }
        const auto started=GetTickCount64();
        struct Event { HANDLE handle=CreateEventW(nullptr,FALSE,FALSE,nullptr);
            ~Event(){if(handle)CloseHandle(handle);} } event;
        if (!event.handle) return refuse("completion-event-unavailable");
        const auto wait=[&](auto* fence,std::uint64_t value)
        {
            if (FenceReached(fence->GetCompletedValue(),value)) return true;
            const auto elapsed=GetTickCount64()-started;
            if (elapsed>=timeout || fence->GetCompletedValue()==UINT64_MAX ||
                FAILED(fence->SetEventOnCompletion(value,event.handle))) return false;
            if (WaitForSingleObject(event.handle,timeout-static_cast<DWORD>(elapsed))!=WAIT_OBJECT_0) return false;
            return FenceReached(fence->GetCompletedValue(),value);
        };
        if (producerValue && (!producerFence || !wait(producerFence,producerValue)))
            return refuse("producer-completion-timeout");
        if (!wait(consumerFence,consumer)) return refuse("consumer-completion-timeout");
        return Reusable(consumerFence->GetCompletedValue()) || refuse("completion-proof-pending");
    }
};
// One representation owner retains its current writer's LIVE use, including
// later copyback signals/failures. This is neither a completion registry nor
// a second writer owner; both observations still come from their native owners.
class CacheWriter
{
    std::shared_ptr<Use> use_;
    Microsoft::WRL::ComPtr<ID3D11Fence> consumer_;
  public:
    bool Reusable() const
    { return !use_ || (consumer_ && use_->Reusable(consumer_->GetCompletedValue())); }
    bool Bind(std::shared_ptr<Use> use,ID3D11Fence* consumer)
    {
        if (!Reusable() || !use || !consumer) return false;
        use_=std::move(use); consumer_=consumer; return true;
    }
    void ClearIfReusable() { if (Reusable()) {use_.reset();consumer_.Reset();} }
};
}
