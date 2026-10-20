// NR-LIFE-001 T08. Addresses below are inert test labels; never converted to pointers.
#include "IdentityTestSupport.h"
using namespace LifeFixture;
static_assert(!std::is_default_constructible_v<L::ResourceIssuer>);
static_assert(!std::is_default_constructible_v<L::ProviderIssuer>);
static_assert(!std::is_copy_constructible_v<L::HostIdentityDomain>);
static_assert(!std::is_move_constructible_v<L::HostIdentityDomain>);
int main()
{
    Checks c;
    auto host=std::make_unique<L::HostIdentityDomain>(Sym("fixture.process.T08"));
    c.Status("T08.session",host->OpenSession(Event(C::OwnerDomain::Session,Key("session.scope"),1)).status,L::IdentityStatus::Ok);
    const auto binding=Binding(C::OwnerDomain::Resource,"resource.slot.A");
    auto owner=host->BindResource(binding);
    if (!owner) return EXIT_FAILURE;
    const auto first=owner->Resource(Event(binding.owner,binding.subject,1));
    const auto object1=owner->Object(Event(binding.owner,binding.subject,2));
    const auto view1=owner->View(Event(binding.owner,binding.subject,3));
    const auto view2=owner->View(Event(binding.owner,binding.subject,4));
    const auto structural=owner->Structure(Event(binding.owner,binding.subject,5));
    auto borrowedChannel=*owner; // Copy shares the issuer slot, not a copied counter.
    const auto second=borrowedChannel.Resource(Event(binding.owner,binding.subject,6));
    const auto object2=owner->Object(Event(binding.owner,binding.subject,7));
    constexpr std::uint64_t addressBefore=0x11223344u,addressAfter=0x11223344u;
    c.Is("T08.same-address-is-only-a-label",addressBefore==addressAfter);
    c.Equal("T08.new-resource-incarnation",Value(second).value,2);
    c.Equal("T08.new-object-incarnation",Value(object2).value,2);
    c.Equal("T08.resource-structure-independent",Value(structural).value,1);
    c.Is("T08.two-view-incarnations",Value(view1).Describe()!=Value(view2).Describe());
    c.Status("T08.old-resource-rejected",L::CompareIdentity(Known(Value(first).Describe()),Known(Value(second).Describe())),L::IdentityStatus::Different);
    c.Status("T08.old-object-rejected",L::CompareIdentity(Known(Value(object1).Describe()),Known(Value(object2).Describe())),L::IdentityStatus::Different);
    auto other=host->BindResource(Binding(C::OwnerDomain::Resource,"resource.slot.B"));
    if (!other) return EXIT_FAILURE;
    auto otherFirst=other->Resource(Event(binding.owner,Key("resource.slot.B"),1));
    c.Equal("T08.owner-counter-coincidence",Value(otherFirst).value,Value(first).value);
    c.Is("T08.owner-scope-not-equal",Value(otherFirst).Describe()!=Value(first).Describe());
    c.Is("T08.duplicate-owner-binding-refused",!host->BindResource(binding));
    auto wrong=Binding(C::OwnerDomain::Diagnostics,"resource.slot.C");
    c.Is("T08.diagnostics-no-resource-channel",!host->BindResource(wrong));
    auto noEvidence=Event(binding.owner,binding.subject,8); noEvidence.evidence={};
    c.Status("T08.missing-evidence",owner->Resource(noEvidence).status,L::IdentityStatus::InvalidEvidence);
    auto wrongSubject=Event(binding.owner,Key("wrong.subject"),8);
    c.Status("T08.wrong-owner-subject",owner->Resource(wrongSubject).status,L::IdentityStatus::InvalidScope);
    auto third=owner->Resource(Event(binding.owner,binding.subject,8));
    c.Equal("T08.failed-issuance-preserves-next",Value(third).value,3);
    auto scope=Scope();scope.session=Known(Value(host->CurrentSession()));
    auto authority=Authority(C::OwnerDomain::Resource,scope);
    C::ResourceIdentityToken ownerCurrent;
    ownerCurrent.resourceIncarnation=Known(Value(first).Describe());
    ownerCurrent.resourceViewIncarnation=Known(Value(view1).Describe());
    authority.currentResource=ownerCurrent;
    auto edge=Edge(L::AssociationRelation::ResourceViewToResource,Value(view1).Describe(),Value(first).Describe(),authority);
    c.Status("T08.view-backing-evidence",L::MakeAssociation(authority,edge).status,L::IdentityStatus::Ok);
    auto resourceChanged=authority;resourceChanged.currentResource->resourceIncarnation=Known(Value(second).Describe());
    c.Status("T08.stale-resource-association",L::MakeAssociation(resourceChanged,edge).status,L::IdentityStatus::Different);
    auto viewChanged=authority;viewChanged.currentResource->resourceViewIncarnation=Known(Value(view2).Describe());
    c.Status("T08.stale-view-association",L::MakeAssociation(viewChanged,edge).status,L::IdentityStatus::Different);
    auto stale=authority; stale.currentScope.topologyIncarnation=Known(Id<C::ObjectIncarnation>("swapchain",2));
    c.Status("T08.stale-topology-incarnation",L::MakeAssociation(stale,edge).status,L::IdentityStatus::Different);
    auto right=authority;right.currentScope.view=Known(Id<C::ViewId>("view.right",1));
    c.Status("T08.view-scope-loss",L::MakeAssociation(right,edge).status,L::IdentityStatus::Different);
    auto secondary=authority;secondary.currentScope.stream=Known(Id<C::RenderStreamId>("stream.B",1));
    c.Status("T08.secondary-stream",L::MakeAssociation(secondary,edge).status,L::IdentityStatus::Different);
    auto unknown=authority;unknown.currentScope.view={};
    c.Status("T08.unknown-view",L::MakeAssociation(unknown,edge).status,L::IdentityStatus::Unknown);
    auto missing=Known(Value(first).Describe());missing={};
    c.Status("T08.unknown-identity",L::CompareIdentity(missing,Known(Value(first).Describe())),L::IdentityStatus::Unknown);
    auto changedIssuer=Value(first).Describe();changedIssuer.issuer=Sym("another.owner");
    c.Status("T08.issuer-collision",L::CompareIdentity(Known(changedIssuer),Known(Value(first).Describe())),L::IdentityStatus::Different);
    auto a=Resource(1,8),b=Resource(2,8);
    c.Status("T08.stale-content-scope",L::CompareExactContent(a,b,{true,true}),L::IdentityStatus::Different);
    b=a;b.resourceViewIncarnation=Known(Id<C::ResourceViewIncarnation>("resource.views",4).Describe());
    c.Status("T08.view-rebind-rejected",L::CompareExactContent(a,b,{true,true}),L::IdentityStatus::Different);

    // R02: direct native current/source/target scope evidence, including Known versus Unknown.
    {
        auto caseAuthority=authority;auto claim=edge;
        caseAuthority.currentScope.session=C::OptionalFact<C::SessionId>::FromKnown(caseAuthority.currentScope.session.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-session-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-current-session-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        caseAuthority.currentScope.session={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-session-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-current-session-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=caseAuthority.currentScope.session.KnownPart()->value;value.issuer={};
        caseAuthority.currentScope.session=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-session-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-current-session-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        caseAuthority.currentScope.stream=C::OptionalFact<C::RenderStreamId>::FromKnown(caseAuthority.currentScope.stream.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-stream-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-current-stream-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        caseAuthority.currentScope.stream={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-stream-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-current-stream-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=caseAuthority.currentScope.stream.KnownPart()->value;value.issuer={};
        caseAuthority.currentScope.stream=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-stream-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-current-stream-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        caseAuthority.currentScope.view=C::OptionalFact<C::ViewId>::FromKnown(caseAuthority.currentScope.view.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-view-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-current-view-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        caseAuthority.currentScope.view={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-view-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-current-view-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=caseAuthority.currentScope.view.KnownPart()->value;value.issuer={};
        caseAuthority.currentScope.view=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-view-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-current-view-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        caseAuthority.currentScope.topologyIncarnation=C::OptionalFact<C::ObjectIncarnation>::FromKnown(caseAuthority.currentScope.topologyIncarnation.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-topologyIncarnation-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-current-topologyIncarnation-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        caseAuthority.currentScope.topologyIncarnation={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-topologyIncarnation-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-current-topologyIncarnation-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=caseAuthority.currentScope.topologyIncarnation.KnownPart()->value;value.issuer={};
        caseAuthority.currentScope.topologyIncarnation=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-current-topologyIncarnation-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-current-topologyIncarnation-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.sourceScope.session=C::OptionalFact<C::SessionId>::FromKnown(claim.sourceScope.session.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-session-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-source-session-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.sourceScope.session={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-session-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-source-session-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=claim.sourceScope.session.KnownPart()->value;value.issuer={};
        claim.sourceScope.session=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-session-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-source-session-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.sourceScope.stream=C::OptionalFact<C::RenderStreamId>::FromKnown(claim.sourceScope.stream.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-stream-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-source-stream-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.sourceScope.stream={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-stream-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-source-stream-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=claim.sourceScope.stream.KnownPart()->value;value.issuer={};
        claim.sourceScope.stream=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-stream-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-source-stream-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.sourceScope.view=C::OptionalFact<C::ViewId>::FromKnown(claim.sourceScope.view.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-view-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-source-view-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.sourceScope.view={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-view-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-source-view-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=claim.sourceScope.view.KnownPart()->value;value.issuer={};
        claim.sourceScope.view=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-view-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-source-view-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.sourceScope.topologyIncarnation=C::OptionalFact<C::ObjectIncarnation>::FromKnown(claim.sourceScope.topologyIncarnation.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-topologyIncarnation-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-source-topologyIncarnation-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.sourceScope.topologyIncarnation={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-topologyIncarnation-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-source-topologyIncarnation-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=claim.sourceScope.topologyIncarnation.KnownPart()->value;value.issuer={};
        claim.sourceScope.topologyIncarnation=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-source-topologyIncarnation-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-source-topologyIncarnation-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.targetScope.session=C::OptionalFact<C::SessionId>::FromKnown(claim.targetScope.session.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-session-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-target-session-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.targetScope.session={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-session-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-target-session-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=claim.targetScope.session.KnownPart()->value;value.issuer={};
        claim.targetScope.session=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-session-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-target-session-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.targetScope.stream=C::OptionalFact<C::RenderStreamId>::FromKnown(claim.targetScope.stream.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-stream-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-target-stream-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.targetScope.stream={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-stream-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-target-stream-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=claim.targetScope.stream.KnownPart()->value;value.issuer={};
        claim.targetScope.stream=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-stream-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-target-stream-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.targetScope.view=C::OptionalFact<C::ViewId>::FromKnown(claim.targetScope.view.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-view-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-target-view-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.targetScope.view={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-view-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-target-view-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=claim.targetScope.view.KnownPart()->value;value.issuer={};
        claim.targetScope.view=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-view-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-target-view-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.targetScope.topologyIncarnation=C::OptionalFact<C::ObjectIncarnation>::FromKnown(claim.targetScope.topologyIncarnation.KnownPart()->value,C::EvidenceRef{});
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-topologyIncarnation-empty-evidence",result.status,L::IdentityStatus::InvalidEvidence);
        c.Is("T08.scope-target-topologyIncarnation-empty-evidence.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        claim.targetScope.topologyIncarnation={};
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-topologyIncarnation-unknown",result.status,L::IdentityStatus::Unknown);
        c.Is("T08.scope-target-topologyIncarnation-unknown.unavailable",result.value.IsKnown(),false);
    }
    {
        auto caseAuthority=authority;auto claim=edge;
        auto value=claim.targetScope.topologyIncarnation.KnownPart()->value;value.issuer={};
        claim.targetScope.topologyIncarnation=Known(value);
        const auto result=L::MakeAssociation(caseAuthority,claim);
        c.Status("T08.scope-target-topologyIncarnation-malformed-identity",result.status,L::IdentityStatus::InvalidScope);
        c.Is("T08.scope-target-topologyIncarnation-malformed-identity.unavailable",result.value.IsKnown(),false);
    }
    {
        auto claim=edge;
        claim.sourceScope.session=Known(claim.sourceScope.session.KnownPart()->value,99);
        claim.sourceScope.stream=Known(claim.sourceScope.stream.KnownPart()->value,99);
        claim.sourceScope.view=Known(claim.sourceScope.view.KnownPart()->value,99);
        claim.sourceScope.topologyIncarnation=Known(claim.sourceScope.topologyIncarnation.KnownPart()->value,99);
        claim.targetScope.session=Known(claim.targetScope.session.KnownPart()->value,99);
        claim.targetScope.stream=Known(claim.targetScope.stream.KnownPart()->value,99);
        claim.targetScope.view=Known(claim.targetScope.view.KnownPart()->value,99);
        claim.targetScope.topologyIncarnation=Known(claim.targetScope.topologyIncarnation.KnownPart()->value,99);
        const auto republished=L::MakeAssociation(authority,claim);
        c.Status("T08.scope-equal-values-different-evidence",republished.status,L::IdentityStatus::Ok);
        c.Is("T08.scope-evidence-preserved",HasValue(republished) &&
            Value(republished).frame.sessionId==claim.sourceScope.session &&
            Value(republished).frame.renderStreamId==claim.sourceScope.stream &&
            Value(republished).frame.viewId==claim.sourceScope.view);
        c.Status("T08.semantic-scope-ignores-evidence-publication",L::CompareScope(authority.currentScope,claim.sourceScope,true),L::IdentityStatus::Ok);
    }
    {
        auto caseAuthority=authority;auto claim=edge;caseAuthority.requireTopologyIncarnation=false;
        caseAuthority.currentScope.topologyIncarnation={};claim.sourceScope.topologyIncarnation={};claim.targetScope.topologyIncarnation={};
        c.Status("T08.optional-topology-unknown",L::MakeAssociation(caseAuthority,claim).status,L::IdentityStatus::Ok);
        claim.sourceScope.topologyIncarnation=C::OptionalFact<C::ObjectIncarnation>::FromKnown(Id<C::ObjectIncarnation>("swapchain",1),{});
        c.Status("T08.optional-known-topology-bad-evidence",L::MakeAssociation(caseAuthority,claim).status,L::IdentityStatus::InvalidEvidence);
    }
    {
        auto claim=edge;claim.ownerEvidence.owner=C::OwnerDomain::Presentation;
        c.Status("T08.association-wrong-owner",L::MakeAssociation(authority,claim).status,L::IdentityStatus::WrongOwner);
        claim=edge;claim.ownerEvidence.evidence.record.nameSpace=Sym("unbound.evidence");
        c.Status("T08.association-wrong-evidence-source",L::MakeAssociation(authority,claim).status,L::IdentityStatus::InvalidEvidence);
        claim=edge;claim.ownerEvidence.evidence.record.issuer=Sym("wrong.owner");
        c.Status("T08.association-wrong-evidence-issuer",L::MakeAssociation(authority,claim).status,L::IdentityStatus::InvalidEvidence);
    }
    // Only the expected active session may be closed; stale copies cannot affect replacements.
    const auto oldSession=Value(host->CurrentSession());
    c.Status("T08.close",host->CloseSession(oldSession),L::IdentityStatus::Ok);
    c.Status("T08.closed-channel",owner->Resource(Event(binding.owner,binding.subject,9)).status,L::IdentityStatus::Closed);
    auto freshSession=host->OpenSession(Event(C::OwnerDomain::Session,Key("session.scope"),2));
    c.Equal("T08.new-session",Value(freshSession).value,2);
    c.Status("T08.stale-close",host->CloseSession(oldSession),L::IdentityStatus::Different);
    c.Status("T08.prior-session-channel",borrowedChannel.Resource(Event(binding.owner,binding.subject,9)).status,L::IdentityStatus::Closed);
    auto newOwner=host->BindResource(binding);
    if (!newOwner) return EXIT_FAILURE;
    c.Is("T08.rebound-namespace-not-reused",Value(newOwner->Resource(Event(binding.owner,binding.subject,1))).Describe()!=Value(first).Describe());
    // Deterministic low issuance ceiling exercises exhaustion without resetting a live counter.
    auto small=std::make_unique<L::HostIdentityDomain>(Sym("fixture.process.small"),2);
    Value(small->OpenSession(Event(C::OwnerDomain::Session,Key("session.scope"),1)));
    auto sb=Binding(C::OwnerDomain::Provider,"provider.A");auto provider=small->BindProvider(sb);
    if (!provider) return EXIT_FAILURE;
    c.Equal("T08.provider-1",Value(provider->Incarnation(Event(sb.owner,sb.subject,1))).value,1);
    c.Equal("T08.provider-2",Value(provider->Incarnation(Event(sb.owner,sb.subject,2))).value,2);
    c.Status("T08.exhaustion-no-wrap",provider->Incarnation(Event(sb.owner,sb.subject,3)).status,L::IdentityStatus::Exhausted);
    c.Status("T08.exhaustion-sticky",provider->Incarnation(Event(sb.owner,sb.subject,4)).status,L::IdentityStatus::Exhausted);
    std::uint64_t successor=77;
    c.Status("T08.uint64-max-no-wrap",L::CheckedSuccessor(UINT64_MAX,UINT64_MAX,successor),L::IdentityStatus::Exhausted);
    c.Equal("T08.exhaustion-preserves-output",successor,77);
    c.Status("T08.last-successor",L::CheckedSuccessor(UINT64_MAX-1,UINT64_MAX,successor),L::IdentityStatus::Ok);
    c.Equal("T08.last-uint64-value",successor,UINT64_MAX);
    // Exhaust the host's initialization-provisioned namespace table. No slot reuse/eviction.
    auto full=std::make_unique<L::HostIdentityDomain>(Sym("fixture.process.capacity"));
    Value(full->OpenSession(Event(C::OwnerDomain::Session,Key("session.scope"),1)));
    for (std::size_t i=0;i<L::HostIdentityDomain::NamespaceCapacity;++i)
    {
        auto one=Binding(C::OwnerDomain::Resource,"capacity");one.subject.value=i;
        c.Is("T08.bounded-bind",full->BindResource(one).has_value());
    }
    auto extra=Binding(C::OwnerDomain::Resource,"capacity.extra");
    c.Is("T08.capacity-refuses",!full->BindResource(extra));
    return c.Finish();
}
