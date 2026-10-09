#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace DlssNr::PresentHistory
{
// The original Present/Present1 result has one owning callback per invocation.
// Ignore duplicate or delayed callbacks before either pacing or history changes.
class HostReturnGate
{
    uint64_t lastCall = 0;
  public:
    bool Accept(uint64_t call) noexcept
    {
        if (call == 0 || call <= lastCall) return false;
        lastCall = call;
        return true;
    }
};
// Present Image-Only has no trustworthy game reset signal.  Only a sequence of completed output
// Presents can share Feature 18 history; every interruption starts the next admitted frame fresh.
class Continuity
{
  public:
    bool ResetForNextEvaluation() const noexcept { return _resetPending; }
    uint64_t CompletedFrames() const noexcept { return _completedFrames; }
    const std::string& ResetReason() const noexcept { return _resetReason; }
    const std::string& LastInvalidationReason() const noexcept { return _lastInvalidationReason; }

    void Invalidate(std::string_view reason)
    {
        _resetPending = true;
        _completedFrames = 0;
        _resetReason.assign(reason);
        _lastInvalidationReason.assign(reason);
    }

    void CompleteOutputPresent() noexcept
    {
        _resetPending = false;
        ++_completedFrames;
    }

  private:
    bool _resetPending = true;
    uint64_t _completedFrames = 0;
    std::string _resetReason = "initial Present frame";
    std::string _lastInvalidationReason;
};
} // namespace DlssNr::PresentHistory
