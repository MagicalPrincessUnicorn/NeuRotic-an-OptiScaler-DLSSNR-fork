#include "../OptiScaler/dlssnr/PreFg.h"
#include "../OptiScaler/dlssnr/NativeFeatureRegistry.h"
#include "../OptiScaler/dlssnr/HdrObservation.h"
#include "../OptiScaler/dlssnr/DlssNr_PresentInputDecision.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

static void TestReadiness(ID3D12Device* device)
{
    using namespace DlssNr::PreFg;
    ConsumerPath consumer;
    ConsumerKey consumerKey {7,8,9};
    assert(!consumer.Ready(consumerKey));
    consumer.key = consumerKey;
    consumer.type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    consumer.observation = std::make_shared<DlssNr::GpuSafety::ExternalExecutionStatus>();
    for (unsigned int i = 0; i < 1000; ++i) assert(!consumer.Ready(consumerKey));
    consumer.observation->evaluated = true;
    assert(!consumer.Ready(consumerKey));
    consumer.observation->submitted = true;
    assert(consumer.Ready(consumerKey));
    assert(!consumer.Ready({8,8,9}) && !consumer.Ready({7,9,9}) && !consumer.Ready({7,8,10}));
    consumer.type = D3D12_COMMAND_LIST_TYPE_COPY;
    assert(!consumer.Ready(consumerKey));
    consumer.type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    assert(consumer.Ready(consumerKey));
    consumer.observation->failed = true;
    assert(!consumer.Ready(consumerKey));
    consumer.Clear();
    assert(!consumer.Ready(consumerKey));
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
    // Match-native workload survives metadata gaps through the existing exact
    // readiness gate. A missing guide never fabricates wait/Present completion.
    {
        namespace D = DlssNr::PresentInputDecision;
        namespace R = DlssNr::PresentResolution;
        auto scalingKey = key;
        scalingKey.width = 2560; scalingKey.height = 1440;
        scalingKey.workWidth = 1704; scalingKey.workHeight = 960;
        StartupReadiness scalingReadiness;
        auto wait = std::make_shared<DlssNr::GpuSafety::ExternalWaitStatus>();
        assert(scalingReadiness.Submit(scalingKey, fence.Get(), 1, 1, wait));
        R::Size admittedWorkload{1704,960};
        for (uint64_t sequence = 2; sequence < 1002; ++sequence)
        {
            const bool fresh = sequence % 2 != 0;
            const auto decision = D::Choose(DlssNr::PresentInput::Policy::AutoGuides,
                fresh, fresh, {R::FollowNative,0});
            const auto workload = D::ResolveWorkload(decision,2560,1440,
                fresh ? 1706 : 0, fresh ? 960 : 0, admittedWorkload);
            auto nextKey = scalingKey;
            nextKey.workWidth = workload.size.width; nextKey.workHeight = workload.size.height;
            assert(!scalingReadiness.Poll(nextKey,device,sequence));
            assert(scalingReadiness.Pending()); // no resize/reprobe loop
            admittedWorkload = workload.size;
        }
        wait->bound = wait->applied = true;
        scalingReadiness.Presented(1,true);
        assert(scalingReadiness.Poll(scalingKey,device,1002));
        const auto changed = D::ResolveWorkload(D::Choose(DlssNr::PresentInput::Policy::AutoGuides,
            true,true,{R::FollowNative,0}),2560,1440,1280,720,admittedWorkload);
        auto resized = scalingKey;
        resized.workWidth = changed.size.width; resized.workHeight = changed.size.height;
        assert(!scalingReadiness.Poll(resized,device,1003)); // actual Native resize revokes proof
        assert(!scalingReadiness.Ready());
        std::puts("Match-native FG workload: 1000 alternating frames (500 metadata gaps), exact proof and resize invalidation PASS");
    }
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
    CHECK_ID(additionalModels); CHECK_ID(additionalRetirements); CHECK_ID(requestedPasses);
    CHECK_ID(hdrIdentity);
#undef CHECK_ID
    prepare(); status->Fail(); assert(!readiness.Ready() && !readiness.Poll(key, device, 3));
    prepare(); readiness.CheckEpoch(key.invalidation + 1); assert(!readiness.Ready());
    prepare(); assert(!readiness.Poll(key, nullptr, 3));
    std::puts("Readiness: 10000 stalled polls, exact proof, next-frame activation, all identities, DD2 replay PASS");
}

static void TestHdrReadiness(ID3D12Device* device, unsigned generated,
    DXGI_FORMAT format, DXGI_COLOR_SPACE_TYPE colorSpace)
{
    using namespace DlssNr::PreFg;
    DlssNr::HdrObservation::Registry registry;
    const auto chain = reinterpret_cast<void*>(uintptr_t(0x3000));
    const auto other = reinterpret_cast<void*>(uintptr_t(0x4000));
    auto* resource = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x5000));
    registry.Register(chain, format);
    registry.Register(other, format);
    registry.RecordColorSpace(chain, colorSpace, S_OK);
    const auto identity = [&](const void* selected) {
        const auto hdr = registry.Read(selected);
        ReadinessIdentity key {};
        key.swapchain = reinterpret_cast<uintptr_t>(selected);
        key.hdrIdentity = hdr.identityGeneration;
        key.colorSpace = hdr.colorSpace;
        key.format = hdr.format;
        return key;
    };
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    StartupReadiness readiness;
    CompletionLedger completions;
    uint64_t value = 0;
    const auto probe = [&] {
        readiness.Reset();
        ++value;
        const auto packet = completions.Reserve(resource, 1, 2, 3, value, value, CompletionKind::Probe, nullptr, true);
        assert(packet && completions.Commit(packet, resource, fence.Get(), value));
        const auto status = completions.Status(packet);
        assert(readiness.Submit(identity(chain), fence.Get(), value, value, status));
        readiness.Presented(value, true);
        for (unsigned child = 1; child <= generated; ++child)
        {
            const auto claim = completions.Claim(resource, 1, 2, 3, value, value, generated, child);
            assert(claim.result == CompletionClaimResult::Ready);
            claim.dependency.status->MarkBound();
            claim.dependency.status->MarkApplied();
            assert(status->applied == (child == generated));
            assert(!readiness.Poll(identity(chain), device, value + 1)); // producer still pending
        }
    };
    const auto revoked = [&] {
        assert(!readiness.Poll(identity(chain), device, value + 1));
        assert(readiness.State() == StartupReadiness::Phase::AwaitingFrame);
        completions.Reset();
        assert(completions.Count() == 1); // readiness revocation never erases unfinished GPU work
        assert(completions.Claim(resource, 1, 2, 3).result == CompletionClaimResult::Refused);
        assert(SUCCEEDED(fence->Signal(value)));
        completions.Reset();
        assert(completions.Count() == 0);
    };

    probe();
    const auto original = identity(chain);
    registry.RecordColorSpace(other, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, S_OK);
    registry.RecordColorSpace(chain, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, E_INVALIDARG);
    registry.RecordMetadata(chain, DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr, E_INVALIDARG);
    registry.RecordMetadata(chain, DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr, S_OK);
    registry.RecordColorSpace(chain, colorSpace, S_OK);
    assert(identity(chain) == original); // failed/repeated calls and metadata do not starve proof
    assert(readiness.Pending());
    assert(SUCCEEDED(fence->Signal(value)));
    assert(readiness.Poll(identity(chain), device, value + 1));
    assert(!readiness.Poll(identity(other), device, value + 2));
    completions.Reset();

    probe();
    registry.RecordColorSpace(chain, colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 ?
        DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, S_OK);
    registry.RecordColorSpace(chain, colorSpace, S_OK);
    revoked(); // same final color still requires a new certificate
    for (const HRESULT result : {S_OK, DXGI_ERROR_INVALID_CALL})
    {
        probe();
        assert(registry.BeginResize(chain).transitioning);
        readiness.CheckIdentity(identity(chain));
        assert(!readiness.Pending());
        const auto resized = registry.CompleteResize(chain, result, DXGI_FORMAT_UNKNOWN);
        assert(!resized.transitioning);
        revoked();
    }
    probe();
    registry.Unregister(chain);
    registry.Register(chain, format);
    registry.RecordColorSpace(chain, colorSpace, S_OK);
    revoked(); // same pointer, format and color are a distinct lifetime

    ConsumerPath consumer;
    ConsumerKey consumerKey {1,2,3};
    for (auto type : {D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE})
    {
        probe();
        consumer.key = consumerKey;
        consumer.type = type;
        consumer.observation = std::make_shared<DlssNr::GpuSafety::ExternalExecutionStatus>();
        consumer.observation->evaluated = consumer.observation->submitted = true;
        assert(consumer.Ready(consumerKey));
        assert(SUCCEEDED(fence->Signal(value)));
        assert(readiness.Poll(identity(chain), device, value + 1));
        completions.Reset();
        ++consumerKey.provider; // unsupported/replaced provider suspends until fresh observation
        assert(!consumer.Ready(consumerKey));
        readiness.Reset();
    }
    registry.Unregister(chain);
    registry.Unregister(other);
    std::printf("HDR/MFG readiness format=%u color=%u generated=%u: color/resize/lifetime invalidation, all child waits and retained GPU ownership PASS\n",
        static_cast<unsigned>(format), static_cast<unsigned>(colorSpace), generated);
}


// A returning old callback must not replace the current frame, and the first
// Present must not discard authenticated callbacks for its provider generation.
static void TestProviderCallbackPublication()
{
    using namespace DlssNr::PreFg;
    Microsoft::WRL::ComPtr<Owner> owner;
    owner.Attach(new Owner(nullptr)); // CPU-only identity fixture, no GPU/provider call.
    PublishProvider(true, true, 1);
    const auto oldGeneration = Provider().generation;
    assert(ObserveConstants(100, 0, oldGeneration));
    assert(ObserveTags(100, 0, oldGeneration));
    PublishProvider(false, false, 1);
    PublishProvider(true, true, 1);
    const auto freshGeneration = Provider().generation;
    assert(freshGeneration > oldGeneration);
    PresentStart(100);
    assert(!Claim(true).valid); // pre-transition records are not new frame proof.
    PresentEnd(100);

    // Resetting at first Present instead of publication would erase this frame.
    assert(ObserveConstants(101, 0, freshGeneration));
    assert(ObserveTags(101, 0, freshGeneration));
    owner->ObserveProvider(true, freshGeneration, 1.0);
    PresentStart(101);
    assert(Claim(true).valid);
    assert(!Claim(true).valid); // preserving records must not permit duplicates.
    PresentEnd(101);

    assert(ObserveConstants(102, 0, freshGeneration));
    assert(ObserveTags(102, 0, freshGeneration));
    // Same numeric frame returned from old in-flight callbacks must neither
    // publish success nor overwrite a current-generation record.
    assert(!ObserveConstants(102, 0, oldGeneration));
    assert(!ObserveTags(102, 0, oldGeneration));
    assert(!ObserveLegacyTags(0, oldGeneration));
    PublishProvider(true, true, 1); // identical options are not a new epoch.
    assert(Provider().generation == freshGeneration);
    owner->ObserveProvider(true, freshGeneration, 2.0);
    PresentStart(102);
    assert(Claim(true).valid);
    PresentEnd(102);

    // Out-of-order future callbacks survive a claim of the earlier frame.
    assert(ObserveConstants(103, 0, freshGeneration));
    assert(ObserveTags(103, 0, freshGeneration));
    assert(ObserveConstants(104, 0, freshGeneration));
    assert(ObserveTags(104, 0, freshGeneration));
    PresentStart(103); assert(Claim(true).valid); PresentEnd(103);
    PresentStart(104); assert(Claim(true).valid); PresentEnd(104);
    assert(ObserveConstants(105, 0, freshGeneration));
    assert(ObserveLegacyTags(0, freshGeneration));
    PresentStart(105); assert(Claim(true).valid); PresentEnd(105);

    // Simulate API entry -> options publication -> old API return on another
    // thread. The existing observer/registry locks are the real synchronization.
    std::atomic<bool> entered {false}, resume {false};
    std::thread returning([&] {
        const auto entryGeneration = Provider().generation;
        entered = true;
        while (!resume.load()) std::this_thread::yield();
        assert(!ObserveConstants(106, 0, entryGeneration));
        assert(!ObserveTags(106, 0, entryGeneration));
        assert(!ObserveLegacyTags(0, entryGeneration));
    });
    while (!entered.load()) std::this_thread::yield();
    PublishProvider(false, false, 1);
    const auto offGeneration = Provider().generation;
    assert(ObserveConstants(106, 0, offGeneration));
    assert(ObserveTags(106, 0, offGeneration));
    resume = true;
    returning.join();
    owner->ObserveProvider(false, offGeneration, 3.0);
    PresentStart(106); assert(Claim(true).valid); PresentEnd(106);

    // Leave the same clean CPU registry initial state expected by existing tests.
    owner.Reset();
    std::lock_guard lock(State().mutex);
    State().ledger.Reset();
    State().provider = {};
    std::puts("Pre-FG provider publication: old callback rejection, first-Present preservation, duplicate/lookahead/legacy and concurrent return PASS");
}

int main()
{
    TestProviderCallbackPublication();
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
    ObserveConstants(23, 0, Provider().generation); ObserveTags(23, 0, Provider().generation);
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
    ObserveConstants(24, 0, Provider().generation); ObserveTags(24, 0, Provider().generation);
    assert(!CurrentFrame() && !Claim().valid);
    State().swapchains = 1;
    ObserveConstants(25, 0, Provider().generation); ObserveTags(25, 0, Provider().generation);
    PresentStart(25); assert(Claim(true).valid); PresentEnd(25);
    State().swapchains = 0;
    assert(!Provider().known);
    State().swapchains = 1;
    PublishProvider(true, true);
    const auto provider = Provider();
    assert(provider.known && provider.enabled && provider.supported && provider.generation);
    ObserveConstants(26, 0, Provider().generation); ObserveTags(26, 0, Provider().generation);
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
    for (unsigned generated = 1; generated <= 5; ++generated)
    {
        TestHdrReadiness(device.Get(), generated, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        TestHdrReadiness(device.Get(), generated, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
        TestHdrReadiness(device.Get(), generated, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
    }
    auto* resourceA = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x1000));
    auto* resourceB = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x2000));
    CompletionLedger completions;
    // One exact packet, one claim per observed provider index. Neither a game
    // request nor an arbitrary repeat is a child association.
    for (unsigned generated = 1; generated <= 5; ++generated) {
        CompletionLedger mfg;
        const auto packet = mfg.Reserve(resourceA, 3, 4, 5, 100, 100, CompletionKind::Probe, nullptr, true);
        assert(packet && mfg.Commit(packet, resourceA, fence.Get(), 1));
        const auto aggregate = mfg.Status(packet);
        if (generated > 1)
            assert(mfg.Claim(resourceA,3,4,5,100,100,generated,2).result == CompletionClaimResult::Refused);
        assert(mfg.Claim(resourceA,3,4,5,101,100,generated,1).result == CompletionClaimResult::Refused);
        for (unsigned child = 1; child <= generated; ++child) {
            auto claim = mfg.Claim(resourceA,3,4,5,100,100,generated,child);
            assert(claim.result == CompletionClaimResult::Ready);
            claim.dependency.status->MarkBound();
            assert(aggregate->bound == (child == generated));
            assert(!aggregate->applied);
            if (child < generated) {
                claim.dependency.status->MarkApplied();
                assert(!aggregate->applied);
            } else {
                claim.dependency.status->MarkApplied();
                assert(aggregate->applied);
                claim.dependency.status->Fail();
                assert(aggregate->failed);
            }
            assert(mfg.Claim(resourceA,3,4,5,100,100,generated,child).result == CompletionClaimResult::Refused);
        }
        assert(mfg.Claim(resourceA,3,4,5,100,100,generated,generated+1).result == CompletionClaimResult::Refused);
    }
    {
        CompletionLedger mfg;
        const auto packet=mfg.Reserve(resourceA,3,4,5,101,101,CompletionKind::Output,nullptr,true);
        assert(mfg.Commit(packet,resourceA,fence.Get(),1));
        assert(mfg.Claim(resourceA,3,4,5,101,101,0,0).result==CompletionClaimResult::Refused);
        assert(mfg.Claim(resourceA,3,4,5,101,101,6,1).result==CompletionClaimResult::Refused);
        assert(mfg.Claim(resourceA,3,4,5,101,101,3,1).result==CompletionClaimResult::Ready);
        assert(mfg.Claim(resourceA,3,4,5,101,101,2,2).result==CompletionClaimResult::Refused);
        assert(mfg.Claim(resourceB,3,4,5,101,101,3,2).result==CompletionClaimResult::None);
        assert(mfg.Claim(resourceA,3,4,6,101,101,3,2).result==CompletionClaimResult::Refused);
        assert(!mfg.Reserve(resourceA,3,4,5,102,102)); // producer still pending
    }
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

    // Runtime capture085556: changing requested count while the old group's
    // second evaluation is pending must retain that input's GPU ordering.
    {
        Microsoft::WRL::ComPtr<ID3D12Fence> transitionFence;
        assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&transitionFence))));
        PublishProvider(true,true,2); PublishNativeFgCreated(99);
        Frame old;
        old.providerGeneration=Provider().generation;
        old.nativeFgGeneration=NativeFg().generation; old.nativeFgInstance=NativeFg().instance;
        old.key=old.sequence=900;
        const auto packet=ReserveCompletion(resourceA,old);
        assert(packet && CommitCompletion(packet,resourceA,transitionFence.Get(),1));
        assert(ClaimCompletion(resourceA,old.providerGeneration,99,2,1).result==CompletionClaimResult::Ready);
        PublishProvider(true,true,3);
        assert(!ReserveCompletion(resourceA,old));
        assert(ClaimCompletion(resourceA,Provider().generation,99,2,2).result==CompletionClaimResult::Ready);
        assert(SUCCEEDED(transitionFence->Signal(1)));
        ResetCompletions(); PublishNativeFgReleased(99,old.nativeFgInstance);
    }

    // Soft settings retirement preserves only the ordered tail of an already
    // started group. Identity, failed waits and hard invalidation still win.
    for (unsigned int generated : {2u,3u,4u,5u})
    for (bool hardReset : {false,true})
    {
        Microsoft::WRL::ComPtr<ID3D12Fence> delayed;
        assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&delayed))));
        CompletionLedger closingLedger;
        const auto packet=closingLedger.Reserve(resourceA,3,4,5,100,101,CompletionKind::Output,nullptr,true);
        assert(closingLedger.Commit(packet,resourceA,delayed.Get(),1));
        const auto first=closingLedger.Claim(resourceA,3,4,5,100,101,generated,1);
        assert(first.result==CompletionClaimResult::Ready);
        closingLedger.CloseAdmission(); closingLedger.CloseAdmission();
        assert(!closingLedger.Status(packet));
        assert(closingLedger.Claim(resourceB,9,4,5,100,101,generated,2).result==CompletionClaimResult::None);
        assert(closingLedger.Claim(resourceA,9,4,5,100,101,generated,1).result==CompletionClaimResult::Refused);
        assert(closingLedger.Claim(resourceA,9,8,5,100,101,generated,2).result==CompletionClaimResult::Refused);
        assert(closingLedger.Claim(resourceA,9,4,8,100,101,generated,2).result==CompletionClaimResult::Refused);
        assert(closingLedger.Claim(resourceA,9,4,5,102,101,generated,2).result==CompletionClaimResult::Refused);
        assert(closingLedger.Claim(resourceA,9,4,5,100,102,generated,2).result==CompletionClaimResult::Refused);
        assert(closingLedger.Claim(resourceA,9,4,5,100,101,generated==2?3:2,2).result==CompletionClaimResult::Refused);
        assert(closingLedger.Claim(resourceA,9,4,5,100,101,generated,3).result==CompletionClaimResult::Refused);
        if (hardReset) closingLedger.Reset();
        for (unsigned child=2;child<=generated;++child) {
            const auto tail=closingLedger.Claim(resourceA,9,4,5,100,101,generated,child);
            assert(tail.result==(hardReset?CompletionClaimResult::Refused:CompletionClaimResult::Ready));
            if (!hardReset) assert(tail.dependency.retiring && tail.dependency.fence==delayed && tail.dependency.value==1);
        }
        assert(closingLedger.Claim(resourceA,9,4,5,100,101,generated,generated).result==CompletionClaimResult::Refused);
        assert(SUCCEEDED(delayed->Signal(1)));
        assert(closingLedger.Claim(resourceA,9,4,5).result==CompletionClaimResult::None);
        const auto unopened=closingLedger.Reserve(resourceA,9,4,5,200,201,CompletionKind::Output,nullptr,true);
        assert(closingLedger.Commit(unopened,resourceA,delayed.Get(),2));
        closingLedger.CloseAdmission();
        assert(closingLedger.Claim(resourceA,9,4,5,200,201,generated,1).result==CompletionClaimResult::Refused);
        assert(SUCCEEDED(delayed->Signal(2)));
        const auto failed=closingLedger.Reserve(resourceA,9,4,5,300,301,CompletionKind::Output,nullptr,true);
        assert(closingLedger.Commit(failed,resourceA,delayed.Get(),3));
        const auto failure=closingLedger.Claim(resourceA,9,4,5,300,301,generated,1);
        closingLedger.CloseAdmission(); failure.dependency.status->Fail();
        assert(closingLedger.Claim(resourceA,10,4,5,300,301,generated,2).result==CompletionClaimResult::Refused);
        assert(SUCCEEDED(delayed->Signal(3)));
    }
    std::puts("MFG settings transition: exact tails, duplicate/identity/failure/hard-reset rejection PASS");

    PublishProvider(true, true);
    PublishNativeFgCreated(77);
    for (auto generated : {1u,2u,3u,5u,4u,2u,1u}) {
        const auto beforeEpoch=State().readinessEpoch.load();
        ObserveGeneratedCount(77,generated,1);
        assert(Provider().observedGenerated==generated && State().readinessEpoch>beforeEpoch);
        const auto steadyEpoch=State().readinessEpoch.load();
        ObserveGeneratedCount(77,generated,1);
        assert(State().readinessEpoch==steadyEpoch);
    }
    ObserveGeneratedCount(77,3,0);
    assert(Provider().observedGenerated==0);
    const auto beforeSettings=Provider().generation;
    PublishProvider(true,true,3);
    assert(Provider().generation>beforeSettings && Provider().observedGenerated==0);
    PublishProvider(true,true);
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
