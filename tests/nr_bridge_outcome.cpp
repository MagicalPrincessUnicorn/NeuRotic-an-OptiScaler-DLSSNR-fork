#include "dlssnr/NrBridgeOutcome.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static unsigned checks = 0;
static void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main()
{
    namespace B = DlssNr::Bridge;
    const auto lost = (std::numeric_limits<std::uint64_t>::max)();
    Check(!B::FenceReached(lost, 7), "device-removal sentinel cannot grant retirement");
    Check(!B::FenceReached(lost, 0), "device-removal sentinel cannot grant idle reuse");
    Check(!B::FenceReached(6, 7), "unfinished consumer retains its generation");
    Check(B::FenceReached(7, 7), "exact consumer completion permits retirement");
    Check(!B::FenceReached(7, lost), "reserved fence target cannot be submitted");

    B::Identity current {1, 2, 3, 0, 4, 5, 6, 7, 8};
    B::Outcome output(current);
    Check(!output.CanCopyBack(current), "request alone cannot publish an output");
    output.Prepared();
    output.ProviderResult(true);
    Check(!output.CanCopyBack(current), "evaluation without submission cannot publish");
    output.Submitted(true);
    Check(output.CanCopyBack(current), "current upscaler-only output is publishable");
    Check(!output.Snapshot().nrComposed, "upscaler-only output does not claim NR composition");
    for (unsigned field = 0; field != 11; ++field)
    {
        auto stale = current;
        switch (field) {
        case 0: ++stale.attempt; break;
        case 1: ++stale.generation; break;
        case 2: ++stale.preparedFrame; break;
        case 3: ++stale.slot; break;
        case 4: ++stale.context; break;
        case 5: ++stale.device; break;
        case 6: ++stale.queue; break;
        case 7: ++stale.output; break;
        case 8: ++stale.sharedOutput; break;
        case 9: ++stale.configuration; break;
        case 10: ++stale.lifecycle; break;
        }
        Check(!output.CanCopyBack(stale), "changed attempt/frame/slot/owner/output refuses publication");
    }
    auto beforeFailure = output.Snapshot();
    output.Fail();
    Check(output.Snapshot().originalPreserved, "pre-copyback failure preserves original output");
    Check(!output.CanCopyBack(current), "a prior successful receipt cannot authorize failure");
    Check(!beforeFailure.copyBackQueued, "published diagnostic copy cannot mutate live outcome");

    B::Outcome queued(current);
    queued.Prepared(); queued.ProviderResult(true); queued.Submitted(true);
    Check(queued.QueueCopyBack(current), "only the current submitted attempt can enqueue copyback");
    Check(!queued.QueueCopyBack(current), "one attempt cannot copy back twice");
    queued.Fail();
    Check(!queued.Snapshot().originalPreserved, "post-copyback failure never claims untouched original");
    Check(!queued.Snapshot().delivered, "uncertain copyback cannot report delivery");

    B::Outcome failedSignal(current);
    failedSignal.Prepared(); failedSignal.ProviderResult(true); failedSignal.Submitted(false);
    Check(failedSignal.Snapshot().submitted, "post-execute signal failure retains submission fact");
    Check(!failedSignal.CanCopyBack(current), "untrackable submission refuses publication");

    B::Outcome composed(current);
    composed.Prepared(); composed.ProviderResult(true); composed.NrResult({true,true,true});
    composed.Submitted(true); Check(composed.QueueCopyBack(current), "NR output can enqueue when current");
    composed.Delivered();
    Check(composed.Snapshot().nrComposed && composed.Snapshot().delivered,
          "observed composition and queued delivery stay distinct from GPU completion");
    Check(!composed.Snapshot().gpuCompleted, "queued delivery does not invent GPU completion");
    composed.Retired(true);
    Check(composed.Snapshot().gpuCompleted && composed.Snapshot().reusable,
          "owner-proven terminal completion reports retirement");
    B::Outcome invalid({}); invalid.Prepared(); invalid.ProviderResult(true); invalid.Submitted(true);
    Check(!invalid.CanCopyBack({}), "empty identity cannot authorize copyback");
    B::Outcome uncertainBinding(current); uncertainBinding.Prepared();
    uncertainBinding.NrResult({false,false,false,true});
    uncertainBinding.ProviderResult(true); uncertainBinding.Submitted(true);
    Check(!uncertainBinding.CanCopyBack(current), "failed NR output restoration refuses outer provider publication");
    B::Outcome nativeShared(current); nativeShared.OriginalMayBeWritten(); nativeShared.Fail();
    Check(!nativeShared.Snapshot().originalPreserved,"shared native output may be written before explicit copyback");
    std::printf("PASS: %u bridge outcome and fence checks (CPU value evidence)\n", checks);
}
