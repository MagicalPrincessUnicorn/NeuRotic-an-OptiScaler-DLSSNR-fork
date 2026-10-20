#define NR_SPECTRE_SOURCE_TESTING
#include <d3d12.h>
#include <wrl/client.h>
#include <dlssnr/NativeFeatureRegistry.h>
#include <cstdlib>
#include <iostream>
namespace DlssNr {class StreamlineSourceScopeTestAccess {
public:
    static auto Frame(unsigned n,unsigned viewport=0) {
        return std::shared_ptr<StreamlineSourceScope::Observation>(new StreamlineSourceScope::Observation(n,viewport));
    }
    static void Complete(StreamlineSourceScope::Observation& value,bool success) {
        value.return_=static_cast<std::uint64_t>(success?StreamlineSourceScope::ReturnStatus::Succeeded:StreamlineSourceScope::ReturnStatus::Failed);
    }
};}
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
int main(){
    using namespace DlssNr;NativeFeatureRegistry<int> registry;registry.Set(10,1);
    auto pin=registry.Pin(10,registry.Read(10));CHECK(pin);
    auto f1=StreamlineSourceScopeTestAccess::Frame(1);auto s1=registry.ObserveTemporalSource(*pin,f1);
    CHECK(s1.source&&s1.source->Valid());CHECK(!s1.predecessor);
    registry.CompleteTemporalSource(*pin,s1,true);StreamlineSourceScopeTestAccess::Complete(*f1,true);
    auto f2=StreamlineSourceScopeTestAccess::Frame(2);auto s2=registry.ObserveTemporalSource(*pin,f2);
    CHECK(s2.predecessor==s1.source);CHECK(s2.source!=s1.source);
    auto duplicate=registry.ObserveTemporalSource(*pin,f2);CHECK(duplicate.duplicate);CHECK(duplicate.source==s2.source);
    registry.CompleteTemporalSource(*pin,s2,true);StreamlineSourceScopeTestAccess::Complete(*f2,false);
    auto s3=registry.ObserveTemporalSource(*pin,StreamlineSourceScopeTestAccess::Frame(3));CHECK(!s3.predecessor);
    CHECK(!registry.ObserveTemporalSource(*pin,nullptr).source);
    auto f4=StreamlineSourceScopeTestAccess::Frame(4);auto s4=registry.ObserveTemporalSource(*pin,f4);CHECK(!s4.predecessor);
    registry.CompleteTemporalSource(*pin,s4,true);StreamlineSourceScopeTestAccess::Complete(*f4,true);
    CHECK(!registry.ObserveTemporalSource(*pin,StreamlineSourceScopeTestAccess::Frame(5,1)).predecessor);
    // Distinct enclosing calls can repeat one source identity. Every relevant
    // enclosing outcome must be successful before a successor is continuous.
    for(int first=0;first<3;++first)for(int repeat=0;repeat<3;++repeat) {
        NativeFeatureRegistry<int> r;r.Set(20,1);auto p=r.Pin(20,r.Read(20));CHECK(p);
        auto original=StreamlineSourceScopeTestAccess::Frame(10);
        auto a=r.ObserveTemporalSource(*p,original);r.CompleteTemporalSource(*p,a,true);
        if(first!=2)StreamlineSourceScopeTestAccess::Complete(*original,first==1);
        auto repeated=StreamlineSourceScopeTestAccess::Frame(10);
        auto b=r.ObserveTemporalSource(*p,repeated);CHECK(b.duplicate&&b.source==a.source);
        r.CompleteTemporalSource(*p,b,true);
        if(repeat!=2)StreamlineSourceScopeTestAccess::Complete(*repeated,repeat==1);
        auto next=r.ObserveTemporalSource(*p,StreamlineSourceScopeTestAccess::Frame(11));
        CHECK(bool(next.predecessor)==(first==1&&repeat==1));
    }
    registry.Set(10,1);CHECK(!registry.ObserveTemporalSource(*pin,StreamlineSourceScopeTestAccess::Frame(6)).source);
    std::cout<<"AFNR source owner PASS "<<checks<<" checks\n";
}
