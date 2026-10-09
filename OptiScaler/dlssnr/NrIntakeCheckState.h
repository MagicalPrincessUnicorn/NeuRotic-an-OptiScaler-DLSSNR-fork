#pragma once
#include "../nr/diagnostics/capability/CapabilityRefresh.h"

namespace DlssNr {
// Presentation state only. The observation service retains sole collection ownership.
class IntakeCheckState {
public:
    enum class Result { Idle, Checking, Complete, Unavailable, Changed };
private:
    std::optional<Capability::RefreshState> context;
    uint64_t operation = 0;
    Result result = Result::Idle;
public:
    void Request(const Capability::RefreshState& current, uint64_t lastOperation)
    {
        context = current;
        operation = lastOperation;
        result = Result::Checking;
    }
    Result Observe(const Capability::RefreshState& current, bool busy,
                   uint64_t completedOperation, bool manual)
    {
        if (context && *context != current)
        {
            context.reset();
            result = Result::Changed;
        }
        if (!context || result == Result::Complete) return result;
        if (manual && completedOperation > operation) result = Result::Complete;
        else result = busy ? Result::Checking : Result::Unavailable;
        return result;
    }
};
}
