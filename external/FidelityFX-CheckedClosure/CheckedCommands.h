// Additive SDK-private helper results; no changes to the frozen Dx12Commands layout.
#pragma once
#include <d3d12.h>
namespace ffx::nr {
struct CheckedCommandSubmission {
    HRESULT result=E_FAIL;
    HRESULT closeResult=S_FALSE;
    bool executeInvoked=false;
    bool signalSucceeded=false;
    bool mayPublish(bool callbackAccepted, uint32_t generatedCount) const noexcept {
        return callbackAccepted && generatedCount!=0 && SUCCEEDED(result) && executeInvoked && signalSucceeded;
    }
};
struct CheckedCommandSnapshot {
    ID3D12CommandQueue* queue;
    ID3D12Fence* fence;
    ID3D12GraphicsCommandList* list;
    uint64_t target;
};
inline HRESULT checkedFenceCompletion(ID3D12Fence* fence,uint64_t target) {
    if(!fence || target==UINT64_MAX) return E_INVALIDARG;
    const auto value=fence->GetCompletedValue();
    if(value==UINT64_MAX) return DXGI_ERROR_DEVICE_REMOVED;
    ID3D12Device* device=nullptr;
    const auto query=fence->GetDevice(IID_PPV_ARGS(&device));
    if(FAILED(query)) return query;
    const auto removed=device->GetDeviceRemovedReason(); device->Release();
    if(FAILED(removed)) return removed;
    return value>=target ? S_OK : S_FALSE;
}
}
