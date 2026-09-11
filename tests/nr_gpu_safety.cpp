// Runs the production submission/reset hooks on a software D3D12 device. No game or NR model.
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/DlssNr_Capture.h"
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <cassert>
#include <cstdio>

using Microsoft::WRL::ComPtr;
namespace Safety = DlssNr::GpuSafety;
static void Check(HRESULT hr) { assert(SUCCEEDED(hr)); }

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
    ComPtr<ID3D12CommandQueue> queue, otherQueue;
    D3D12_COMMAND_QUEUE_DESC queueDesc {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&otherQueue)));
    ComPtr<ID3D12CommandAllocator> allocator, nextAllocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&nextAllocator)));
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    auto submit = [&](ID3D12CommandQueue* q) { ID3D12CommandList* lists[] = {list.Get()}; q->ExecuteCommandLists(1, lists); };

    auto abandoned = Safety::Record(list.Get());
    assert(!Safety::OrderedOn(abandoned, queue.Get()));
    assert(abandoned && !Safety::Reusable(abandoned) && !Safety::Drain(0));
    Check(list->Close());
    Check(list->Reset(allocator.Get(), nullptr));
    assert(Safety::Reusable(abandoned) && !Safety::Readable(abandoned));

    ComPtr<ID3D12Fence> gate;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gate)));
    Check(queue->Wait(gate.Get(), 1));
    auto pending = Safety::Record(list.Get());
    auto retirement = Safety::Pending();
    bool retiredSessionReleased = false;
    bool replacementSessionCreated = false;
    Check(list->Close());
    submit(queue.Get());
    Check(list->Reset(nextAllocator.Get(), nullptr)); // list Reset is legal while old work runs
    assert(Safety::OrderedOn(pending, queue.Get())); // GPU need not be CPU-complete
    assert(!Safety::OrderedOn(pending, otherQueue.Get()));
    for (int i = 0; i < 1000; ++i)
    {
        if (Safety::Reusable(retirement)) retiredSessionReleased = true;
        if (retiredSessionReleased) replacementSessionCreated = true;
        assert(!Safety::Reusable(pending) && !Safety::Readable(pending) && !retiredSessionReleased &&
               !replacementSessionCreated);
    }
    assert(!Safety::Drain(1));
    Check(gate->Signal(1));
    assert(Safety::Drain(5000));
    if (Safety::Reusable(retirement)) retiredSessionReleased = true;
    if (retiredSessionReleased) replacementSessionCreated = true;
    assert(Safety::Reusable(pending) && Safety::Readable(pending) && retiredSessionReleased &&
           replacementSessionCreated);
    assert(Safety::TimestampFrequency(pending) > 0);

    auto replay = Safety::Record(list.Get());
    Check(list->Close());
    submit(queue.Get());
    assert(Safety::Drain(5000));
    assert(!Safety::Reusable(replay)); // completed but still executable
    Check(queue->Wait(gate.Get(), 2));
    submit(queue.Get());
    Check(list->Reset(allocator.Get(), nullptr));
    assert(!Safety::OrderedOn(replay, queue.Get()));
    assert(!Safety::Reusable(replay) && !Safety::Drain(0));
    Check(gate->Signal(2));
    assert(Safety::Drain(5000) && Safety::Reusable(replay));

    // Track both queues without introducing a cycle into the host's Wait/Signal graph.
    Check(queue->Wait(gate.Get(), 3));
    auto first = Safety::Record(list.Get());
    Check(list->Close());
    submit(queue.Get());
    Check(list->Reset(nextAllocator.Get(), nullptr));
    auto second = Safety::Record(list.Get());
    Check(list->Close());
    submit(otherQueue.Get());
    Check(list->Reset(allocator.Get(), nullptr));
    assert(!Safety::Reusable(first));
    // This queue must be able to release the first queue, even though it submitted NR second.
    Check(otherQueue->Signal(gate.Get(), 3));
    assert(Safety::Drain(5000) && Safety::Reusable(first) && Safety::Reusable(second));

    // A single recording replayed on another queue needs both completion points.
    auto multiQueue = Safety::Record(list.Get());
    Check(list->Close());
    submit(queue.Get());
    assert(Safety::Drain(5000));
    Check(otherQueue->Wait(gate.Get(), 4));
    submit(otherQueue.Get());
    Check(list->Reset(nextAllocator.Get(), nullptr));
    assert(!Safety::Reusable(multiQueue) && !Safety::Readable(multiQueue));
    Check(gate->Signal(4));
    assert(Safety::Drain(5000) && Safety::Reusable(multiQueue));
    assert(Safety::TimestampFrequency(multiQueue) == 0); // no ambiguous timing calculation

    auto destroyedInFlight = Safety::Record(list.Get());
    Check(list->Close());
    Check(queue->Wait(gate.Get(), 5));
    submit(queue.Get());
    list.Reset();
    assert(!Safety::Reusable(destroyedInFlight));
    Check(gate->Signal(5));
    assert(Safety::Drain(5000) && Safety::Reusable(destroyedInFlight));
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));

    auto destroyed = Safety::Record(list.Get());
    list.Reset();
    assert(Safety::Reusable(destroyed) && !Safety::Readable(destroyed));
    assert(Safety::Drain(0));
    Safety::NewSession();
    assert(Safety::Pending().empty());

    // Capture must not copy a new shape into an old footprint or free an unsubmitted copy.
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    auto makeTexture = [&](UINT width, UINT height = 4,
                           DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM, UINT16 mips = 1)
    {
        ComPtr<ID3D12Resource> texture;
        D3D12_HEAP_PROPERTIES heap {}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = mips;
        desc.Format = format; desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&texture)));
        return texture;
    };
    auto smallTexture = makeTexture(4), largeTexture = makeTexture(8);
    capture::FrameCapture capture;
    capture.request(2);
    capture.record(list.Get(), device.Get(), smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.progress() == 1);
    capture.record(list.Get(), device.Get(), largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.progress() == 1 && !capture.readyToWrite());
    Check(list->Close());
    Check(list->Reset(nextAllocator.Get(), nullptr)); // discard the old recording
    assert(capture.readyToWrite());
    assert(capture.write("unused-capture-test-path").empty());
    assert(capture.isActive() && capture.progress() == 0); // rearmed, no files written

    // A temporary unsupported replacement must not silently cancel the rearmed request.
    auto mipTexture = makeTexture(8, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 2);
    capture.record(list.Get(), device.Get(), largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   mipTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.isActive() && capture.progress() == 0 && !capture.readyToWrite());
    capture.record(nullptr, device.Get(), largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    capture.record(list.Get(), nullptr, largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.isActive() && capture.progress() == 0);
    capture.record(list.Get(), device.Get(), largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.progress() == 1);
    Check(list->Close());
    Check(list->Reset(allocator.Get(), nullptr));
    capture.release();

    // Exercise both inputs and each copy-relevant resize independently. The original test
    // canceled its list: these copies are actually submitted behind a GPU gate.
    auto tallTexture = makeTexture(4, 8);
    auto floatTexture = makeTexture(4, 4, DXGI_FORMAT_R16G16B16A16_FLOAT);
    UINT64 captureGate = 6;
    for (auto* replacement : {largeTexture.Get(), tallTexture.Get(), floatTexture.Get(), mipTexture.Get()})
    {
        for (bool changeBefore : {false, true})
        {
            capture.request(2);
            capture.record(list.Get(), device.Get(), smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            assert(capture.progress() == 1);
            Check(list->Close());
            Check(queue->Wait(gate.Get(), captureGate));
            submit(queue.Get());
            Check(list->Reset(nextAllocator.Get(), nullptr));
            capture.record(list.Get(), device.Get(), changeBefore ? replacement : smallTexture.Get(),
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           changeBefore ? smallTexture.Get() : replacement,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            for (int poll = 0; poll < 100; ++poll)
                assert(capture.progress() == 1 && !capture.readyToWrite());
            Check(gate->Signal(captureGate++));
            assert(Safety::Drain(5000));
            assert(capture.readyToWrite());
            assert(capture.write("unused-capture-test-path").empty());
            assert(capture.isActive() && capture.progress() == 0);
            capture.release();
            Check(list->Close());
            Check(list->Reset(allocator.Get(), nullptr));
        }
    }

    // A completed but replayable capture still owns all shots. Saturating the requested
    // bound must neither overrun the vectors nor start a new size while waiting.
    capture.request(capture::kMaxFrames + 10);
    for (unsigned int i = 0; i < capture::kMaxFrames + 10; ++i)
        capture.record(list.Get(), device.Get(), smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       smallTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.progress() == capture::kMaxFrames);
    Check(list->Close());
    submit(queue.Get());
    assert(Safety::Drain(5000) && !capture.readyToWrite());
    Check(queue->Wait(gate.Get(), captureGate));
    submit(queue.Get());
    Check(list->Reset(nextAllocator.Get(), nullptr));
    capture.record(list.Get(), device.Get(), largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   largeTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(capture.progress() == capture::kMaxFrames && !capture.readyToWrite());
    Check(gate->Signal(captureGate));
    assert(Safety::Drain(5000) && capture.readyToWrite());
    capture.release();
    list.Reset();

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

    // Fence failure is a permanent failure, never mistaken for permission to reclaim.
    auto failed = std::make_shared<Safety::Recording>();
    failed->sealed = true;
    failed->failed = true;
    assert(!Safety::Reusable(failed) && !Safety::Readable(failed));
    ComPtr<ID3D12Device5> removable;
    Check(device.As(&removable));
    removable->RemoveDevice();
    // The sentinel UINT64_MAX is device loss, not a very large successful fence value.
    assert(!Safety::Reusable(pending) && !Safety::Readable(pending));
    std::puts("PASS: unsubmitted cancellation, 1000 premature reuse/read checks, delayed GPU completion,");
    std::puts("      replay, independent queues/host dependencies, retirement, capture shape changes, failure and device loss.");
}
