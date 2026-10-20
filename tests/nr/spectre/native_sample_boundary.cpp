#define main inherited_context_main
#include "../vertical-slice/native_context_chain.cpp"
#undef main
#include <iostream>
template<class Reader>bool Boundary(const C::BoundaryDescription& a,const C::BoundaryDescription& b,
    const C::FrameIdentity& frame,const Reader& reader)
{
    using namespace Neurotic::Context;
    if constexpr(requires{SameObservationBoundary(a,b,frame,reader);})
        return SameObservationBoundary(a,b,frame,reader);
    else return S::SameBoundary(a,b);
}
int main()
{
    int count=0,failed=0;const auto check=[&](bool value,const char* name){++count;if(!value){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto f=std::make_unique<Fixture>(false,"evaluate",true,false,false,true,true,false,true,true,true);
    f->profile.sourceSchemas.Get(0)->domain=S::SourceSchemaDomain::NativeNgxAdapter;
    const auto result=f->Run();
    check(result.context&&result.certificate&&result.certificate->eligibility==C::Eligibility::Eligible,
          "Native callback context qualifies with unknown episode");
    const auto* frame=S::ResolveMetadata(f->request.frame,f->context);const auto a=f->request.boundary;
    check(frame&&!frame->episodeId.IsKnown()&&!a.episode.IsKnown()&&Boundary(a,a,*frame,f->context),
          "exact Native sample supplies observation boundary association");
    auto bad=*frame;bad.nativeSample.reset();
    check(!Boundary(a,a,bad,f->context),"ordinary frame still requires established episode");
    bad=*frame;++bad.nativeSample->revision;
    check(!Boundary(a,a,bad,f->context),"stale Native publication cannot replace episode association");
    bad=*frame;bad.nativeSample->owner=C::OwnerDomain::Context;
    check(!Boundary(a,a,bad,f->context),"non-source sample ownership refuses");
    auto b=a;b.kind=Known(C::BoundaryKind::AfterUpscale);
    check(!Boundary(a,b,*frame,f->context),"different known boundary kind refuses");
    b=a;b.episode=C::OptionalFact<C::EpisodeId>::FromUnknown(S::Missing("revoked",C::UnknownReason::Revoked));
    check(!Boundary(a,b,*frame,f->context),"explicit episode refusal is not ordinary absence");
    b=a;b.episode=NrContextTests::Frame().episodeId;
    check(!Boundary(a,b,*frame,f->context),"mixed known and unknown episode refuses");
    bad=*frame;bad.episodeId=b.episode;
    check(!Boundary(a,a,bad,f->context),"known frame episode cannot lose its boundary identity");
    auto first=a;first.hudIncluded=Known(true);b=a;b.hudIncluded=Known(false);
    check(!Boundary(first,b,*frame,f->context),"Native association does not hide conflicting boundary facts");
    for(const bool revoked:{false,true})
    {
        auto source=f->port->Candidates()[0];auto conflicting=*frame;
        conflicting.episodeId=revoked?C::OptionalFact<C::EpisodeId>::FromUnknown(S::Missing("revoked",C::UnknownReason::Revoked)):
            NrContextTests::Frame().episodeId;
        auto provider=f->arena->ForOwner(C::OwnerDomain::Provider);source.frame=provider.Publish(conflicting);
        auto evidence=*S::ResolveMetadata(source.evidence,f->context);
        evidence.coverage=Known(C::CoverageKind::Complete);evidence.semanticCertainty=Known(C::SemanticCertainty::Qualified);
        C::Symbol type=Sym(C::AcquisitionCandidate::WireName);
        const std::array<S::SourceQualification,1> qualifications={S::SourceQualification{
            {type,source.header.record,source.header.revision},f->context.Publish(evidence)}};
        S::AdmittedCandidate admitted;C::UnknownFact reason;
        check(!S::AdmitSource(source,*frame,a,f->profile,f->context,admitted,reason,qualifications),
              "different source-frame episode refuses admission even with exact Native sample");
        C::BoundedList<C::ContractRef<C::ContractId::C01>,64> references;
        references.Push({type,source.header.record,source.header.revision});
        auto observations=*f->port->Observations();observations.candidates.count=1;
        observations.candidates.backing=provider.Publish(references);
        const std::array<C::AcquisitionCandidate,1> sources={source};
        auto canonical=S::BuildCanonicalFrameContext(observations,sources,f->request,f->context,nullptr,nullptr,qualifications);
        check(canonical.context&&canonical.context->Value().coherentCandidates.count==0&&canonical.rejected.Size()==1,
              "canonical context excludes conflicting source-frame episode");
    }
    auto legacy=std::make_unique<Fixture>();const auto ordinary=legacy->Run();
    check(ordinary.context&&ordinary.certificate&&ordinary.certificate->eligibility==C::Eligibility::Eligible,
          "established legacy episode path preserved");
    std::cout<<"Native sample boundary: "<<count<<" checks, "<<failed<<" failures; synthetic CPU context only\n";
    return failed?1:0;
}
