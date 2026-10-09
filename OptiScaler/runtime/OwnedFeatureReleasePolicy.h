#pragma once
#include <atomic>

namespace Neurotic::Runtime
{
// Actual provider-instance entry is sticky. An opaque failure or exception
// cannot authorize another call from release, backend change or shutdown.
class ProviderReleaseGate
{
    enum class State { Ready, Entered, Complete };
    std::atomic<State> state_{State::Ready};
  public:
    bool Quarantined() const { return state_.load()==State::Entered; }
    template<class Result,class Call>Result CallOnce(Result success,Result unavailable,Call&& call)
    {
        auto expected=State::Ready;
        if(!state_.compare_exchange_strong(expected,State::Entered))
            return expected==State::Complete?success:unavailable;
        try
        {
            const auto result=call();
            if(result==success)state_=State::Complete;
            return result;
        }
        catch(...){return unavailable;} // retain entry quarantine and full owner
    }
};

// Called under the existing native NGX call lease before any owned-create
// state/allocation or caller-handle mutation. This grants no retirement proof.
template<class Contexts,class Handle>bool CanCreateOwnedFeature(const Contexts& contexts,const Handle* caller)
{
    if(caller&&contexts.contains(caller->Id))return false;
    unsigned deferred=0;
    for(const auto& [id,context]:contexts)
    {
        (void)id;
        if(context.ownedReleaseUnresolved||
           (context.feature&&context.feature->ProviderReleaseQuarantined()))return false;
        if constexpr(requires {context.ownedReleaseDeferred;})
            if(context.ownedReleaseDeferred&&++deferred>=16)return false;
    }
    return true;
}
}
