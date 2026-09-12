#pragma once

#include "../OptiScaler/dlssnr/NativeIdentity.h"
#include <atomic>

// Matches Streamline's distinct IUnknown identity and owning base-interface QI.
// Other failure modes deliberately exercise fail-closed identity resolution.
class IdentityProxy final : public IUnknown
{
    std::atomic<ULONG> refs {1};
    Microsoft::WRL::ComPtr<IUnknown> base;
  public:
    enum Mode { Normal, NullBase, Self, Error, Unsupported };
    Mode mode;
    IdentityProxy(IUnknown* value, Mode behavior = Normal) : base(value), mode(behavior) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown)) { *out = this; AddRef(); return S_OK; }
        if (id != DlssNr::NativeIdentity::streamlineBase || mode == Unsupported) return E_NOINTERFACE;
        if (mode == Error) return E_FAIL;
        if (mode == NullBase) return S_OK;
        auto* value = mode == Self ? static_cast<IUnknown*>(this) : base.Get();
        *out = value;
        if (value) value->AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    { const auto left = --refs; if (!left) delete this; return left; }
    ULONG References() const { return refs.load(); }
};

class QueueProxy final : public ID3D12CommandQueue
{
    std::atomic<ULONG> refs {1};
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> base;
    Microsoft::WRL::ComPtr<IUnknown> deviceProxy;
  public:
    QueueProxy(ID3D12CommandQueue* value, IUnknown* device) : base(value), deviceProxy(device) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == DlssNr::NativeIdentity::streamlineBase)
        { *out = base.Get(); base->AddRef(); return S_OK; }
        if (id == __uuidof(IUnknown) || id == __uuidof(ID3D12Object) ||
            id == __uuidof(ID3D12DeviceChild) || id == __uuidof(ID3D12Pageable) || id == __uuidof(ID3D12CommandQueue))
        { *out = this; AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto left = --refs; if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* s, void* d) override { return base->GetPrivateData(g,s,d); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT s, const void* d) override { return base->SetPrivateData(g,s,d); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* d) override { return base->SetPrivateDataInterface(g,d); }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR n) override { return base->SetName(n); }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id, void** out) override { return deviceProxy->QueryInterface(id,out); }
    void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource* r, UINT n, const D3D12_TILED_RESOURCE_COORDINATE* c,
        const D3D12_TILE_REGION_SIZE* s, ID3D12Heap* h, UINT m, const D3D12_TILE_RANGE_FLAGS* f,
        const UINT* o, const UINT* t, D3D12_TILE_MAPPING_FLAGS flags) override
    { base->UpdateTileMappings(r,n,c,s,h,m,f,o,t,flags); }
    void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource* d, const D3D12_TILED_RESOURCE_COORDINATE* dc,
        ID3D12Resource* s, const D3D12_TILED_RESOURCE_COORDINATE* sc, const D3D12_TILE_REGION_SIZE* size,
        D3D12_TILE_MAPPING_FLAGS f) override { base->CopyTileMappings(d,dc,s,sc,size,f); }
    void STDMETHODCALLTYPE ExecuteCommandLists(UINT n, ID3D12CommandList* const* lists) override { base->ExecuteCommandLists(n,lists); }
    void STDMETHODCALLTYPE SetMarker(UINT m, const void* d, UINT s) override { base->SetMarker(m,d,s); }
    void STDMETHODCALLTYPE BeginEvent(UINT m, const void* d, UINT s) override { base->BeginEvent(m,d,s); }
    void STDMETHODCALLTYPE EndEvent() override { base->EndEvent(); }
    HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* f, UINT64 v) override { return base->Signal(f,v); }
    HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* f, UINT64 v) override { return base->Wait(f,v); }
    HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* v) override { return base->GetTimestampFrequency(v); }
    HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* g, UINT64* c) override { return base->GetClockCalibration(g,c); }
    D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc() override { return base->GetDesc(); }
};
