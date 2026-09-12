#pragma once

#include "NrPresentStageCapture.h"
#include <wrl/client.h>

namespace DlssNr::Screenshots
{
// One private submission on the swapchain's own queue. The game output is copied
// before NeuRotic's overlay; no NR-stage texture or second game render is used.
class FinalOutputSubmission
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
    GpuSafety::Ticket use_;

  public:
    bool active() const { return use_ != nullptr; }

    void poll()
    {
        if (!use_) return;
        const auto state = GpuSafety::InspectSlots(&use_, 1);
        // This list is owned here and is never replayed. Destroy only after its
        // actual submission completes; the private-data cookie then seals it.
        if (state.completedUnsealed || state.reusable)
            releaseAfterDrain();
    }

    // Only for an unsubmitted list, observed completion, or successful host drain.
    void releaseAfterDrain()
    {
        list_.Reset();
        allocator_.Reset();
        use_.reset();
    }

    void retainUntilExit()
    {
        // Match the renderer's failed-drain policy: do not let static teardown
        // free command memory which the device may still be using.
        list_.Detach();
        allocator_.Detach();
        use_.reset();
    }

    bool submit(ID3D12CommandQueue* queue, ID3D12Resource* output,
                StageCapture::PresentStages& capture, const char* name,
                UINT64 frameId, const std::string& settings, const Identity& identity = {})
    {
        if (active() || !queue || !output) return false;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        auto fail = [&](const char* operation, HRESULT error)
        {
            releaseAfterDrain(); // Nothing has been submitted on these failure paths.
            capture.cancel();
            capture.poll({});
            std::ostringstream reason;
            reason << "Screenshot failed: " << operation << " (0x" << std::hex
                   << std::uppercase << static_cast<unsigned long>(error) << ").";
            capture.setIdleStatus(reason.str());
            return false;
        };
        HRESULT hr = queue->GetDevice(IID_PPV_ARGS(&device));
        if (FAILED(hr)) return fail("get output device", hr);
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_));
        if (FAILED(hr)) return fail("create capture allocator", hr);
        hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
                                       IID_PPV_ARGS(&list_));
        if (FAILED(hr)) return fail("create capture recording", hr);
        use_ = GpuSafety::Record(list_.Get());
        if (!use_) return fail("track capture completion", E_FAIL);
        if (!capture.record(list_.Get(), device.Get(), {{name, output, D3D12_RESOURCE_STATE_PRESENT}},
                            frameId, false, settings, identity))
        {
            releaseAfterDrain();
            return false; // Preserve the specific allocation/format failure.
        }
        hr = list_->Close();
        if (FAILED(hr)) return fail("close capture recording", hr);
        ID3D12CommandList* submitted[] {list_.Get()};
        queue->ExecuteCommandLists(1, submitted);
        return true;
    }
};
} // namespace DlssNr::Screenshots
