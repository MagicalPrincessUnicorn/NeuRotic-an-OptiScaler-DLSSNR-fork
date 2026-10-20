#include "../representations/RepresentationFixture.h"
#if __has_include(<nr/contracts/NativeStageDelivery.h>)
#include <nr/contracts/NativeStageDelivery.h>
#include <nr/context/NativeStageQualification.h>
#define NR_STAGE_DELIVERY 1
#endif
using namespace NrTest002;namespace S=Neurotic::Context;
int main()
{
    int failed=0,count=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
#ifdef NR_STAGE_DELIVERY
    auto f=std::make_unique<NrRepresentationTests::Fixture>();
    C::NativeOutputContentV1 content;content.view=f->store.Publish(f->target,C::OwnerDomain::Resource);
    content.recording=Key("recording",1);content.producerOrdinal=3;content.semanticDomain=Sym("SceneLinear");
    check(S::ResolveNativeOutput(content,f->store)!=nullptr,"N02 full canonical resource content and recording binding");
    auto stale=content;++stale.view.revision;
    check(!S::ResolveNativeOutput(stale,f->store),"N02 stale content reference refuses");
    auto invalid=f->target;invalid.identity.contentRevision={};auto unknown=content;
    unknown.view=f->store.Publish(invalid,C::OwnerDomain::Resource);
    check(!S::ResolveNativeOutput(unknown,f->store),"N02 unknown content version refuses");
    unknown=content;unknown.view=f->store.Publish(f->target,C::OwnerDomain::Provider);
    check(!S::ResolveNativeOutput(unknown,f->store),"N02 borrowed provider description cannot impersonate resource owner");
    // Equal target structure does not make a write revision interchangeable.
    auto changed=f->target;changed.identity.contentRevision=Known(C::ContentRevision{2});
    auto newer=content;newer.view=f->store.Publish(changed,C::OwnerDomain::Resource);
    check(S::SameNativeOutputTarget(content,newer,f->store)&&!S::SameNativeOutputContent(content,newer,f->store),
          "N02 exact target versus exact content are separate predicates");
    newer=content;newer.recording=Key("recording",2);
    check(!S::SameNativeOutputContent(content,newer,f->store),"N02 recording reset cannot reuse output receipt");
    content.version=2;check(!S::ResolveNativeOutput(content,f->store),"R07 future output version refuses");
    C::NativeStageDeliveryV1 stage;
    check(!S::ValidValues(stage),"N03 default stage metadata is not successful delivery");
    check(C::EnumName(C::NativeStageOutcome::HostReturnRecorded)=="HostReturnRecorded",
          "N03 explicit local delivery diagnostic vocabulary");
    check(C::EnumName(C::NativeStageOutcome::DeliveryRejected)=="DeliveryRejected",
          "N03 failure is distinct from model result and C13");
#else
    check(false,"N02 exact Native output-content contract missing");
    check(false,"N03 separate Native stage-delivery contract missing");
#endif
    std::cout<<"Stage contracts: "<<count<<" checks, "<<failed<<" failures; CPU metadata only\n";return failed?1:0;
}
