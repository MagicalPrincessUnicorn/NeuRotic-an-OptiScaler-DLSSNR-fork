// Runs the production submission/reset hooks on a software D3D12 device. No game or NR model.
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/DlssNr_Capture.h"
#include "../OptiScaler/dlssnr/PreFg.h"
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

    // The provider list must wait on the producer fence without blocking the CPU.
    ComPtr<ID3D12Fence> externalProducer, externalConsumer;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&externalProducer)));
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&externalConsumer)));
    assert(!Safety::BindExternalWait(nullptr, externalProducer.Get(), 1, 41, 42));
    assert(!Safety::BindExternalWait(list.Get(), nullptr, 1, 41, 42));
    assert(!Safety::BindExternalWait(list.Get(), externalProducer.Get(), 0, 41, 42));
    assert(!Safety::BindExternalWait(list.Get(), externalProducer.Get(), UINT64_MAX, 41, 42));
    namespace PreFg = DlssNr::PreFg;
    PreFg::CompletionLedger probeLedger;
    auto* probeResource = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x1000));
    const auto probeReservation = probeLedger.Reserve(probeResource, 3, 4, 5, 41, 42, PreFg::CompletionKind::Probe);
    assert(probeReservation && probeLedger.Commit(probeReservation, probeResource,
        externalProducer.Get(), 1, PreFg::CompletionKind::Probe));
    assert(probeLedger.Claim(probeResource, 3, 4, 5, 40, 42).result == PreFg::CompletionClaimResult::Refused);
    assert(probeLedger.Claim(probeResource, 3, 4, 6, 41, 42).result == PreFg::CompletionClaimResult::Refused);
    const auto probe = probeLedger.Claim(probeResource, 3, 4, 5, 41, 42);
    assert(probe.result == PreFg::CompletionClaimResult::Ready &&
        probe.dependency.kind == PreFg::CompletionKind::Probe && probe.dependency.reservation == probeReservation);
    assert(probeLedger.Claim(probeResource, 3, 4, 5, 41, 42).result == PreFg::CompletionClaimResult::Refused);
    auto waitStatus = probe.dependency.status;
    PreFg::StartupReadiness certificate;
    PreFg::ReadinessIdentity certificateKey {};
    certificateKey.provider = 3; certificateKey.nativeGeneration = 4; certificateKey.nativeInstance = 5;
    assert(certificate.Submit(certificateKey, probe.dependency.fence.Get(), probe.dependency.value,
        probe.dependency.sequence, waitStatus));
    certificate.Presented(42, true);
    assert(Safety::BindExternalWait(list.Get(), externalProducer.Get(), 1, 41, 42, waitStatus));
    assert(waitStatus->bound && !waitStatus->applied && !waitStatus->failed);
    assert(!Safety::BindExternalWait(list.Get(), externalProducer.Get(), 1, 43, 44));
    Check(list->Close());
    submit(otherQueue.Get());
    assert(Safety::Get<Safety::ExternalWait>(list.Get(), Safety::externalWaitGuid));
    Check(otherQueue->Signal(externalConsumer.Get(), 1));
    assert(externalConsumer->GetCompletedValue() == 0);
    for (unsigned int i = 0; i < 1000; ++i) assert(!certificate.Poll(certificateKey, device.Get(), 43 + i));
    Check(externalProducer->Signal(1));
    assert(waitStatus->applied && !waitStatus->failed);
    HANDLE externalDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(externalDone);
    Check(externalConsumer->SetEventOnCompletion(1, externalDone));
    assert(WaitForSingleObject(externalDone, 5000) == WAIT_OBJECT_0);
    CloseHandle(externalDone);
    assert(certificate.Poll(certificateKey, device.Get(), 43));
    probeLedger.RetireBefore(43);
    assert(probeLedger.Claim(probeResource, 3, 4, 5, 43, 43).result == PreFg::CompletionClaimResult::None);
    assert(certificate.Ready()); // retained proof survives ordinary packet retirement
    // A provider may reuse a list whose completed wait cookie is still attached.
    // The finished producer can retire without Reset, but the new unfinished
    // dependency must survive duplicates and further attempted replacements.
    auto replacementStatus = std::make_shared<Safety::ExternalWaitStatus>();
    assert(Safety::BindExternalWait(list.Get(), externalProducer.Get(), 2, 43, 44, replacementStatus));
    assert(waitStatus->applied && !waitStatus->failed && certificate.Ready());
    assert(!Safety::BindExternalWait(list.Get(), externalProducer.Get(), 2, 43, 44));
    assert(!Safety::BindExternalWait(list.Get(), externalProducer.Get(), 3, 45, 46));
    assert(Safety::Get<Safety::ExternalWait>(list.Get(), Safety::externalWaitGuid)->status == replacementStatus);
    Check(list->Reset(allocator.Get(), nullptr));
    assert(!Safety::Get<Safety::ExternalWait>(list.Get(), Safety::externalWaitGuid));
    assert(replacementStatus->failed && !replacementStatus->applied);

    // Completed production alone must never fabricate a provider queue-wait
    // acknowledgement. Replacing an unexecuted old wait cancels its proof.
    auto unobservedStatus = std::make_shared<Safety::ExternalWaitStatus>();
    auto retainedStatus = std::make_shared<Safety::ExternalWaitStatus>();
    assert(Safety::BindExternalWait(list.Get(), externalProducer.Get(), 1, 50, 51, unobservedStatus));
    assert(Safety::BindExternalWait(list.Get(), externalProducer.Get(), 1, 52, 53, retainedStatus));
    assert(unobservedStatus->failed && !unobservedStatus->applied);
    assert(retainedStatus->bound && !retainedStatus->failed && !retainedStatus->applied);
    Check(list->Close());
    Check(list->Reset(nextAllocator.Get(), nullptr));
    assert(retainedStatus->failed && !retainedStatus->applied);

    // Reset before submission cancels the borrowed dependency.
    ComPtr<ID3D12Fence> canceledProducer;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&canceledProducer)));
    std::atomic<uint64_t> failureEpoch {1};
    auto canceledStatus = std::make_shared<Safety::ExternalWaitStatus>();
    canceledStatus->failureEpoch = &failureEpoch;
    assert(Safety::BindExternalWait(list.Get(), canceledProducer.Get(), 1, 45, 46, canceledStatus));
    Check(list->Close());
    Check(list->Reset(nextAllocator.Get(), nullptr));
    Check(list->Close());
    submit(otherQueue.Get());
    Check(otherQueue->Signal(externalConsumer.Get(), 2));
    externalDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(externalDone);
    Check(externalConsumer->SetEventOnCompletion(2, externalDone));
    assert(WaitForSingleObject(externalDone, 5000) == WAIT_OBJECT_0);
    CloseHandle(externalDone);
    Check(list->Reset(allocator.Get(), nullptr));

    auto abandoned = Safety::Record(list.Get());
    assert(!Safety::OrderBefore(abandoned, otherQueue.Get()));
    assert(!Safety::OrderBefore(abandoned, queue.Get()));
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
    assert(Safety::OrderBefore(pending, queue.Get())); // submitted, GPU blocked, not Reset
    assert(!Safety::Reusable(pending) && !Safety::Readable(pending)); // ordering does not retire work
    assert(!Safety::OrderBefore(pending, otherQueue.Get())); // unsealed lists could replay
    Check(list->Reset(nextAllocator.Get(), nullptr)); // list Reset is legal while old work runs
    assert(Safety::OrderedOn(pending, queue.Get())); // GPU need not be CPU-complete
    assert(!Safety::OrderedOn(pending, otherQueue.Get()));
    ComPtr<ID3D12Fence> consumerPassed;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&consumerPassed)));
    assert(Safety::OrderBefore(pending, otherQueue.Get()));
    Check(otherQueue->Signal(consumerPassed.Get(), 1));
    assert(consumerPassed->GetCompletedValue() == 0); // wait cannot pass the blocked producer
    for (int i = 0; i < 1000; ++i)
    {
        if (Safety::Reusable(retirement)) retiredSessionReleased = true;
        if (retiredSessionReleased) replacementSessionCreated = true;
        assert(!Safety::Reusable(pending) && !Safety::Readable(pending) && !retiredSessionReleased &&
               !replacementSessionCreated);
    }
    assert(!Safety::Drain(1));
    Check(gate->Signal(1));
    HANDLE handoffDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(handoffDone);
    Check(consumerPassed->SetEventOnCompletion(1, handoffDone));
    assert(WaitForSingleObject(handoffDone, 5000) == WAIT_OBJECT_0);
    CloseHandle(handoffDone);
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
    assert(!Safety::OrderBefore(replay, queue.Get())); // observed replay still fails same-queue admission
    assert(!Safety::Reusable(replay) && !Safety::Drain(0));
    Check(gate->Signal(2));
    assert(Safety::Drain(5000) && Safety::Reusable(replay));

    // Sealing an owned recording never bypasses the actual GPU completion point.
    ComPtr<ID3D12Fence> ownedGate;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ownedGate)));
    Check(queue->Wait(ownedGate.Get(), 1));
    auto owned = Safety::Record(list.Get());
    auto ownedRetirement = Safety::Pending();
    assert(!Safety::SealOwnedRecording(list.Get())); // still unsubmitted
    Check(list->Close());
    submit(queue.Get());
    assert(Safety::SealOwnedRecording(list.Get()));
    assert(!Safety::Reusable(owned) && !Safety::Readable(owned) && !Safety::Reusable(ownedRetirement));
    Check(ownedGate->Signal(1));
    assert(Safety::Drain(5000));
    assert(Safety::Reusable(owned) && Safety::Readable(owned) && Safety::Reusable(ownedRetirement));
    Check(list->Reset(allocator.Get(), nullptr));

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
    assert(!Safety::OrderBefore(multiQueue, otherQueue.Get()));

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

    // Exercise actual DIRECT -> COMPUTE -> DIRECT provider queues. CPU admission
    // stays non-blocking while a different GPU queue holds the producer fence.
    for (auto providerType : {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                              D3D12_COMMAND_LIST_TYPE_DIRECT})
    {
        ComPtr<ID3D12CommandQueue> providerQueue;
        D3D12_COMMAND_QUEUE_DESC desc {}; desc.Type = providerType;
        Check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&providerQueue)));
        ComPtr<ID3D12CommandAllocator> providerAllocator;
        ComPtr<ID3D12GraphicsCommandList> providerList;
        Check(device->CreateCommandAllocator(providerType, IID_PPV_ARGS(&providerAllocator)));
        Check(device->CreateCommandList(0, providerType, providerAllocator.Get(), nullptr, IID_PPV_ARGS(&providerList)));
        auto observation = Safety::ObserveExternalExecution(providerList.Get());
        assert(observation && !observation->Ready());
        observation->evaluated = true;
        assert(!observation->Ready()); // successful evaluate alone is not submission
        ComPtr<ID3D12Fence> producer, blocker, consumer;
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&producer)));
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&blocker)));
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&consumer)));
        Check(queue->Wait(blocker.Get(), 1));
        Check(queue->Signal(producer.Get(), 1));
        auto status = std::make_shared<Safety::ExternalWaitStatus>();
        assert(Safety::BindExternalWait(providerList.Get(), producer.Get(), 1, 100, 101, status));
        Check(providerList->Close());
        ID3D12CommandList* batch[] = {providerList.Get()};
        providerQueue->ExecuteCommandLists(1, batch);
        assert(observation->Ready() && status->applied && !status->failed);
        Check(providerQueue->Signal(consumer.Get(), 1));
        assert(consumer->GetCompletedValue() == 0);
        Check(blocker->Signal(1));
        HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr); assert(done);
        Check(consumer->SetEventOnCompletion(1, done));
        assert(WaitForSingleObject(done, 5000) == WAIT_OBJECT_0);
        // Replay retains the exact dependency on the same compatible queue.
        providerQueue->ExecuteCommandLists(1, batch);
        Check(providerQueue->Signal(consumer.Get(), 2));
        Check(consumer->SetEventOnCompletion(2, done));
        assert(WaitForSingleObject(done, 5000) == WAIT_OBJECT_0);
        CloseHandle(done);
        Check(providerList->Reset(providerAllocator.Get(), nullptr));
        assert(observation->Ready());
        auto canceled = Safety::ObserveExternalExecution(providerList.Get());
        assert(canceled); canceled->evaluated = true;
        Check(providerList->Close());
        Check(providerList->Reset(providerAllocator.Get(), nullptr));
        assert(canceled->failed && !canceled->Ready());
    }
    {
        ComPtr<ID3D12CommandAllocator> copyAllocator;
        ComPtr<ID3D12GraphicsCommandList> copyList;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&copyAllocator)));
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, copyAllocator.Get(), nullptr,
                                       IID_PPV_ARGS(&copyList)));
        assert(!Safety::ObserveExternalExecution(copyList.Get()));
        assert(!Safety::BindExternalWait(copyList.Get(), externalProducer.Get(), 1, 110, 111));
        Check(copyList->Close());
    }
    std::puts("PASS: DIRECT/COMPUTE/DIRECT actual queue handoffs, cross-queue waits, replay, observation cancellation and COPY refusal");

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

    // An invalid consumer queue must not execute the dependent batch, or report
    // its recording complete. Exercise the real hook with an intercepted trampoline
    // so the intentionally wrong queue/list pairing never reaches the GPU.
    ComPtr<ID3D12GraphicsCommandList> refusedList;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                   IID_PPV_ARGS(&refusedList)));
    auto refusedTicket = Safety::Record(refusedList.Get());
    assert(refusedTicket);
    assert(canceledStatus->failed && !canceledStatus->applied && failureEpoch == 2);
    auto refusedStatus = std::make_shared<Safety::ExternalWaitStatus>();
    refusedStatus->failureEpoch = &failureEpoch;
    assert(Safety::BindExternalWait(refusedList.Get(), externalProducer.Get(), 2, 47, 48, refusedStatus));
    Check(refusedList->Close());
    ComPtr<ID3D12CommandQueue> computeQueue;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&computeQueue)));
    static unsigned int forwarded = 0;
    const auto savedExecute = Safety::originalExecute;
    Safety::originalExecute = [](ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) { ++forwarded; };
    ID3D12CommandList* refusedBatch[] = {refusedList.Get()};
    Safety::Execute(computeQueue.Get(), 1, refusedBatch);
    Safety::originalExecute = savedExecute;
    assert(forwarded == 0 && Safety::State().failed && refusedTicket->failed);
    assert(refusedStatus->bound && !refusedStatus->applied && refusedStatus->failed && failureEpoch == 3);
    assert(Safety::Get<Safety::ExternalWait>(refusedList.Get(), Safety::externalWaitGuid));
    assert(!Safety::Reusable(refusedTicket) && !Safety::Readable(refusedTicket));
    refusedList.Reset();

    // Fence failure is a permanent failure, never mistaken for permission to reclaim.
    auto failed = std::make_shared<Safety::Recording>();
    failed->sealed = true;
    failed->failed = true;
    assert(!Safety::Reusable(failed) && !Safety::Readable(failed));
    ComPtr<ID3D12Device5> removable;
    Check(device.As(&removable));
    removable->RemoveDevice();
    assert(!certificate.Poll(certificateKey, device.Get(), 44));
    // The sentinel UINT64_MAX is device loss, not a very large successful fence value.
    assert(!Safety::Reusable(pending) && !Safety::Readable(pending));
    std::puts("PASS: unsubmitted cancellation, 1000 premature reuse/read checks, delayed GPU completion,");
    std::puts("      replay, explicit owned-list sealing, independent queues/host dependencies, retirement,");
    std::puts("      capture shape changes, failure and device loss.");
}
