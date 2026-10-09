#include "OwnedFrameRing.h"
// Standalone owned Feature 18 lane. GPL-3.0; composition is the unchanged current
// NeuRotic shader (including attributed RenoDX composition), not a new algorithm.
#include "../../../OptiScaler/dlssnr/CanonicalProviderIdentity.h"
#include "../../../OptiScaler/dlssnr/Feature18HostContract.h"
#include "../../../OptiScaler/dlssnr/NativeProviderEntry.h"
#include "../../../OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader.h"
#include "NrPolicy.h"
#include <WindowNrForwarderIdentity.h>
#include <array>
#include <chrono>
#include <cstring>
#include <d3d11_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <filesystem>
#include <mutex>
#include <nvsdk_ngx.h>
#include <stdexcept>

#include "GpuTimings.h"
#include "../diagnostics/FrameProbe.h"

namespace nrw
{
namespace
{
using namespace DlssNr::Canonical;
using CreateFn = void *(__cdecl *)(const wchar_t *, const wchar_t *, ID3D12Device *,
                                   ID3D12GraphicsCommandList *, void *, unsigned, unsigned, int, float, int,
                                   float, float, float, int, int);
using EvalFn = int(__cdecl *)(ID3D12GraphicsCommandList *, void *, void *, ID3D12Resource *, ID3D12Resource *,
                              ID3D12Resource *, ID3D12Resource *, unsigned, unsigned, unsigned, unsigned,
                              unsigned, unsigned, int, int, float, int, float, float, float, int, float,
                              float, float, float, const unsigned *, const DlssNr::ProviderEntryCallbacks *);
using ReleaseFn = int(__cdecl *)(void *, DlssNr::FeatureReleaseResult *);
using CoreInitFn = NVSDK_NGX_Result(__cdecl *)(unsigned long long, const wchar_t *, ID3D12Device *,
                                               NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo *);
using ParamsFn = NVSDK_NGX_Result(__cdecl *)(NVSDK_NGX_Parameter **);
using DestroyFn = NVSDK_NGX_Result(__cdecl *)(NVSDK_NGX_Parameter *);
void require(HRESULT hr, const char *what)
{
    if (FAILED(hr))
        throw std::runtime_error(std::string(what) +
                                 ": HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));
}
template <class T> T symbol(HMODULE m, const char *name)
{
    auto p = reinterpret_cast<T>(GetProcAddress(m, name));
    if (!p)
        throw std::runtime_error(std::string("Required NR export missing: ") + name);
    return p;
}
std::wstring modulePath(HMODULE module)
{
    std::wstring p(32768, L'\0');
    auto n = GetModuleFileNameW(module, p.data(), DWORD(p.size()));
    if (!n || n >= p.size())
        throw std::runtime_error("Module identity path unavailable");
    p.resize(n);
    return p;
}
// Prevent a selected root or any ancestor from being renamed between admission
// and LoadLibrary. File locks separately prohibit replacing its selected bytes.
struct DirectoryLocks
{
    std::vector<Handle> handles;
    void Hold(const std::filesystem::path &path)
    {
        // Attribute-only opens do not enforce Windows write/delete sharing.
        // Directory list/read access makes this a real namespace lease.
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Canonical directory namespace lease unavailable");
        handles.emplace_back(h);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(h, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Canonical directory lease rejects reparse/non-directory ancestors");
        std::wstring final(32768, L'\0');
        const auto size = GetFinalPathNameByHandleW(h, final.data(), DWORD(final.size()),
                                                    FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!size || size >= final.size())
            throw std::runtime_error("Canonical directory lease identity unavailable");
        final.resize(size);
        if (final.rfind(L"\\\\?\\", 0) == 0)
            final.erase(0, 4);
        if (!SamePath(std::filesystem::path(final), path))
            throw std::runtime_error("Canonical directory lease path differs from selected namespace");
    }
    void Open(const std::filesystem::path &requested)
    {
        if (!handles.empty() || !requested.is_absolute())
            throw std::runtime_error("Canonical directory lease requires a fresh absolute local path");
        std::wstring full(32768, L'\0');
        const auto size = GetFullPathNameW(requested.c_str(), DWORD(full.size()), full.data(), nullptr);
        if (!size || size >= full.size())
            throw std::runtime_error("Canonical absolute directory path unavailable");
        full.resize(size);
        const auto normalized = std::filesystem::path(full).lexically_normal();
        const auto drive = normalized.root_name().wstring();
        if (drive.size() != 2 || drive[1] != L':' ||
            !((drive[0] >= L'A' && drive[0] <= L'Z') || (drive[0] >= L'a' && drive[0] <= L'z')))
            throw std::runtime_error("Canonical directory lease refuses remote/device namespaces");
        auto path = normalized.root_path();
        Hold(path);
        for (const auto &component : normalized.relative_path())
        {
            if (component.empty() || component == L".")
                continue;
            path /= component;
            Hold(path); // ancestors freeze before descending into the selected root
        }
    }
};
// Flat 4-byte scalars match the current HLSL cbuffer. The 256-byte stride is
// required for D12 root CBVs. No alternate colour math is embedded here.
struct Constants
{
    uint32_t mode = 0;
    float white = 1;
    uint32_t width = 0, height = 0;
    float transfer = 1, colour = 1;
    uint32_t debug = 0;
    float maxRatio = 2;
    uint32_t passthrough = 1;
    float mvX = 1, mvY = 1;
    uint32_t guideWidth = 0, guideHeight = 0;
    uint32_t compare = 0;
    float split = 0, zoom = 1;
    uint32_t swap = 0, transferMode = 1;
    float debugScale = 1;
    uint32_t reversible = 0, apply = 1, gameExposure = 0;
    float preMul = 1;
    uint32_t signedPresent = 0;
    uint32_t padding[40]{};
};
static_assert(sizeof(Constants) == 256 && offsetof(Constants, signedPresent) == 92);
struct Image
{
    ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};
std::mutex globalOwnerMutex;
bool globalOwner = false;
} // namespace
struct NrHost::Impl
{
    std::mutex mutex;
    bool ready = false, poisoned = false, owner = false, coreInitialized = false, providerEntered = false,
         flight = false, recreateFeature = false;
    NrOptions options;
    GpuTimings gpuTimings;
    uint64_t historyEpoch=0;
    std::string adapterLuid;
    FrameStamp previous;
    bool previousEstimated = false;
    bool previousRawDepth = false, previousDepthReversed = false;
    UINT previousGuideWidth = 0, previousGuideHeight = 0;
    ComPtr<ID3D11Device> d11;
    ComPtr<ID3D11DeviceContext> c11;
    ComPtr<ID3D11Device5> d115;
    ComPtr<ID3D11DeviceContext4> c114;
    ComPtr<ID3D12Device> d12;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence, ingressFence;
    ComPtr<ID3D11Fence> ingress11;
    Handle event;
    uint64_t submitted = 0, ingressValue = 0;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12DescriptorHeap> heap, clearCpuHeap;
    ComPtr<ID3D12Resource> cb;
#ifdef NRW_NATIVE_TEST
    ComPtr<ID3D12Resource> readback;
#endif
    unsigned char *constants = nullptr;
    UINT descriptorSize = 0, width = 0, height = 0, workWidth = 0, workHeight = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 readbackBytes = 0;
    struct OutputSlot {bool retired=true;FrameStamp stamp;ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;};
    OwnedFrameRing<OutputSlot> outputs;
    ComPtr<ID3D11Texture2D> shared11, pendingOutput, pendingInput;
    ComPtr<ID3D11Texture2D> output11;
    ComPtr<ID3D11ShaderResourceView> outputView;
    ComPtr<ID3D11VertexShader> publishVertex;
    ComPtr<ID3D11PixelShader> publishPixel;
    bool pendingIngress = false, pendingEgress = false;
#ifdef NRW_NATIVE_TEST
    bool fixtureFailIngressSignal = false, fixtureFailIngressWait = false, fixtureFailEgressSignal = false;
#endif
    Image source, proxy, original, reducedProxy, model, output, depth, motion;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    DirectoryLocks modelDirectories, forwarderDirectories, coreDirectories;
    std::unique_ptr<LockedFile> modelFile, forwarderFile, coreFile;
    Package package;
    HMODULE forwarder = nullptr, core = nullptr;
    NVSDK_NGX_Parameter *params = nullptr;
    void *feature = nullptr;
    CreateFn create = nullptr;
    EvalFn evaluate = nullptr;
    ReleaseFn release = nullptr;
    int(__cdecl *shutdown)() = nullptr;
    const char *(__cdecl *contract)() = nullptr;
    DestroyFn destroyParams = nullptr;
    NVSDK_NGX_Result(__cdecl *coreShutdown)() = nullptr;
    DlssNr::FeatureReleaseAttempt releaseAttempt;
    bool ObserveCanonical()
    {
        return package.Observe(nullptr,true) && package.MatchesReviewedProvider() && package.ObservedProviderModule();
    }
    ~Impl()
    {
        if (cb && constants)
            cb->Unmap(0, nullptr);
        if (core)
            FreeLibrary(core);
        if (forwarder)
            FreeLibrary(forwarder);
        if (owner)
        {
            std::lock_guard<std::mutex> l(globalOwnerMutex);
            globalOwner = false;
        }
    }
    bool Drain()
    {
        if (!queue || !fence)
            return !flight;
        auto value = ++submitted;
        if (FAILED(queue->Signal(fence.Get(), value)))
            return false;
        flight = true;
        const auto completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX)
            return false;
        if (completed < value && (FAILED(fence->SetEventOnCompletion(value, event.value)) ||
                                  WaitForSingleObject(event.value, 5000) != WAIT_OBJECT_0))
            return false;
        if (fence->GetCompletedValue() == UINT64_MAX || fence->GetCompletedValue() < value)
            return false;
        flight = false;
        return true;
    }
    void Begin()
    {
        require(allocator->Reset(), "Reset private allocator");
        require(list->Reset(allocator.Get(), nullptr), "Reset private command list");
    }
    void Submit(NrResult *result = nullptr)
    {
        auto submitBegin=MeasureClock::now();
        require(list->Close(), "Close private command list");
        ID3D12CommandList *lists[]{list.Get()};
        queue->ExecuteCommandLists(1, lists);
        flight = true;
        if (result)
            result->submitted = true;
        if(result)result->measurements.submitCallMs=ElapsedMs(submitBegin);
        auto waitBegin=MeasureClock::now();
        const bool drained=Drain();
        if(result)result->measurements.queueWaitMs=ElapsedMs(waitBegin);
        if (!drained)
        {
            poisoned = true;
            throw std::runtime_error("NR queue completion unproved; generation quarantined");
        }
        if(result){result->completionValue=submitted;result->measurements.gpuCompleted=true;gpuTimings.Collect(fence.Get(),submitted,result->measurements);}
        // Queue completion covers the exact imported ingress wait as well as
        // this invocation's model/composition commands.
        pendingIngress = false;
        pendingInput.Reset();
        uploads.clear();
    }
    void Transition(Image &image, D3D12_RESOURCE_STATES state)
    {
        if (image.state == state)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {image.resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, image.state, state};
        list->ResourceBarrier(1, &b);
        image.state = state;
    }
    Image Texture(UINT w, UINT h, DXGI_FORMAT format)
    {
        Image i;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w;
        d.Height = h;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = format;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        require(d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, i.state, nullptr,
                                             IID_PPV_ARGS(&i.resource)),
                "Create NR image");
        return i;
    }
    ComPtr<ID3D12Resource> Buffer(UINT64 size, D3D12_HEAP_TYPE type)
    {
        ComPtr<ID3D12Resource> r;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = type;
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = size;
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        require(d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d,
                                             type == D3D12_HEAP_TYPE_UPLOAD
                                                 ? D3D12_RESOURCE_STATE_GENERIC_READ
                                                 : D3D12_RESOURCE_STATE_COPY_DEST,
                                             nullptr, IID_PPV_ARGS(&r)),
                "Create NR transfer buffer");
        return r;
    }
    void Pipeline()
    {
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, 0};
        ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 5};
        D3D12_ROOT_PARAMETER rp[2]{};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rp[0].Descriptor.ShaderRegister = 0;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable = {2, ranges};
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 2;
        desc.pParameters = rp;
        desc.NumStaticSamplers = 1;
        desc.pStaticSamplers = &sampler;
        ComPtr<ID3DBlob> signature, error;
        require(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error),
                "Serialize current composition root");
        require(d12->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                         IID_PPV_ARGS(&root)),
                "Create composition root");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = root.Get();
        pd.CS = {DlssNr_cso, sizeof(DlssNr_cso)};
        require(d12->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)),
                "Create current composition pipeline");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 23; // Three dispatch tables, followed by two neutral UAVs.
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        require(d12->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "Create owned descriptor heap");
        hd.NumDescriptors = 2;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        require(d12->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&clearCpuHeap)), "Create CPU-only clear descriptors");
        descriptorSize = d12->GetDescriptorHandleIncrementSize(hd.Type);
        cb = Buffer(3 * sizeof(Constants), D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RANGE noRead{};
        require(cb->Map(0, &noRead, reinterpret_cast<void **>(&constants)), "Map constant slots");
    }
    void Dispatch(UINT slot, const Constants &c, std::array<Image *, 5> srvs, std::array<Image *, 2> uavs)
    {
        for (auto *i : srvs)
            if (i)
                Transition(*i, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        for (auto *i : uavs)
            if (i)
                Transition(*i, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += SIZE_T(slot * 7) * descriptorSize;
        for (auto *i : srvs)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC v{};
            v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            v.Format = i ? i->resource->GetDesc().Format : DXGI_FORMAT_R16G16B16A16_FLOAT;
            v.Texture2D.MipLevels = 1;
            d12->CreateShaderResourceView(i ? i->resource.Get() : nullptr, &v, cpu);
            cpu.ptr += descriptorSize;
        }
        for (auto *i : uavs)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC v{};
            v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            v.Format = i ? i->resource->GetDesc().Format : DXGI_FORMAT_R16G16B16A16_FLOAT;
            d12->CreateUnorderedAccessView(i ? i->resource.Get() : nullptr, nullptr, &v, cpu);
            cpu.ptr += descriptorSize;
        }
        std::memcpy(constants + slot * sizeof(Constants), &c, sizeof(c));
        ID3D12DescriptorHeap *heaps[]{heap.Get()};
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(root.Get());
        list->SetPipelineState(pso.Get());
        list->SetComputeRootConstantBufferView(0, cb->GetGPUVirtualAddress() + slot * sizeof(Constants));
        auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += UINT64(slot * 7) * descriptorSize;
        list->SetComputeRootDescriptorTable(1, gpu);
        list->Dispatch((c.width + 7) / 8, (c.height + 7) / 8, 1);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        list->ResourceBarrier(1, &barrier);
    }
    void Upload(Image &image, const float *values, UINT channels)
    {
        auto desc = image.resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64 bytes = 0;
        d12->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, &bytes);
        auto upload = Buffer(bytes, D3D12_HEAP_TYPE_UPLOAD);
        unsigned char *mapped = nullptr;
        D3D12_RANGE noRead{};
        require(upload->Map(0, &noRead, reinterpret_cast<void **>(&mapped)), "Map guide upload");
        for (UINT y = 0; y < desc.Height; ++y)
            std::memcpy(mapped + SIZE_T(y) * fp.Footprint.RowPitch,
                        values + SIZE_T(y) * desc.Width * channels,
                        SIZE_T(desc.Width) * channels * sizeof(float));
        upload->Unmap(0, nullptr);
        Transition(image, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource = upload.Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION to{};
        to.pResource = image.resource.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        uploads.push_back(upload);
    }
    void PrepareNeutralGuides()
    {
        // Reclear every frame: the provider is not assumed to preserve inputs.
        // Begin follows exact completion of the previous frame, so these two
        // descriptors cannot be overwritten while a preceding clear is in flight.
        ID3D12DescriptorHeap* heaps[]{heap.Get()};
        list->SetDescriptorHeaps(1, heaps);
        auto visibleCpu=heap->GetCPUDescriptorHandleForHeapStart();
        auto visibleGpu=heap->GetGPUDescriptorHandleForHeapStart();
        visibleCpu.ptr+=SIZE_T(21)*descriptorSize;
        visibleGpu.ptr+=UINT64(21)*descriptorSize;
        auto cpu=clearCpuHeap->GetCPUDescriptorHandleForHeapStart();
        const float zero[4]{};
        for(auto* image:{&depth,&motion}) {
            Transition(*image,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource=image->resource.Get();list->ResourceBarrier(1,&barrier);
            D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
            view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;view.Format=image->resource->GetDesc().Format;
            d12->CreateUnorderedAccessView(image->resource.Get(),nullptr,&view,cpu);
            d12->CreateUnorderedAccessView(image->resource.Get(),nullptr,&view,visibleCpu);
            list->ClearUnorderedAccessViewFloat(visibleGpu,cpu,image->resource.Get(),zero,0,nullptr);
            list->ResourceBarrier(1,&barrier);
            visibleCpu.ptr+=descriptorSize;visibleGpu.ptr+=descriptorSize;cpu.ptr+=descriptorSize;
        }
    }
    bool ReleaseFeature()
    {
        if (!feature)
            return true;
        const bool ok = releaseAttempt.Try(!flight, [&] {
            DlssNr::FeatureReleaseResult r;
            if (!release(feature, &r))
                r.disposition = DlssNr::FeatureReleaseDisposition::Unavailable;
            return r;
        });
        if (ok)
        {
            feature = nullptr;
            releaseAttempt = {};
        }
        return ok;
    }
    void Resources(UINT w, UINT h, UINT ww, UINT wh)
    {
        width = w;
        height = h;
        workWidth = ww;
        workHeight = wh;
        previous = {};
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        shared11.Reset();
        source = {};
        require(d11->CreateTexture2D(&td, nullptr, &shared11), "Create private D11 ingress");
        ComPtr<IDXGIResource1> dxgi;
        require(shared11.As(&dxgi), "Query actual shared ingress");
        HANDLE shared = nullptr;
        require(dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                         nullptr, &shared),
                "Create ingress NT handle");
        Handle sharedHandle(shared);
        require(d12->OpenSharedHandle(shared, IID_PPV_ARGS(&source.resource)),
                "Import actual D11 resource into D12");
        proxy = Texture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
        original = Texture(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT);
        reducedProxy = Texture(ww, wh, DXGI_FORMAT_R16G16B16A16_FLOAT);
        model = Texture(ww, wh, DXGI_FORMAT_R16G16B16A16_FLOAT);
        outputView.Reset();output11.Reset();
        output = {};
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        require(d11->CreateTexture2D(&td,nullptr,&output11),"Create interoperable GPU output");
        ComPtr<IDXGIResource1> sharedOutputResource;
        require(output11.As(&sharedOutputResource),"GPU output sharing interface");
        HANDLE outputHandle=nullptr;
        require(sharedOutputResource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&outputHandle),"Share completed GPU output");
        Handle sharedOutput(outputHandle);
        require(d12->OpenSharedHandle(outputHandle,IID_PPV_ARGS(&output.resource)),"Open same-adapter GPU output in D12");
        require(d11->CreateShaderResourceView(output11.Get(),nullptr,&outputView),"GPU output SRV");
        depth = Texture(w, h, DXGI_FORMAT_R32_FLOAT);
        motion = Texture(w, h, DXGI_FORMAT_R32G32_FLOAT);
#ifdef NRW_NATIVE_TEST
        auto outDesc = output.resource->GetDesc();
        d12->GetCopyableFootprints(&outDesc, 0, 1, 0, &footprint, nullptr, nullptr, &readbackBytes);
        readback = Buffer(readbackBytes, D3D12_HEAP_TYPE_READBACK);
#endif
    }
    void Shape(UINT w, UINT h)
    {
        const auto [ww,wh] = detail::WorkingExtent(options,w,h);
        std::string admission;
        if (!detail::AdmitShape(w, h, ww, wh, admission))
            throw std::runtime_error(admission);
        if (width == w && height == h && workWidth == ww && workHeight == wh && !recreateFeature)
            return;
        if (!Drain() || !ReleaseFeature())
        {
            poisoned = true;
            throw std::runtime_error("Previous model generation retirement unproved; quarantined");
        }
        Resources(w, h, ww, wh);
        Begin();
        providerEntered = true;
        feature =
            create(options.modelPath.c_str(), std::filesystem::path(options.modelPath).parent_path().c_str(),
                   d12.Get(), list.Get(), params, ww, wh, 0, 1, options.modelStyle, 1, 1, -1, 1, 1);
        if (!feature)
        {
            poisoned = true;
            throw std::runtime_error("Feature 18 create refused; possible provider recording quarantined");
        }
        const char *actual = contract();
        if (!actual || std::strcmp(actual, DlssNr::Feature18HostContractIdentity))
        {
            poisoned = true;
            throw std::runtime_error("Exact provider host-recorded contract unavailable");
        }
        Submit();
        recreateFeature=false;
    }
    void SetupGpu(ID3D11Device *device, ID3D11DeviceContext *context)
    {
        d11 = device;
        c11 = context;
        require(d11.As(&d115), "D11 shared-fence device required");
        require(c11.As(&c114), "D11 shared-fence context required");
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        require(d11.As(&dxgi), "Query capture adapter");
        require(dxgi->GetAdapter(&adapter), "Capture adapter");
        DXGI_ADAPTER_DESC adapterDesc{};
        if(SUCCEEDED(adapter->GetDesc(&adapterDesc)))adapterLuid=std::to_string(uint32_t(adapterDesc.AdapterLuid.HighPart))+":"+std::to_string(adapterDesc.AdapterLuid.LowPart);
        require(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d12)),
                "Create D12 owner on capture adapter");
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        require(d12->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)), "Create private NR queue");
        require(d12->CreateCommandAllocator(qd.Type, IID_PPV_ARGS(&allocator)), "Create private allocator");
        require(d12->CreateCommandList(0, qd.Type, allocator.Get(), nullptr, IID_PPV_ARGS(&list)),
                "Create private list");
        require(list->Close(), "Close initial list");
        require(d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create completion fence");
        event.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event.value == nullptr)
        {
            event.value = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Completion event unavailable");
        }
        require(d115->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&ingress11)),
                "Create D11 shared fence");
        HANDLE fenceHandle = nullptr;
        require(ingress11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fenceHandle),
                "Share D11 fence");
        Handle sharedFence(fenceHandle);
        require(d12->OpenSharedHandle(fenceHandle, IID_PPV_ARGS(&ingressFence)),
                "Import exact ingress fence");
        // Resolve UNORM RGBA into an independently owned BGRA render target on GPU.
        // The source has a proven D12 completion before D11 samples it.
        constexpr char blit[]="Texture2D<float4> completed:register(t0); float4 VS(uint id:SV_VertexID):SV_Position {float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1);} float4 PS(float4 p:SV_Position):SV_Target{return completed.Load(int3(int2(p.xy),0));}";
        ComPtr<ID3DBlob> vs,ps,errors;
        require(D3DCompile(blit,sizeof(blit)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&errors),"Compile GPU output vertex shader");
        require(D3DCompile(blit,sizeof(blit)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&errors),"Compile GPU output pixel shader");
        require(d11->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&publishVertex),"GPU output vertex shader");
        require(d11->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&publishPixel),"GPU output pixel shader");
        Pipeline();
        if(options.gpuTiming)gpuTimings.Initialize(d12.Get(),queue.Get());
    }
    void Initialize(ID3D11Device *device, ID3D11DeviceContext *context, const NrOptions &requested)
    {
        std::string reason;
        if (!detail::ValidOptions(requested, reason))
            throw std::runtime_error(reason);
        if (!device || !context)
            throw std::runtime_error("Capture D11 device and immediate context required");
        ComPtr<ID3D11Device> contextDevice;
        context->GetDevice(&contextDevice);
        if (contextDevice.Get() != device || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
            throw std::runtime_error("NR requires the capture device's immediate context");
        options = requested;
        modelDirectories.Open(std::filesystem::path(options.modelPath).parent_path());
        modelFile = std::make_unique<LockedFile>(options.modelPath);
        if (_wcsicmp(std::filesystem::path(options.modelPath).filename().c_str(), L"nvngx_dlssnr.dll") ||
            modelFile->bytes != Members[4].bytes || modelFile->hash != Members[4].sha256)
            throw std::runtime_error("Exact canonical DLSS NR file required (size/SHA-256 mismatch)");
        forwarderDirectories.Open(std::filesystem::path(options.forwarderPath).parent_path());
        forwarderFile = std::make_unique<LockedFile>(options.forwarderPath);
        if (_wcsicmp(std::filesystem::path(options.forwarderPath).filename().c_str(),
                     L"nvngx.dll_dlssnr.dll") ||
            std::strlen(WindowNrForwarderSha256) != 64 || forwarderFile->hash != WindowNrForwarderSha256)
            throw std::runtime_error("NR Anything's worker and forwarder do not match. Extract the complete matching App package. Expected SHA-256 " +
                                     std::string(WindowNrForwarderSha256) + "; found " + forwarderFile->hash);
        {
            std::lock_guard<std::mutex> lock(globalOwnerMutex);
            if (globalOwner)
                throw std::runtime_error("Only one standalone NR owner is admitted per worker");
            globalOwner = true;
            owner = true;
        }
        SetupGpu(device, context);
        if (!package.Open(options.modelPath))
            throw std::runtime_error("Canonical provider admission: " + package.Error());
        forwarder = LoadLibraryExW(options.forwarderPath.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!forwarder)
            throw std::runtime_error("Standalone NR forwarder failed to load");
        if (!SamePath(modulePath(forwarder), options.forwarderPath))
            throw std::runtime_error("Loaded forwarder identity differs from selection");
        create = symbol<CreateFn>(forwarder, "dlssnr_call_create");
        evaluate = symbol<EvalFn>(forwarder, "dlssnr_call_evaluate_guides_v4");
        release = symbol<ReleaseFn>(forwarder, "dlssnr_call_release_checked_v1");
        shutdown = symbol<decltype(shutdown)>(forwarder, "dlssnr_call_shutdown");
        contract = symbol<decltype(contract)>(forwarder, "dlssnr_call_host_recorded_contract_v1");
        auto probe = symbol<int(__cdecl *)(const wchar_t *)>(forwarder, "dlssnr_call_probe_d3d12");
        if (probe(options.modelPath.c_str()) != 31 || !ObserveCanonical())
            throw std::runtime_error("Exact loaded canonical provider path/bytes or D12 exports refused");
        if (!contract() || std::strcmp(contract(), DlssNr::Feature18HostContractIdentity))
            throw std::runtime_error("Provider recording contract differs from reviewed ABI");
        wchar_t coreRoot[32768]{};
        DWORD bytes = sizeof(coreRoot);
        DWORD type = 0;
        LSTATUS reg =
            RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\nvlddmkm\\NGXCore",
                         L"NGXPath", RRF_RT_REG_SZ, &type, coreRoot, &bytes);
        if (reg != ERROR_SUCCESS || !coreRoot[0])
            throw std::runtime_error("Installed NVIDIA NGX core path unavailable");
        if (!std::filesystem::path(coreRoot).is_absolute())
            throw std::runtime_error("Installed NVIDIA NGX core path is not absolute");
        coreDirectories.Open(coreRoot);
        auto corePath = detail::CoreAtRoot(coreRoot);
        if (corePath.empty())
            throw std::runtime_error("Installed NVIDIA NGX core binary unavailable in locked driver root");
        coreFile = std::make_unique<LockedFile>(corePath);
        core = LoadLibraryExW(corePath.c_str(), nullptr,
                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!core)
            throw std::runtime_error("Installed NVIDIA NGX core cannot load");
        if (!SamePath(modulePath(core), corePath))
            throw std::runtime_error("Loaded NVIDIA NGX core path differs from locked driver path");
        auto init = symbol<CoreInitFn>(core, "NVSDK_NGX_D3D12_Init_Ext");
        auto capability = symbol<ParamsFn>(core, "NVSDK_NGX_D3D12_GetCapabilityParameters");
        destroyParams = symbol<DestroyFn>(core, "NVSDK_NGX_D3D12_DestroyParameters");
        coreShutdown = symbol<decltype(coreShutdown)>(core, "NVSDK_NGX_D3D12_Shutdown");
        NVSDK_NGX_FeatureCommonInfo info{};
        const std::wstring parent = std::filesystem::path(options.modelPath).parent_path().wstring();
        const wchar_t *search = parent.c_str();
        info.PathListInfo.Path = &search;
        info.PathListInfo.Length = 1;
        providerEntered = true;
        auto rc = init(0x24480451ull, parent.c_str(), d12.Get(), NVSDK_NGX_Version_API, &info);
        if (rc != NVSDK_NGX_Result_Success)
        {
            poisoned = true;
            throw std::runtime_error("NGX core init refused: " + std::to_string(static_cast<unsigned>(rc)));
        }
        coreInitialized = true;
        if (capability(&params) != NVSDK_NGX_Result_Success || !params)
        {
            poisoned = true;
            throw std::runtime_error("Driver capability parameter block unavailable");
        }
        constexpr float expected = .375f;
        float value = 0;
        params->Set("DLSSNR.TypedFloatAbiCheck", expected);
        if (params->Get("DLSSNR.TypedFloatAbiCheck", &value) != NVSDK_NGX_Result_Success || value != expected)
            throw std::runtime_error("Driver typed parameter ABI refused");
        auto probeFloat =
            symbol<void(__cdecl *)(void *, const char *, float, int)>(forwarder, "dlssnr_call_probe_float");
        probeFloat(params, "DLSSNR.OptiScalerFloatProbe", expected, 6);
        value = 0;
        if (params->Get("DLSSNR.OptiScalerFloatProbe", &value) != NVSDK_NGX_Result_Success ||
            value != expected)
            throw std::runtime_error("Driver MSVC x64 float slot ABI refused");
        symbol<void(__cdecl *)(int)>(forwarder, "dlssnr_call_set_float_slot")(6);
        ready = true;
    }
    void Ingress(ID3D11Texture2D *texture)
    {
        pendingInput = texture;
        pendingIngress = true; // before CopyResource can enqueue D11 work
        c11->CopyResource(shared11.Get(), texture);
#ifdef NRW_NATIVE_TEST
        if (fixtureFailIngressSignal)
            require(E_FAIL, "Injected ingress signal failure");
#endif
        require(c114->Signal(ingress11.Get(), ++ingressValue), "Signal capture ingress");
        c11->Flush();
#ifdef NRW_NATIVE_TEST
        if (fixtureFailIngressWait)
            require(E_FAIL, "Injected ingress wait failure");
#endif
        require(queue->Wait(ingressFence.Get(), ingressValue), "Wait actual ingress completion");
    }
    ComPtr<ID3D11Texture2D> PublishD11(D3D11_TEXTURE2D_DESC td,NrResult& publication,FrameProbe* probe=nullptr)
    {
        td.MiscFlags = 0;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.CPUAccessFlags = 0;
        auto slot=outputs.TryAcquire();
        if(!slot)throw std::runtime_error("Output frame ring busy; latest frame dropped");
        if(slot->texture) {
            D3D11_TEXTURE2D_DESC previous{};slot->texture->GetDesc(&previous);
            if(previous.Width!=td.Width||previous.Height!=td.Height||previous.Format!=td.Format) {slot->target.Reset();slot->texture.Reset();}
        }
        if(!slot->texture) {
            require(d11->CreateTexture2D(&td,nullptr,&slot->texture),"Create owned BGRA output slot");
            require(d11->CreateRenderTargetView(slot->texture.Get(),nullptr,&slot->target),"GPU publication render target");
        }
        slot->stamp=publication.stamp;
        pendingOutput=slot->texture;
        const auto& target=slot->target;
        pendingEgress = true; // before draw can read shared output or write destination
        c11->ClearState();
        auto* rtv=target.Get();auto* srv=outputView.Get();
        c11->OMSetRenderTargets(1,&rtv,nullptr);c11->PSSetShaderResources(0,1,&srv);
        c11->VSSetShader(publishVertex.Get(),nullptr,0);c11->PSSetShader(publishPixel.Get(),nullptr,0);
        c11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};c11->RSSetViewports(1,&viewport);
        c11->Draw(3,0);c11->ClearState();
        const bool measured=probe&&probe->RecordPublication(c11.Get(),slot->texture.Get(),outputView.Get());
#ifdef NRW_NATIVE_TEST
        if (fixtureFailEgressSignal)
            require(E_FAIL, "Injected egress signal failure");
#endif
        require(c114->Signal(ingress11.Get(), ++ingressValue), "Signal completed BGRA upload");
        c11->Flush();
        if (ingress11->GetCompletedValue() == UINT64_MAX ||
            (ingress11->GetCompletedValue() < ingressValue &&
             (FAILED(ingress11->SetEventOnCompletion(ingressValue, event.value)) ||
              WaitForSingleObject(event.value, 5000) != WAIT_OBJECT_0)) ||
            ingress11->GetCompletedValue() == UINT64_MAX || ingress11->GetCompletedValue() < ingressValue)
        {
            poisoned = true;
            throw std::runtime_error("D11 output visibility unproved; generation quarantined");
        }
        pendingEgress = false;
        if(measured)probe->CompletePublication(ingress11.Get(),ingressValue);
        slot->retired=true; publication.ownership=slot;
        auto result = pendingOutput;
        pendingOutput.Reset();
        return result;
    }
    NrResult Process(const CapturedFrame &frame, const GuideResult *guides,FrameProbe* probe=nullptr)
    {
        NrResult r;
        r.stamp = frame.stamp;
        r.measurements.adapterLuid=adapterLuid;
        auto start = std::chrono::steady_clock::now();
        try
        {
            if (!ready || poisoned)
                throw std::runtime_error(poisoned ? "NR generation quarantined" : "NR is not initialized");
            if (!frame.texture || !frame.stamp.session || !frame.stamp.sequence || !frame.stamp.width ||
                !frame.stamp.height || !frame.stamp.timestampQpc)
                throw std::runtime_error("Stamped owned capture required");
            ComPtr<ID3D11Device> frameDevice;
            frame.texture->GetDevice(&frameDevice);
            if (frameDevice.Get() != d11.Get())
                throw std::runtime_error("Frame is not on the capture device");
            D3D11_TEXTURE2D_DESC td{};
            frame.texture->GetDesc(&td);
            if (td.Width != frame.stamp.width || td.Height != frame.stamp.height || td.Width > 16384 ||
                td.Height > 16384 || td.Format != DXGI_FORMAT_B8G8R8A8_UNORM || td.SampleDesc.Count != 1 ||
                td.MipLevels != 1 || td.ArraySize != 1)
                throw std::runtime_error("Frame must be an exact BGRA8 SDR snapshot");
            const bool estimated=detail::UseGuides(options,frame.stamp,guides);
            std::vector<float> relativeDepth;bool effectiveDepthInverted=false;
            if(guides&&guides->rawDeviceDepth&&!estimated)
                throw std::runtime_error("Prepared device-depth frame admission refused");
            if(estimated&&!PrepareDepthUpload(*guides,relativeDepth,effectiveDepthInverted))
                throw std::runtime_error("Prepared/estimated depth values outside declared range");
            auto shapeBegin=MeasureClock::now();
            Shape(td.Width, td.Height);
            r.measurements.shapeMs=ElapsedMs(shapeBegin);
            r.workWidth = workWidth;
            r.workHeight = workHeight;
            auto ingressBegin=MeasureClock::now();
            Ingress(frame.texture.Get());
            r.measurements.ingressMs=ElapsedMs(ingressBegin);
            Begin();
            gpuTimings.Begin();gpuTimings.Mark(list.Get(),0);
            auto guideBegin=MeasureClock::now();
            const UINT guideWidth = estimated ? guides->width : width,
                       guideHeight = estimated ? guides->height : height;
            if (depth.resource->GetDesc().Width != guideWidth ||
                depth.resource->GetDesc().Height != guideHeight)
            {
                depth = Texture(guideWidth, guideHeight, DXGI_FORMAT_R32_FLOAT);
                motion = Texture(guideWidth, guideHeight, DXGI_FORMAT_R32G32_FLOAT);
            }
            if (estimated)
            {
                // Ordinary estimates use normalized relative inverse depth. An
                // explicit prepared device-depth adapter preserves validated [0,1]
                // values and declared reversal; neither certifies engine velocity.
                Upload(depth, relativeDepth.data(), 1);
                Upload(motion, guides->motionXY.data(), 2);
            }
            else
            {
                PrepareNeutralGuides();
            }
            r.measurements.guidePrepareMs=ElapsedMs(guideBegin);
            r.measurements.guideUploadBytes=estimated ? uint64_t(guideWidth)*guideHeight*3*sizeof(float) : 0;
            r.measurements.guidePreparation=estimated ? "cpu-upload" : "gpu-clear";
            gpuTimings.Mark(list.Get(),1);
            Constants c;
            c.width = width;
            c.height = height;
            c.guideWidth = guideWidth;
            c.guideHeight = guideHeight;
            c.transfer = options.transferStrength;
            c.colour = options.colourStrength;
            Dispatch(0, c, {&source, nullptr, nullptr, nullptr, nullptr}, {&proxy, &original});
            Image *input = &proxy;
            if (workWidth != width || workHeight != height)
            {
                c.mode = 2;
                c.width = workWidth;
                c.height = workHeight;
                Dispatch(1, c, {&proxy, nullptr, nullptr, nullptr, nullptr}, {&reducedProxy, nullptr});
                input = &reducedProxy;
            }
            Transition(*input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Transition(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Transition(motion, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Transition(model, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            auto identityBegin=MeasureClock::now();
            if (!ObserveCanonical() || !contract() ||
                std::strcmp(contract(), DlssNr::Feature18HostContractIdentity))
            {
                poisoned = true;
                throw std::runtime_error("Provider identity/recording contract changed");
            }
            r.measurements.identityMs=ElapsedMs(identityBegin);
            struct Entry
            {
                bool entered = false, returned = false, success = false;
            } entry;
            DlssNr::ProviderEntryCallbacks callbacks{&entry,
                                                     [](void *p) noexcept {
                                                         static_cast<Entry *>(p)->entered = true;
                                                         return true;
                                                     },
                                                     [](void *p, bool success) noexcept {
                                                         auto &e = *static_cast<Entry *>(p);
                                                         e.returned = true;
                                                         e.success = success;
                                                     }};
            unsigned origins[4]{};
            providerEntered = true;
            r.measurements.reset=detail::ResetHistory(frame.stamp, previous) || estimated != previousEstimated ||
                guideWidth != previousGuideWidth || guideHeight != previousGuideHeight ||
                (estimated&&(guides->rawDeviceDepth!=previousRawDepth||effectiveDepthInverted!=previousDepthReversed));
            if(r.measurements.reset)++historyEpoch;
            r.measurements.historyEpoch=historyEpoch;
            r.measurements.prepareMs=ElapsedMs(start);
            gpuTimings.Mark(list.Get(),2);
            auto evaluateBegin=MeasureClock::now();
            int rc = evaluate(list.Get(), feature, params, input->resource.Get(), depth.resource.Get(),
                              motion.resource.Get(), model.resource.Get(), workWidth, workHeight, guideWidth,
                              guideHeight, guideWidth, guideHeight, effectiveDepthInverted ? 1 : 0,
                              r.measurements.reset,
                              1, options.modelStyle, 1, 1, -1, 1, float(workWidth) / guideWidth,
                              float(workHeight) / guideHeight, 0, 0, origins, &callbacks);
            r.measurements.evaluateCallMs=ElapsedMs(evaluateBegin);
            gpuTimings.Mark(list.Get(),3);
            r.recorded = entry.entered && entry.returned && entry.success && rc == 1;
            if (!r.recorded)
            {
                poisoned = true;
                throw std::runtime_error("Feature 18 evaluation refused; possible provider work quarantined");
            }
            D3D12_RESOURCE_BARRIER uav{};
            uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            uav.UAV.pResource = model.resource.Get();
            list->ResourceBarrier(1, &uav);
            c.mode = 1;
            c.width = width;
            c.height = height;
            Dispatch(2, c, {input, &model, &original, &motion, nullptr}, {&output, nullptr});
            bool measured=false;
            if(probe&&probe->Pending()) {
                const auto& id=probe->Identity();
                if(id.frame.session!=frame.stamp.session||id.frame.sequence!=frame.stamp.sequence||id.frame.timestampQpc!=frame.stamp.timestampQpc||id.frame.streamEpoch!=frame.stamp.streamEpoch||id.frame.geometryEpoch!=frame.stamp.geometryEpoch||id.frame.configEpoch!=frame.stamp.configEpoch||id.workWidth!=workWidth||id.workHeight!=workHeight||id.modelStyle!=options.modelStyle)
                    probe->Invalidate("NR frame or effective processing differs from the admitted frame");
                else {
                    std::array<uint32_t,24> effective{};std::memcpy(effective.data(),&c,sizeof(effective));
                    if(probe->CaptureConstants(effective))measured=probe->RecordNr(d12.Get(),d11.Get(),c11.Get(),list.Get(),model.resource.Get(),input->resource.Get(),output.resource.Get(),source.resource.Get());
                }
            }
            Transition(output, D3D12_RESOURCE_STATE_COMMON);
            Transition(source, D3D12_RESOURCE_STATE_COMMON);
            gpuTimings.Mark(list.Get(),4);gpuTimings.Resolve(list.Get());
            Submit(&r);
            if(measured)probe->CompleteNr(fence.Get(),r.completionValue);
            auto publishBegin=MeasureClock::now();
            r.texture = PublishD11(td,r,probe);
            r.measurements.publishMs=ElapsedMs(publishBegin);
            r.completed = true;
            r.estimatedGuidesUsed = estimated;
            previous = frame.stamp;
            previousEstimated = estimated;
            previousRawDepth=estimated&&guides->rawDeviceDepth;
            previousDepthReversed=effectiveDepthInverted;
            previousGuideWidth = guideWidth;
            previousGuideHeight = guideHeight;
            r.reason =
                estimated&&guides->rawDeviceDepth
                    ? "Completed NR with prepared raw device depth [0,1] and derived current-to-previous pixel motion"
                    : estimated ? "Completed NR with experimental estimated normalized relative inverse-depth [0,1] "
                      "(larger=nearer) and guide-pixel current-to-previous motion scaled into model pixels"
                    : "Completed NR with constant depth and zero motion";
        }
        catch (const std::exception &e)
        {
            if(probe&&probe->Busy()) {
                if(pendingIngress||pendingEgress||flight)probe->Unknown("NR or publication GPU completion is unproved");
                else probe->Invalidate("NR frame did not complete");
            }
            r.reason = e.what();
            if (pendingIngress || pendingEgress ||
                (providerEntered && (flight || r.recorded) && !r.completed))
                poisoned = true;
        }
        r.milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return r;
    }
    bool Close()
    {
        ready = false;
        if (poisoned || pendingIngress || pendingEgress)
            return false;
        if (!Drain() || !ReleaseFeature())
            return false;
        if (providerEntered && shutdown && shutdown() != 1)
            return false;
        if (params && destroyParams)
        {
            if (destroyParams(params) != NVSDK_NGX_Result_Success)
                return false;
            params = nullptr;
        }
        if (coreInitialized && coreShutdown && coreShutdown() != NVSDK_NGX_Result_Success)
            return false;
        coreInitialized = false;
        return true;
    }
};
// Quarantine is intentional process-lifetime retention: an unknown queue or
// provider tail never becomes a successful drain by releasing the owner.
NrHost::NrHost() : impl_(std::make_unique<Impl>())
{
}
NrHost::~NrHost()
{
    Stop();
}
bool NrHost::Initialize(ID3D11Device *device, ID3D11DeviceContext *context, const NrOptions &options,
                        std::string &reason)
{
    Stop();
    if (retained_)
    {
        reason = "NR work remains quarantined; restart the worker process.";
        return false;
    }
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try
    {
        impl_->Initialize(device, context, options);
        reason.clear();
        return true;
    }
    catch (const std::exception &e)
    {
        reason = e.what();
        return false;
    }
}
NrResult NrHost::Process(const CapturedFrame &frame, const GuideResult *guides,FrameProbe* probe)
{
    if (!impl_)
    {
        NrResult r;
        r.stamp = frame.stamp;
        r.reason = "NR is stopped";
        return r;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->Process(frame, guides,probe);
}
bool NrHost::RequiresRestart() const
{
    return retained_;
}
bool NrHost::Reconfigure(const NrProcessingOptions& requested,std::string& reason)
{
    if(!impl_||retained_){reason="NR owner is stopped or quarantined";return false;}
    std::lock_guard<std::mutex> lock(impl_->mutex);auto& owner=*impl_;
    if(!owner.ready||owner.poisoned||owner.pendingIngress||owner.pendingEgress){reason="NR generation cannot be reconfigured before proven completion";return false;}
    auto options=owner.options;options.transferStrength=requested.transferStrength;options.colourStrength=requested.colourStrength;
    options.nrScalePercent=requested.nrScalePercent;options.modelStyle=requested.modelStyle;options.workWidth=options.workHeight=0;
    if(!detail::ValidOptions(options,reason))return false;
    if(!owner.Drain()){owner.poisoned=true;reason="NR work could not be retired before reconfiguration";return false;}
    owner.recreateFeature|=owner.options.modelStyle!=options.modelStyle;
    owner.options=std::move(options);owner.previous={}; // first affected evaluation resets temporal history
    reason.clear();return true;
}
void NrHost::Stop()
{
    if (!impl_)
        return;
    std::unique_lock<std::mutex> lock(impl_->mutex);
    bool closed = false;
    try
    {
        closed = impl_->Close();
    }
    catch (...)
    {
    }
    lock.unlock();
    if (!closed)
    {
        retained_ = true;
        OutputDebugStringA(
            "Standalone NR owner quarantined until worker process exit; GPU/provider retirement unproved.");
        (void)impl_.release();
    }
    else
        impl_.reset();
}
} // namespace nrw
