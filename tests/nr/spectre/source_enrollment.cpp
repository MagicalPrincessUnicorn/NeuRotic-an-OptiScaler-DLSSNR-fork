#define NR_SPECTRE_SOURCE_TESTING
#include <iostream>
#include <nr/contracts/FixtureCodec.h>
#include <type_traits>
#if __has_include(<nr/lifecycle/NativeSourceEnrollment.h>)
#include <nr/lifecycle/NativeSourceEnrollment.h>
namespace Neurotic::Lifecycle
{
class NativeSourceTestAccess
{
  public:
    template<class Feature>static auto Open(NativeOwnerSet& owners,DlssNr::NativeFeatureRegistry<Feature>& registry,
        const typename DlssNr::NativeFeatureRegistry<Feature>::CallbackPin& pin)
    {return std::unique_ptr<NativeSourceEnrollment<Feature>>(new NativeSourceEnrollment<Feature>(owners,registry,pin));}
};
}
template<class Source>auto Transaction(const Source& source)
{
    if constexpr(requires{source.Transaction();})return source.Transaction();
    else return std::shared_ptr<const int>{};
}
template<class Record>bool HasSubject(const std::shared_ptr<const Record>& record)
{
    if constexpr(requires{record->id;record->sample;record->episode;record->version;})
        return record&&record->version==1&&record->Check()==Neurotic::Contracts::Error::None&&record->id.value!=0;
    return false;
}
int main()
{
    namespace L=Neurotic::Lifecycle;namespace C=Neurotic::Contracts;
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    C::Symbol name;name.Assign("spectre.source.enrollment.test");L::NativeOwnerSet owners(name);
    DlssNr::NativeFeatureRegistry<unsigned> registry,foreign;
    registry.Set(7,11);foreign.Set(7,11);
    auto pin=registry.Pin(7,registry.Read(7));auto other=foreign.Pin(7,foreign.Read(7));
    auto source=L::NativeSourceTestAccess::Open(owners,registry,*pin);
    const auto sample=source->Observe(*pin);
    check(sample&&sample->Check()==C::Error::None,"N03 actual issuer-backed source sample");
    if(!sample)return 1;
    const auto transaction=Transaction(*source);
    static_assert(!std::is_convertible_v<C::InterceptedSourceTransactionIdV1,C::BaseRealFrameId>);
    static_assert(static_cast<unsigned>(C::IdentityKind::HandoffContractGeneration)==25);
    const auto encoded=C::Codec::Encode(*transaction);
    const auto decoded=C::Codec::Decode<C::InterceptedSourceTransactionV1>(encoded.text);
    check(encoded.error==C::Error::None&&decoded.record&&decoded.record->Value()==*transaction,"CPU-24 new subject round trips with explicit version/type");
    check(!C::Codec::Decode<C::FrameIdentity>(encoded.text).record,"CPU-24 transaction wire cannot masquerade as old FrameIdentity");
    auto future=*transaction;future.version=2;
    check(future.Check()==C::Error::UnsupportedVersion,"CPU-24 unsupported subject version refused");
    check(!transaction->device.IsKnown(),"CPU-05 authentic callback alone leaves device/originality unknown");
    check(owners.Render()->NewBase({owners.IdentityJournal().Event(),L::RenderObservation::Unknown}).status==L::IdentityStatus::Unknown,
        "CPU-24 Unknown still cannot issue original base");
    check(HasSubject(transaction),"CPU-24 authentic disjoint source transaction observation");
    check(source->Observe(*pin)==sample&&Transaction(*source)==transaction,"CPU-01 same pin retains exact transaction observation");
    check(sample->session==*owners.Session()&&sample->stream==source->Enrollment().stream&&sample->view==source->Enrollment().view,
          "N03 sample retains exact enrolled topology");
    check(source->Observe(*pin)==sample,"N04 repeated nested observation preserves exact sample and callback");
    check(!source->Observe(*other),"N03 foreign registry cannot enroll through equal handle and generation");
    auto next=registry.Pin(7,registry.Read(7));const auto newer=source->Observe(*next);
    check(newer&&newer->producerOrdinal>sample->producerOrdinal&&!newer->SameSample(*sample),"N04 genuine next outer callback is a new sample");
    check(Transaction(*source)!=transaction,"CPU-02 independent observed callback is distinct without asserting new content");
    check(!source->Observe(*pin),"N08 old callback cannot move source order backwards");
    registry.Set(7,11);
    auto replacement=registry.Pin(7,registry.Read(7));
    check(!source->Observe(*next)&&!source->Observe(*replacement),"N08 replaced feature cannot join existing source enrollment");
    auto replaced=L::NativeSourceTestAccess::Open(owners,registry,*replacement);
    const auto replacementSample=replaced->Observe(*replacement);
    check(replacementSample&&replacementSample->feature!=sample->feature&&replacementSample->ingress!=sample->ingress,
          "N08 genuine new enrollment has new canonical feature and ingress identities");
    const auto retained=Transaction(*replaced);
    replaced->Close();
    check(Transaction(*replaced)==retained&&HasSubject(retained),"CPU-08 closure preserves retained observation without retirement");
    check(!replaced->Observe(*replacement),"L15 closed source enrollment never reopens");
    bool rejected=false;try{auto invalid=L::NativeSourceTestAccess::Open(owners,registry,*other);(void)invalid;}catch(...){rejected=true;}
    check(rejected,"N03 enrollment rejects foreign source before publication");
    std::cout<<"Source enrollment: "<<count<<" checks, "<<failed<<" failures; CPU canonical owner only\n";
    return failed?1:0;
}
#else
int main(){std::cerr<<"FAIL N03 authentic source enrollment missing\n";return 1;}
#endif
