#pragma once
#include <cstdint>

namespace DlssNr::NativeDx11
{
// Own one admitted evaluation, including a partially throwing Prepare. Parameters
// must be restored before completion retires any Pre-SR input reservation.
template <typename Feature, typename Context, typename Parameters> class Evaluation
{
    Feature& feature;
    Context* context;
    Parameters* parameters;
    bool active = false;
    std::uint64_t attempt = 0;
  public:
    Evaluation(Feature& feature, Context* context, Parameters* parameters)
        : feature(feature), context(context), parameters(parameters) {}
    void Begin()
    {
        if (active) return;
        attempt = feature.AcquireAttempt();
        if (!attempt) return; // an overlapping caller never owns the live reservation
        active = true;
        feature.Prepare(context, parameters, attempt);
    }
    void Complete(bool succeeded)
    {
        if (!active) return;
        active = false;
        try { feature.Restore(parameters, attempt); }
        catch (...)
        {
            feature.Complete(context, false, attempt);
            throw;
        }
        feature.Complete(context, succeeded, attempt);
    }
    ~Evaluation() noexcept
    {
        // Preserve an original upscaler/Prepare exception while still retiring
        // the admitted attempt. Never retry a completion which itself threw.
        try { Complete(false); } catch (...) {}
    }
    Evaluation(const Evaluation&) = delete;
    Evaluation& operator=(const Evaluation&) = delete;
};
}
