#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace DlssNr
{
enum class BridgeStage
{
    NotObserved,
    ConfigSnapshotFailed,
    Disabled,
    WrongRoute,
    LifecycleClosed,
    MissingInputResource,
    AwaitingModel,
    ModelCreated,
    Evaluated,
    Composed,
    CommandListCloseFailed,
    SubmissionFailed,
    CopyBackFailed,
    CopyBackComplete
};

struct BridgeTelemetrySnapshot
{
    bool observed = false;
    bool eligible = false;
    BridgeStage stage = BridgeStage::NotObserved;
    std::string reason = "waiting for a D3D11 native-bridge frame";
    std::uint64_t handoffs = 0;
    std::uint64_t modelCreations = 0;
    std::uint64_t modelEvaluations = 0;
    std::uint64_t compositions = 0;
    std::uint64_t copyBacks = 0;
};

class BridgeTelemetryTracker
{
  public:
    void Begin(bool configAvailable, bool enabled, bool nativeRoute, bool lifecycleOpen, bool hasInputs,
               const char* lifecycleReason = "")
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _state.observed = true;
        ++_state.handoffs;
        _state.eligible = configAvailable && enabled && nativeRoute && lifecycleOpen && hasInputs;
        if (!configAvailable)
            Set(BridgeStage::ConfigSnapshotFailed, "configuration snapshot could not be captured");
        else if (!enabled)
            Set(BridgeStage::Disabled, "Neural Rendering is disabled");
        else if (!nativeRoute)
            Set(BridgeStage::WrongRoute, "Present Image-Only is selected; the native bridge is idle");
        else if (!lifecycleOpen)
            Set(BridgeStage::LifecycleClosed,
                lifecycleReason != nullptr && lifecycleReason[0] != 0 ? lifecycleReason
                                                                     : "the NR lifecycle is closed for this session");
        else if (!hasInputs)
            Set(BridgeStage::MissingInputResource, "a required shared D3D12 input resource is missing");
        else
            Set(BridgeStage::AwaitingModel, "waiting for model creation or evaluation");
    }

    void RecordNr(std::uint64_t buildsBefore, std::uint64_t buildsAfter,
                  std::uint64_t evaluationsBefore, std::uint64_t evaluationsAfter,
                  std::uint64_t compositionsBefore, std::uint64_t compositionsAfter,
                  const char* failureReason = "")
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_state.eligible)
            return;
        const auto builds = buildsAfter >= buildsBefore ? buildsAfter - buildsBefore : 0;
        const auto evaluations = evaluationsAfter >= evaluationsBefore ? evaluationsAfter - evaluationsBefore : 0;
        const auto compositions = compositionsAfter >= compositionsBefore ? compositionsAfter - compositionsBefore : 0;
        _state.modelCreations += builds;
        _state.modelEvaluations += evaluations;
        _state.compositions += compositions;
        if (compositions != 0)
            Set(BridgeStage::Composed, "model evaluated and NR composition completed");
        else if (evaluations != 0)
            Set(BridgeStage::Evaluated, "model evaluated but composition did not complete");
        else if (builds != 0)
            Set(BridgeStage::ModelCreated, "model created; waiting for its first evaluation");
        else if (failureReason != nullptr && failureReason[0] != 0)
            Set(BridgeStage::AwaitingModel, failureReason);
    }

    void CommandListClosed(bool success)
    {
        if (success) return;
        std::lock_guard<std::mutex> lock(_mutex);
        if (_state.eligible)
            Set(BridgeStage::CommandListCloseFailed, "the bridge command list could not close");
    }

    void Submitted(bool success)
    {
        if (success) return;
        std::lock_guard<std::mutex> lock(_mutex);
        if (_state.eligible)
            Set(BridgeStage::SubmissionFailed, "the bridge command list could not be submitted");
    }

    void CopyBack(bool success)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_state.eligible)
            return;
        if (!success)
            Set(BridgeStage::CopyBackFailed, "the composed output could not be copied back to D3D11");
        else
        {
            ++_state.copyBacks;
            if (_state.stage == BridgeStage::Composed)
                Set(BridgeStage::CopyBackComplete, "model evaluation and composition completed before copy-back");
        }
    }

    BridgeTelemetrySnapshot Snapshot() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _state;
    }

  private:
    void Set(BridgeStage stage, const char* reason)
    {
        _state.stage = stage;
        _state.reason = reason != nullptr ? reason : "unknown bridge state";
    }

    mutable std::mutex _mutex;
    BridgeTelemetrySnapshot _state;
};

inline BridgeTelemetryTracker& BridgeTelemetry()
{
    static BridgeTelemetryTracker tracker;
    return tracker;
}
} // namespace DlssNr
