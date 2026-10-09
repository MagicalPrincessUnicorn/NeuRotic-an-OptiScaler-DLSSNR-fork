#pragma once
#include <d3d11_4.h>
#include <wrl/client.h>
#include <cstdint>

namespace DlssNr
{
// Serialized by the Present owner. Context calls occur only at its rendering
// boundary; the supervisor's proof query reads the private fence alone.
class PresentDx11CopyCompletion
{
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D11Device5> device_;
    ComPtr<ID3D11DeviceContext4> context_;
    ComPtr<ID3D11Fence> fence_;
    // The private bridge source survives all pending/unknown consumers. Never
    // retain a DXGI backbuffer here: that would prevent the game's ResizeBuffers.
    ComPtr<ID3D11Resource> source_;
    UINT64 submitted_ = 0;
    bool quarantined_ = false;

    static bool SameObject(IUnknown* left, IUnknown* right) noexcept
    {
        if (!left || !right) return false;
        ComPtr<IUnknown> a, b;
        return SUCCEEDED(left->QueryInterface(IID_PPV_ARGS(&a))) &&
               SUCCEEDED(right->QueryInterface(IID_PPV_ARGS(&b))) && a.Get() == b.Get();
    }

public:
    static bool FenceComplete(UINT64 observed, UINT64 submitted) noexcept
    {
        return observed != UINT64_MAX && observed >= submitted;
    }

    bool CanYield() const noexcept
    {
        if (quarantined_) return false;
        if (!submitted_) return true;
        return device_ && fence_ && device_->GetDeviceRemovedReason() == S_OK &&
               FenceComplete(fence_->GetCompletedValue(), submitted_);
    }

    bool Quarantined() const noexcept { return quarantined_; }

    bool ResetCompleted() noexcept
    {
        if (!CanYield()) return false;
        source_.Reset(); context_.Reset(); fence_.Reset(); device_.Reset();
        submitted_ = 0;
        return true;
    }

    bool Copy(ID3D11DeviceContext4* context, ID3D11Resource* target, ID3D11Resource* source)
    {
        if (quarantined_ || !context || !target || !source ||
            context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE || SameObject(target, source)) return false;
        ComPtr<ID3D11Device> base, targetDevice, sourceDevice;
        context->GetDevice(&base); target->GetDevice(&targetDevice); source->GetDevice(&sourceDevice);
        ComPtr<ID3D11Device5> device;
        if (!base || FAILED(base.As(&device)) || device->GetDeviceRemovedReason() != S_OK ||
            !SameObject(base.Get(), targetDevice.Get()) || !SameObject(base.Get(), sourceDevice.Get())) return false;

        const bool sameDevice = SameObject(device_.Get(), device.Get());
        const bool sameContext = SameObject(context_.Get(), context);
        const bool sameSource = SameObject(source_.Get(), source);
        if (submitted_ && (!sameDevice || !sameContext || !sameSource) && !CanYield()) return false;
        if (!fence_ || !sameDevice || !sameContext)
        {
            if (!ResetCompleted()) return false;
            ComPtr<ID3D11Fence> fence;
            if (FAILED(device->CreateFence(0, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
            device_ = device; context_ = context; fence_ = fence;
        }
        // UINT64_MAX is the removed-device sentinel, never a submitted value.
        if (submitted_ >= UINT64_MAX - 1) return false;
        source_ = source;
        const auto value = submitted_ + 1;
        context->CopyResource(target, source);
        submitted_ = value;
        const auto signaled = context->Signal(fence_.Get(), value);
        if (FAILED(signaled)) quarantined_ = true;
        context->Flush();
        return SUCCEEDED(signaled);
    }
};
}
