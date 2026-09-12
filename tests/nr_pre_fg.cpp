#include "../OptiScaler/dlssnr/PreFg.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
    using namespace DlssNr::PreFg;
    StartupGate startup;
    assert(!startup.Ready());
    // The observed startup had short success bursts interspersed with refusals.
    for (int burst = 1; burst <= 7; ++burst)
    {
        for (int i = 0; i < burst; ++i) { assert(!startup.Ready()); startup.Observe(true); }
        assert(!startup.Ready()); startup.Observe(false);
    }
    for (unsigned int i = 0; i < StartupGate::required; ++i)
    { assert(!startup.Ready()); startup.Observe(true); }
    assert(startup.Ready());
    for (int i = 0; i < 1000; ++i) startup.Observe(true);
    assert(startup.Count() == StartupGate::required);
    startup.Observe(false); assert(!startup.Ready()); // token/model/Present failure
    startup.Observe(true); startup.Reset(); assert(!startup.Count()); // route/resize/off/provider change
    // Exact success/refusal runs from DD2 session 378c3ac68c034ac3bfc4e77f3848281d
    // (68d7b76f, 523 Image Only attempts). Positive = successful model/output;
    // negative = initialization/token refusal. Replay does not predict new GPU timing.
    const int capturedRuns[] = {-7,1,-6,1,-5,1,-4,1,-3,1,-3,1,-2,1,-2,1,-2,1,-2,1,-2,1,
        -3,1,-2,1,-2,2,-3,2,-2,2,-2,2,-2,3,-2,4,-2,6,-2,429};
    unsigned int attempts = 0, published = 0, transitions = 0;
    bool previousPublished = false;
    for (int run : capturedRuns)
        for (int i = 0; i < (run < 0 ? -run : run); ++i)
        {
            ++attempts;
            const bool publish = startup.Ready() && run > 0;
            if (publish) { ++published; assert(attempts >= 103); }
            if (publish != previousPublished) ++transitions;
            previousPublished = publish;
            startup.Observe(run > 0);
        }
    assert(attempts == 523 && published == 421 && transitions == 1);
    Ledger ledger;
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

    State().swapchains = 1;
    ObserveConstants(23, 0); ObserveTags(23, 0);
    std::atomic<int> successes {0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 16; ++i)
        workers.emplace_back([&] { if (Claim().valid) ++successes; });
    for (auto& worker : workers) worker.join();
    assert(successes == 1);
    State().swapchains = 2;
    ObserveConstants(24, 0); ObserveTags(24, 0);
    assert(!CurrentFrame() && !Claim().valid);
    State().swapchains = 1;
    ObserveConstants(25, 0); ObserveTags(25, 0);
    assert(Claim().valid);
    State().swapchains = 0;
    assert(!Provider().known);
    PublishProvider(true, true);
    const auto provider = Provider();
    assert(provider.known && provider.enabled && provider.supported && provider.generation);
    PublishProvider(true, true);
    assert(Provider().generation == provider.generation);
    PublishProvider(true, false);
    assert(Provider().generation > provider.generation && !Provider().supported);
    PublishProvider(false, false);
    assert(!Provider().enabled);

    NativeFgState nativeFg;
    const auto firstNativeInstance = nativeFg.Create(77);
    assert(firstNativeInstance && nativeFg.Read().active == 1 &&
           nativeFg.Read().instance == firstNativeInstance);
    const auto firstNativeGeneration = nativeFg.Read().generation;
    const auto replacementInstance = nativeFg.Create(77); // reused numeric handle
    assert(replacementInstance > firstNativeInstance && nativeFg.Read().generation > firstNativeGeneration);
    assert(!nativeFg.Release(77, firstNativeInstance));
    assert(nativeFg.Release(77, replacementInstance) && nativeFg.Read().active == 0);

    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
    assert(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    auto* resourceA = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x1000));
    auto* resourceB = reinterpret_cast<ID3D12Resource*>(uintptr_t(0x2000));
    CompletionLedger completions;
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
    assert(completions.Claim(resourceA, 3, 4, 5).result == CompletionClaimResult::None);
    for (uintptr_t i = 1; i <= 8; ++i)
        assert(completions.Reserve(reinterpret_cast<ID3D12Resource*>(i), 3, 4, 5, i, i));
    assert(!completions.Reserve(reinterpret_cast<ID3D12Resource*>(9), 3, 4, 5, 9, 9));
    completions.Reset();
    assert(completions.Count() == 0);
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
