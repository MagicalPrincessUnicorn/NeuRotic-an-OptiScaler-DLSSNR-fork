// NR-LIFE-001 T09. Structural signatures are distinct from content freshness.
#include "IdentityTestSupport.h"
using namespace LifeFixture;
static_assert(!std::is_convertible_v<C::ContentRevision,C::GenerationToken>);
static_assert(!std::is_convertible_v<C::ContentRevision,C::ResourceGeneration>);
static_assert(!std::is_convertible_v<C::ResourceGeneration,C::RepresentationGeneration>);
int main()
{
    Checks c;
    auto host=std::make_unique<L::HostIdentityDomain>(Sym("fixture.process.T09"));
    Value(host->OpenSession(Event(C::OwnerDomain::Session,Key("session.scope"),1)));
    const auto rb=Binding(C::OwnerDomain::Resource,"resource.A");auto resource=host->BindResource(rb);
    auto route=host->BindRoute(Binding(C::OwnerDomain::StreamCoordinator,"route.A"));
    auto representation=host->BindRepresentation(Binding(C::OwnerDomain::Context,"representation.A"));
    auto history=host->BindHistory(Binding(C::OwnerDomain::History,"history.A"));
    auto fg=host->BindFgPolicy(Binding(C::OwnerDomain::FrameGeneration,"fg.A"));
    auto provider=host->BindProvider(Binding(C::OwnerDomain::Provider,"provider.A"));
    if (!resource||!route||!representation||!history||!fg||!provider) return EXIT_FAILURE;
    auto ri=resource->Resource(Event(rb.owner,rb.subject,1));
    auto rg=resource->Structure(Event(rb.owner,rb.subject,2));
    auto rt=route->CommittedChange(Event(C::OwnerDomain::StreamCoordinator,Key("route.A"),1));
    auto pg=representation->Structure(Event(C::OwnerDomain::Context,Key("representation.A"),1));
    auto hg=history->AppliedReset(Event(C::OwnerDomain::History,Key("history.A"),1));
    auto pi=provider->Incarnation(Event(C::OwnerDomain::Provider,Key("provider.A"),1));
    auto cr1=resource->ContentWrite(Event(rb.owner,rb.subject,3),Value(ri));
    auto cr2=resource->ContentWrite(Event(rb.owner,rb.subject,4),Value(ri));
    auto cr3=resource->ContentWrite(Event(rb.owner,rb.subject,5),Value(ri));
    c.Equal("T09.content-1",Value(cr1).value,1);c.Equal("T09.content-2",Value(cr2).value,2);c.Equal("T09.content-3",Value(cr3).value,3);
    // Next explicit structural event is the second token, not the fifth one.
    c.Equal("T09.no-hidden-resource-churn",Value(resource->Structure(Event(rb.owner,rb.subject,6))).value,2);
    c.Equal("T09.no-hidden-route-churn",Value(route->CommittedChange(Event(C::OwnerDomain::StreamCoordinator,Key("route.A"),2))).value,2);
    c.Equal("T09.no-hidden-representation-churn",Value(representation->Structure(Event(C::OwnerDomain::Context,Key("representation.A"),2))).value,2);
    c.Equal("T09.no-hidden-history-churn",Value(history->AppliedReset(Event(C::OwnerDomain::History,Key("history.A"),2))).value,2);
    c.Equal("T09.original-resource-structure",Value(rg).value,1);
    c.Equal("T09.original-route",Value(rt).value,1);c.Equal("T09.original-representation",Value(pg).value,1);c.Equal("T09.original-history",Value(hg).value,1);
    Value(fg->PolicyChange(Event(C::OwnerDomain::FrameGeneration,Key("fg.A"),1)));
    Value(fg->PolicyChange(Event(C::OwnerDomain::FrameGeneration,Key("fg.A"),2)));
    c.Equal("T09.fg-policy-not-provider-replacement",Value(provider->Incarnation(Event(C::OwnerDomain::Provider,Key("provider.A"),2))).value,2);
    c.Equal("T09.original-provider",Value(pi).value,1);
    auto replacement=resource->Resource(Event(rb.owner,rb.subject,7));
    c.Status("T09.stale-write-scope",resource->ContentWrite(Event(rb.owner,rb.subject,8),Value(ri)).status,L::IdentityStatus::Different);
    c.Equal("T09.new-backing-freshness-explicit",Value(resource->ContentWrite(Event(rb.owner,rb.subject,8),Value(replacement))).value,4);
    auto selection=Selection();auto publication=Publication();
    auto base=std::make_unique<L::ResolvedStructuralSignature>();
    auto now=std::make_unique<L::ResolvedStructuralSignature>();
    const auto g=Generations(42,7,9,1);
    c.Status("T09.signature-build",L::BuildSignature(g,selection,publication,*base),L::IdentityStatus::Ok);
    c.Is("T09.signature-bodies-established",base->generationBody.has_value() && base->dependencyBody.has_value());
    if (!base->generationBody || !base->dependencyBody) return c.Finish();
    c.Equal("T09.only-three-selected-axes",base->generationBody->second.Entries().Size(),3);
    C::GenerationVector literalSelected;
    for (const auto& token : {Gen(C::IdentityKind::ResourceGeneration,"resource.family",42),
                             Gen(C::IdentityKind::RepresentationGeneration,"representation",7),
                             Gen(C::IdentityKind::RouteGeneration,"route",9)})
        if (literalSelected.Insert(token)!=C::Error::None) return EXIT_FAILURE;
    c.Is("T09.selected-values-exact",base->generationBody->second==literalSelected);
    c.Is("T09.selected-generation-reference-exact",base->generationBody->first==publication.generations);
    c.Is("T09.structural-record-reference-exact",base->dependencyBody->first==std::optional{publication.records});
    c.Is("T09.structural-record-value-exact",base->dependencyBody->second.Size()==1 &&
         *base->dependencyBody->second.Get(0)==Key("stable.stream-view-profile",1));
    // Literal content revisions never enter BuildSignature's input API.
    for (std::uint64_t content : {901ull,902ull,903ull})
    {
        auto pixels=Resource(32,content);
        c.Status("T09.republication-build",L::BuildSignature(g,selection,Publication(content),*now),L::IdentityStatus::Ok);
        c.Status("T09.content-and-publication-stable",L::CompareSignatures(*base,*now),L::IdentityStatus::Ok);
        c.Equal("T09.literal-freshness",pixels.contentRevision.KnownPart()->value.value,content);
    }
    c.Status("T09.unrelated-fg-build",L::BuildSignature(Generations(42,7,9,2),selection,publication,*now),L::IdentityStatus::Ok);
    c.Status("T09.unrelated-fg-ignored",L::CompareSignatures(*base,*now),L::IdentityStatus::Ok);
    c.Status("T09.resource-change-build",L::BuildSignature(Generations(43,7,9,1),selection,publication,*now),L::IdentityStatus::Ok);
    c.Status("T09.resource-change-invalidates",L::CompareSignatures(*base,*now),L::IdentityStatus::Different);
    c.Status("T09.representation-change-build",L::BuildSignature(Generations(42,8,9,1),selection,publication,*now),L::IdentityStatus::Ok);
    c.Status("T09.representation-change-invalidates",L::CompareSignatures(*base,*now),L::IdentityStatus::Different);
    c.Status("T09.route-change-build",L::BuildSignature(Generations(42,7,10,1),selection,publication,*now),L::IdentityStatus::Ok);
    c.Status("T09.route-change-invalidates",L::CompareSignatures(*base,*now),L::IdentityStatus::Different);
    auto narrow=Selection(false,false);
    c.Status("T09.narrow-base",L::BuildSignature(g,narrow,publication,*base),L::IdentityStatus::Ok);
    c.Status("T09.narrow-now",L::BuildSignature(Generations(999,888,9,2),narrow,publication,*now),L::IdentityStatus::Ok);
    c.Status("T09.unconsumed-structure-ignored",L::CompareSignatures(*base,*now),L::IdentityStatus::Ok);
    auto preserved=std::make_unique<L::ResolvedStructuralSignature>(*now);
    C::GenerationVector missing;
    c.Status("T09.missing-required-axis",L::BuildSignature(missing,narrow,publication,*now),L::IdentityStatus::Unknown);
    c.Status("T09.no-partial-signature-publication",L::CompareSignatures(*preserved,*now),L::IdentityStatus::Ok);
    auto duplicate=narrow;duplicate.axes.Push(*narrow.axes.Get(0));
    c.Status("T09.duplicate-selector",L::BuildSignature(g,duplicate,publication,*now),L::IdentityStatus::InvalidRule);
    auto bad=narrow;bad.axes.Get(0)->kind=C::IdentityKind::PresentId;
    c.Status("T09.per-frame-selector-rejected",L::BuildSignature(g,bad,publication,*now),L::IdentityStatus::WrongKind);
    C::IdentityKind kind=C::IdentityKind::ResourceGeneration;
    c.Is("T09.no-content-kind",C::ParseEnum("ContentRevision",kind),false);
    C::GenerationVector vector;
    c.Is("T09.base-not-generation",vector.Insert({Id<C::BaseRealFrameId>("real",1).Describe()})==C::Error::WrongIdentityKind);
    auto a=Resource(32,901),b=Resource(32,902);
    c.Status("T09.exact-content-stale",L::CompareExactContent(a,b,{true,true}),L::IdentityStatus::Different);
    b=a;b.contentRevision=Known(C::ContentRevision{901},99);
    c.Status("T09.freshness-evidence-ref-not-value",L::CompareExactContent(a,b,{true,true}),L::IdentityStatus::Ok);
    b.contentRevision={};
    c.Status("T09.unknown-freshness-not-current",L::CompareExactContent(a,b,{true,true}),L::IdentityStatus::Unknown);
    b=a;b.resourceGeneration=Known(Gen(C::IdentityKind::ResourceGeneration,"resource.family",43));
    c.Status("T09.freshness-is-not-structural-permission",L::CompareExactContent(a,b,{true,true}),L::IdentityStatus::Ok);
    // The last check is deliberately narrow: exact contents do not grant a lease or structural compatibility.
    auto before=Known(Gen(C::IdentityKind::RepresentationGeneration,"representation",7));
    auto after=Known(Gen(C::IdentityKind::RepresentationGeneration,"representation",8));
    const L::GenerationAxis axis{C::IdentityKind::RepresentationGeneration,Sym("representation"),Sym("fixture.owner")};
    auto change=L::DescribeGenerationChange(axis,before,after,Ev(1));
    c.Status("T09.c08-change",change.status,L::IdentityStatus::Ok);
    c.Status("T09.c08-full-build",L::BuildSignature(g,selection,publication,*base),L::IdentityStatus::Ok);
    c.Is("T09.c08-selected-affected",Value(L::ChangeAffects(*base,axis,Value(change),Ev(2))));
    const auto unselected=L::ChangeAffects(*preserved,axis,Value(change),Ev(2));
    c.Status("T09.c08-unselected-status",unselected.status,L::IdentityStatus::Ok);
    c.Is("T09.c08-unselected-survives",HasValue(unselected) && !Value(unselected));
    c.Status("T09.c08-already-current-build",L::BuildSignature(Generations(42,8,9,1),selection,publication,*now),L::IdentityStatus::Ok);
    c.Is("T09.c08-already-current-survives",!Value(L::ChangeAffects(*now,axis,Value(change),Ev(2))));
    c.Status("T09.c08-unrelated-lineage-build",L::BuildSignature(Generations(42,6,9,1),selection,publication,*now),L::IdentityStatus::Ok);
    c.Status("T09.c08-unrelated-lineage-refused",L::ChangeAffects(*now,axis,Value(change),Ev(2)).status,L::IdentityStatus::Different);
    auto unchanged=L::DescribeGenerationChange(axis,before,before,Ev(1));
    c.Is("T09.c08-no-change",!Value(L::ChangeAffects(*base,axis,Value(unchanged),Ev(2))));
    auto unknownAfter=C::OptionalFact<C::GenerationToken>{};
    auto uncertain=L::DescribeGenerationChange(axis,before,unknownAfter,Ev(1));
    c.Status("T09.c08-missing-after",L::ChangeAffects(*base,axis,Value(uncertain),Ev(2)).status,L::IdentityStatus::Unknown);
    auto otherKind=Known(Gen(C::IdentityKind::ResourceGeneration,"resource.family",43));
    c.Status("T09.c08-kind-mismatch",L::DescribeGenerationChange(axis,before,otherKind,Ev(1)).status,L::IdentityStatus::WrongKind);
    C::HistoryKey h;h.owner=Key("history.owner");h.slot=Sym("main");h.dependencies=base->publication;
    h.generation=Known(Id<C::HistoryGeneration>("history",9));
    c.Status("T09.history-republication",L::BuildSignature(g,selection,Publication(2),*now),L::IdentityStatus::Ok);
    auto h2=h;h2.dependencies=now->publication;h2.generation=Known(Id<C::HistoryGeneration>("history",9),99);
    c.Status("T09.history-publication-stable",L::CompareHistoryDependencies(h,*base,h2,*now),L::IdentityStatus::Ok);
    c.Status("T09.unbound-history-body",L::CompareHistoryDependencies(h,*base,h2,*base),L::IdentityStatus::InvalidRule);
    h2.generation=Known(Id<C::HistoryGeneration>("history",10));
    c.Status("T09.history-lineage-change",L::CompareHistoryDependencies(h,*base,h2,*now),L::IdentityStatus::Different);

    // R01: absence is not an established empty body, even with an otherwise valid descriptor.
    {
        auto incomplete=*base;incomplete.generationBody.reset();
        c.Status("T09.unestablished-generation-body",L::CheckSignature(incomplete),L::IdentityStatus::Unknown);
        c.Status("T09.incomplete-signatures-not-equivalent",L::CompareSignatures(incomplete,incomplete),L::IdentityStatus::Unknown);
        const auto affected=L::ChangeAffects(incomplete,axis,Value(change),Ev(2));
        c.Status("T09.c08-unavailable-body",affected.status,L::IdentityStatus::Unknown);
        c.Is("T09.c08-unavailable-not-known-false",affected.value.IsKnown(),false);
        c.Status("T09.incomplete-history-body",L::CompareHistoryDependencies(h,incomplete,h,incomplete),L::IdentityStatus::Unknown);
        incomplete=*base;incomplete.dependencyBody.reset();
        c.Status("T09.unestablished-record-body",L::CheckSignature(incomplete),L::IdentityStatus::Unknown);
        c.Status("T09.incomplete-record-body-comparison",L::CompareSignatures(incomplete,*base),L::IdentityStatus::Unknown);
        auto wrong=*base;++wrong.generationBody->first.revision;
        c.Status("T09.wrong-generation-body-revision",L::CheckSignature(wrong),L::IdentityStatus::InvalidRule);
        wrong=*base;wrong.generationBody->first.record=Key("other.generations");
        c.Status("T09.wrong-generation-body-identity",L::CheckSignature(wrong),L::IdentityStatus::InvalidRule);
        wrong=*base;++wrong.dependencyBody->first->revision;
        c.Status("T09.wrong-record-body-revision",L::CheckSignature(wrong),L::IdentityStatus::InvalidRule);
        wrong=*base;wrong.dependencyBody->first->record=Key("other.dependencies");
        c.Status("T09.wrong-record-body-identity",L::CheckSignature(wrong),L::IdentityStatus::InvalidRule);
        wrong=*base;wrong.dependencyBody->first.reset();
        c.Status("T09.missing-record-body-binding",L::CheckSignature(wrong),L::IdentityStatus::InvalidRule);
        wrong=*base;wrong.dependencyBody->second={};
        c.Status("T09.record-body-count-mismatch",L::CheckSignature(wrong),L::IdentityStatus::InvalidRule);
        L::ResolvedStructuralSignature descriptorOnly;
        descriptorOnly.publication=base->publication;descriptorOnly.record=base->record;
        c.Status("T09.descriptor-only-unavailable",L::CheckSignature(descriptorOnly),L::IdentityStatus::Unknown);
    }
    {
        L::SignatureSelection empty;empty.schema=Sym("LIFE.fixture.empty");
        L::ResolvedStructuralSignature one,two;
        c.Status("T09.established-empty-build",L::BuildSignature(C::GenerationVector{},empty,Publication(40),one),L::IdentityStatus::Ok);
        c.Status("T09.established-empty-republication",L::BuildSignature(C::GenerationVector{},empty,Publication(41),two),L::IdentityStatus::Ok);
        c.Is("T09.empty-bodies-are-established",one.generationBody.has_value() && one.dependencyBody.has_value());
        if (!one.generationBody || !one.dependencyBody) return c.Finish();
        c.Equal("T09.empty-generation-count",one.generationBody->second.Entries().Size(),0);
        c.Equal("T09.empty-record-count",one.dependencyBody->second.Size(),0);
        c.Is("T09.empty-records-have-no-backing",!one.dependencyBody->first && !one.record.structuralDependencies.backing);
        c.Status("T09.empty-signatures-equivalent",L::CompareSignatures(one,two),L::IdentityStatus::Ok);
        auto unaffected=L::ChangeAffects(one,axis,Value(change),Ev(2));
        c.Status("T09.empty-dependencies-c08-status",unaffected.status,L::IdentityStatus::Ok);
        c.Is("T09.established-empty-c08-false",HasValue(unaffected) && !Value(unaffected));
        two.generationBody.reset();
        c.Status("T09.empty-versus-unestablished",L::CompareSignatures(one,two),L::IdentityStatus::Unknown);
        one.dependencyBody.reset();
        c.Status("T09.zero-count-not-body-establishment",L::CheckSignature(one),L::IdentityStatus::Unknown);
    }
    // R03: direct native descriptor validation, not a parser/codec surrogate.
    {
        auto p=Publication();p.signature.owner=static_cast<C::OwnerDomain>(UINT32_MAX);
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-signature-undefined-owner",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-signature-undefined-owner.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.signature.owner=C::OwnerDomain::Unspecified;
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-signature-unspecified-owner",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-signature-unspecified-owner.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.signature.record.issuer={};
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-signature-bad-key",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-signature-bad-key.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.signature.schemaVersion.major=2;
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-signature-bad-schema",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-signature-bad-schema.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.generations.owner=static_cast<C::OwnerDomain>(UINT32_MAX);
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-generations-undefined-owner",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-generations-undefined-owner.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.generations.owner=C::OwnerDomain::Unspecified;
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-generations-unspecified-owner",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-generations-unspecified-owner.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.generations.record.issuer={};
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-generations-bad-key",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-generations-bad-key.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.generations.schemaVersion.major=2;
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-generations-bad-schema",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-generations-bad-schema.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.records.owner=static_cast<C::OwnerDomain>(UINT32_MAX);
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-records-undefined-owner",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-records-undefined-owner.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.records.owner=C::OwnerDomain::Unspecified;
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-records-unspecified-owner",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-records-unspecified-owner.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.records.record.issuer={};
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-records-bad-key",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-records-bad-key.output-preserved",output==saved);
    }
    {
        auto p=Publication();p.records.schemaVersion.major=2;
        auto output=*base;const auto saved=output;
        c.Status("T09.metadata-records-bad-schema",L::BuildSignature(g,selection,p,output),L::IdentityStatus::InvalidRule);
        c.Is("T09.metadata-records-bad-schema.output-preserved",output==saved);
    }
    {
        auto s=*base;(s.publication).owner=static_cast<C::OwnerDomain>(UINT32_MAX);
        c.Status("T09.resolved-root-undefined-owner",L::CheckSignature(s),L::IdentityStatus::InvalidRule);
    }
    {
        auto s=*base;(s.record.generations).owner=static_cast<C::OwnerDomain>(UINT32_MAX);
        c.Status("T09.resolved-generation-reference-undefined-owner",L::CheckSignature(s),L::IdentityStatus::InvalidRule);
    }
    {
        auto s=*base;(s.generationBody->first).owner=static_cast<C::OwnerDomain>(UINT32_MAX);
        c.Status("T09.resolved-generation-body-undefined-owner",L::CheckSignature(s),L::IdentityStatus::InvalidRule);
    }
    {
        auto s=*base;(*s.record.structuralDependencies.backing).owner=static_cast<C::OwnerDomain>(UINT32_MAX);
        c.Status("T09.resolved-record-reference-undefined-owner",L::CheckSignature(s),L::IdentityStatus::InvalidRule);
    }
    {
        auto s=*base;(*s.dependencyBody->first).owner=static_cast<C::OwnerDomain>(UINT32_MAX);
        c.Status("T09.resolved-record-body-undefined-owner",L::CheckSignature(s),L::IdentityStatus::InvalidRule);
    }
    {
        auto invalid=h;invalid.dependencies.owner=static_cast<C::OwnerDomain>(UINT32_MAX);
        c.Status("T09.history-reference-undefined-owner",L::CompareHistoryDependencies(invalid,*base,h,*base),L::IdentityStatus::InvalidRule);
        invalid=h;invalid.dependencies.schemaVersion.major=2;
        c.Status("T09.history-reference-bad-schema",L::CompareHistoryDependencies(invalid,*base,h,*base),L::IdentityStatus::InvalidRule);
    }
    {
        L::SignatureSelection empty;empty.schema=Sym("LIFE.fixture.empty");
        auto p=Publication();p.records.owner=static_cast<C::OwnerDomain>(UINT32_MAX);
        L::ResolvedStructuralSignature output;
        c.Status("T09.unused-record-descriptor-not-consumed",L::BuildSignature(g,empty,p,output),L::IdentityStatus::Ok);
        auto knownZero=Publication(0);knownZero.signature.record.value=0;knownZero.generations.record.value=0;
        c.Status("T09.known-zero-metadata-not-unknown",L::BuildSignature(g,empty,knownZero,output),L::IdentityStatus::Ok);
    }
    return c.Finish();
}
