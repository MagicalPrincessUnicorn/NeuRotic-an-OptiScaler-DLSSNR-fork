#pragma once
#include "NrPreflightSignals.h"
#include "NrDiagnosticSignals.h"
#include <cmath>
#include <optional>
#include <cstdint>
#include <cstdio>
#include <string>

namespace DlssNr::JitterDisplay
{
// Hold numeric display samples only. Availability/status still follows live data.
class ReceiptSnapshot
{
    std::optional<SignalReceipts::Fact> value_;
    SignalReceipts::Epoch epoch_{};
    uint64_t published_=0,lastSeen_=0,sequence_=0;
public:
    const std::optional<SignalReceipts::Fact>& Update(const SignalReceipts::Reading& r,bool eligible,uint64_t now)
    {
        const bool changed=r.epoch!=epoch_||r.sequence<sequence_||now<lastSeen_;
        epoch_=r.epoch;sequence_=r.sequence;lastSeen_=now;
        if(!eligible||!DiagnosticSignals::Current(r)||!r.fact.valuesKnown||
            !std::isfinite(r.fact.x)||!std::isfinite(r.fact.y))value_.reset();
        else if(changed||!value_||now-published_>=500||value_->stage!=r.fact.stage||
            value_->width!=r.fact.width||value_->height!=r.fact.height)
        {value_=r.fact;published_=now;}
        return value_;
    }
};
struct Context
{
    uintptr_t owner = 0;
    int api = 0;
    uint32_t route = 0;
    uint64_t profile = 0, lifecycle = 0, resource = 0;
    uint64_t resets = 0, builds = 0, rebuilds = 0;
    bool operator==(const Context&) const = default;
};

// UI-only projection. The owner continues recording every input.
class Snapshot
{
public:
    const std::string& Update(const Context& context, const NrPreflightSignals::NativeInputs& input,
                              bool observed, double now)
    {
        const bool changed = !seen_ || context != context_ || input.observations < sequence_ || now < lastSeen_;
        context_ = context; sequence_ = input.observations; lastSeen_ = now; seen_ = true;
        if (!observed || !input.observations)
        {
            valid_ = false;
            text_ = "No Native DLSS input observed";
        }
        else if (!input.jitter)
        {
            valid_ = false;
            text_ = input.jitterSupplied ? "Supplied but incomplete or non-finite" :
                "Not supplied on last Native DLSS input";
        }
        else if (changed || !valid_ || now - published_ >= 2.0)
        {
            char text[96] {};
            std::snprintf(text, sizeof(text), "X %.4g, Y %.4g (last Native DLSS input)",
                          input.jitter->x, input.jitter->y);
            text_ = text;
            published_ = now; valid_ = true;
        }
        return text_;
    }
private:
    std::string text_;
    Context context_ {};
    uint64_t sequence_ = 0;
    double lastSeen_ = 0, published_ = 0;
    bool seen_ = false, valid_ = false;
};
}
