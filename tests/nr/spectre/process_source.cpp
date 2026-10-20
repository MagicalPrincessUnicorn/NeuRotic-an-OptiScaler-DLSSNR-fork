#define NR_SPECTRE_SOURCE_TESTING
#include <iostream>
#include <type_traits>
#include <vector>
#if __has_include(<nr/lifecycle/NativeProcessBootstrap.h>)
#include <nr/lifecycle/NativeProcessBootstrap.h>
namespace Neurotic::Lifecycle
{
class NativeProcessTestAccess
{
  public:
    static auto Open(){return std::unique_ptr<NativeProcessBootstrap>(new NativeProcessBootstrap);}
    template<class Feature>static auto Capture(NativeProcessBootstrap& host,DlssNr::NativeFeatureRegistry<Feature>& registry,
        typename DlssNr::NativeFeatureRegistry<Feature>::CallbackPin&& pin)
    {return host.Capture(registry,std::move(pin));}
    static void Close(NativeProcessBootstrap& host){host.CloseSource();}
};
}
int main()
try
{
    namespace L=Neurotic::Lifecycle;namespace C=Neurotic::Contracts;
    int count=0,failures=0;const auto check=[&](bool value,const char* text){++count;if(!value){++failures;std::cerr<<"FAIL "<<text<<'\n';}};
    static_assert(!std::is_default_constructible_v<L::NativeProcessBootstrap>);
    static_assert(!std::is_copy_constructible_v<L::NativeProcessBootstrap::Callback>);
    auto host=L::NativeProcessTestAccess::Open();
    DlssNr::NativeFeatureRegistry<unsigned> registry,foreign;registry.Set(7,11);foreign.Set(7,11);
    auto pin=registry.Pin(7,registry.Read(7));
    auto first=L::NativeProcessTestAccess::Capture(*host,registry,std::move(*pin));
    check(first&&first->Current(),"actual source pin retained by bootstrap callback");
    if(!first)return 1;
    const auto original=first->Sample();const auto scope=first->Scope();
    check(original.Check()==C::Error::None&&original.session==scope.session&&original.stream==scope.stream&&original.view==scope.view,
          "canonical session and source enrollment agree");
    check(!registry.BeginRelease(7,registry.Read(7)),"retained bootstrap callback blocks physical source release");
    auto foreignPin=foreign.Pin(7,foreign.Read(7));
    check(!L::NativeProcessTestAccess::Capture(*host,foreign,std::move(*foreignPin)),"foreign source registry cannot reconstruct the same facade");
    auto nextPin=registry.Pin(7,registry.Read(7));
    auto second=L::NativeProcessTestAccess::Capture(*host,registry,std::move(*nextPin));
    check(second&&second->Scope()==scope&&second->Sample().feature==original.feature&&second->Sample().ingress==original.ingress&&
          !second->Sample().SameSample(original),"next sample preserves sole enrolled owner");
    check(first->Sample()==original&&first->Current(),"in-flight capture is immutable after a later callback");
    auto anotherHost=L::NativeProcessTestAccess::Open();auto anotherPin=foreign.Pin(7,foreign.Read(7));
    auto another=L::NativeProcessTestAccess::Capture(*anotherHost,foreign,std::move(*anotherPin));
    check(another&&another->Sample().session!=original.session,"cold OS namespaces do not reuse session identities");
    first.reset();second.reset();
    auto release=registry.BeginRelease(7,registry.Read(7));
    check(release.has_value(),"callback destruction returns exact source borrow");
    if(release)release->Complete(false);
    std::vector<L::NativeProcessBootstrap::Callback> retained;
    for(std::size_t i=0;i<L::NativeProcessBootstrap::MaximumCallbacks;++i)
    {
        auto p=registry.Pin(7,registry.Read(7));auto c=L::NativeProcessTestAccess::Capture(*host,registry,std::move(*p));
        if(c)retained.push_back(std::move(*c));
    }
    auto fullPin=registry.Pin(7,registry.Read(7));
    check(retained.size()==L::NativeProcessBootstrap::MaximumCallbacks&&
          !L::NativeProcessTestAccess::Capture(*host,registry,std::move(*fullPin)),"source callback capacity is bounded");
    retained.front()=std::move(retained.back());retained.pop_back();
    auto recoveredPin=registry.Pin(7,registry.Read(7));auto recovered=L::NativeProcessTestAccess::Capture(*host,registry,std::move(*recoveredPin));
    check(recovered.has_value(),"move assignment drops one reservation without losing the surviving pin");
    L::NativeProcessTestAccess::Close(*host);
    check(recovered&&!recovered->Current()&&!retained.front().Current(),"source closure revokes every live capture");
    retained.pop_back(); // ensure capacity cannot mask a missing closure gate
    auto closedPin=registry.Pin(7,registry.Read(7));
    check(!L::NativeProcessTestAccess::Capture(*host,registry,std::move(*closedPin)),"source facade never reopens spent scope");
    retained.clear();recovered.reset();
    check(registry.StopCallbacks(),"closed callback destruction drains source pins only");
    std::cout<<"Process source: "<<count<<" checks, "<<failures<<" failures; CPU source/OS namespace only\n";
    return failures?1:0;
}
catch(const std::exception& e){std::cerr<<"INFRA_ERROR "<<e.what()<<'\n';return 2;}
#else
int main(){std::cerr<<"FAIL authentic process source bootstrap is absent\n";return 1;}
#endif
