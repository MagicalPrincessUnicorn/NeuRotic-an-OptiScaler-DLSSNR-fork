#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace DlssNr::PresentHistory
{
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
