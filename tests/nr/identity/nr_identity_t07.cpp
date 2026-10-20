// NR-LIFE-001 T07. Literal synthetic owner events; no game/FG/history execution.
#include "IdentityTestSupport.h"
using namespace LifeFixture;
template<class T> concept HasNewBase = requires(T& t, const L::RenderEvidence& e) { t.NewBase(e); };
template<class T> concept HasGeneratedMint = requires(T& t, const L::OwnerEvent& e) { t.Generated(e); };
template<class T> concept HasFinalSealMint = requires(T& t, const L::OwnerEvent& e) { t.FinalSeal(e); };
static_assert(HasNewBase<L::RenderIssuer>);
static_assert(!HasNewBase<L::PresentIssuer> && !HasNewBase<L::EvaluationIssuer>);
static_assert(!HasGeneratedMint<L::PresentIssuer> && !HasGeneratedMint<L::RenderIssuer>);
static_assert(!HasFinalSealMint<L::HostIdentityDomain>);
static_assert(!std::is_default_constructible_v<L::ImportedGeneratedFact>);
static_assert(!std::is_convertible_v<C::PresentId,C::BaseRealFrameId>);
static_assert(!std::is_convertible_v<C::EvaluationId,C::BaseRealFrameId>);
static_assert(!std::is_convertible_v<C::GeneratedFrameId,C::BaseRealFrameId>);
int main()
{
    Checks c;
    auto host = std::make_unique<L::HostIdentityDomain>(Sym("fixture.process.T07"));
    auto session = host->OpenSession(Event(C::OwnerDomain::Session, Key("session.scope"),1));
    c.Status("T07.session",session.status,L::IdentityStatus::Ok);
    auto rb = Binding(C::OwnerDomain::IdentityRegistry,"render.A");
    auto eb = Binding(C::OwnerDomain::Strategy,"evaluation.primary");
    auto pb = Binding(C::OwnerDomain::Presentation,"present.A");
    auto topology = host->BindTopology(Binding(C::OwnerDomain::Topology,"topology.A"));
    auto episode = host->BindEpisode(Binding(C::OwnerDomain::IdentityRegistry,"episode.A"));
    auto render = host->BindRender(rb);
    auto evaluate = host->BindEvaluation(eb);
    auto present = host->BindPresent(pb);
    if (!topology || !episode || !render || !evaluate || !present) return EXIT_FAILURE;
    auto st = topology->Stream(Event(C::OwnerDomain::Topology,Key("topology.A"),1));
    auto view = topology->View(Event(C::OwnerDomain::Topology,Key("topology.A"),2));
    auto ep = episode->Episode(Event(C::OwnerDomain::IdentityRegistry,Key("episode.A"),1));
    c.Is("T07.topology-kinds",Value(st).Kind==C::IdentityKind::RenderStreamId &&
         Value(view).Kind==C::IdentityKind::ViewId && Value(ep).Kind==C::IdentityKind::EpisodeId);
    L::RenderEvidence newEvent{Event(rb.owner,rb.subject,1),L::RenderObservation::NewGameContent};
    auto r1=render->NewBase(newEvent);
    auto ren=render->Render(Event(rb.owner,rb.subject,2));
    auto ev1=evaluate->Evaluation(Event(eb.owner,eb.subject,1));
    auto ev2=evaluate->Evaluation(Event(eb.owner,eb.subject,2));
    auto p1=present->Present(Event(pb.owner,pb.subject,1));
    auto p2=present->Present(Event(pb.owner,pb.subject,2));
    auto p3=present->Present(Event(pb.owner,pb.subject,3));
    c.Equal("T07.first-base",Value(r1).value,1);
    c.Equal("T07.two-evaluations",Value(ev2).value,2);
    c.Equal("T07.three-presents",Value(p3).value,3);
    c.Is("T07.coincident-counter-not-identity",Value(r1).Describe()!=Value(ren).Describe() &&
         Value(ren).Describe()!=Value(ev1).Describe() && Value(ev1).Describe()!=Value(p1).Describe());
    auto r2=render->NewBase({Event(rb.owner,rb.subject,3),L::RenderObservation::NewGameContent});
    c.Equal("T07.only-two-proven-bases",Value(r2).value,2);
    // Neither a repeated nor generated disposition is evidence of new game rendering.
    bool nonRealRefused=true;
    for (auto observation : {L::RenderObservation::RepeatedContent,L::RenderObservation::GeneratedOutput,
                            L::RenderObservation::ExistingGameContent})
    {
        const auto result=render->NewBase({Event(rb.owner,rb.subject,4),observation});
        c.Status("T07.nonreal-refused",result.status,L::IdentityStatus::NotNewReal);
        nonRealRefused=nonRealRefused && result.status==L::IdentityStatus::NotNewReal;
    }
    // LIFE-M03 must report the semantic mismatch and finish normally. A mutant may have
    // consumed event 4; do not let a later fixture dereference mask that intended failure.
    if (!nonRealRefused) return c.Finish();
    c.Status("T07.unknown-render-not-proven",render->NewBase({Event(rb.owner,rb.subject,4),
             L::RenderObservation::Unknown}).status,L::IdentityStatus::Unknown);
    auto r3=render->NewBase({Event(rb.owner,rb.subject,4),L::RenderObservation::NewGameContent});
    c.Status("T07.next-proven-base-status",r3.status,L::IdentityStatus::Ok);
    c.Is("T07.next-proven-base-known",r3.value.IsKnown());
    if (!HasValue(r3)) return c.Finish();
    c.Equal("T07.refusals-do-not-mint",Value(r3).value,3);
    c.Status("T07.duplicate-render-evidence",render->NewBase({Event(rb.owner,rb.subject,4),
             L::RenderObservation::NewGameContent}).status,L::IdentityStatus::StaleEvidence);
    auto wrong=Event(C::OwnerDomain::Presentation,rb.subject,5);
    c.Status("T07.presentation-cannot-act-as-render",render->NewBase({wrong,
             L::RenderObservation::NewGameContent}).status,L::IdentityStatus::WrongOwner);
    auto scope=Scope();
    scope.session=Known(Value(session));scope.stream=Known(Value(st));scope.view=Known(Value(view));
    const auto ra=Authority(C::OwnerDomain::IdentityRegistry,scope);
    const auto ea=Authority(C::OwnerDomain::Strategy,scope);
    const auto pa=Authority(C::OwnerDomain::Presentation,scope);
    auto renderEdge=L::MakeAssociation(ra,Edge(L::AssociationRelation::RenderToBase,
        Value(ren).Describe(),Value(r1).Describe(),ra));
    c.Status("T07.render-link",renderEdge.status,L::IdentityStatus::Ok);
    auto expectedRender=ExpectedProjection(L::AssociationRelation::RenderToBase,scope);
    expectedRender.frame.renderId=Known(Value(ren));
    expectedRender.frame.baseRealFrameId=Known(Value(r1));
    if (!CheckProjection(c,"T07.render-base",renderEdge,expectedRender)) return c.Finish();
    auto evalEdge=L::MakeAssociation(ea,Edge(L::AssociationRelation::EvaluationToRender,
        Value(ev1).Describe(),Value(ren).Describe(),ea));
    c.Status("T07.evaluation-link",evalEdge.status,L::IdentityStatus::Ok);
    auto expectedEvaluation=ExpectedProjection(L::AssociationRelation::EvaluationToRender,scope);
    expectedEvaluation.frame.evaluationId=Known(Value(ev1));
    expectedEvaluation.frame.renderId=Known(Value(ren));
    if (!CheckProjection(c,"T07.evaluation-render",evalEdge,expectedEvaluation)) return c.Finish();
    auto evalBase1=L::MakeAssociation(ea,Edge(L::AssociationRelation::EvaluationToBase,
        Value(ev1).Describe(),Value(r1).Describe(),ea));
    auto evalBase2=L::MakeAssociation(ea,Edge(L::AssociationRelation::EvaluationToBase,
        Value(ev2).Describe(),Value(r1).Describe(),ea));
    auto expectedEvalBase=ExpectedProjection(L::AssociationRelation::EvaluationToBase,scope);
    expectedEvalBase.frame.evaluationId=Known(Value(ev1));
    expectedEvalBase.frame.baseRealFrameId=Known(Value(r1));
    if (!CheckProjection(c,"T07.evaluation-base-first",evalBase1,expectedEvalBase)) return c.Finish();
    expectedEvalBase.frame.evaluationId=Known(Value(ev2));
    if (!CheckProjection(c,"T07.evaluation-base-second",evalBase2,expectedEvalBase)) return c.Finish();
    c.Is("T07.two-evaluations-one-base",Value(evalBase1).frame.baseRealFrameId==Known(Value(r1)) &&
         Value(evalBase2).frame.baseRealFrameId==Known(Value(r1)) && Value(ev1)!=Value(ev2));
    auto realPresent=L::MakeAssociation(pa,Edge(L::AssociationRelation::PresentToBase,
        Value(p1).Describe(),Value(r1).Describe(),pa));
    auto expectedPresent=ExpectedProjection(L::AssociationRelation::PresentToBase,scope);
    expectedPresent.frame.presentId=Known(Value(p1));expectedPresent.frame.baseRealFrameId=Known(Value(r1));
    if (!CheckProjection(c,"T07.present-base",realPresent,expectedPresent)) return c.Finish();
    auto episodeBase=L::MakeAssociation(ra,Edge(L::AssociationRelation::EpisodeToBase,
        Value(ep).Describe(),Value(r1).Describe(),ra));
    auto expectedEpisode=ExpectedProjection(L::AssociationRelation::EpisodeToBase,scope);
    expectedEpisode.frame.episodeId=Known(Value(ep));expectedEpisode.frame.baseRealFrameId=Known(Value(r1));
    if (!CheckProjection(c,"T07.episode-base",episodeBase,expectedEpisode)) return c.Finish();
    const auto finalizer=Authority(C::OwnerDomain::Finalizer,scope);
    // A literal supplied by the synthetic finalizer. No LIFE seal allocator is introduced.
    const C::FinalSealId seal{Sym("finalizer.seal"),Sym("finalizer.owner"),100};
    auto sealClaim=Edge(L::AssociationRelation::FinalSealToBase,seal.Describe(),Value(r1).Describe(),finalizer);
    sealClaim.source=C::OptionalFact<C::StableIdentity>::FromKnown(seal.Describe(),
        C::EvidenceRef{{Sym("finalizer.evidence"),Sym("finalizer.owner"),100}});
    auto sealed=L::MakeAssociation(finalizer,sealClaim);
    auto expectedSeal=ExpectedProjection(L::AssociationRelation::FinalSealToBase,scope);
    expectedSeal.frame.finalSealId=C::OptionalFact<C::FinalSealId>::FromKnown(seal,sealClaim.source.KnownPart()->evidence);
    expectedSeal.frame.baseRealFrameId=Known(Value(r1));
    if (!CheckProjection(c,"T07.final-seal-base",sealed,expectedSeal)) return c.Finish();
    const auto resourceAuthorityBase=Authority(C::OwnerDomain::Resource,scope);
    auto resourceAuthority=resourceAuthorityBase;
    const auto literalResource=Id<C::ResourceIncarnation>("projection.resource",31);
    const auto literalView=Id<C::ResourceViewIncarnation>("projection.resource-view",17);
    C::ResourceIdentityToken currentResource;
    currentResource.resourceIncarnation=Known(literalResource.Describe());
    currentResource.resourceViewIncarnation=Known(literalView.Describe());
    resourceAuthority.currentResource=currentResource;
    auto resourceEdge=L::MakeAssociation(resourceAuthority,Edge(L::AssociationRelation::ResourceViewToResource,
        literalView.Describe(),literalResource.Describe(),resourceAuthority));
    auto expectedResource=ExpectedProjection(L::AssociationRelation::ResourceViewToResource,scope);
    expectedResource.resource.resourceIncarnation=Known(literalResource.Describe());
    expectedResource.resource.resourceViewIncarnation=Known(literalView.Describe());
    if (!CheckProjection(c,"T07.resource-view-resource",resourceEdge,expectedResource)) return c.Finish();
    auto afterAssociations=render->NewBase({Event(rb.owner,rb.subject,5),L::RenderObservation::NewGameContent});
    c.Status("T07.after-associations-status",afterAssociations.status,L::IdentityStatus::Ok);
    c.Is("T07.after-associations-known",afterAssociations.value.IsKnown());
    if (!HasValue(afterAssociations)) return c.Finish();
    c.Equal("T07.associations-do-not-mint-base",Value(afterAssociations).value,4);
    auto repeat=L::MakeAssociation(pa,Edge(L::AssociationRelation::RepeatedPresentToPresent,
        Value(p2).Describe(),Value(p1).Describe(),pa));
    c.Status("T07.repeat-link",repeat.status,L::IdentityStatus::Ok);
    auto expectedRepeat=ExpectedProjection(L::AssociationRelation::RepeatedPresentToPresent,scope);
    expectedRepeat.frame.presentId=Known(Value(p2));expectedRepeat.repeatedFrom=Known(Value(p1));
    if (!CheckProjection(c,"T07.repeated-present",repeat,expectedRepeat)) return c.Finish();
    c.Is("T07.repeat-not-new-base",!Value(repeat).frame.baseRealFrameId.IsKnown());
    c.Is("T07.repeat-prior-present-preserved",Value(repeat).repeatedFrom==Known(Value(p1)));
    // Generated IDs are literal FG-owner inputs, not output from a LIFE allocator.
    auto fg=Authority(C::OwnerDomain::FrameGeneration,scope);
    const auto generated=Id<C::GeneratedFrameId>("provider.output",1);
    const auto providerId=Id<C::ProviderIncarnation>("provider",1);
    fg.currentGeneratedProvider=Known(providerId);
    L::GeneratedIdentityAuthority fgAuthority{Binding(C::OwnerDomain::FrameGeneration,"fg.output"),scope,
        providerId,Sym("provider.output"),true};
    L::GeneratedIdentityClaim fgClaim{Known(generated,20),Known(providerId),scope,
        Event(C::OwnerDomain::FrameGeneration,Key("fg.output"),20)};
    auto imported=L::ImportGeneratedIdentity(fgAuthority,fgClaim);
    c.Status("T07.fg-import",imported.status,L::IdentityStatus::Ok);
    c.Is("T07.fg-import-known",imported.value.IsKnown());
    if (!HasValue(imported)) return c.Finish();
    auto sourceEdge=Edge(L::AssociationRelation::GeneratedToSourceReal,generated.Describe(),Value(r1).Describe(),fg);
    sourceEdge.generatedOwnerFact=Value(imported);
    auto generatedEdge=L::MakeAssociation(fg,sourceEdge);
    c.Status("T07.fg-proven-link",generatedEdge.status,L::IdentityStatus::Ok);
    auto expectedGenerated=ExpectedProjection(L::AssociationRelation::GeneratedToSourceReal,scope);
    expectedGenerated.frame.generatedFrameId=Known(generated,20);
    expectedGenerated.sourceRealFrame=Known(Value(r1));
    if (!expectedGenerated.frame.associationEvidence.Push(Ev(20))) return EXIT_FAILURE;
    if (!CheckProjection(c,"T07.generated-source-real",generatedEdge,expectedGenerated)) return c.Finish();
    c.Is("T07.generated-source-not-current-base",!Value(generatedEdge).frame.baseRealFrameId.IsKnown() &&
         Value(generatedEdge).sourceRealFrame.IsKnown());
    auto presentFg=pa;presentFg.currentGeneratedProvider=Known(providerId);
    auto gpClaim=Edge(L::AssociationRelation::PresentToGenerated,Value(p3).Describe(),generated.Describe(),presentFg);
    c.Status("T07.unproven-generated-reference",L::MakeAssociation(presentFg,gpClaim).status,L::IdentityStatus::Unknown);
    gpClaim.generatedOwnerFact=Value(imported);
    auto gp=L::MakeAssociation(presentFg,gpClaim);
    c.Status("T07.present-generated-link",gp.status,L::IdentityStatus::Ok);
    auto expectedGeneratedPresent=ExpectedProjection(L::AssociationRelation::PresentToGenerated,scope);
    expectedGeneratedPresent.frame.presentId=Known(Value(p3));
    expectedGeneratedPresent.frame.generatedFrameId=Known(generated,20);
    if (!expectedGeneratedPresent.frame.associationEvidence.Push(Ev(20))) return EXIT_FAILURE;
    if (!CheckProjection(c,"T07.present-generated",gp,expectedGeneratedPresent)) return c.Finish();
    c.Is("T07.generated-present-not-real",!Value(gp).frame.baseRealFrameId.IsKnown());
    c.Is("T07.fg-provenance-preserved",Value(gp).frame.generatedFrameId==Known(generated,20));
    auto replacementAuthority=presentFg;replacementAuthority.currentGeneratedProvider=Known(Id<C::ProviderIncarnation>("provider",2));
    c.Status("T07.stale-provider-association",L::MakeAssociation(replacementAuthority,gpClaim).status,L::IdentityStatus::Different);
    auto wrongFg=fgAuthority;wrongFg.binding.owner=C::OwnerDomain::Presentation;
    c.Status("T07.non-fg-import-refused",L::ImportGeneratedIdentity(wrongFg,fgClaim).status,L::IdentityStatus::WrongOwner);
    auto opaqueFg=fgClaim;opaqueFg.identity={};
    c.Status("T07.opaque-import",L::ImportGeneratedIdentity(fgAuthority,opaqueFg).status,L::IdentityStatus::Unknown);
    auto secondSource=sourceEdge;secondSource.target=Known(Value(r2).Describe());
    c.Status("T07.generated-second-source",L::MakeAssociation(fg,secondSource).status,L::IdentityStatus::Ok);
    auto opaque=Edge(L::AssociationRelation::PresentToGenerated,Value(p3).Describe(),generated.Describe(),pa);
    opaque.target=C::OptionalFact<C::StableIdentity>{};
    auto unresolved=L::MakeAssociation(pa,opaque);
    c.Status("T07.opaque-fg-unknown",unresolved.status,L::IdentityStatus::Unknown);
    c.Is("T07.unknown-not-zero",!unresolved.value.IsKnown());
    auto bad=Edge(L::AssociationRelation::GeneratedToSourceReal,generated.Describe(),Value(r1).Describe(),pa);
    c.Status("T07.non-fg-generated-link-refused",L::MakeAssociation(pa,bad).status,L::IdentityStatus::WrongOwner);
    auto numeric=Edge(L::AssociationRelation::RenderToBase,Value(p1).Describe(),Value(r1).Describe(),ra);
    c.Status("T07.present-kind-not-render",L::MakeAssociation(ra,numeric).status,L::IdentityStatus::WrongKind);
    auto same=Id<C::BaseRealFrameId>("same.number.other.namespace",Value(r1).value);
    c.Status("T07.namespace-number-collision",L::CompareIdentity(Known(Value(r1).Describe()),Known(same.Describe())),
             L::IdentityStatus::Different);

    // R02: admission validates evidence separately from semantic scope equality.
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.session=C::OptionalFact<C::SessionId>::FromKnown(authority.currentScope.session.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-session-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-current-session-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.session={};
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-session-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T07.import-current-session-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.session=C::OptionalFact<C::SessionId>::FromKnown(authority.currentScope.session.KnownPart()->value,C::EvidenceRef{{Sym("fixture.evidence"),C::Symbol{},1}});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-session-malformed-issuer",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-current-session-malformed-issuer.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.stream=C::OptionalFact<C::RenderStreamId>::FromKnown(authority.currentScope.stream.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-stream-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-current-stream-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.stream={};
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-stream-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T07.import-current-stream-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.stream=C::OptionalFact<C::RenderStreamId>::FromKnown(authority.currentScope.stream.KnownPart()->value,C::EvidenceRef{{Sym("fixture.evidence"),C::Symbol{},1}});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-stream-malformed-issuer",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-current-stream-malformed-issuer.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.view=C::OptionalFact<C::ViewId>::FromKnown(authority.currentScope.view.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-view-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-current-view-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.view={};
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-view-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T07.import-current-view-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.view=C::OptionalFact<C::ViewId>::FromKnown(authority.currentScope.view.KnownPart()->value,C::EvidenceRef{{Sym("fixture.evidence"),C::Symbol{},1}});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-view-malformed-issuer",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-current-view-malformed-issuer.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.topologyIncarnation=C::OptionalFact<C::ObjectIncarnation>::FromKnown(authority.currentScope.topologyIncarnation.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-topologyIncarnation-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-current-topologyIncarnation-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.topologyIncarnation={};
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-topologyIncarnation-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T07.import-current-topologyIncarnation-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        authority.currentScope.topologyIncarnation=C::OptionalFact<C::ObjectIncarnation>::FromKnown(authority.currentScope.topologyIncarnation.KnownPart()->value,C::EvidenceRef{{Sym("fixture.evidence"),C::Symbol{},1}});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-current-topologyIncarnation-malformed-issuer",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-current-topologyIncarnation-malformed-issuer.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.session=C::OptionalFact<C::SessionId>::FromKnown(claim.scope.session.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-session-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-claimed-session-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.session={};
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-session-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T07.import-claimed-session-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.session=C::OptionalFact<C::SessionId>::FromKnown(claim.scope.session.KnownPart()->value,C::EvidenceRef{{Sym("fixture.evidence"),C::Symbol{},1}});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-session-malformed-issuer",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-claimed-session-malformed-issuer.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.stream=C::OptionalFact<C::RenderStreamId>::FromKnown(claim.scope.stream.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-stream-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-claimed-stream-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.stream={};
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-stream-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T07.import-claimed-stream-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.stream=C::OptionalFact<C::RenderStreamId>::FromKnown(claim.scope.stream.KnownPart()->value,C::EvidenceRef{{Sym("fixture.evidence"),C::Symbol{},1}});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-stream-malformed-issuer",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-claimed-stream-malformed-issuer.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.view=C::OptionalFact<C::ViewId>::FromKnown(claim.scope.view.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-view-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-claimed-view-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.view={};
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-view-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T07.import-claimed-view-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.view=C::OptionalFact<C::ViewId>::FromKnown(claim.scope.view.KnownPart()->value,C::EvidenceRef{{Sym("fixture.evidence"),C::Symbol{},1}});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-view-malformed-issuer",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-claimed-view-malformed-issuer.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.topologyIncarnation=C::OptionalFact<C::ObjectIncarnation>::FromKnown(claim.scope.topologyIncarnation.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-topologyIncarnation-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-claimed-topologyIncarnation-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.topologyIncarnation={};
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-topologyIncarnation-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T07.import-claimed-topologyIncarnation-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto authority=fgAuthority;auto claim=fgClaim;
        claim.scope.topologyIncarnation=C::OptionalFact<C::ObjectIncarnation>::FromKnown(claim.scope.topologyIncarnation.KnownPart()->value,C::EvidenceRef{{Sym("fixture.evidence"),C::Symbol{},1}});
        const auto result=L::ImportGeneratedIdentity(authority,claim);
        c.Status("T07.import-claimed-topologyIncarnation-malformed-issuer",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T07.import-claimed-topologyIncarnation-malformed-issuer.unavailable",result.value.IsKnown(),false);
    }
    {
        auto claim=fgClaim;
        claim.scope.session=Known(claim.scope.session.KnownPart()->value,99);
        claim.scope.stream=Known(claim.scope.stream.KnownPart()->value,99);
        claim.scope.view=Known(claim.scope.view.KnownPart()->value,99);
        claim.scope.topologyIncarnation=Known(claim.scope.topologyIncarnation.KnownPart()->value,99);
        const auto republished=L::ImportGeneratedIdentity(fgAuthority,claim);
        c.Status("T07.import-equal-scope-distinct-evidence",republished.status,L::IdentityStatus::Ok);
        c.Is("T07.import-retains-claim-provenance",HasValue(republished) &&
            Value(republished).Scope().session==claim.scope.session && Value(republished).Scope().stream==claim.scope.stream &&
            Value(republished).Scope().view==claim.scope.view && Value(republished).Scope().topologyIncarnation==claim.scope.topologyIncarnation);
    }
    {
        auto claim=fgClaim;claim.ownerEvidence.evidence.record.issuer=Sym("wrong.owner");
        c.Status("T07.import-wrong-owner-evidence",L::ImportGeneratedIdentity(fgAuthority,claim).status,L::IdentityStatus::InvalidEvidence);
        claim=fgClaim;claim.ownerEvidence.publisher=Key("wrong.publisher");
        c.Status("T07.import-wrong-publisher",L::ImportGeneratedIdentity(fgAuthority,claim).status,L::IdentityStatus::WrongOwner);
    }
    // R04 direct relation negatives: valid values are not enough to manufacture an edge.
    {
        auto claim=Edge(L::AssociationRelation::EvaluationToBase,Value(ev1).Describe(),Value(r1).Describe(),ea);
        claim.source=Known(Value(p1).Describe());
        c.Status("T07.evaluation-base-wrong-kind",L::MakeAssociation(ea,claim).status,L::IdentityStatus::WrongKind);
        claim=Edge(L::AssociationRelation::EvaluationToBase,Value(ev1).Describe(),Value(r1).Describe(),pa);
        c.Status("T07.evaluation-base-wrong-owner",L::MakeAssociation(pa,claim).status,L::IdentityStatus::WrongOwner);
        claim=Edge(L::AssociationRelation::EpisodeToBase,Value(ep).Describe(),Value(r1).Describe(),ra);claim.target={};
        c.Status("T07.episode-base-unknown-target",L::MakeAssociation(ra,claim).status,L::IdentityStatus::Unknown);
        claim=sealClaim;claim.targetScope.topologyIncarnation=Known(Id<C::ObjectIncarnation>("swapchain",2));
        c.Status("T07.final-seal-stale-scope",L::MakeAssociation(finalizer,claim).status,L::IdentityStatus::Different);
        claim=Edge(L::AssociationRelation::FinalSealToBase,seal.Describe(),Value(r1).Describe(),ra);
        c.Status("T07.final-seal-wrong-owner",L::MakeAssociation(ra,claim).status,L::IdentityStatus::WrongOwner);
        claim=Edge(L::AssociationRelation::PresentToBase,Value(p1).Describe(),Value(r1).Describe(),pa);
        claim.ownerEvidence.evidence={};
        c.Status("T07.coincident-values-without-edge-evidence",L::MakeAssociation(pa,claim).status,L::IdentityStatus::InvalidEvidence);
    }
    return c.Finish();
}
