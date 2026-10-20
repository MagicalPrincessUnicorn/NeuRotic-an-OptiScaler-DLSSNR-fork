#include "finalizer_fixture.h"
#if __has_include(<nr/lifecycle/FinalRealFramePreFgAdapter.h>)
#include <nr/lifecycle/FinalRealFramePreFgAdapter.h>
#define FINAL_PREFG_AVAILABLE 1
#endif
using namespace FinalizerTests;
template<class Finalizer> void ClaimPair(Report& r,Finalizer& finalizer,const L::PrimaryNrClaimHandle& a,const C::FinalSealId& sealA,const L::PrimaryNrClaimHandle& b,const C::FinalSealId& sealB)
{
    if constexpr(requires {FinalPreFgClaimMatches(finalizer,a,sealA);})
    {
        r.Check("T28P.claim-a-matches",FinalPreFgClaimMatches(finalizer,a,sealA),true,"FinalPreFgClaimMatches");
        r.Check("T28P.claim-b-wrong-seal",FinalPreFgClaimMatches(finalizer,b,sealA),false,"FinalPreFgClaimMatches");
        r.Check("T28P.claim-b-matches",FinalPreFgClaimMatches(finalizer,b,sealB),true,"FinalPreFgClaimMatches");
    }
    else r.Check("T28P.claim-seal-api",false,true,"FinalPreFgClaimMatches");
}
int main(int argc,char** argv)
{
    return Guard([&]{Report r("finalizer-prefg",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef FINAL_PREFG_AVAILABLE
        auto f=std::make_unique<Fixture>();auto c=f->Claim();f->Produce(*c.claim);f->Prepare(*c.claim);auto s=f->Commit(*c.claim);
        L::ProviderLeaseBinding binding;binding.resource=f->binding.description.resource;binding.provider=f->boundary.provider;
        binding.handoff={f->request.handoff.Describe()};binding.consumer=f->binding.consumer;
        binding.ledgerKey={7,8,9};binding.token=41;binding.sequence=42;
        auto snapshot=f->finalizer->Snapshot(s.packet->seal);
        r.Check("T28P.exact-canonical-mapping",L::FinalPreFgBindingMatches(*snapshot,binding,f->Proof(),f->store),true,"FinalPreFgBindingMatches");
        auto wrong=binding;wrong.provider.value=2;
        r.Check("T28P.foreign-provider",L::FinalPreFgBindingMatches(*snapshot,wrong,f->Proof(),f->store),false,"FinalPreFgBindingMatches");
        wrong=binding;wrong.resource.contentRevision=Known(C::ContentRevision{2});
        r.Check("T28P.foreign-content",L::FinalPreFgBindingMatches(*snapshot,wrong,f->Proof(),f->store),false,"FinalPreFgBindingMatches");
        wrong=binding;wrong.handoff.identity.value=2;
        r.Check("T28P.foreign-handoff",L::FinalPreFgBindingMatches(*snapshot,wrong,f->Proof(),f->store),false,"FinalPreFgBindingMatches");
        wrong=binding;wrong.consumer=Key("foreign.consumer");
        r.Check("T28P.foreign-consumer",L::FinalPreFgBindingMatches(*snapshot,wrong,f->Proof(),f->store),false,"FinalPreFgBindingMatches");
        wrong=binding;wrong.token=0;
        r.Check("T28P.unknown-provider-token",L::FinalPreFgBindingMatches(*snapshot,wrong,f->Proof(),f->store),false,"FinalPreFgBindingMatches");
        DlssNr::PreFg::CompletionLedger ledger;
        r.Check("T28P.null-resource-no-reserve",L::ReserveFinalPreFg(ledger,nullptr,binding)==0,true,"ReserveFinalPreFg");
        auto frame=*f->store.Resolve(f->request.recipe.frame).value;frame.viewId=Known(Id<C::ViewId>("view",2));f->Frame(frame);
        auto second=f->Claim();f->Produce(*second.claim);f->Prepare(*second.claim);auto secondSeal=f->Commit(*second.claim);
        ClaimPair(r,*f->finalizer,*c.claim,s.packet->seal,*second.claim,secondSeal.packet->seal);
#else
        r.Check("T28P.adapter-available",false,true,"FinalRealFramePreFgAdapter");
#endif
        return r.Finish();});
}
