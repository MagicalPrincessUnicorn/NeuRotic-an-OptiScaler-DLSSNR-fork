#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <atomic>

// Typed test proxy matching ReShade 6.8's owning IID_UnwrappedObject and
// distinct device/IUnknown identity. Real D3D12 operations forward to WARP.
// No ReShade DLL, game or proprietary provider is loaded by this fixture.
class ReShadeDeviceProxy final : public ID3D12Device
{
    std::atomic<ULONG> refs {1};
    Microsoft::WRL::ComPtr<ID3D12Device> base;
public:
    inline static constexpr GUID unwrap =
        {0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
    enum Mode { Normal, NullBase, Self, Error, Unsupported, MalformedNoInterface, FailedBaseWithValue, DeviceLost };
    Mode mode;
    unsigned commandListsCreated = 0, fencesCreated = 0;
    explicit ReShadeDeviceProxy(ID3D12Device* device, Mode value = Normal) : base(device), mode(value) {}
    ULONG References() const { return refs.load(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(ID3D12Object) || id == __uuidof(ID3D12Device))
        { *out = static_cast<ID3D12Device*>(this); AddRef(); return S_OK; }
        if (id != unwrap || mode == Unsupported) return E_NOINTERFACE;
        if (mode == Error) return E_FAIL;
        if (mode == NullBase) return S_OK;
        ID3D12Device* value = mode == Self ? this : base.Get();
        if (value) { value->AddRef(); *out = value; }
        if (mode == MalformedNoInterface) return E_NOINTERFACE;
        if (mode == FailedBaseWithValue) return E_FAIL;
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
#define NR_FWD(ret, name, params, args) ret STDMETHODCALLTYPE name params override { return base->name args; }
    NR_FWD(HRESULT, GetPrivateData, (REFGUID g,UINT* n,void* p), (g,n,p))
    NR_FWD(HRESULT, SetPrivateData, (REFGUID g,UINT n,const void* p), (g,n,p))
    NR_FWD(HRESULT, SetPrivateDataInterface, (REFGUID g,const IUnknown* p), (g,p))
    NR_FWD(HRESULT, SetName, (LPCWSTR n), (n))
    NR_FWD(UINT, GetNodeCount, (), ())
    NR_FWD(HRESULT, CreateCommandQueue, (const D3D12_COMMAND_QUEUE_DESC* d,REFIID i,void** p), (d,i,p))
    NR_FWD(HRESULT, CreateCommandAllocator, (D3D12_COMMAND_LIST_TYPE t,REFIID i,void** p), (t,i,p))
    NR_FWD(HRESULT, CreateGraphicsPipelineState, (const D3D12_GRAPHICS_PIPELINE_STATE_DESC* d,REFIID i,void** p), (d,i,p))
    NR_FWD(HRESULT, CreateComputePipelineState, (const D3D12_COMPUTE_PIPELINE_STATE_DESC* d,REFIID i,void** p), (d,i,p))
    HRESULT STDMETHODCALLTYPE CreateCommandList(UINT n,D3D12_COMMAND_LIST_TYPE t,
        ID3D12CommandAllocator* a,ID3D12PipelineState* s,REFIID i,void** p) override
    { ++commandListsCreated; return base->CreateCommandList(n,t,a,s,i,p); }
    NR_FWD(HRESULT, CheckFeatureSupport, (D3D12_FEATURE f,void* p,UINT n), (f,p,n))
    NR_FWD(HRESULT, CreateDescriptorHeap, (const D3D12_DESCRIPTOR_HEAP_DESC* d,REFIID i,void** p), (d,i,p))
    NR_FWD(UINT, GetDescriptorHandleIncrementSize, (D3D12_DESCRIPTOR_HEAP_TYPE t), (t))
    NR_FWD(HRESULT, CreateRootSignature, (UINT n,const void* b,SIZE_T s,REFIID i,void** p), (n,b,s,i,p))
    NR_FWD(void, CreateConstantBufferView, (const D3D12_CONSTANT_BUFFER_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h), (d,h))
    NR_FWD(void, CreateShaderResourceView, (ID3D12Resource* r,const D3D12_SHADER_RESOURCE_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h), (r,d,h))
    NR_FWD(void, CreateUnorderedAccessView, (ID3D12Resource* r,ID3D12Resource* c,const D3D12_UNORDERED_ACCESS_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h), (r,c,d,h))
    NR_FWD(void, CreateRenderTargetView, (ID3D12Resource* r,const D3D12_RENDER_TARGET_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h), (r,d,h))
    NR_FWD(void, CreateDepthStencilView, (ID3D12Resource* r,const D3D12_DEPTH_STENCIL_VIEW_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h), (r,d,h))
    NR_FWD(void, CreateSampler, (const D3D12_SAMPLER_DESC* d,D3D12_CPU_DESCRIPTOR_HANDLE h), (d,h))
    NR_FWD(void, CopyDescriptors, (UINT n,const D3D12_CPU_DESCRIPTOR_HANDLE* d,const UINT* ds,UINT m,const D3D12_CPU_DESCRIPTOR_HANDLE* s,const UINT* ss,D3D12_DESCRIPTOR_HEAP_TYPE t), (n,d,ds,m,s,ss,t))
    NR_FWD(void, CopyDescriptorsSimple, (UINT n,D3D12_CPU_DESCRIPTOR_HANDLE d,D3D12_CPU_DESCRIPTOR_HANDLE s,D3D12_DESCRIPTOR_HEAP_TYPE t), (n,d,s,t))
    NR_FWD(D3D12_RESOURCE_ALLOCATION_INFO, GetResourceAllocationInfo, (UINT m,UINT n,const D3D12_RESOURCE_DESC* d), (m,n,d))
    NR_FWD(D3D12_HEAP_PROPERTIES, GetCustomHeapProperties, (UINT m,D3D12_HEAP_TYPE t), (m,t))
    NR_FWD(HRESULT, CreateCommittedResource, (const D3D12_HEAP_PROPERTIES* h,D3D12_HEAP_FLAGS f,const D3D12_RESOURCE_DESC* d,D3D12_RESOURCE_STATES s,const D3D12_CLEAR_VALUE* c,REFIID i,void** p), (h,f,d,s,c,i,p))
    NR_FWD(HRESULT, CreateHeap, (const D3D12_HEAP_DESC* d,REFIID i,void** p), (d,i,p))
    NR_FWD(HRESULT, CreatePlacedResource, (ID3D12Heap* h,UINT64 o,const D3D12_RESOURCE_DESC* d,D3D12_RESOURCE_STATES s,const D3D12_CLEAR_VALUE* c,REFIID i,void** p), (h,o,d,s,c,i,p))
    NR_FWD(HRESULT, CreateReservedResource, (const D3D12_RESOURCE_DESC* d,D3D12_RESOURCE_STATES s,const D3D12_CLEAR_VALUE* c,REFIID i,void** p), (d,s,c,i,p))
    NR_FWD(HRESULT, CreateSharedHandle, (ID3D12DeviceChild* o,const SECURITY_ATTRIBUTES* a,DWORD r,LPCWSTR n,HANDLE* h), (o,a,r,n,h))
    NR_FWD(HRESULT, OpenSharedHandle, (HANDLE h,REFIID i,void** p), (h,i,p))
    NR_FWD(HRESULT, OpenSharedHandleByName, (LPCWSTR n,DWORD a,HANDLE* h), (n,a,h))
    NR_FWD(HRESULT, MakeResident, (UINT n,ID3D12Pageable* const* p), (n,p))
    NR_FWD(HRESULT, Evict, (UINT n,ID3D12Pageable* const* p), (n,p))
    HRESULT STDMETHODCALLTYPE CreateFence(UINT64 v,D3D12_FENCE_FLAGS f,REFIID i,void** p) override
    { ++fencesCreated; return base->CreateFence(v,f,i,p); }
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override
    { return mode == DeviceLost ? DXGI_ERROR_DEVICE_REMOVED : base->GetDeviceRemovedReason(); }
    NR_FWD(void, GetCopyableFootprints, (const D3D12_RESOURCE_DESC* d,UINT f,UINT n,UINT64 b,D3D12_PLACED_SUBRESOURCE_FOOTPRINT* l,UINT* r,UINT64* s,UINT64* t), (d,f,n,b,l,r,s,t))
    NR_FWD(HRESULT, CreateQueryHeap, (const D3D12_QUERY_HEAP_DESC* d,REFIID i,void** p), (d,i,p))
    NR_FWD(HRESULT, SetStablePowerState, (BOOL b), (b))
    NR_FWD(HRESULT, CreateCommandSignature, (const D3D12_COMMAND_SIGNATURE_DESC* d,ID3D12RootSignature* r,REFIID i,void** p), (d,r,i,p))
    NR_FWD(void, GetResourceTiling, (ID3D12Resource* r,UINT* n,D3D12_PACKED_MIP_INFO* m,D3D12_TILE_SHAPE* s,UINT* k,UINT f,D3D12_SUBRESOURCE_TILING* t), (r,n,m,s,k,f,t))
    NR_FWD(LUID, GetAdapterLuid, (), ())
#undef NR_FWD
};
