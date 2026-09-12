#include "../OptiScaler/dlssnr/PreFg.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
    using namespace DlssNr::PreFg;
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
    std::puts("Pre-FG identity: missing/stale/future/duplicate/viewport/resize/wrap/concurrency PASS");
}
