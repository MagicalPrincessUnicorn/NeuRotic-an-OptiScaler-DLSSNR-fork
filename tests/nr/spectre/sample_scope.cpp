#include "../context/ContextFixtureStore.h"
#include <nr/contracts/NativeDelivery.h>
#include <nr/context/CapabilityQualification.h>
#if __has_include(<nr/context/NativeSampleQualification.h>)
#include <nr/context/NativeSampleQualification.h>
#define NR_SAMPLE_SCOPE 1
#endif
#include <iostream>
namespace C=Neurotic::Contracts;namespace S=Neurotic::Context;
using namespace NrTest002;
int main()
{
    int failed=0,count=0;
    const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
#ifdef NR_SAMPLE_SCOPE
    NrContextTests::Store store;
    C::NativeSampleIdentityV1 sample;
    sample.session=Id<C::SessionId>("session",1);sample.stream=Id<C::RenderStreamId>("stream",1);
    sample.view=Id<C::ViewId>("view",1);sample.feature=Id<C::ObjectIncarnation>("feature",1);
    sample.ingress=Id<C::ObjectIncarnation>("ingress",1);sample.producerOrdinal=1;sample.callback=Ev(41).record;
    auto frame=NrContextTests::Frame();frame.baseRealFrameId={};frame.nativeSample=store.Publish(sample,C::OwnerDomain::Provider);
    check(!S::SameScope(frame,frame),"N09 generic final frame validator remains strict");
    check(S::SameObservationScope(frame,frame,store),"N10 same known Native sample needs no base frame");
    auto different=frame;auto other=sample;++other.producerOrdinal;
    different.nativeSample=store.Publish(other,C::OwnerDomain::Provider);
    check(!S::SameObservationScope(frame,different,store),"N11 two Unknown bases cannot join different samples");
    auto republished=frame;republished.nativeSample=store.Publish(sample,C::OwnerDomain::Provider);
    check(!S::SameObservationScope(frame,republished,store),"N12 exact current publication remains required even for equal values");
    auto stale=frame;++stale.nativeSample->revision;
    check(!S::SameObservationScope(frame,stale,store),"N12 stale sample metadata refuses");
    auto borrowed=frame;borrowed.nativeSample=store.Publish(sample,C::OwnerDomain::Context);
    check(!S::SameObservationScope(borrowed,borrowed,store),"N12 wrong sample owner refuses");
    auto wrong=frame;wrong.renderStreamId=Known(Id<C::RenderStreamId>("different",1));
    check(!S::SameObservationScope(wrong,wrong,store),"N11 sample and descriptive stream must agree");
    auto evidence=C::EvidenceVector{};
    evidence.nativeFreshness=S::NativeFreshness(*frame.nativeSample,sample);
    check(S::NativeEvidenceFresh(evidence,frame,store),"N10 explicit callback freshness without real-frame age");
    evidence.ageInRealFrames=Known(std::uint64_t{0});
    check(!S::NativeEvidenceFresh(evidence,frame,store),"N09 reject mixed or fabricated age units");
    evidence.ageInRealFrames={};
    check(!S::NativeEvidenceFresh(evidence,different,store),"N11 callback freshness cannot migrate to another sample");
    evidence.nativeFreshness->version=2;
    check(!S::NativeEvidenceFresh(evidence,frame,store),"R07 future freshness version refuses");
#else
    check(false,"N10 explicit Native sample-scope/freshness path absent");
#endif
    std::cout<<"Sample scope: "<<count<<" checks, "<<failed<<" failures; CPU only\n";
    return failed?1:0;
}
