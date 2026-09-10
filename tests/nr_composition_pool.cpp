// Exercise the production pool, real descriptor/upload allocations and real replay hooks on WARP.
#define NOMINMAX
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include <cstdio>
#include <cassert>
#define LOG_ERROR(...) ((void)0)
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while (0)
namespace Util { static void GetDeviceRemovedReason(ID3D12Device*) {} }
#include "../OptiScaler/shaders/dlssnr/NrCompositionPool.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>

using Microsoft::WRL::ComPtr;
namespace Safety = DlssNr::GpuSafety;
using Pool = DlssNr::CompositionPool;
using Admission = Pool::Admission;
static void Check(HRESULT hr) { assert(SUCCEEDED(hr)); }

// A private-data witness proves every successfully allocated COM object in a failed
// transaction was released, including a buffer whose descriptor-heap allocation failed.
struct Witness final : IUnknown
{
    ULONG refs = 1;
    static inline unsigned int alive = 0;
    Witness() { ++alive; }
    ~Witness() { --alive; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != __uuidof(IUnknown)) return E_NOINTERFACE;
        *out = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const auto left = --refs;
        if (!left) delete this;
        return left;
    }
};
static void Observe(ID3D12Object* object)
{
    constexpr GUID key = {0x706f6f6c,0x1234,0x4321,{1,2,3,4,5,6,7,8}};
    auto* witness = new Witness;
    Check(object->SetPrivateDataInterface(key, witness));
    witness->Release();
}

int main()
{
    ComPtr<ID3D12Debug> debug;
    const bool debugEnabled = SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));
    if (debugEnabled) debug->EnableDebugLayer();
    std::puts(debugEnabled ? "D3D12 debug layer enabled." : "D3D12 debug layer unavailable; API validation omitted.");
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> warp;
    ComPtr<ID3D12Device> device;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qdesc {};
    Check(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> gate;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));

    struct Commands
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        Safety::Ticket ticket;
    };
    auto makeCommands = [&]()
    {
        auto c = std::make_unique<Commands>();
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&c->allocator)));
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, c->allocator.Get(), nullptr,
                                       IID_PPV_ARGS(&c->list)));
        c->ticket = Safety::Record(c->list.Get());
        assert(c->ticket);
        return c;
    };
    auto submit = [&](Commands& c)
    {
        ID3D12CommandList* lists[] = {c.list.Get()};
        queue->ExecuteCommandLists(1, lists);
    };
    auto seal = [&](Commands& c) { Check(c.list->Reset(c.allocator.Get(), nullptr)); };
    auto consume = [](Pool& pool, const Safety::Ticket& ticket, unsigned int count)
    {
        std::vector<Pool::Slot*> slots;
        for (unsigned int i = 0; i < count; ++i)
        {
            auto* slot = pool.Consume(ticket);
            assert(slot && slot->owner == ticket && slot->constants && slot->heap.GetHeapCSU());
            assert(std::find(slots.begin(), slots.end(), slot) == slots.end());
            slots.push_back(slot);
        }
        return slots;
    };

    {
        Pool pool;
        assert(pool.Initialize(device.Get()) && pool.Capacity() == 48);
        std::vector<std::unique_ptr<Commands>> retained;
        std::vector<Pool::Slot*> originalSlots;
        // 14 completed, legally replayable recordings each own three distinct composition slots.
        for (unsigned int i = 0; i < 14; ++i)
        {
            auto c = makeCommands();
            assert(pool.Begin(device.Get(), c->ticket) == Admission::Accepted);
            auto slots = consume(pool, c->ticket, 3);
            originalSlots.insert(originalSlots.end(), slots.begin(), slots.end());
            Check(c->list->Close()); submit(*c);
            retained.push_back(std::move(c));
        }
        assert(Safety::Drain(5000));
        auto occupancy = pool.Snapshot();
        assert(pool.Capacity() == 48 && occupancy.completedUnsealed == 42 && occupancy.reusable == 6);

        // Replay three of these slots behind a blocked queue: 39 complete/unsealed + 3 GPU pending.
        Check(queue->Wait(gate.Get(), 1)); submit(*retained.front());
        occupancy = pool.Snapshot();
        assert(occupancy.completedUnsealed == 39 && occupancy.gpuPending == 3 && occupancy.reusable == 6);
        auto newcomer = makeCommands();
        assert(pool.Begin(device.Get(), newcomer->ticket) == Admission::Accepted && pool.Capacity() == 80);
        auto expanded = consume(pool, newcomer->ticket, 8);
        assert(pool.Consume(newcomer->ticket) == nullptr); // cannot overrun a reservation
        assert(pool.Consume(retained.front()->ticket) == nullptr); // exact owner only
        for (auto* slot : expanded)
            assert(std::find(originalSlots.begin(), originalSlots.end(), slot) == originalSlots.end());
        for (unsigned int i = 0; i < originalSlots.size(); ++i)
            assert(originalSlots[i]->owner == retained[i / 3]->ticket);
        seal(*retained.front());
        assert(!Safety::Reusable(retained.front()->ticket));
        Check(gate->Signal(1));
        // The newcomer's unsubmitted live recording deliberately prevents a drain.
        Check(newcomer->list->Close()); seal(*newcomer);
        assert(Safety::Drain(5000) && Safety::Reusable(retained.front()->ticket));
        for (unsigned int i = 1; i < retained.size(); ++i) seal(*retained[i]);
        occupancy = pool.Snapshot();
        assert(occupancy.reusable == 80 && pool.Capacity() == 80); // no shrink
        std::puts("PASS: GTA pressure, duplicate owners, replay, delayed completion, reservation and recovery.");
    }
    assert(Safety::Drain(5000));
    Safety::NewSession();

    {
        Pool pool;
        assert(pool.Initialize(device.Get()));
        auto owner = makeCommands();
        // Repeated evaluations can share one unreset recording; each pass still needs a new slot.
        for (unsigned int i = 0; i < 32; ++i)
        {
            assert(pool.Begin(device.Get(), owner->ticket) == Admission::Accepted);
            consume(pool, owner->ticket, 8);
        }
        assert(pool.Capacity() == 256 && pool.Snapshot().unsubmittedUnsealed == 256);
        const auto attempts = pool.Stats().allocationAttempts;
        for (unsigned int i = 0; i < 10; ++i)
        {
            assert(pool.Begin(device.Get(), owner->ticket) == Admission::Capacity);
            assert(!pool.Consume(owner->ticket)); // fail closed, nothing recordable
        }
        assert(pool.Stats().allocationAttempts == attempts && pool.Stats().capacityRejects == 10);
        assert(pool.Stats().maxRejectRun == 10 && pool.Stats().highWater == 256);
        Check(owner->list->Close()); seal(*owner);
        auto next = Safety::Record(owner->list.Get());
        assert(next && next != owner->ticket);
        assert(pool.Begin(device.Get(), next) == Admission::Accepted);
        consume(pool, next, 8);
        assert(pool.Capacity() == 256 && pool.Stats().rejectRun == 0);
        owner->list.Reset(); // unsubmitted destruction seals the replacement recording
        assert(Safety::Drain(5000));
        std::puts("PASS: grow beyond 128, bounded cap, no extra allocation at cap, recovery and destruction.");
    }
    Safety::NewSession();

    // Fail each buffer/heap creation and host allocation position of a runtime chunk.
    // Attach witnesses to real allocations; no partial publication or COM resource leak is allowed.
    for (unsigned int failPosition = 0; failPosition < Pool::GrowthChunk; ++failPosition)
    {
        for (unsigned int failureKind = 0; failureKind < 3; ++failureKind)
        {
            Pool pool;
            assert(pool.Initialize(device.Get()));
            auto owner = makeCommands();
            std::vector<Pool::Slot*> held;
            for (unsigned int i = 0; i < 6; ++i)
            {
                assert(pool.Begin(device.Get(), owner->ticket) == Admission::Accepted);
                auto slots = consume(pool, owner->ticket, 8);
                held.insert(held.end(), slots.begin(), slots.end());
            }
            unsigned int position = 0;
            auto fail = [&](ID3D12Device* d, Pool::Slot& slot)
            {
                const bool thisSlot = position++ == failPosition;
                if (thisSlot && failureKind == 0) return false; // buffer creation failed
                if (thisSlot && failureKind == 2) throw std::bad_alloc();
                auto properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
                auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(DlssNrConstants));
                Check(d->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc,
                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&slot.constants)));
                Observe(slot.constants.Get());
                if (thisSlot) return false; // descriptor heap creation failed after buffer success
                assert(slot.heap.Initialize(d, Pool::SrvCount, Pool::UavCount, 1));
                Observe(slot.heap.GetHeapCSU());
                return true;
            };
            assert(pool.Begin(device.Get(), owner->ticket, fail) == Admission::Allocation);
            assert(pool.Capacity() == 48 && pool.Stats().allocationFailures == 1 && Witness::alive == 0);
            assert(!pool.Consume(owner->ticket));
            for (auto* slot : held) assert(slot->owner == owner->ticket && slot->constants && slot->heap.GetHeapCSU());
            assert(pool.Begin(device.Get(), owner->ticket) == Admission::Accepted && pool.Capacity() == 80);
            consume(pool, owner->ticket, 1);
            owner->list.Reset();
            assert(Safety::Drain(5000));
            Safety::NewSession();
        }
    }
    {
        Pool pool;
        auto fail = [](ID3D12Device*, Pool::Slot&) { return false; };
        assert(!pool.Initialize(device.Get(), fail) && pool.Capacity() == 0);
        auto owner = makeCommands();
        assert(pool.Begin(device.Get(), owner->ticket) == Admission::Accepted && pool.Capacity() == 48);
        assert(pool.Begin(device.Get(), {}) == Admission::Tracking);
        assert(!pool.Consume(owner->ticket));
        auto failed = std::make_shared<Safety::Recording>();
        failed->failed = true;
        assert(pool.Begin(device.Get(), failed) == Admission::Device);
        assert(!pool.Consume(failed));
        owner->list.Reset();
        assert(Safety::Drain(5000));
    }
    std::puts("PASS: 96 runtime allocation-failure positions, rollback cleanup, initial failure and retry.");

    // Actual GPU replay of an upload slot must continue seeing its original constants after growth.
    {
        Pool pool;
        auto owner = makeCommands();
        assert(pool.Begin(device.Get(), owner->ticket) == Admission::Accepted);
        auto* first = pool.Consume(owner->ticket);
        DlssNrConstants constants {}; constants.Width = 0x1234;
        assert(CreateConstantsBuffer(device.Get(), first->constants.Get(), constants, first->heap.GetCbvCPU(0)));
        ComPtr<ID3D12Resource> readback;
        auto properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(constants));
        Check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
        owner->list->CopyBufferRegion(readback.Get(), 0, first->constants.Get(), 0, sizeof(constants));
        Check(owner->list->Close()); submit(*owner);
        assert(Safety::Drain(5000));
        auto other = makeCommands();
        for (unsigned int i = 0; i < 20; ++i)
        {
            assert(pool.Begin(device.Get(), other->ticket) == Admission::Accepted);
            for (auto* slot : consume(pool, other->ticket, 8))
            {
                constants.Width = 0x9876;
                assert(CreateConstantsBuffer(device.Get(), slot->constants.Get(), constants, slot->heap.GetCbvCPU(0)));
                assert(slot != first);
            }
        }
        other->list.Reset();
        submit(*owner); assert(Safety::Drain(5000));
        void* data = nullptr;
        Check(readback->Map(0, nullptr, &data));
        assert(static_cast<DlssNrConstants*>(data)->Width == 0x1234);
        readback->Unmap(0, nullptr);
        seal(*owner);
        assert(Safety::Reusable(first->owner));
        std::puts("PASS: actual GPU upload replay preserves original constants across multiple growth events.");
    }
    assert(Safety::Drain(5000));
    Safety::NewSession();

    // New device/pool after a drained session: allocations and tickets must belong to
    // that device, without inheriting any prior slot or reservation.
    {
        ComPtr<ID3D12Device> replacementDevice;
        Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&replacementDevice)));
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12CommandQueue> replacementQueue;
        Check(replacementDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        Check(replacementDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                                   IID_PPV_ARGS(&list)));
        Check(replacementDevice->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&replacementQueue)));
        Pool replacementPool;
        const auto ticket = Safety::Record(list.Get());
        assert(replacementPool.Begin(replacementDevice.Get(), ticket) == Admission::Accepted);
        auto slots = consume(replacementPool, ticket, 8);
        ComPtr<ID3D12Device> allocationDevice;
        Check(slots[0]->constants->GetDevice(IID_PPV_ARGS(&allocationDevice)));
        assert(allocationDevice.Get() == replacementDevice.Get() && replacementPool.Capacity() == 48);
        Check(list->Close());
        ID3D12CommandList* lists[] = {list.Get()};
        replacementQueue->ExecuteCommandLists(1, lists);
        assert(Safety::Drain(5000));
        list.Reset();
        assert(Safety::Reusable(ticket));
        Safety::NewSession();
        std::puts("PASS: new device and pool after drained session own independent resources and tickets.");
    }

    // Ten passes reserve the first-pass budget plus four composition dispatches for each later
    // pass. The reservation is bounded, consumes every slot exactly once, and rejects overflow.
    {
        Pool pool;
        auto owner = makeCommands();
        const unsigned int required = Pool::RequiredSlots(10);
        assert(required == 44);
        assert(pool.Begin(device.Get(), owner->ticket, &Pool::CreateSlot, required) == Admission::Accepted);
        consume(pool, owner->ticket, required);
        assert(!pool.Consume(owner->ticket));
        assert(pool.Begin(device.Get(), owner->ticket, &Pool::CreateSlot,
                          Pool::MaxAdmissionSlots + Pool::SlotsPerAdditionalPass) == Admission::Capacity);
        Check(owner->list->Close()); submit(*owner);
        assert(Safety::Drain(5000));
        seal(*owner);
        std::puts("PASS: ten-pass forty-four-slot bounded admission and ownership.");
    }

    // Integration: preserve the flagship's conservative twelve-slot two-layer reservation.
    // A completed recording remains replayable; growth must not recycle any of its slots.
    {
        Pool pool;
        auto owner = makeCommands();
        for (unsigned int n = 0; n < 21; ++n)
        {
            assert(pool.Begin(device.Get(), owner->ticket, &Pool::CreateSlot,
                              Pool::TwoLayerAdmissionSlots) == Admission::Accepted);
            consume(pool, owner->ticket, 12);
            assert(!pool.Consume(owner->ticket));
        }
        assert(pool.Capacity() == 256 && pool.Snapshot().unsubmittedUnsealed == 252);
        assert(pool.Begin(device.Get(), owner->ticket, &Pool::CreateSlot, 12) == Admission::Capacity);
        Check(owner->list->Close()); submit(*owner);
        assert(Safety::Drain(5000));
        assert(pool.Begin(device.Get(), owner->ticket, &Pool::CreateSlot, 12) == Admission::Capacity);
        seal(*owner);
        owner->ticket = Safety::Record(owner->list.Get());
        assert(pool.Begin(device.Get(), owner->ticket, &Pool::CreateSlot, 12) == Admission::Accepted);
        consume(pool, owner->ticket, 12);
        assert(pool.Begin(device.Get(), owner->ticket, &Pool::CreateSlot, 13) == Admission::Capacity);
        assert(!pool.Consume(owner->ticket));
        Check(owner->list->Close()); submit(*owner);
        assert(Safety::Drain(5000));
        seal(*owner);
        std::puts("PASS: two-layer twelve-slot admission, transactional growth, hard cap, replay ownership and recovery.");
    }
    Safety::NewSession();

    if (debugEnabled)
    {
        ComPtr<ID3D12InfoQueue> messages;
        Check(device.As(&messages));
        for (UINT64 i = 0; i < messages->GetNumStoredMessages(); ++i)
        {
            SIZE_T bytes = 0;
            Check(messages->GetMessage(i, nullptr, &bytes));
            std::vector<unsigned char> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(messages->GetMessage(i, message, &bytes));
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
            {
                std::fprintf(stderr, "D3D12 error: %s\n", message->pDescription);
                assert(false);
            }
        }
    }
    {
        Pool pool;
        auto owner = makeCommands();
        assert(pool.Begin(device.Get(), owner->ticket) == Admission::Accepted);
        consume(pool, owner->ticket, 8);
        Check(owner->list->Close()); submit(*owner);
        assert(Safety::Drain(5000));
        seal(*owner);
        ComPtr<ID3D12Device5> removable;
        Check(device.As(&removable)); removable->RemoveDevice();
        assert(pool.Snapshot().failed == 8); // inspect never authorizes reuse
        assert(!Safety::Reusable(owner->ticket));
        assert(pool.Begin(device.Get(), Safety::Record(owner->list.Get())) == Admission::Device);
        assert(!pool.Consume(owner->ticket) && pool.Capacity() == 48);
        assert(pool.Stats().deviceRejects == 1);
    }
    std::puts("PASS: drained teardown/recreation, device loss and failed admission; pool suite complete.");
}
