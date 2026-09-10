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
    std::cout << "PASS: BG3 bridge route, resource, lifecycle, outcome, and copy-back reporting\n";
}
