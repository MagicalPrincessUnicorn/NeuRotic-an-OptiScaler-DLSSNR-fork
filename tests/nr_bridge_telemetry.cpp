#include "dlssnr/DlssNr_BridgeTelemetry.h"

#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    DlssNr::BridgeTelemetryTracker tracker;
    tracker.Begin(false, false, false, true, true);
    Check(tracker.Snapshot().stage == DlssNr::BridgeStage::ConfigSnapshotFailed,
          "configuration snapshot rejection is explicit");
    tracker.Begin(true, false, true, true, true);
    Check(tracker.Snapshot().stage == DlssNr::BridgeStage::Disabled, "disabled route is explicit");
    tracker.Begin(true, true, false, true, true);
    Check(tracker.Snapshot().stage == DlssNr::BridgeStage::WrongRoute, "wrong route is explicit");
    tracker.Begin(true, true, true, false, true, "shutdown test");
    Check(tracker.Snapshot().stage == DlssNr::BridgeStage::LifecycleClosed,
          "lifecycle closure is explicit");
    tracker.Begin(true, true, true, true, false);
    Check(tracker.Snapshot().stage == DlssNr::BridgeStage::MissingInputResource,
          "missing input rejection is explicit");

    tracker.Begin(true, true, true, true, true);
    tracker.RecordNr(7, 8, 10, 11, 4, 5);
    auto state = tracker.Snapshot();
    Check(state.stage == DlssNr::BridgeStage::Composed && state.modelCreations == 1 &&
              state.modelEvaluations == 1 && state.compositions == 1,
          "model creation, evaluation, and composition are counted from real deltas");
    tracker.CopyBack(true);
    state = tracker.Snapshot();
    Check(state.stage == DlssNr::BridgeStage::CopyBackComplete && state.copyBacks == 1,
          "copy-back is reported only after composition");

    tracker.Begin(true, true, true, true, true);
    tracker.RecordNr(8, 8, 11, 11, 5, 5, "model did not run");
    tracker.CopyBack(true);
    state = tracker.Snapshot();
    Check(state.stage == DlssNr::BridgeStage::AwaitingModel && state.copyBacks == 2,
          "an untouched upscaler copy-back cannot masquerade as NR composition");
    tracker.Begin(true, true, true, true, true);
    tracker.CopyBack(false);
    Check(tracker.Snapshot().stage == DlssNr::BridgeStage::CopyBackFailed,
          "copy-back failure remains explicit");
    namespace B=DlssNr::Bridge;
    B::Identity identity {1,2,3,0,4,5,6,7,8};
    B::Outcome outcome(identity);
    tracker.Begin(true,true,true,true,true);
    // Even a diagnostic claiming composition cannot authorize a live outcome.
    tracker.RecordNr(0,100,0,100,0,100);
    Check(!outcome.CanCopyBack(identity),"cumulative telemetry cannot authorize output publication");
    outcome.Prepared(); outcome.ProviderResult(true); outcome.Submitted(true);
    Check(outcome.QueueCopyBack(identity),"current upscaler-only attempt enqueues output");
    outcome.Delivered(); tracker.Begin(true,true,true,true,true); tracker.ObserveOutcome(outcome.Snapshot());
    Check(tracker.Snapshot().stage==DlssNr::BridgeStage::AwaitingModel &&
        tracker.Snapshot().receipt.delivered && !tracker.Snapshot().receipt.nrComposed,
        "receipt distinguishes upscaler-only delivery from NR composition");
    auto diagnostic=tracker.Snapshot(); diagnostic.receipt.nrComposed=true;
    Check(!outcome.Snapshot().nrComposed,"diagnostic copy cannot mutate current outcome");
    std::cout << "PASS: 13 bridge route, lifecycle, outcome, and telemetry non-authority checks\n";
}
