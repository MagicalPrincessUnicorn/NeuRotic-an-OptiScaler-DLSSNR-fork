// Production allocation/epilogue helpers on WARP; no game, model, or installation.
#include "../OptiScaler/dlssnr/NrDispatchResources.h"
#include "../OptiScaler/shaders/dlssnr/DlssNr_Common.h"
#include <cmath>
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

using Microsoft::WRL::ComPtr;
using DlssNr::Detail::ScratchTransaction;
using DlssNr::Detail::DispatchResourceStates;

static void Require(bool ok)
{
    if (!ok) { std::fputs("NR dispatch-resource assertion failed\n", stderr); std::abort(); }
}
static void Check(HRESULT hr) { Require(SUCCEEDED(hr)); }

static ID3D12Resource* Allocate(ID3D12Device* device, DXGI_FORMAT format, UINT width, UINT height,
                              D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
{
    D3D12_HEAP_PROPERTIES heap {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* resource = nullptr;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                          IID_PPV_ARGS(&resource)));
    return resource;
}

static void AllocationFaults(ID3D12Device* device)
{
    using Resources = std::array<ID3D12Resource*, ScratchTransaction::Count>;
    constexpr auto format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    // Empty allocation, resize, output-only, and companions-only partial bundles.
    for (int initial = 0; initial != 4; ++initial)
    {
        const int missing = initial == 2 ? 4 : initial == 3 ? 1 : 5;
        for (int failAt = 1; failAt <= missing; ++failAt)
        {
            Resources live {};
            if (initial == 1)
                for (auto& resource : live) resource = Allocate(device, format, 8, 8);
            if (initial == 2) live[0] = Allocate(device, format, 16, 16);
            if (initial == 3)
                for (size_t i = 1; i != live.size(); ++i) live[i] = Allocate(device, format, 16, 16);
            const auto original = live;
            std::array<ScratchTransaction::Request, 5> requests;
            for (size_t i = 0; i != live.size(); ++i)
                requests[i] = { &live[i], format, 16, 16, true };
            int allocations = 0;
            std::vector<ID3D12Resource*> retired;
            {
                ScratchTransaction transaction(requests);
                const bool ready = transaction.Prepare([&](DXGI_FORMAT f, UINT w, UINT h)
                {
                    return ++allocations == failAt ? nullptr : Allocate(device, f, w, h);
                });
                Require(!ready && allocations == failAt);
                Require(live == original && retired.empty());
            }
            Require(live == original);
            // The same request recovers after the injected failure is removed.
            {
                ScratchTransaction retry(requests);
                Require(retry.Prepare([&](DXGI_FORMAT f, UINT w, UINT h) { return Allocate(device, f, w, h); }));
                retry.Commit([&](ID3D12Resource*& resource)
                {
                    if (resource) retired.push_back(resource);
                    resource = nullptr; // retain old ownership, as ParkNrResource does
                });
            }
            Require(retired.size() == (initial == 1 ? 5u : 0u));
            for (auto resource : retired) Require(resource->GetDesc().Width == 8);
            for (auto resource : live) Require(resource && resource->GetDesc().Width == 16);
            // A matching bundle performs no allocation and no retirement.
            {
                ScratchTransaction noop(requests);
                Require(noop.Prepare([](DXGI_FORMAT, UINT, UINT) -> ID3D12Resource* { Require(false); return nullptr; }));
                noop.Commit([](ID3D12Resource*&) { Require(false); });
            }
            for (auto resource : retired) resource->Release();
            for (auto resource : live) resource->Release();
        }
    }
    // Returning to native scale retires both optional surfaces without rebuilding the core trio.
    Resources live {};
    std::array<ScratchTransaction::Request, 5> requests;
    for (size_t i = 0; i != live.size(); ++i)
    {
        live[i] = Allocate(device, format, 8, 8);
        requests[i] = { &live[i], format, 8, 8, i < 3 };
    }
    int retired = 0;
    ScratchTransaction native(requests);
    Require(native.Prepare([](DXGI_FORMAT, UINT, UINT) -> ID3D12Resource* { Require(false); return nullptr; }));
    native.Commit([&](ID3D12Resource*& resource) { ++retired; resource->Release(); resource = nullptr; });
    Require(retired == 2 && !live[3] && !live[4]);
    for (auto resource : live) if (resource) resource->Release();
}

struct ObservedState
{
    ID3D12Resource* resource;
    D3D12_RESOURCE_STATES state;
};
static std::array<ObservedState, 8> observed;
static size_t barrierCount;
static void Emit(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource,
                 D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    bool found = false;
    for (auto& entry : observed)
    {
        if (entry.resource != resource) continue;
        Require(entry.state == from && from != to);
        entry.state = to;
        found = true;
        break;
    }
    Require(found);
    ++barrierCount;
    D3D12_RESOURCE_BARRIER barrier {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    cmd->ResourceBarrier(1, &barrier);
}

static void ReturnAtStage(ID3D12GraphicsCommandList* cmd, size_t stage,
                          const std::array<ObservedState, 8>& arrival, bool explicitRestore)
{
    DispatchResourceStates states(cmd, Emit);
    for (size_t i = 0; i != arrival.size(); ++i)
    {
        if (i == stage) return;
        states.Transition(arrival[i].resource, arrival[i].state,
                          D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (explicitRestore) states.Restore(); // destructor must not restore twice
}

static void EpilogueFaults(ID3D12Device* device)
{
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    D3D12_COMMAND_QUEUE_DESC queueDesc {};
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Require(event != nullptr);
    UINT64 submission = 0;
    for (const auto outputArrival : { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE })
    {
        std::array<ComPtr<ID3D12Resource>, 8> owned;
        for (size_t i = 0; i != owned.size(); ++i)
        {
            const auto initial = i == 0 ? outputArrival : (i == 4 || i == 5) ? D3D12_RESOURCE_STATE_COPY_DEST
                                                                 : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            owned[i].Attach(Allocate(device, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, 8, initial));
            observed[i] = { owned[i].Get(), initial };
        }
        const auto arrival = observed;
        for (size_t stage = 0; stage <= owned.size(); ++stage)
        {
            barrierCount = 0;
            ReturnAtStage(list.Get(), stage, arrival, false);
            Require(barrierCount == 2 * stage);
            for (size_t i = 0; i != observed.size(); ++i) Require(observed[i].state == arrival[i].state);
        }
        barrierCount = 0;
        ReturnAtStage(list.Get(), owned.size(), arrival, true);
        Require(barrierCount == 2 * owned.size());
        Check(list->Close());
        ID3D12CommandList* lists[] = { list.Get() };
        queue->ExecuteCommandLists(1, lists);
        Check(queue->Signal(fence.Get(), ++submission));
        Check(fence->SetEventOnCompletion(submission, event));
        Require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0);
        Check(device->GetDeviceRemovedReason());
        Check(allocator->Reset());
        Check(list->Reset(allocator.Get(), nullptr));
    }
    CloseHandle(event);
}

static void OptionalLayerFaults(ID3D12Device* device)
{
    constexpr auto format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    // Fail every optional allocation, with either matching or resized first-layer resources.
    for (bool resizeFirst : { false, true })
    for (int failAt = 1; failAt <= 5; ++failAt)
    {
        std::array<ID3D12Resource*, 5> first {}, second {};
        std::array<ScratchTransaction::Request, 5> firstRequests, secondRequests;
        for (size_t i = 0; i < 5; ++i)
        {
            first[i] = Allocate(device, format, resizeFirst ? 8 : 16, resizeFirst ? 8 : 16);
            second[i] = Allocate(device, format, 8, 8);
            firstRequests[i] = { &first[i], format, 16, 16, true };
            secondRequests[i] = { &second[i], format, 32, 32, true };
        }
        const auto oldFirst = first, oldSecond = second;
        ScratchTransaction firstTx(firstRequests);
        auto secondTx = std::make_unique<ScratchTransaction>(secondRequests);
        int secondAllocations = 0;
        const auto result = DlssNr::Detail::PrepareLayerScratch(firstTx, secondTx,
            [&](DXGI_FORMAT f, UINT w, UINT h) -> ID3D12Resource*
            {
                if (w == 32 && ++secondAllocations == failAt) return nullptr;
                return Allocate(device, f, w, h);
            });
        Require(result == DlssNr::Detail::LayerScratchResult::SecondUnavailable);
        Require(!secondTx && first == oldFirst && second == oldSecond);
        firstTx.Commit([](ID3D12Resource*& resource) { resource->Release(); resource = nullptr; });
        for (size_t i = 0; i < 5; ++i)
        {
            Require(first[i]->GetDesc().Width == 16 && second[i] == oldSecond[i]);
            if (!resizeFirst) Require(first[i] == oldFirst[i]);
            first[i]->Release(); second[i]->Release();
        }
    }
    const auto motion = DlssNrWorkingMotionScale(1920, 1080, 960, 536);
    Require(motion.x == 0.5f && std::abs(motion.y - 536.0f / 1080.0f) < 1e-7f);
    // Convert one normalized vertical screen traversal to exactly the working raster's height.
    Require(std::abs(1080.0f * motion.y - 536.0f) < 1e-4f);
    const auto native = DlssNrWorkingMotionScale(1920, 1080, 1920, 1080);
    Require(native.x == 1.0f && native.y == 1.0f);
    const auto safe = DlssNrWorkingMotionScale(0, 0, 8, 8);
    Require(safe.x == 1.0f && safe.y == 1.0f);
}

int main()
{
    ComPtr<ID3D12Debug> debug;
    Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;
    Check(device.As(&messages));
    AllocationFaults(device.Get());
    OptionalLayerFaults(device.Get());
    EpilogueFaults(device.Get());
    for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i)
    {
        SIZE_T size = 0;
        Check(messages->GetMessage(i, nullptr, &size));
        std::vector<unsigned char> storage(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Check(messages->GetMessage(i, message, &size));
        if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR || message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
        {
            std::fprintf(stderr, "%s\n", message->pDescription);
            Require(false);
        }
    }
    std::puts("NR allocation rollback/retry/no-op/optional retirement and DX12 error epilogues passed.");
}
