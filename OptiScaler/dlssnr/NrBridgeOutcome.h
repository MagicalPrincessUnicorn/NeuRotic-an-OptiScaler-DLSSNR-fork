#pragma once
#include <cstdint>
#include <limits>

namespace DlssNr::Bridge
{
// Value evidence only. Native owners must supply completion and currentness.
inline bool FenceReached(std::uint64_t observed, std::uint64_t required)
{
    constexpr auto invalid=(std::numeric_limits<std::uint64_t>::max)();
    return observed!=invalid && required!=invalid && observed>=required;
}
struct Identity
{
    std::uint64_t attempt=0, generation=0, preparedFrame=0, slot=0;
    std::uintptr_t context=0, device=0, queue=0, output=0, sharedOutput=0;
    std::uint64_t configuration=0, lifecycle=0;
    bool operator==(const Identity&) const = default;
    bool Valid() const { return attempt && generation && preparedFrame && context && device && queue && output && sharedOutput; }
};
struct NrWork { bool modelCreated=false, evaluated=false, composed=false, outputRestoreFailed=false; };
struct Receipt
{
    Identity identity;
    bool requested=true, accepted=false, prepared=false, providerEvaluated=false;
    bool nrModelCreated=false, nrEvaluated=false, nrComposed=false, outputProduced=false;
    bool submitted=false, submissionTracked=false, copyBackQueued=false, delivered=false;
    bool gpuCompleted=false, reusable=false, failed=false, originalPreserved=true;
};
class Outcome
{
    Receipt facts_;
  public:
    explicit Outcome(Identity identity) { facts_.identity=identity; facts_.accepted=identity.Valid(); }
    void Prepared() { facts_.prepared=facts_.accepted; }
    void ProviderResult(bool success) { facts_.providerEvaluated=success; facts_.outputProduced=success && facts_.prepared; }
    void NrResult(NrWork work)
    {
        facts_.nrModelCreated |= work.modelCreated;
        facts_.nrEvaluated |= work.evaluated;
        facts_.nrComposed |= work.composed;
        if (work.outputRestoreFailed) Fail();
    }
    void Submitted(bool tracked) { facts_.submitted=true; facts_.submissionTracked=tracked; if (!tracked) Fail(); }
    bool CanCopyBack(Identity current) const
    {
        return current.Valid() && current==facts_.identity && facts_.accepted && facts_.prepared &&
            facts_.outputProduced && facts_.submitted && facts_.submissionTracked && !facts_.failed && !facts_.copyBackQueued;
    }
    bool QueueCopyBack(Identity current)
    {
        if (!CanCopyBack(current)) return false;
        facts_.copyBackQueued=true;
        facts_.originalPreserved=false;
        return true;
    }
    void Delivered() { facts_.delivered=facts_.copyBackQueued && !facts_.failed; }
    void Fail() { facts_.failed=true; facts_.delivered=false; }
    void OriginalMayBeWritten() { facts_.originalPreserved=false; }
    void Retired(bool proven) { facts_.gpuCompleted=proven; facts_.reusable=proven; }
    Receipt Snapshot() const { return facts_; }
};
}
