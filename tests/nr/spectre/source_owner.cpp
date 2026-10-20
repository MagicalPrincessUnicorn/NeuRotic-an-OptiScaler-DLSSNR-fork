#define NR_SPECTRE_SOURCE_TESTING
#include <dlssnr/NativeFeatureRegistry.h>
#include <iostream>
#include <type_traits>
#include <limits>

namespace DlssNr
{
class NativeFeatureRegistryTestAccess
{
  public:
    template<class Registry>static void Exhaust(Registry& value)
    {
        if constexpr(requires{value.generation;})value.generation=(std::numeric_limits<uint64_t>::max)();
    }
};
}

template<class Registry>int Cases()
{
    int count=0,failed=0;
    const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    Registry registry;registry.Set(7,11u);const auto original=registry.Read(7);
    if constexpr(requires{registry.Pin(7,original);registry.BeginRelease(7,original);registry.Protected(7);})
    {
        using Pin=typename Registry::CallbackPin;
        static_assert(!std::is_default_constructible_v<Pin> && !std::is_copy_constructible_v<Pin>);
        auto pin=registry.Pin(7,original);
        check(pin&&pin->Current()&&pin->Value().generation==original.generation,"N03 actual source owner pin");
        if constexpr(requires{pin->Ordinal();pin->Handle();pin->BelongsTo(registry);})
        {
            auto nested=registry.Pin(7,original);Registry foreign;
            const auto ordinal=pin->Ordinal();
            check(ordinal!=0&&nested->Ordinal()>ordinal&&pin->Ordinal()==ordinal,
                  "N04 one outer source pin retains one nonreused ordinal");
            check(pin->Handle()==7&&pin->BelongsTo(registry)&&!pin->BelongsTo(foreign),
                  "N03 source pin identifies its authentic registry and handle");
            auto moved=std::move(*nested);
            check(moved.Ordinal()>ordinal&&moved.Current()&&!nested->Current(),"N08 moved source pin has one live owner");
        }
        else check(false,"N04 source ordinal missing");
        check(registry.Protected(7),"N03 participation retained for release after config toggle");
        check(!registry.BeginRelease(7,original),"L03 live callback blocks physical feature release");
        check(!registry.Released(7,original,true),"L03 legacy completion cannot bypass pinned source");
        registry.Set(7,13u);const auto replacement=registry.Read(7);
        check(!pin->Current()&&!registry.Pin(7,original),"N08 replacement invalidates stale ingress");
        check(!registry.BeginRelease(7,replacement),"L03 replacement cannot release while old callback is live");
        pin.reset();
        auto release=registry.BeginRelease(7,replacement);
        check(release.has_value()&&!registry.Pin(7,replacement),"L03 release closes new callback admission");
        check(release&&!release->Complete(false)&&registry.Pin(7,replacement).has_value(),"L03 failed release reopens same authentic feature");
        release=registry.BeginRelease(7,replacement);
        check(release&&release->Complete(true)&&!registry.Read(7),"L03 successful release removes exact source");
        check(!registry.Pin(7,replacement),"N08 released feature cannot admit callback");
        registry.Set(7,11u);const auto next=registry.Read(7);
        auto abandoned=registry.BeginRelease(7,next);abandoned.reset();
        check(!registry.Pin(7,next),"L03 indeterminate release remains closed");
        registry.Set(7,13u);const auto newest=registry.Read(7);
        check(newest.generation!=next.generation&&!registry.Pin(7,newest),"N08 replacement cannot erase indeterminate physical release");
        registry.Set(8,11u);const auto other=registry.Read(8);
        auto firstRelease=registry.BeginRelease(8,other);
        check(firstRelease&&!registry.Pin(8,other),"L03 release before first candidate callback also excludes ingress");
        registry.Set(8,13u);
        check(firstRelease&&!firstRelease->Complete(true)&&!registry.Pin(8,registry.Read(8)),
              "N08 stale physical release result cannot certify replacement lifetime");
        Registry shutdown;shutdown.Set(9,11u);const auto last=shutdown.Read(9);
        if constexpr(requires{shutdown.StopCallbacks();})
        {
            auto pending=shutdown.Pin(9,last);
            check(!shutdown.StopCallbacks()&&!pending->Current()&&!shutdown.Pin(9,last),
                  "L15 shutdown revokes ingress and refuses physical stop with live callback");
            pending.reset();
            check(shutdown.StopCallbacks()&&!shutdown.Pin(9,last),"L15 callback drain does not reopen stopped source");
            shutdown.Set(10,13u);
            check(!shutdown.Pin(10,shutdown.Read(10)),"L15 new handle cannot reopen stopped source session");
            Registry releasing;releasing.Set(20,11u);
            auto inRelease=releasing.BeginRelease(20,releasing.Read(20));
            check(inRelease&&!releasing.StopCallbacks(),"L15 physical release excludes concurrent provider shutdown");
            inRelease->Complete(false);
            check(releasing.StopCallbacks(),"L15 definitive release failure permits closed source drain");
        }
        else check(false,"L15 source shutdown exclusion missing");
        Registry exhausted;exhausted.Set(11,11u);const auto prior=exhausted.Read(11);
        auto priorPin=exhausted.Pin(11,prior);
        DlssNr::NativeFeatureRegistryTestAccess::Exhaust(exhausted);
        exhausted.Set(11,13u);
        check(!exhausted.Read(11)&&!priorPin->Current(),"N08 feature generation exhaustion cannot alias earlier incarnation");
        exhausted.Set(12,11u);
        check(!exhausted.Read(12),"N08 generation exhaustion stays terminal");
        if constexpr(requires{registry.BeginShutdown();})
        {
            Registry closing;closing.Set(30,11u);auto active=closing.Pin(30,closing.Read(30));
            check(!closing.BeginShutdown()&&!active->Current(),"L15 busy shutdown closes ingress without entering provider");
            active.reset();auto shutdownGuard=closing.BeginShutdown();
            check(shutdownGuard.has_value(),"L15 drained source owns one physical shutdown reservation");
            check(!closing.BeginShutdown(),"L15 duplicate shutdown cannot enter provider twice");
            check(!closing.BeginRelease(30,closing.Read(30)),"L15 new feature release cannot enter reserved shutdown");
            shutdownGuard->Complete();
            check(!closing.Pin(30,closing.Read(30)),"L15 physical shutdown return never reopens Native ingress");
            auto legacy=closing.BeginRelease(30,closing.Read(30));
            check(legacy.has_value(),"L15 legacy release remains possible after definitive shutdown return");
            if(legacy)legacy->Complete(false);
            shutdownGuard=closing.BeginShutdown();shutdownGuard.reset();
            check(!closing.BeginShutdown()&&!closing.BeginRelease(30,closing.Read(30)),"L15 indeterminate shutdown retains exclusion");
        }
        else check(false,"L15 exclusive shutdown reservation missing");
    }
    else check(false,"N03 callback-scoped feature ownership missing");
    std::cout<<"Source owner: "<<count<<" checks, "<<failed<<" failures; CPU registry lifetime only\n";
    return failed?1:0;
}
int main(){return Cases<DlssNr::NativeFeatureRegistry<unsigned int>>();}
