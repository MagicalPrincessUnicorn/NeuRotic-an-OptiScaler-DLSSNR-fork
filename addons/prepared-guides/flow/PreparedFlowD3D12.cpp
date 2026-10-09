#include "PreparedFlowD3D12.h"
#include "../../../OptiScaler/dlssnr/NativeIdentity.h"
#include "../../../OptiScaler/dlssnr/connections/GpuBudget.h"
#include <cstdio>
#include "PreparedFlowShader.h"
#include "vendor/nvof/nvOpticalFlowD3D12.h"
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>
#include <d3dcompiler.h>
#include <array>
#include <atomic>
#include <cstring>
#include <vector>

namespace Neurotic::PreparedFlow
{
namespace Budget=DlssNr::Connections::GpuBudget;
namespace
{
std::atomic<unsigned> LiveSessions{0};
constexpr unsigned MaxSessions = 4;
std::string Hr(HRESULT value){char text[11]{};std::snprintf(text,sizeof(text),"0x%08X",static_cast<unsigned>(value));return text;}
bool SameDevice(ID3D12Device* device, ID3D12DeviceChild* child, std::string* reason=nullptr, const char* role="Flow child")
{
    ComPtr<ID3D12Device> parent;
    const auto get=child?child->GetDevice(IID_PPV_ARGS(&parent)):E_POINTER;
    if(!device||FAILED(get)||!parent){
        if(reason)*reason=std::string(role)+" GetDevice failed: "+Hr(get)+(parent?"":"; no parent device");
        return false;
    }
    // Normalize ownership identity only. Creation, execution and callbacks
    // continue through the supplied device, resource and fence interfaces.
    const auto comparison=DlssNr::NativeIdentity::CompareDevices(device,parent.Get());
    if(comparison.equal)return true;
    if(reason)*reason=std::string(role)+" device identity mismatch: GetDevice="+Hr(get)+
        "; expected QI="+Hr(comparison.left.result)+" layers="+std::to_string(comparison.left.layers)+
        "; child QI="+Hr(comparison.right.result)+" layers="+std::to_string(comparison.right.layers)+
        "; device status="+Hr(device->GetDeviceRemovedReason());
    return false;
}
bool ValidCompletion(ID3D12Device* device, const Completion& point, std::string* reason=nullptr, const char* role="Flow fence")
{
    if(!point.fence||!point.value||point.value==UINT64_MAX){
        if(reason)*reason=std::string(role)+" missing fence or valid timeline";return false;
    }
    if(!SameDevice(device,point.fence.Get(),reason,role))return false;
    if(point.fence->GetCompletedValue()==UINT64_MAX){
        if(reason)*reason=std::string(role)+" reports device removal";return false;
    }
    return true;
}
void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    if (before == after) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &b);
}
void Uav(ID3D12GraphicsCommandList* list, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource = resource;
    list->ResourceBarrier(1, &b);
}
struct Constants
{
    std::uint32_t width, height, grid, nvidia, reset, warmup, costThreshold, spare = 0;
    float appearance, consistency;
};
}

struct Session::Impl
{
    // Reservations outlive the textures and any quarantined session. FidelityFX
    // internal allocations use a conservative envelope before creation, checked
    // against the SDK's reported allocation total before any dispatch.
    std::vector<std::shared_ptr<Budget::Reservation>> allocations;
    std::shared_ptr<Budget::Reservation> ffxAllocation;
    Options options;
    Backend selected = Backend::Software;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    std::array<ComPtr<ID3D12CommandAllocator>, 2> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>, 2> lists;
    ComPtr<ID3D12Fence> completed, prepared, vendorCompleted;
    HANDLE completionEvent=nullptr;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> convert, luma;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    ComPtr<ID3D12Resource> vectors, reverseVectors, cost, reverseCost, scene, sceneReadback;
    ComPtr<ID3D12Resource> motion, distrust;
    std::array<ComPtr<ID3D12Resource>, 2> gray;
    std::array<ComPtr<ID3D12Fence>, 6> registration;
    std::array<NvOFGPUBufferHandle, 6> registered{};
    HMODULE driver = nullptr;
    NV_OF_D3D12_API_FUNCTION_LIST nv{};
    NvOFHandle nvHandle = nullptr;
    std::vector<std::uint8_t> scratch;
    FfxOpticalflowContext ffx{};
    bool ffxCreated = false, initialized = false, poisoned = false, submitted = false, available = false, returned = true;
    bool outputInReadState = false, counted = false;
    std::uint64_t sequence = 0;
    std::uint32_t historyFrames = 0, grayIndex = 0;
    PairHistory history;
    Input inFlight, previous;
    std::vector<Completion> readers;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT scdFootprint{};
    std::uint64_t readbackBytes = 0;
    ~Impl()
    {
        if(completionEvent)CloseHandle(completionEvent);
        // Session::Close/destructor retain the entire object if any work is
        // unresolved. This destructor is entered only after safe retirement.
        DestroyBackend();
        if (counted) --LiveSessions;
    }
    bool CanRetire() const
    {
        if (poisoned || !returned || (submitted && !Completed(completed->GetCompletedValue(), sequence))) return false;
        for (const auto& r : readers) if (!r.Ready()) return false;
        for (std::size_t i = 0; i < registered.size(); ++i)
            if (registered[i] && (!registration[i] || !Completed(registration[i]->GetCompletedValue(), 1))) return false;
        return true;
    }
    bool DestroyBackend()
    {
        for (std::size_t i = 0; i < registered.size(); ++i)
        {
            if (!registered[i]) continue;
            NV_OF_UNREGISTER_RESOURCE_PARAMS_D3D12 p{}; p.hOFGpuBuffer = registered[i];
            if (nv.nvOFUnregisterResourceD3D12(&p) != NV_OF_SUCCESS) { poisoned = true; return false; }
            registered[i] = nullptr;
        }
        if (nvHandle)
        {
            // Match SDK buffer-before-session teardown after unregister and
            // actual GPU retirement, while the driver remains mapped.
            gray = {}; vectors.Reset(); reverseVectors.Reset(); cost.Reset(); reverseCost.Reset();
            registration = {}; vendorCompleted.Reset();
            if (nv.nvOFDestroy(nvHandle) != NV_OF_SUCCESS) { poisoned = true; return false; }
        }
        nvHandle = nullptr;
        if (ffxCreated && ffxOpticalflowContextDestroy(&ffx) != FFX_OK) { poisoned = true; return false; }
        ffxCreated = false;
        // Release vendor resources while its module still resides in memory.
        gray = {}; vectors.Reset(); reverseVectors.Reset(); cost.Reset(); reverseCost.Reset();
        registration = {}; vendorCompleted.Reset();
        if (driver) FreeLibrary(driver);
        driver = nullptr;
        return true;
    }
    bool Texture(ComPtr<ID3D12Resource>& target, std::uint32_t width, std::uint32_t height,
        DXGI_FORMAT format, D3D12_RESOURCE_STATES state, bool uav = true)
    {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = width; d.Height = height; d.DepthOrArraySize = 1; d.MipLevels = 1;
        d.Format = format; d.SampleDesc.Count = 1;
        d.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
        auto charged=Budget::Reserve(Budget::TextureBytes(device.Get(),d));if(!charged)return false;
        if(FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&target))))return false;
        allocations.push_back(std::move(charged));return true;
    }
    bool Common(std::string& reason)
    {
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&completed))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&prepared))))
        { reason = "Private flow queue/fence creation failed"; return false; }
        for (unsigned i = 0; i < 2; ++i)
            if (FAILED(device->CreateCommandAllocator(q.Type, IID_PPV_ARGS(&allocators[i]))) ||
                FAILED(device->CreateCommandList(0, q.Type, allocators[i].Get(), nullptr, IID_PPV_ARGS(&lists[i]))) ||
                FAILED(lists[i]->Close())) { reason = "Flow recording allocation failed"; return false; }
        if (!Texture(motion, options.width, options.height, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
            !Texture(distrust, options.width, options.height, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        { reason = "Normalized output allocation failed"; return false; }
        D3D12_DESCRIPTOR_HEAP_DESC h{}; h.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        h.NumDescriptors = 9; h.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&descriptors))))
        { reason = "Flow descriptor allocation failed"; return false; }
        std::array<D3D12_DESCRIPTOR_RANGE, 2> ranges{};
        ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 0, 0, 0};
        ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 0, 0, 6};
        std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[0].DescriptorTable = {2, ranges.data()};
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[1].Constants = {0, 0, sizeof(Constants)/4};
        D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = 2; rd.pParameters = parameters.data();
        ComPtr<ID3DBlob> blob, error;
        if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
            FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root))))
        { reason = "Flow conversion root signature failed"; return false; }
        const auto pipeline = [&](const char* entry, ComPtr<ID3D12PipelineState>& destination)
        {
            ComPtr<ID3DBlob> code;
            if (FAILED(D3DCompile(PreparedFlowShader, sizeof(PreparedFlowShader)-1, "PreparedFlowConvert.hlsl", nullptr,
                nullptr, entry, "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &error))) return false;
            D3D12_COMPUTE_PIPELINE_STATE_DESC p{}; p.pRootSignature = root.Get();
            p.CS = {code->GetBufferPointer(), code->GetBufferSize()};
            return SUCCEEDED(device->CreateComputePipelineState(&p, IID_PPV_ARGS(&destination)));
        };
        if (!pipeline("Convert", convert) || !pipeline("PrepareLuma", luma))
        { reason = "Flow shader compilation/pipeline failed"; return false; }
        return true;
    }
    bool Software(std::string& reason)
    {
        if(!Budget::AdmitRaster(options.width,options.height,true)){reason="Flow raster exceeds allocation contract";return false;}
        const auto envelope=std::uint64_t(options.width)*options.height*8+16ull*1024*1024;
        ffxAllocation=Budget::Reserve(envelope);
        if(!ffxAllocation){reason="Flow aggregate GPU allocation budget exhausted";return false;}
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_2};
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 wave{};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
            model.HighestShaderModel < D3D_SHADER_MODEL_6_2 ||
            FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &wave, sizeof(wave))) || !wave.WaveOps)
        { reason = "FidelityFX flow requires positively queried shader model 6.2 and wave operations"; return false; }
        scratch.resize(ffxGetScratchMemorySizeDX12(FFX_OPTICALFLOW_CONTEXT_COUNT));
        FfxOpticalflowContextDescription description{};
        if (ffxGetInterfaceDX12(&description.backendInterface, ffxGetDeviceDX12(device.Get()), scratch.data(),
            scratch.size(), FFX_OPTICALFLOW_CONTEXT_COUNT) != FFX_OK)
        { reason = "FidelityFX DX12 backend initialization failed"; return false; }
        description.resolution = {options.width, options.height};
        if (ffxOpticalflowContextCreate(&ffx, &description) != FFX_OK)
        { reason = "FidelityFX optical-flow context creation failed"; return false; }
        ffxCreated = true;
        FfxEffectMemoryUsage usage{};
        if(ffxOpticalflowContextGetGpuMemoryUsage(&ffx,&usage)!=FFX_OK||usage.totalUsageInBytes>envelope){reason="FidelityFX allocation exceeds reserved envelope";return false;}
        FfxOpticalflowSharedResourceDescriptions shared{};
        if (ffxOpticalflowGetSharedResourceDescriptions(&ffx, &shared) != FFX_OK ||
            shared.opticalFlowVector.resourceDescription.format != FFX_SURFACE_FORMAT_R16G16_SINT ||
            shared.opticalFlowSCD.resourceDescription.format != FFX_SURFACE_FORMAT_R32_UINT)
        { reason = "FidelityFX shared format negotiation failed"; return false; }
        const auto& v = shared.opticalFlowVector.resourceDescription;
        const auto& s = shared.opticalFlowSCD.resourceDescription;
        if (!Texture(vectors, v.width, v.height, DXGI_FORMAT_R16G16_SINT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) ||
            !Texture(scene, s.width, s.height, DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        { reason = "FidelityFX shared output allocation failed"; return false; }
        const auto desc = scene->GetDesc();
        device->GetCopyableFootprints(&desc, 0, 1, 0, &scdFootprint, nullptr, nullptr, &readbackBytes);
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = readbackBytes; buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto readbackCharge=Budget::Reserve(Budget::TextureBytes(device.Get(),buffer));
        if(!readbackCharge){reason="Scene-cut readback budget exhausted";return false;}
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&sceneReadback))))
        { reason = "Scene-cut readback allocation failed"; return false; }
        allocations.push_back(std::move(readbackCharge));
        selected = Backend::Software; reason = "FidelityFX 1.1.2 software flow initialized; GPU qualification pending"; return true;
    }
    bool Caps(NV_OF_CAPS cap, std::vector<std::uint32_t>& values)
    {
        std::uint32_t count = 0;
        if (nv.nvOFGetCaps(nvHandle, cap, nullptr, &count) != NV_OF_SUCCESS || !count || count > 256) return false;
        values.assign(count, 0);
        if (nv.nvOFGetCaps(nvHandle, cap, values.data(), &count) != NV_OF_SUCCESS || !count || count > values.size()) return false;
        values.resize(count); return true;
    }
    bool Format(NV_OF_BUFFER_USAGE usage, DXGI_FORMAT format)
    {
        std::uint32_t count = 0;
        if (nv.nvOFGetSurfaceFormatCountD3D12(nvHandle, usage, NV_OF_MODE_OPTICALFLOW, &count) != NV_OF_SUCCESS ||
            !count || count > 256) return false;
        std::vector<DXGI_FORMAT> formats(count, DXGI_FORMAT_UNKNOWN);
        return nv.nvOFGetSurfaceFormatD3D12(nvHandle, usage, NV_OF_MODE_OPTICALFLOW, formats.data()) == NV_OF_SUCCESS &&
            std::find(formats.begin(), formats.end(), format) != formats.end();
    }
    bool Nvidia(std::string& reason)
    {
        static_assert(NV_OF_API_MAJOR_VERSION == 5 && NV_OF_API_MINOR_VERSION == 0);
        if (options.encoding != Encoding::Srgb) { reason = "NVIDIA grayscale profile requires SDR sRGB input"; return false; }
        driver = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!driver) { reason = "Installed NVIDIA optical-flow runtime unavailable"; return false; }
        const auto maximum = reinterpret_cast<decltype(&NvOFGetMaxSupportedApiVersion)>(GetProcAddress(driver, "NvOFGetMaxSupportedApiVersion"));
        const auto create = reinterpret_cast<decltype(&NvOFAPICreateInstanceD3D12)>(GetProcAddress(driver, "NvOFAPICreateInstanceD3D12"));
        std::uint32_t version = 0;
        if (!maximum || !create || maximum(&version) != NV_OF_SUCCESS || version < NV_OF_API_VERSION ||
            create(NV_OF_API_VERSION, &nv) != NV_OF_SUCCESS || !nv.nvCreateOpticalFlowD3D12 || !nv.nvOFGetCaps ||
            !nv.nvOFGetSurfaceFormatCountD3D12 || !nv.nvOFGetSurfaceFormatD3D12 || !nv.nvOFInit || !nv.nvOFDestroy ||
            !nv.nvOFRegisterResourceD3D12 || !nv.nvOFUnregisterResourceD3D12 || !nv.nvOFExecuteD3D12 ||
            nv.nvCreateOpticalFlowD3D12(device.Get(), &nvHandle) != NV_OF_SUCCESS || !nvHandle)
        { reason = "Installed NVIDIA API5/device negotiation failed"; return false; }
        std::vector<std::uint32_t> values;
        const auto bound = [&](NV_OF_CAPS cap, std::uint32_t value, bool minimum)
        { return Caps(cap, values) && values.size() == 1 && (minimum ? value >= values[0] : value <= values[0]); };
        if (!Caps(NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES, values) ||
            std::find(values.begin(), values.end(), 4u) == values.end() ||
            !bound(NV_OF_CAPS_WIDTH_MIN, options.width, true) || !bound(NV_OF_CAPS_WIDTH_MAX, options.width, false) ||
            !bound(NV_OF_CAPS_HEIGHT_MIN, options.height, true) || !bound(NV_OF_CAPS_HEIGHT_MAX, options.height, false) ||
            !Format(NV_OF_BUFFER_USAGE_INPUT, DXGI_FORMAT_R8_UNORM) ||
            !Format(NV_OF_BUFFER_USAGE_OUTPUT, DXGI_FORMAT_R16G16_SINT) || !Format(NV_OF_BUFFER_USAGE_COST, DXGI_FORMAT_R8_UINT))
        { reason = "NVIDIA dimensions/grid4/grayscale/vector/cost capability absent"; return false; }
        NV_OF_INIT_PARAMS init{}; init.width = options.width; init.height = options.height;
        init.mode = NV_OF_MODE_OPTICALFLOW; init.outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;
        init.perfLevel = NV_OF_PERF_LEVEL_MEDIUM; init.enableOutputCost = NV_OF_TRUE;
        init.predDirection = NV_OF_PRED_DIRECTION_BOTH; init.inputBufferFormat = NV_OF_BUFFER_FORMAT_GRAYSCALE8;
        if (nv.nvOFInit(nvHandle, &init) != NV_OF_SUCCESS)
        { reason = "NVIDIA bidirectional initialization failed"; return false; }
        const auto w = GridExtent(options.width, 4), h = GridExtent(options.height, 4);
        if (!Texture(gray[0], options.width, options.height, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_STATE_COMMON) ||
            !Texture(gray[1], options.width, options.height, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_STATE_COMMON) ||
            !Texture(vectors, w, h, DXGI_FORMAT_R16G16_SINT, D3D12_RESOURCE_STATE_COMMON, false) ||
            !Texture(reverseVectors, w, h, DXGI_FORMAT_R16G16_SINT, D3D12_RESOURCE_STATE_COMMON, false) ||
            !Texture(cost, w, h, DXGI_FORMAT_R8_UINT, D3D12_RESOURCE_STATE_COMMON, false) ||
            !Texture(reverseCost, w, h, DXGI_FORMAT_R8_UINT, D3D12_RESOURCE_STATE_COMMON, false) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&vendorCompleted))))
        { reason = "NVIDIA private surfaces/fence allocation failed"; return false; }
        const std::array<ID3D12Resource*, 6> resources{gray[0].Get(), gray[1].Get(), vectors.Get(), reverseVectors.Get(), cost.Get(), reverseCost.Get()};
        for (std::size_t i = 0; i < resources.size(); ++i)
        {
            if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&registration[i]))))
            { reason = "NVIDIA registration fence creation failed"; return false; }
            NV_OF_REGISTER_RESOURCE_PARAMS_D3D12 p{}; p.resource = resources[i]; p.hOFGpuBuffer = &registered[i];
            p.inputFencePoint = {registration[i].Get(), 0}; p.outputFencePoint = {registration[i].Get(), 1};
            if (nv.nvOFRegisterResourceD3D12(nvHandle, &p) != NV_OF_SUCCESS || !registered[i])
            { poisoned = true; reason = "NVIDIA registration uncertain; session quarantined"; return false; }
        }
        selected = Backend::Nvidia; reason = "Installed NVIDIA API5 bidirectional flow initialized; GPU qualification pending"; return true;
    }
    void Views(const Input& input)
    {
        auto handle = descriptors->GetCPUDescriptorHandleForHeapStart();
        const auto increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const auto srv = [&](ID3D12Resource* resource, DXGI_FORMAT format)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC s{}; s.Format = format; s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; s.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(resource, &s, handle); handle.ptr += increment;
        };
        srv(vectors.Get(), DXGI_FORMAT_R16G16_SINT); srv(scene.Get(), DXGI_FORMAT_R32_UINT);
        srv(cost.Get(), DXGI_FORMAT_R8_UINT); srv(reverseVectors.Get(), DXGI_FORMAT_R16G16_SINT);
        srv(input.color.Get(), input.color->GetDesc().Format);
        srv(previous.color ? previous.color.Get() : input.color.Get(), input.color->GetDesc().Format);
        const auto uav = [&](ID3D12Resource* resource, DXGI_FORMAT format)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format = format; u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device->CreateUnorderedAccessView(resource, nullptr, &u, handle); handle.ptr += increment;
        };
        uav(motion.Get(), DXGI_FORMAT_R16G16_FLOAT); uav(distrust.Get(), DXGI_FORMAT_R8_UNORM);
        uav(gray[grayIndex].Get(), DXGI_FORMAT_R8_UNORM);
    }
    void Bind(ID3D12GraphicsCommandList* list, const Input& input, bool prepareLuma)
    {
        ID3D12DescriptorHeap* heaps[]{descriptors.Get()}; list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(root.Get()); list->SetComputeRootDescriptorTable(0, descriptors->GetGPUDescriptorHandleForHeapStart());
        Constants c{options.width, options.height, Grid(selected), selected == Backend::Nvidia,
            input.reset, selected == Backend::Software && historyFrames <= 5, options.nvidiaCostThreshold, 0,
            options.appearanceThreshold, options.consistencyThreshold};
        list->SetComputeRoot32BitConstants(1, sizeof(c)/4, &c, 0);
        list->SetPipelineState(prepareLuma ? luma.Get() : convert.Get());
        list->Dispatch(GridExtent(options.width, 8), GridExtent(options.height, 8), 1);
    }
    bool Begin(unsigned index)
    { return SUCCEEDED(allocators[index]->Reset()) && SUCCEEDED(lists[index]->Reset(allocators[index].Get(), nullptr)); }
    bool Execute(unsigned index, ID3D12Fence* signal)
    {
        if (FAILED(lists[index]->Close())) return false;
        ID3D12CommandList* commands[]{lists[index].Get()};
        // Mark possible use before the void submission API; failure afterwards
        // cannot justify releasing resources or resetting an allocator.
        submitted = true; queue->ExecuteCommandLists(1, commands);
        return SUCCEEDED(queue->Signal(signal, sequence));
    }
    bool ConvertAndSubmit(const Input& input, unsigned index)
    {
        auto list = lists[index].Get();
        const auto nativeState = selected == Backend::Nvidia ? D3D12_RESOURCE_STATE_COMMON : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        Barrier(list, vectors.Get(), nativeState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (scene) Barrier(list, scene.Get(), nativeState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (cost) Barrier(list, cost.Get(), nativeState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (reverseVectors) Barrier(list, reverseVectors.Get(), nativeState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (outputInReadState)
        {
            Barrier(list, motion.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier(list, distrust.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        Barrier(list, input.color.Get(), input.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (previous.color && previous.color.Get() != input.color.Get())
            Barrier(list, previous.color.Get(), previous.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Bind(list, input, false);
        Uav(list, motion.Get()); Uav(list, distrust.Get());
        Barrier(list, motion.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(list, distrust.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(list, input.color.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, input.state);
        if (previous.color && previous.color.Get() != input.color.Get())
            Barrier(list, previous.color.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, previous.state);
        Barrier(list, vectors.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nativeState);
        if (cost) Barrier(list, cost.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nativeState);
        if (reverseVectors) Barrier(list, reverseVectors.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nativeState);
        if (scene)
        {
            Barrier(list, scene.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = sceneReadback.Get();
            to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = scdFootprint;
            D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = scene.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            Barrier(list, scene.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, nativeState);
        }
        outputInReadState = true;
        return Execute(index, completed.Get());
    }
};

Session::~Session()
{
    if (!impl_) return;
    std::string ignored;
    if (!Close(ignored))
    {
        // At most MaxSessions such owners exist process-wide. Preserve context,
        // module, queue, recordings, aliases and upstream rights after uncertain
        // work. Timeout and destruction are not GPU/vendor-release evidence.
        HMODULE pin = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&LiveSessions), &pin);
        (void)new std::shared_ptr<Impl>(std::move(impl_));
    }
}
bool Session::Initialize(ID3D12Device* device, const Options& options, std::string& reason)
{
    if (impl_ || !device || !Dimensions(options.width, options.height) ||
        !FiniteLuminance(options.minLuminance, options.maxLuminance) ||
        static_cast<unsigned>(options.requested) > static_cast<unsigned>(Backend::Auto) ||
        static_cast<unsigned>(options.encoding) > static_cast<unsigned>(Encoding::ScRgb) ||
        !std::isfinite(options.appearanceThreshold) || options.appearanceThreshold < 0 ||
        !std::isfinite(options.consistencyThreshold) || options.consistencyThreshold < 0 || options.nvidiaCostThreshold > 255)
    { reason = "Invalid/repeated flow initialization"; return false; }
    auto count = LiveSessions.load();
    do { if (count >= MaxSessions) { reason = "Bounded flow session capacity exhausted"; return false; } }
    while (!LiveSessions.compare_exchange_weak(count, count + 1));
    impl_ = std::make_shared<Impl>(); impl_->counted = true; impl_->device = device; impl_->options = options;
    if (!impl_->Common(reason)) return false;
    if (options.requested == Backend::Software) return impl_->initialized = impl_->Software(reason);
    if (impl_->Nvidia(reason)) return impl_->initialized = true;
    if (options.requested == Backend::Nvidia || !impl_->CanRetire()) return false;
    const auto fallbackReason = reason;
    if (!impl_->DestroyBackend() || !impl_->Software(reason)) return false;
    reason = "Selected FidelityFX software flow; NVIDIA unavailable: " + fallbackReason;
    return impl_->initialized = true;
}
bool Session::Submit(const Input& input, std::string& reason)
{ return SubmitChecked(input,reason)==SubmitDisposition::Submitted; }
SubmitDisposition Session::SubmitChecked(const Input& input, std::string& reason)
{
    auto p = impl_.get();
    if(p&&p->poisoned){reason="Flow session quarantined after uncertain GPU work";return SubmitDisposition::Unsafe;}
    if (!p || !p->initialized || !p->CanRetire() || impl_.use_count() != 1)
    { reason = "Flow session/history/output readers are unavailable or still in use"; return SubmitDisposition::Rejected; }
    const auto removed=p->device->GetDeviceRemovedReason();
    if(removed!=S_OK){p->poisoned=true;reason="Flow device removed: "+Hr(removed);return SubmitDisposition::Unsafe;}
    if(!input.color||!input.ownership){reason="Flow color or owning lease missing";return SubmitDisposition::Rejected;}
    if(!ValidCompletion(p->device.Get(),input.producer,&reason,"Flow producer fence"))return SubmitDisposition::Rejected;
    if(!SameDevice(p->device.Get(),input.color.Get(),&reason,"Flow color"))return SubmitDisposition::Rejected;
    if(!p->history.Accepts(input.pair,input.reset)){
        reason="Flow capture pair rejected: current="+std::to_string(input.pair.capture)+
            " previous="+std::to_string(input.pair.previousCapture)+" stream="+std::to_string(input.pair.stream)+
            " generation="+std::to_string(input.pair.generation)+" reset="+(input.reset?"yes":"no");
        return SubmitDisposition::Rejected;
    }
    const auto d = input.color->GetDesc();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.Width != p->options.width || d.Height != p->options.height ||
        d.DepthOrArraySize != 1 || d.MipLevels != 1 || d.SampleDesc.Count != 1 ||
        (d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
        (d.Format != DXGI_FORMAT_R8G8B8A8_UNORM && d.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
            d.Format != DXGI_FORMAT_R16G16B16A16_FLOAT && d.Format != DXGI_FORMAT_R10G10B10A2_UNORM) ||
        (p->previous.color && !input.reset && p->previous.color.Get() == input.color.Get()))
    { reason = "Flow requires separate immutable whole-color captures with a supported typed color format"; return SubmitDisposition::Rejected; }
    if (p->previous.color && !input.reset && p->previous.color->GetDesc().Format != d.Format)
    { reason = "Color format changed without history reset"; return SubmitDisposition::Rejected; }
    if (p->sequence == UINT64_MAX - 1) { reason = "Flow fence sequence exhausted"; return SubmitDisposition::Rejected; }
    if (input.reset) { p->historyFrames = 0; p->previous = {}; }
    else if (p->historyFrames != UINT32_MAX) ++p->historyFrames;
    p->readers.clear(); p->inFlight = input; p->available = false; p->returned = false; ++p->sequence;
    if (FAILED(p->queue->Wait(input.producer.fence.Get(), input.producer.value)) || !p->Begin(0))
    { p->poisoned = true; reason = "Producer queue wait/recording reset failed; session retained"; return SubmitDisposition::Unsafe; }
    p->Views(input);
    if (p->selected == Backend::Software)
    {
        auto list = p->lists[0].Get();
        Barrier(list, input.color.Get(), input.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        FfxOpticalflowDispatchDescription dispatch{};
        dispatch.commandList = ffxGetCommandListDX12(list);
        dispatch.color = ffxGetResourceDX12(input.color.Get(), ffxGetResourceDescriptionDX12(input.color.Get()), L"PreparedFlow_Color");
        dispatch.opticalFlowVector = ffxGetResourceDX12(p->vectors.Get(), ffxGetResourceDescriptionDX12(p->vectors.Get()),
            L"PreparedFlow_Grid8", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.opticalFlowSCD = ffxGetResourceDX12(p->scene.Get(), ffxGetResourceDescriptionDX12(p->scene.Get()),
            L"PreparedFlow_SCD", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.reset = input.reset; dispatch.backbufferTransferFunction = static_cast<int>(p->options.encoding);
        dispatch.minMaxLuminance = {p->options.minLuminance, p->options.maxLuminance};
        if (ffxOpticalflowContextDispatch(&p->ffx, &dispatch) != FFX_OK)
        { p->poisoned = true; reason = "FidelityFX dispatch failed; recorded context retained"; return SubmitDisposition::Unsafe; }
        Barrier(list, input.color.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, input.state);
        if (!p->ConvertAndSubmit(input, 0))
        { p->poisoned = true; reason = "Software flow conversion/submission failed; possible GPU use retained"; return SubmitDisposition::Unsafe; }
    }
    else
    {
        for (const auto& fence : p->registration)
            if (FAILED(p->queue->Wait(fence.Get(), 1)))
            { p->poisoned = true; reason = "NVIDIA registration queue wait failed"; return SubmitDisposition::Unsafe; }
        auto list = p->lists[0].Get();
        Barrier(list, input.color.Get(), input.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier(list, p->gray[p->grayIndex].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        p->Bind(list, input, true); Uav(list, p->gray[p->grayIndex].Get());
        Barrier(list, p->gray[p->grayIndex].Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        if (input.reset)
        {
            // First pair is current/current only to initialize the vendor's
            // private resources; reset distrust covers every output pixel.
            Barrier(list, p->gray[p->grayIndex].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
            Barrier(list, p->gray[1-p->grayIndex].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            list->CopyResource(p->gray[1-p->grayIndex].Get(), p->gray[p->grayIndex].Get());
            Barrier(list, p->gray[p->grayIndex].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
            Barrier(list, p->gray[1-p->grayIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        }
        Barrier(list, input.color.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, input.state);
        if (!p->Execute(0, p->prepared.Get()))
        { p->poisoned = true; reason = "NVIDIA luma submission uncertain"; return SubmitDisposition::Unsafe; }
        std::array<NV_OF_FENCE_POINT, 7> waits{};
        for (std::size_t i = 0; i < 6; ++i) waits[i] = {p->registration[i].Get(), 1};
        waits[6] = {p->prepared.Get(), p->sequence};
        NV_OF_EXECUTE_INPUT_PARAMS_D3D12 in{}; in.inputFrame = p->registered[p->grayIndex];
        in.referenceFrame = p->registered[1-p->grayIndex]; in.disableTemporalHints = NV_OF_TRUE;
        in.numFencePoints = static_cast<std::uint32_t>(waits.size()); in.fencePoint = waits.data();
        NV_OF_FENCE_POINT complete{p->vendorCompleted.Get(), p->sequence};
        NV_OF_EXECUTE_OUTPUT_PARAMS_D3D12 out{}; out.outputBuffer = p->registered[2]; out.bwdOutputBuffer = p->registered[3];
        out.outputCostBuffer = p->registered[4]; out.bwdOutputCostBuffer = p->registered[5]; out.fencePoint = &complete;
        if (p->nv.nvOFExecuteD3D12(p->nvHandle, &in, &out) != NV_OF_SUCCESS ||
            FAILED(p->queue->Wait(p->vendorCompleted.Get(), p->sequence)) || !p->Begin(1) || !p->ConvertAndSubmit(input, 1))
        { p->poisoned = true; reason = "NVIDIA flow/conversion submission uncertain; vendor resources retained"; return SubmitDisposition::Unsafe; }
        p->grayIndex = 1-p->grayIndex;
    }
    p->history.Commit(input.pair); reason = "Flow submitted; actual completion pending"; return SubmitDisposition::Submitted;
}
bool Session::Poll(Output& output, std::string& reason)
{
    auto p = impl_.get();
    if (!p || p->poisoned || !p->submitted || p->returned || !Completed(p->completed->GetCompletedValue(), p->sequence))
    { reason = "Flow unavailable or completion pending"; return false; }
    bool cut = p->inFlight.reset;
    if (p->sceneReadback)
    {
        D3D12_RANGE range{0, static_cast<SIZE_T>(p->readbackBytes)}; void* mapped = nullptr;
        if (FAILED(p->sceneReadback->Map(0, &range, &mapped))) { reason = "Scene-cut readback failed"; return false; }
        std::uint32_t bits = 0; std::memcpy(&bits, static_cast<std::uint8_t*>(mapped) + p->scdFootprint.Offset + 4, 4);
        D3D12_RANGE written{0,0}; p->sceneReadback->Unmap(0, &written); cut = cut || (bits & 15) != 0;
    }
    output = {}; output.motion = p->motion; output.distrust = p->distrust;
    output.producer = {p->completed, p->sequence}; output.pair = p->inFlight.pair;
    output.selected = p->selected; output.sourceGrid = Grid(p->selected); output.explicitReset = p->inFlight.reset;
    output.sceneCut = cut; output.warmup = p->selected == Backend::Software && p->historyFrames <= 5;
    output.ownership = impl_; p->available = true;
    reason = "Owned current-to-previous pixel flow completed; derived distrust is separate"; return true;
}
bool Session::Wait(Output& output,std::string& reason,unsigned timeoutMs)
{
    auto* p=impl_.get();
    if(!p||p->poisoned||!p->submitted||p->returned){reason="Flow wait has no live submitted output";return false;}
    if(!Completed(p->completed->GetCompletedValue(),p->sequence)){
        if(!p->completionEvent)p->completionEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!p->completionEvent||p->completed->GetCompletedValue()==UINT64_MAX||
           !ResetEvent(p->completionEvent)||FAILED(p->completed->SetEventOnCompletion(p->sequence,p->completionEvent))||
           WaitForSingleObject(p->completionEvent,timeoutMs)!=WAIT_OBJECT_0||
           !Completed(p->completed->GetCompletedValue(),p->sequence)){
            p->poisoned=true;reason="Flow completion unknown after bounded event wait; owner retained";return false;
        }
    }
    return Poll(output,reason);
}
bool Session::AddReader(const Completion& reader, std::string& reason)
{
    auto p = impl_.get();
    if (!p || !p->available || p->returned || p->readers.size() >= 8)
    { reason = "Output unavailable or reader capacity reached"; return false; }
    if(!ValidCompletion(p->device.Get(),reader,&reason,"Flow reader fence"))return false;
    p->readers.push_back(reader); reason = "Reader completion retained"; return true;
}
bool Session::ReturnOutput(std::string& reason)
{
    auto p = impl_.get();
    if (!p || !p->available || p->returned || impl_.use_count() != 1)
    { reason = "Destroy all output copies after registering consumer completion before returning output"; return false; }
    p->returned = true; p->available = false; p->previous = std::move(p->inFlight);
    reason = "Output returned; actual consumer completion still governs reuse"; return true;
}
Backend Session::Selected() const { return impl_ ? impl_->selected : Backend::Software; }
bool Session::Close(std::string& reason)
{
    if (!impl_) return true;
    if (impl_.use_count() != 1 || !impl_->CanRetire())
    { reason = "Flow recordings/vendor registrations/output readers remain unresolved; no release"; return false; }
    if (!impl_->DestroyBackend()) { reason = "Backend release failed; context/runtime retained"; return false; }
    impl_.reset(); reason = "Flow session retired after actual completion"; return true;
}
}
