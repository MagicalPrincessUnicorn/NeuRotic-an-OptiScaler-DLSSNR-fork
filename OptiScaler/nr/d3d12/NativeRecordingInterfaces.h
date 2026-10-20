#pragma once
#include <dlssnr/NativeIdentity.h>
#include <array>

namespace Neurotic::D3D12
{
// Owned observation interfaces only. Execution stays on the caller's interface;
// this does not change NativeIdentity's rendering/resource unwrap contracts.
struct NativeRecordingInterfaces
{
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> caller, terminal;
    HRESULT result = E_POINTER;
    unsigned layers = 0;

    static NativeRecordingInterfaces Resolve(ID3D12GraphicsCommandList* input)
    {
        NativeRecordingInterfaces resolved;
        if (!input) return resolved;
        resolved.caller = input;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> current = input;
        std::array<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>, 5> visited;
        for (unsigned index = 0; index < visited.size(); ++index)
        {
            for (unsigned prior = 0; prior < index; ++prior)
                if (visited[prior].Get() == current.Get())
                { resolved.result = E_UNEXPECTED; return resolved; }
            visited[index] = current;
            Microsoft::WRL::ComPtr<IUnknown> base;
            resolved.result = current->QueryInterface(DlssNr::NativeIdentity::streamlineBase,
                reinterpret_cast<void**>(base.GetAddressOf()));
            if (resolved.result == E_NOINTERFACE)
            {
                if (base) { resolved.result = E_UNEXPECTED; return resolved; }
                // The public optional owning UnwrappedObject contract is used
                // for observation, without identifying a file/game/class IID.
                resolved.result = current->QueryInterface(DlssNr::NativeIdentity::reshadeDeviceBase,
                    reinterpret_cast<void**>(base.GetAddressOf()));
            }
            if (resolved.result == E_NOINTERFACE)
            {
                if (base) { resolved.result = E_UNEXPECTED; return resolved; }
                resolved.terminal = std::move(current);
                resolved.result = S_OK;
                return resolved;
            }
            if (FAILED(resolved.result)) return resolved;
            if (!base || index + 1 == visited.size())
            { resolved.result = E_UNEXPECTED; return resolved; }
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> next;
            resolved.result = base.As(&next);
            if (FAILED(resolved.result) || !next) return resolved;
            ++resolved.layers;
            current = std::move(next);
        }
        resolved.result = E_UNEXPECTED;
        return resolved;
    }
};
}
