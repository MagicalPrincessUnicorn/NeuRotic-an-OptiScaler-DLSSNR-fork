#include "../OptiScaler/dlssnr/PreFg.h"
#include "../OptiScaler/dlssnr/NativeFeatureRegistry.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

static void TestReadiness(ID3D12Device* device)
{
    using namespace DlssNr::PreFg;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    ReadinessIdentity key {1,2,3,4,5,6,7,8,9,10,11,2,1920,1080,28,1,0,1280,720,0};
    StartupReadiness readiness;
    auto status = std::make_shared<DlssNr::GpuSafety::ExternalWaitStatus>();
    assert(!readiness.Poll(key, device, 1));
    assert(!readiness.Submit(key, fence.Get(), UINT64_MAX, 1, status));
    assert(readiness.Submit(key, fence.Get(), 1, 1, status));
    assert(!readiness.Submit(key, fence.Get(), 1, 2, status)); // one pending probe
    status->bound = status->applied = true;
    for (uint64_t i = 2; i != 10002; ++i) assert(!readiness.Poll(key, device, i));
    assert(SUCCEEDED(fence->Signal(1)));
    assert(!readiness.Poll(key, device, 2)); // successful original Present is independent evidence
    readiness.Presented(2, true);
    assert(!readiness.Poll(key, device, 2)); // wrong sequence cannot complete probe Present
    readiness.Presented(1, true);
    assert(!readiness.Poll(key, device, 1)); // never publish the probe itself
    status->bound = false;
    assert(!readiness.Poll(key, device, 2));
    status->bound = true; status->applied = false;
    assert(!readiness.Poll(key, device, 2));
    status->applied = true;
    assert(readiness.Poll(key, device, 2));

    // Exact DD2 token/refusal runs are an admission replay, not GPU timing data.
    const int runs[] = {-7,1,-6,1,-5,1,-4,1,-3,1,-3,1,-2,1,-2,1,-2,1,-2,1,-2,1,
        -3,1,-2,1,-2,2,-3,2,-2,2,-2,2,-2,3,-2,4,-2,6,-2,429};
    Ledger frames;
    unsigned int attempts = 0, admitted = 0;
    for (int run : runs)
        for (int i = 0; i < (run < 0 ? -run : run); ++i)
        {
            frames.Constants(++attempts, 0);
            if (run > 0) frames.Tags(attempts, 0);
            const auto frame = frames.Claim();
            if (frame.valid) ++admitted;
            assert(readiness.Ready()); // frame-local refusals never reset generation readiness
        }
    assert(attempts == 523 && admitted == 463);
    readiness.Presented(3, false); assert(!readiness.Ready());

    const auto prepare = [&] {
        readiness.Reset();
        status = std::make_shared<DlssNr::GpuSafety::ExternalWaitStatus>();
        status->bound = status->applied = true;
        assert(readiness.Submit(key, fence.Get(), 1, 1, status));
        readiness.Presented(1, true);
        assert(readiness.Poll(key, device, 2));
    };
    // Every certificate field is independently structural, including model,
    // effective configuration, resource size, MSAA quality, and unique FG instance.
    const auto change = [&](auto member) {
        prepare(); auto other = key; ++(other.*member);
        assert(!readiness.Poll(other, device, 3));
        assert(readiness.State() == StartupReadiness::Phase::AwaitingFrame);
    };
#define CHECK_ID(field) change(&ReadinessIdentity::field)
    CHECK_ID(swapchain); CHECK_ID(device); CHECK_ID(queue); CHECK_ID(provider);
    CHECK_ID(nativeGeneration); CHECK_ID(nativeInstance); CHECK_ID(configuration);
    CHECK_ID(resume); CHECK_ID(resources); CHECK_ID(model); CHECK_ID(invalidation);
    CHECK_ID(route); CHECK_ID(width); CHECK_ID(height); CHECK_ID(format);
    CHECK_ID(samples); CHECK_ID(quality); CHECK_ID(workWidth); CHECK_ID(workHeight); CHECK_ID(colorSpace);
    CHECK_ID(modelLifecycle);
#undef CHECK_ID
    prepare(); status->Fail(); assert(!readiness.Ready() && !readiness.Poll(key, device, 3));
    prepare(); readiness.CheckEpoch(key.invalidation + 1); assert(!readiness.Ready());
    prepare(); assert(!readiness.Poll(key, nullptr, 3));
    std::puts("Readiness: 10000 stalled polls, exact proof, next-frame activation, all identities, DD2 replay PASS");
}

int main()
{
    using namespace DlssNr::PreFg;
    DlssNr::NativeFeatureRegistry<unsigned int> features;
    features.Set(77, 11);
    const auto oldFeature = features.Read(77);
    assert(!features.Released(77, oldFeature, false) && features.Has(11));
    features.Set(77, 11);
    assert(!features.Released(77, oldFeature, true));
    assert(features.Released(77, features.Read(77), true) && !features.Has(11));
    std::vector<std::thread> registryWorkers;
    for (unsigned int thread = 0; thread < 8; ++thread)
        registryWorkers.emplace_back([&, thread] {
            for (unsigned int i = 0; i < 2000; ++i)
            {
                features.Set(thread, i % 2 ? 11 : 13);
                const auto entry = features.Read(thread);
                assert(entry && features.Has(entry.feature));
                assert(!features.Released(thread, entry, false));
                assert(features.Released(thread, entry, true));
            }
        });
    for (auto& thread : registryWorkers) thread.join();
    assert(!features.Has(11) && !features.Has(13));
    Ledger ledger;
    // Captured Wilds interleaving: N is complete and being presented when the
    // producer publishes N+1 constants. Select N without consuming N+1.
    ledger.Constants(14681, 0); ledger.Tags(14681, 0);
    ledger.Constants(14682, 0);
    auto snapshot = ledger.Inspect();
    assert(snapshot.constants == 14683 && snapshot.tags == 14682 && snapshot.consumed == 0);
    const auto presented = ledger.Claim(14682, true);
    assert(presented.valid && presented.key == 14682);
    ledger.Tags(14682, 0);
    snapshot = ledger.Inspect();
    assert(snapshot.constants == snapshot.tags && snapshot.consumed == 14682);
    const auto next = ledger.Claim(14683, true);
    assert(next.valid && next.key == 14683); // future frame was not consumed by N
    assert(!ledger.Claim(14683, true).valid);
    Ledger generationLedger;
    generationLedger.Constants(7, 0, 1); generationLedger.Tags(7, 0, 1);
    assert(!generationLedger.Current(2)); // same token from an old provider generation
    generationLedger.Reset();
    generationLedger.Constants(7, 0, 2); generationLedger.Tags(7, 0, 2);
    assert(generationLedger.Claim(8, true, 2).valid);
    ledger.Reset();
    assert(!ledger.Claim().valid);
    ledger.Constants(0, 0);
    assert(ledger.Current() == 1);
    assert(!ledger.Claim().valid);
    ledger.Tags(0, 0);
    assert(!ledger.Claim().valid); // the already attempted frame remains consumed
    ledger.Constants(1, 0); ledger.Tags(1, 0);
    assert(ledger.Claim().valid);
    assert(!ledger.Claim().valid);
    ledger.Constants(2, 0); ledger.Tags(1, 0);
    assert(!ledger.Claim().valid);
    ledger.Constants(3, 0); ledger.Tags(4, 0);
    assert(!ledger.Claim().valid);
    ledger.Constants(5, 0); ledger.Constants(5, 1); ledger.Tags(5, 0);
    assert(!ledger.Current() && !ledger.Claim().valid);
    ledger.Constants(6, 0); ledger.Tags(6, 0);
    assert(ledger.Claim().valid); // fresh frame recovers
    ledger.Constants(4, 0); ledger.Tags(4, 0);
    assert(!ledger.Claim().valid); // older token cannot roll the consumed high-water back
    ledger.Constants(6, 0); ledger.Tags(6, 0);
    assert(!ledger.Claim().valid);
    ledger.Tags(7, 1); ledger.Constants(7, 0);
    assert(!ledger.Claim().valid); // tag-before-constants must preserve foreign-viewport refusal
    ledger.Reset();
    assert(!ledger.Current() && !ledger.Claim().valid);
    ledger.Constants(UINT32_MAX, 0); ledger.Tags(UINT32_MAX, 0);
    assert(ledger.Claim().valid);
    ledger.Constants(0, 0); ledger.Tags(0, 0);
    assert(ledger.Claim().valid); // uint32 wrap is not the missing-token sentinel
    ledger.Constants(1, 0); ledger.LegacyTags(0);
    const auto legacy = ledger.Claim();
    assert(legacy.valid && legacy.legacyTags);
    ledger.LegacyTags(0); ledger.Constants(2, 0);
    assert(!ledger.Claim().valid); // legacy tags cannot be assigned to a future token

    // Explicit Present identity fails closed and stays thread-local.
    ledger.Reset();
    ledger.Constants(30, 0); ledger.Tags(30, 0);
    assert(!ledger.Claim(0, true).valid);
    PresentStart(30);
    assert(PresentFrame() == 31);
    assert(ledger.Claim(PresentFrame(), true).valid);
    std::thread markerThread([] { assert(!PresentFrame()); PresentStart(31); assert(PresentFrame() == 32); PresentEnd(31); });
    markerThread.join();
    assert(PresentFrame() == 31);
    PresentStart(32); // overlapping marker on one thread is ambiguous
    assert(!PresentFrame());
    PresentEnd(32);
    assert(!PresentFrame());
    PresentStart(30); PresentMarkerFailed(); assert(!PresentFrame());

    State().swapchains = 1;
    ObserveConstants(23, 0); ObserveTags(23, 0);
    PresentStart(23);
    std::atomic<int> successes {0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 16; ++i)
        workers.emplace_back([&] {
            PresentStart(23);
            if (Claim(true).valid) ++successes;
            PresentEnd(23);
        });
    for (auto& worker : workers) worker.join();
    assert(successes == 1);
    PresentEnd(23);
    State().swapchains = 2;
    ObserveConstants(24, 0); ObserveTags(24, 0);
    assert(!CurrentFrame() && !Claim().valid);
    State().swapchains = 1;
    ObserveConstants(25, 0); ObserveTags(25, 0);
    PresentStart(25); assert(Claim(true).valid); PresentEnd(25);
    State().swapchains = 0;
    assert(!Provider().known);
    State().swapchains = 1;
    PublishProvider(true, true);
    const auto provider = Provider();
    assert(provider.known && provider.enabled && provider.supported && provider.generation);
    ObserveConstants(26, 0); ObserveTags(26, 0);
    assert(CurrentFrame() == 27); // Exact provider identity is available while FG is enabled.
    PublishProvider(true, true);
    assert(Provider().generation == provider.generation);
    PublishProvider(true, false);
    assert(Provider().generation > provider.generation && !Provider().supported);
    PublishProvider(false, false);
    assert(!Provider().enabled && !CurrentFrame()); // DD2 dialogue/menu FG-off frames use interval identity.
    State().swapchains = 0;

    NativeFgState nativeFg;
    const auto firstNativeInstance = nativeFg.Create(77);
    assert(firstNativeInstance && nativeFg.Read().active == 1 &&
           nativeFg.Read().instance == firstNativeInstance);
    const auto firstNativeGeneration = nativeFg.Read().generation;
    const auto replacementInstance = nativeFg.Create(77); // reused numeric handle
    assert(replacementInstance > firstNativeInstance && nativeFg.Read().generation > firstNativeGeneration);
    assert(!nativeFg.Release(77, firstNativeInstance));
    assert(nativeFg.Release(77, replacementInstance) && nativeFg.Read().active == 0);
    assert(!nativeFg.Release(0, 0));
    NativeFgState overflow;
    std::array<uint64_t, 8> instances {};
    for (uintptr_t i = 0; i < instances.size(); ++i) instances[i] = overflow.Create(i + 1);
    assert(!overflow.Create(9));
    for (uintptr_t i = 0; i < 7; ++i) assert(overflow.Release(i + 1, instances[i]));
    assert(overflow.Read().active != 1 && !overflow.Read().instance);

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
    assert(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    TestReadiness(device.Get());
    auto* resourceA = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x1000));
    auto* resourceB = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x2000));
    CompletionLedger completions;
    assert(!completions.Reserve(resourceA, 3, 4, 5, 0, 1));
    assert(!completions.Reserve(resourceA, 3, 4, 5, 1, 0));
    // A claimed packet must not disappear while its GPU copyback is outstanding.
    const auto duplicateReservation = completions.Reserve(resourceA, 3, 4, 5, 1, 1);
    assert(completions.Commit(duplicateReservation, resourceA, fence.Get(), 1));
    assert(completions.Claim(resourceA, 3, 4, 5).result == CompletionClaimResult::Ready);
    assert(completions.Claim(resourceA, 3, 4, 5).result == CompletionClaimResult::Refused);
    completions.Reset();
    assert(completions.Claim(resourceA, 4, 4, 5).result == CompletionClaimResult::Refused);
    assert(!completions.Reserve(resourceA, 4, 4, 5, 2, 2));
    assert(SUCCEEDED(fence->Signal(1)));
    completions.Reset();
    const auto reservation = completions.Reserve(resourceA, 3, 4, 5, 6, 7);
    assert(reservation && !completions.Reserve(resourceA, 3, 4, 5, 8, 9));
    assert(!completions.Commit(reservation, resourceB, fence.Get(), 1));
    assert(completions.Commit(reservation, resourceA, fence.Get(), 1));
    auto wrongProvider = completions.Claim(resourceA, 2, 4, 5);
    assert(wrongProvider.result == CompletionClaimResult::Refused && completions.Count() == 1);
    assert(completions.Claim(resourceA, 3, 4, 5).result == CompletionClaimResult::Ready);
    const auto readyReservation = completions.Reserve(resourceA, 3, 4, 5, 8, 9);
    assert(readyReservation && completions.Commit(readyReservation, resourceA, fence.Get(), 2));
    const auto ready = completions.Claim(resourceA, 3, 4, 5);
    assert(ready.result == CompletionClaimResult::Ready && ready.dependency.fence.Get() == fence.Get() &&
           ready.dependency.value == 2 && ready.dependency.token == 8 && ready.dependency.sequence == 9);
    assert(completions.Claim(resourceA, 3, 4, 5).result == CompletionClaimResult::Refused);
    assert(!completions.Reserve(resourceA, 3, 4, 5, 10, 10));
    assert(SUCCEEDED(fence->Signal(2)));
    completions.Reset();
    std::array<uint64_t, 8> pendingReservations {};
    for (uintptr_t i = 1; i <= 8; ++i)
    {
        pendingReservations[i - 1] = completions.Reserve(reinterpret_cast<ID3D12Resource*>(i), 3, 4, 5, i, i);
        assert(pendingReservations[i - 1]);
    }
    assert(!completions.Reserve(reinterpret_cast<ID3D12Resource*>(9), 3, 4, 5, 9, 9));
    completions.Reset();
    assert(completions.Count() == 8); // uncommitted may already have submitted a copyback
    for (auto id : pendingReservations) completions.Cancel(id);
    assert(completions.Count() == 0);

    // Publication racing invalidation retains the fence, but refuses the old frame.
    const auto invalidated = completions.Reserve(resourceA, 3, 4, 5, 12, 12);
    completions.Reset();
    assert(!completions.Commit(invalidated, resourceA, fence.Get(), 3));
    assert(completions.Claim(resourceA, 4, 4, 5).result == CompletionClaimResult::Refused);
    assert(SUCCEEDED(fence->Signal(3)));
    assert(completions.Claim(resourceA, 4, 4, 5).result == CompletionClaimResult::None);
    const auto skipped = completions.Reserve(resourceA, 4, 4, 5, 13, 13);
    assert(!completions.Commit(skipped, resourceA, fence.Get(), UINT64_MAX));
    assert(completions.Commit(skipped, resourceA, fence.Get(), 3));
    assert(!completions.Reserve(resourceA, 4, 4, 5, 13, 13));
    const auto recovered = completions.Reserve(resourceA, 4, 4, 5, 14, 14);
    assert(recovered); // fresh real frame recovers when FG skipped the prior one
    completions.Cancel(recovered);

    PublishProvider(true, true);
    PublishNativeFgCreated(77);
    Frame current;
    current.providerGeneration = Provider().generation;
    current.nativeFgGeneration = NativeFg().generation;
    current.nativeFgInstance = NativeFg().instance;
    current.key = current.sequence = 1;
    PublishProvider(false, false);
    assert(!ReserveCompletion(resourceA, current)); // stale snapshot cannot republish after reset
    PublishProvider(true, true);
    current.providerGeneration = Provider().generation;
    PublishNativeFgCreated(77);
    assert(!ReserveCompletion(resourceA, current)); // same handle, different instance
    current.nativeFgGeneration = NativeFg().generation;
    current.nativeFgInstance = NativeFg().instance;
    const auto live = ReserveCompletion(resourceA, current);
    assert(live && CommitCompletion(live, resourceA, fence.Get(), 4));
    std::atomic<unsigned int> claims {0}, refusals {0};
    workers.clear();
    for (int i = 0; i < 16; ++i)
        workers.emplace_back([&] {
            const auto result = ClaimCompletion(resourceA, current.providerGeneration, 77).result;
            if (result == CompletionClaimResult::Ready) ++claims;
            if (result == CompletionClaimResult::Refused) ++refusals;
        });
    for (auto& worker : workers) worker.join();
    assert(claims == 1 && refusals == 15);
    PublishNativeFgReleased(77, current.nativeFgInstance);
    assert(ClaimCompletion(resourceA, current.providerGeneration, 77).result == CompletionClaimResult::Refused);
    assert(SUCCEEDED(fence->Signal(4)));
    ResetCompletions();

    // NR Off must resume unmodified FG input without changing provider options,
    // recreating FG, or reserving another NR frame. Exercise both claimed and
    // skipped FG packets across repeated Off/On cycles with delayed producers.
    PublishNativeFgCreated(77);
    current.nativeFgGeneration = NativeFg().generation;
    current.nativeFgInstance = NativeFg().instance;
    const auto steadyProvider = Provider();
    const auto steadyNative = NativeFg();
    for (uint64_t cycle = 0; cycle < 32; ++cycle)
    {
        current.key = current.sequence = cycle + 100;
        const auto signalValue = cycle + 5;
        for (auto* resource : {resourceA, resourceB})
        {
            const auto packet = ReserveCompletion(resource, current);
            assert(packet && CommitCompletion(packet, resource, fence.Get(), signalValue));
        }
        assert(ClaimCompletion(resourceA, current.providerGeneration, 77).result == CompletionClaimResult::Ready);
        ResetCompletions(); // production disabled-request Present boundary
        ResetCompletions(); // repeated disabled Present cannot drop unfinished work
        for (auto* resource : {resourceA, resourceB})
            assert(ClaimCompletion(resource, current.providerGeneration, 77).result == CompletionClaimResult::Refused);
        assert(SUCCEEDED(fence->Signal(signalValue)));
        for (auto* resource : {resourceA, resourceB})
            for (int frame = 0; frame < 16; ++frame)
                assert(ClaimCompletion(resource, current.providerGeneration, 77).result == CompletionClaimResult::None);
        assert(!PendingCompletions());
        assert(Provider().generation == steadyProvider.generation);
        assert(NativeFg().generation == steadyNative.generation && NativeFg().instance == steadyNative.instance);
    }
    std::puts("NR Off/On: delayed completion retained, original FG resumed, unchanged provider, 32 cycles PASS");
    Frame outer, inner;
    outer.key = 123; inner.key = 124;
    assert(!forwardingFrame);
    {
        ForwardFrame a(outer);
        assert(forwardingFrame->key == 123);
        { ForwardFrame b(inner); assert(forwardingFrame->key == 124); }
        assert(forwardingFrame->key == 123);
        std::thread other([] { assert(!forwardingFrame); });
        other.join();
    }
    assert(!forwardingFrame);
    std::puts("Pre-FG identity/completion handoff: lifecycle/reuse/bounds/mismatch/claim-once PASS");
}
