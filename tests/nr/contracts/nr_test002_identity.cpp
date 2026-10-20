#include "Test002Support.h"
#include "../identity/IdentityTestSupport.h"
// Only neutral fixture constructors from the accepted LIFE support are used.
// No LIFE expected-value projector, CheckProjection, or test normalizer is reused.
using namespace NrTest002;
namespace F = LifeFixture;
int main(int argc, char** argv)
{
    return Guard([&] {
        Report r("identity-freshness", argc == 2 ? std::string_view(argv[1]) : std::string_view{});
        const auto exact = [](const auto& fact, const auto& expected) {
            return fact.IsKnown() && fact.KnownPart()->value == expected;
        };
        const auto projection = [](const auto& result) -> const L::AssociationProjection* {
            return result.value.IsKnown() ? &result.value.KnownPart()->value : nullptr;
        };
        auto host = std::make_unique<L::HostIdentityDomain>(F::Sym("test002.timeline"));
        const auto session = host->OpenSession(F::Event(C::OwnerDomain::Session, F::Key("session.scope"), 1));
        r.Status("T07.session", session.status, L::IdentityStatus::Ok, "HostIdentityDomain::OpenSession");
        auto render = host->BindRender(F::Binding(C::OwnerDomain::IdentityRegistry, "render.scope"));
        auto evaluation = host->BindEvaluation(F::Binding(C::OwnerDomain::Strategy, "eval.scope"));
        auto present = host->BindPresent(F::Binding(C::OwnerDomain::Presentation, "present.scope"));
        r.Check("T07.bindings", render.has_value() && evaluation.has_value() && present.has_value(), true,
                "HostIdentityDomain::BindRender/BindEvaluation/BindPresent");
        if (!render || !evaluation || !present) return r.Finish();

        const auto base1 = render->NewBase({F::Event(C::OwnerDomain::IdentityRegistry, F::Key("render.scope"), 1),
                                            L::RenderObservation::NewGameContent});
        const auto render1 = render->Render(F::Event(C::OwnerDomain::IdentityRegistry, F::Key("render.scope"), 2));
        r.Status("T07.base.first-status", base1.status, L::IdentityStatus::Ok, "RenderIssuer::NewBase");
        r.Check("T07.base.first-known", base1.value.IsKnown(), true, "RenderIssuer::NewBase");
        r.Check("T07.render.first-known", render1.value.IsKnown(), true, "RenderIssuer::Render");
        if (!base1.value.IsKnown() || !render1.value.IsKnown()) return r.Finish();
        r.Equal("T07.base.first-value", base1.value.KnownPart()->value.value, 1, "RenderIssuer::NewBase");
        r.Equal("T07.render.first-value", render1.value.KnownPart()->value.value, 1, "RenderIssuer::Render");

        const auto eval1 = evaluation->Evaluation(F::Event(C::OwnerDomain::Strategy, F::Key("eval.scope"), 1));
        const auto eval2 = evaluation->Evaluation(F::Event(C::OwnerDomain::Strategy, F::Key("eval.scope"), 2));
        const auto present1 = present->Present(F::Event(C::OwnerDomain::Presentation, F::Key("present.scope"), 1));
        const auto present2 = present->Present(F::Event(C::OwnerDomain::Presentation, F::Key("present.scope"), 2));
        const auto presentGenerated = present->Present(F::Event(C::OwnerDomain::Presentation, F::Key("present.scope"), 3));
        r.Check("T07.events.known", eval1.value.IsKnown() && eval2.value.IsKnown() && present1.value.IsKnown() &&
                present2.value.IsKnown() && presentGenerated.value.IsKnown(), true,
                "EvaluationIssuer::Evaluation/PresentIssuer::Present");
        if (!eval1.value.IsKnown() || !eval2.value.IsKnown() || !present1.value.IsKnown() ||
            !present2.value.IsKnown() || !presentGenerated.value.IsKnown()) return r.Finish();
        r.Equal("T07.eval.second", eval2.value.KnownPart()->value.value, 2, "EvaluationIssuer::Evaluation");
        r.Equal("T07.present.second", present2.value.KnownPart()->value.value, 2, "PresentIssuer::Present");

        const auto nextEvent = F::Event(C::OwnerDomain::IdentityRegistry, F::Key("render.scope"), 3);
        r.Status("T07.generated.no-base", render->NewBase({nextEvent, L::RenderObservation::GeneratedOutput}).status,
                L::IdentityStatus::NotNewReal, "RenderIssuer::NewBase");
        r.Status("T07.repeated.no-base", render->NewBase({nextEvent, L::RenderObservation::RepeatedContent}).status,
                L::IdentityStatus::NotNewReal, "RenderIssuer::NewBase");
        r.Status("T07.existing.no-base", render->NewBase({nextEvent, L::RenderObservation::ExistingGameContent}).status,
                L::IdentityStatus::NotNewReal, "RenderIssuer::NewBase");
        r.Status("T07.unknown.no-base", render->NewBase({nextEvent, L::RenderObservation::Unknown}).status,
                L::IdentityStatus::Unknown, "RenderIssuer::NewBase");
        auto base2 = render->NewBase({nextEvent, L::RenderObservation::NewGameContent});
        r.Check("T07.base.after-events-known", base2.value.IsKnown(), true, "RenderIssuer::NewBase");
        // Preserve the same-event assertion above. A fresh real event only permits independent
        // association checks to finish when a mutant wrongly consumed the refused event.
        if (!base2.value.IsKnown())
            base2 = render->NewBase({F::Event(C::OwnerDomain::IdentityRegistry, F::Key("render.scope"), 5),
                                     L::RenderObservation::NewGameContent});
        const auto render2 = render->Render(F::Event(C::OwnerDomain::IdentityRegistry, F::Key("render.scope"), 6));
        const auto eval3 = evaluation->Evaluation(F::Event(C::OwnerDomain::Strategy, F::Key("eval.scope"), 3));
        const auto presentNext = present->Present(F::Event(C::OwnerDomain::Presentation, F::Key("present.scope"), 4));
        r.Check("T07.next-events-known", render2.value.IsKnown() && eval3.value.IsKnown() && presentNext.value.IsKnown(),
                true, "RenderIssuer::Render/EvaluationIssuer::Evaluation/PresentIssuer::Present");
        if (!base2.value.IsKnown() || !render2.value.IsKnown() || !eval3.value.IsKnown() || !presentNext.value.IsKnown())
            return r.Finish();
        r.Equal("T07.base.only-two", base2.value.KnownPart()->value.value, 2, "RenderIssuer::NewBase");

        const auto scope = F::Scope();
        auto renderAuthority = F::Authority(C::OwnerDomain::IdentityRegistry, scope);
        auto evaluationAuthority = F::Authority(C::OwnerDomain::Strategy, scope);
        auto presentAuthority = F::Authority(C::OwnerDomain::Presentation, scope);

        const auto renderBase1 = L::MakeAssociation(renderAuthority, F::Edge(L::AssociationRelation::RenderToBase,
            render1.value.KnownPart()->value.Describe(), base1.value.KnownPart()->value.Describe(), renderAuthority));
        const auto* renderBase1Value = projection(renderBase1);
        r.Status("T07.projection.render-base.status", renderBase1.status, L::IdentityStatus::Ok, "MakeAssociation");
        r.Check("T07.projection.render-base.render", renderBase1Value &&
                exact(renderBase1Value->frame.renderId, render1.value.KnownPart()->value), true, "MakeAssociation");
        r.Check("T07.projection.render-base.base", renderBase1Value &&
                exact(renderBase1Value->frame.baseRealFrameId, base1.value.KnownPart()->value), true, "MakeAssociation");

        const auto evalRender = L::MakeAssociation(evaluationAuthority, F::Edge(L::AssociationRelation::EvaluationToRender,
            eval1.value.KnownPart()->value.Describe(), render1.value.KnownPart()->value.Describe(), evaluationAuthority));
        const auto* evalRenderValue = projection(evalRender);
        r.Status("T07.projection.eval-render.status", evalRender.status, L::IdentityStatus::Ok, "MakeAssociation");
        r.Check("T07.projection.eval-render.evaluation", evalRenderValue &&
                exact(evalRenderValue->frame.evaluationId, eval1.value.KnownPart()->value), true, "MakeAssociation");
        r.Check("T07.projection.eval-render.render", evalRenderValue &&
                exact(evalRenderValue->frame.renderId, render1.value.KnownPart()->value), true, "MakeAssociation");

        const auto evalBase1 = L::MakeAssociation(evaluationAuthority, F::Edge(L::AssociationRelation::EvaluationToBase,
            eval1.value.KnownPart()->value.Describe(), base1.value.KnownPart()->value.Describe(), evaluationAuthority));
        const auto evalBase2 = L::MakeAssociation(evaluationAuthority, F::Edge(L::AssociationRelation::EvaluationToBase,
            eval2.value.KnownPart()->value.Describe(), base1.value.KnownPart()->value.Describe(), evaluationAuthority));
        const auto* evalBase1Value = projection(evalBase1);
        const auto* evalBase2Value = projection(evalBase2);
        r.Status("T07.projection.eval1-base.status", evalBase1.status, L::IdentityStatus::Ok, "MakeAssociation");
        r.Status("T07.projection.eval2-base.status", evalBase2.status, L::IdentityStatus::Ok, "MakeAssociation");
        r.Check("T07.projection.eval1-base.evaluation", evalBase1Value &&
                exact(evalBase1Value->frame.evaluationId, eval1.value.KnownPart()->value), true, "MakeAssociation");
        r.Check("T07.projection.eval2-base.evaluation", evalBase2Value &&
                exact(evalBase2Value->frame.evaluationId, eval2.value.KnownPart()->value), true, "MakeAssociation");
        r.Check("T07.projection.eval1-base.base", evalBase1Value &&
                exact(evalBase1Value->frame.baseRealFrameId, base1.value.KnownPart()->value), true, "MakeAssociation");
        r.Check("T07.projection.eval2-base.base", evalBase2Value &&
                exact(evalBase2Value->frame.baseRealFrameId, base1.value.KnownPart()->value), true, "MakeAssociation");
        r.Check("T07.projection.evaluations-distinct", evalBase1Value && evalBase2Value &&
                evalBase1Value->frame.evaluationId.IsKnown() && evalBase2Value->frame.evaluationId.IsKnown() &&
                evalBase1Value->frame.evaluationId.KnownPart()->value != evalBase2Value->frame.evaluationId.KnownPart()->value,
                true, "independent evaluation identities");

        const auto presentBase1 = L::MakeAssociation(presentAuthority, F::Edge(L::AssociationRelation::PresentToBase,
            present1.value.KnownPart()->value.Describe(), base1.value.KnownPart()->value.Describe(), presentAuthority));
        const auto* presentBase1Value = projection(presentBase1);
        r.Status("T07.projection.present-base.status", presentBase1.status, L::IdentityStatus::Ok, "MakeAssociation");
        r.Check("T07.projection.present-base.present", presentBase1Value &&
                exact(presentBase1Value->frame.presentId, present1.value.KnownPart()->value), true, "MakeAssociation");
        r.Check("T07.projection.present-base.base", presentBase1Value &&
                exact(presentBase1Value->frame.baseRealFrameId, base1.value.KnownPart()->value), true, "MakeAssociation");

        const auto repeat = L::MakeAssociation(presentAuthority, F::Edge(L::AssociationRelation::RepeatedPresentToPresent,
            present2.value.KnownPart()->value.Describe(), present1.value.KnownPart()->value.Describe(), presentAuthority));
        const auto* repeatValue = projection(repeat);
        r.Check("T07.repeat.projection", repeatValue && exact(repeatValue->frame.presentId, present2.value.KnownPart()->value) &&
                exact(repeatValue->repeatedFrom, present1.value.KnownPart()->value) &&
                !repeatValue->frame.baseRealFrameId.IsKnown(), true, "MakeAssociation");

        L::GeneratedIdentityAuthority ga;
        ga.binding = F::Binding(C::OwnerDomain::FrameGeneration, "generated.scope");
        ga.currentScope = scope; ga.currentProvider = F::Id<C::ProviderIncarnation>("provider", 5);
        ga.generatedNamespace = F::Sym("generated.provider");
        L::GeneratedIdentityClaim gc;
        gc.identity = F::Known(F::Id<C::GeneratedFrameId>("generated.provider", 99), 30);
        gc.provider = F::Known(ga.currentProvider, 29); gc.scope = scope;
        gc.ownerEvidence = F::Event(ga.binding.owner, ga.binding.subject, 30);
        const auto imported = L::ImportGeneratedIdentity(ga, gc);
        r.Status("T07.generated.import", imported.status, L::IdentityStatus::Ok, "ImportGeneratedIdentity");
        auto badAuthority = ga; badAuthority.binding.owner = C::OwnerDomain::Diagnostics;
        r.Status("T07.generated.wrong-owner", L::ImportGeneratedIdentity(badAuthority, gc).status,
                L::IdentityStatus::WrongOwner, "ImportGeneratedIdentity");
        auto stale = gc; stale.provider = F::Known(F::Id<C::ProviderIncarnation>("provider", 4), 29);
        r.Status("T07.generated.old-provider", L::ImportGeneratedIdentity(ga, stale).status,
                L::IdentityStatus::Different, "ImportGeneratedIdentity");
        auto unavailable = gc; unavailable.identity = {};
        r.Status("T07.generated.opaque", L::ImportGeneratedIdentity(ga, unavailable).status,
                L::IdentityStatus::Unknown, "ImportGeneratedIdentity");
        if (imported.value.IsKnown())
        {
            auto generatedAuthority = F::Authority(C::OwnerDomain::FrameGeneration, scope);
            generatedAuthority.currentGeneratedProvider = F::Known(ga.currentProvider);
            auto generatedEdge = F::Edge(L::AssociationRelation::GeneratedToSourceReal,
                gc.identity.KnownPart()->value.Describe(), base1.value.KnownPart()->value.Describe(), generatedAuthority);
            generatedEdge.generatedOwnerFact = imported.value.KnownPart()->value;
            const auto generatedSource = L::MakeAssociation(generatedAuthority, generatedEdge);
            const auto* generatedSourceValue = projection(generatedSource);
            r.Status("T07.generated.source.status", generatedSource.status, L::IdentityStatus::Ok, "MakeAssociation");
            r.Check("T07.generated.source.generated", generatedSourceValue &&
                    exact(generatedSourceValue->frame.generatedFrameId, gc.identity.KnownPart()->value), true, "MakeAssociation");
            r.Check("T07.generated.source.real", generatedSourceValue &&
                    exact(generatedSourceValue->sourceRealFrame, base1.value.KnownPart()->value), true, "MakeAssociation");
            r.Check("T07.generated.source.base-absent", generatedSourceValue &&
                    !generatedSourceValue->frame.baseRealFrameId.IsKnown(), true, "MakeAssociation");

            auto generatedPresentAuthority = presentAuthority;
            generatedPresentAuthority.currentGeneratedProvider = F::Known(ga.currentProvider);
            auto generatedPresentEdge = F::Edge(L::AssociationRelation::PresentToGenerated,
                presentGenerated.value.KnownPart()->value.Describe(), gc.identity.KnownPart()->value.Describe(),
                generatedPresentAuthority);
            generatedPresentEdge.generatedOwnerFact = imported.value.KnownPart()->value;
            const auto generatedPresent = L::MakeAssociation(generatedPresentAuthority, generatedPresentEdge);
            const auto* generatedPresentValue = projection(generatedPresent);
            r.Status("T07.generated.present.status", generatedPresent.status, L::IdentityStatus::Ok, "MakeAssociation");
            r.Check("T07.generated.present.present", generatedPresentValue &&
                    exact(generatedPresentValue->frame.presentId, presentGenerated.value.KnownPart()->value), true, "MakeAssociation");
            r.Check("T07.generated.present.generated", generatedPresentValue &&
                    exact(generatedPresentValue->frame.generatedFrameId, gc.identity.KnownPart()->value), true, "MakeAssociation");
            r.Check("T07.generated.present.base-absent", generatedPresentValue &&
                    !generatedPresentValue->frame.baseRealFrameId.IsKnown(), true, "MakeAssociation");
        }

        const auto renderBase2 = L::MakeAssociation(renderAuthority, F::Edge(L::AssociationRelation::RenderToBase,
            render2.value.KnownPart()->value.Describe(), base2.value.KnownPart()->value.Describe(), renderAuthority));
        const auto evalBase3 = L::MakeAssociation(evaluationAuthority, F::Edge(L::AssociationRelation::EvaluationToBase,
            eval3.value.KnownPart()->value.Describe(), base2.value.KnownPart()->value.Describe(), evaluationAuthority));
        const auto presentBase2 = L::MakeAssociation(presentAuthority, F::Edge(L::AssociationRelation::PresentToBase,
            presentNext.value.KnownPart()->value.Describe(), base2.value.KnownPart()->value.Describe(), presentAuthority));
        const auto* renderBase2Value = projection(renderBase2);
        const auto* evalBase3Value = projection(evalBase3);
        const auto* presentBase2Value = projection(presentBase2);
        r.Check("T07.next.render-lineage", renderBase2Value &&
                exact(renderBase2Value->frame.renderId, render2.value.KnownPart()->value) &&
                exact(renderBase2Value->frame.baseRealFrameId, base2.value.KnownPart()->value), true, "MakeAssociation");
        r.Check("T07.next.evaluation-lineage", evalBase3Value &&
                exact(evalBase3Value->frame.evaluationId, eval3.value.KnownPart()->value) &&
                exact(evalBase3Value->frame.baseRealFrameId, base2.value.KnownPart()->value) &&
                evalBase3Value->frame.baseRealFrameId.KnownPart()->value != base1.value.KnownPart()->value,
                true, "MakeAssociation");
        r.Check("T07.next.present-lineage", presentBase2Value &&
                exact(presentBase2Value->frame.presentId, presentNext.value.KnownPart()->value) &&
                exact(presentBase2Value->frame.baseRealFrameId, base2.value.KnownPart()->value) &&
                presentBase2Value->frame.baseRealFrameId.KnownPart()->value != base1.value.KnownPart()->value,
                true, "MakeAssociation");

        auto finalizerAuthority = F::Authority(C::OwnerDomain::Finalizer, scope);
        const auto finalSeal = F::Id<C::FinalSealId>("finalizer.seal", 71);
        const auto finalEdge = L::MakeAssociation(finalizerAuthority, F::Edge(L::AssociationRelation::FinalSealToBase,
            finalSeal.Describe(), base1.value.KnownPart()->value.Describe(), finalizerAuthority));
        r.Check("T07.finalizer.supplied-seal", finalEdge.value.IsKnown() &&
                exact(finalEdge.value.KnownPart()->value.frame.finalSealId, finalSeal), true, "MakeAssociation");

        const auto value = F::Id<C::ResourceIncarnation>("resource.a", 13).Describe();
        auto alternate = value; alternate.nameSpace = F::Sym("resource.b");
        r.Status("T08.identity.namespace", L::CompareIdentity(F::Known(value), F::Known(alternate)), L::IdentityStatus::Different, "CompareIdentity");
        alternate = value; alternate.issuer = F::Sym("another.owner");
        r.Status("T08.identity.issuer", L::CompareIdentity(F::Known(value), F::Known(alternate)), L::IdentityStatus::Different, "CompareIdentity");
        alternate = value; alternate.kind = C::IdentityKind::ResourceGeneration;
        r.Status("T08.identity.kind", L::CompareIdentity(F::Known(value), F::Known(alternate)), L::IdentityStatus::Different, "CompareIdentity");
        r.Status("T08.identity.unknown", L::CompareIdentity(F::Known(value), {}), L::IdentityStatus::Unknown, "CompareIdentity");
        r.Status("T08.identity.republication", L::CompareIdentity(F::Known(value, 1), F::Known(value, 99)), L::IdentityStatus::Ok, "CompareIdentity");
        const auto zeroIdentity = F::Id<C::ResourceIncarnation>("resource.zero", 0).Describe();
        r.Status("T08.identity.zero-equals-zero", L::CompareIdentity(F::Known(zeroIdentity), F::Known(zeroIdentity)),
                L::IdentityStatus::Ok, "CompareIdentity");
        r.Status("T08.identity.zero-vs-unknown", L::CompareIdentity(F::Known(zeroIdentity), {}),
                L::IdentityStatus::Unknown, "CompareIdentity");
        auto otherView = F::Edge(L::AssociationRelation::EvaluationToBase, eval1.value.KnownPart()->value.Describe(),
                                 base1.value.KnownPart()->value.Describe(), evaluationAuthority);
        otherView.targetScope = F::Scope("stream.A", "view.right");
        r.Status("T08.association.view", L::MakeAssociation(evaluationAuthority, otherView).status,
                L::IdentityStatus::Different, "MakeAssociation");
        auto otherStream = otherView; otherStream.targetScope = F::Scope("stream.B");
        r.Status("T08.association.stream", L::MakeAssociation(evaluationAuthority, otherStream).status,
                L::IdentityStatus::Different, "MakeAssociation");
        auto oldScope = otherView; oldScope.targetScope = F::Scope("stream.A", "view.main", 2);
        r.Status("T08.association.incarnation", L::MakeAssociation(evaluationAuthority, oldScope).status,
                L::IdentityStatus::Different, "MakeAssociation");
        auto noEvidence = F::Edge(L::AssociationRelation::EvaluationToBase, eval1.value.KnownPart()->value.Describe(),
                                  base1.value.KnownPart()->value.Describe(), evaluationAuthority);
        noEvidence.targetScope.view = C::OptionalFact<C::ViewId>::FromKnown(
            F::Id<C::ViewId>("view.main", 1), C::EvidenceRef{});
        r.Status("T08.association.evidence", L::MakeAssociation(evaluationAuthority, noEvidence).status,
                L::IdentityStatus::InvalidEvidence, "MakeAssociation");

        const std::uint64_t addressLabelBefore = 0xc0ffee, addressLabelAfter = 0xc0ffee;
        // Address labels never enter an SUT call or serialized identity.
        const auto resourceA = F::Resource(9, 100), resourceB = F::Resource(10, 100);
        r.Precondition("T08.address-label.same", addressLabelBefore == addressLabelAfter, true,
                       "descriptive same-address fixture label");
        r.Status("T08.pointer-reuse.new-incarnation", L::CompareExactContent(resourceA, resourceB, {true, true}),
                L::IdentityStatus::Different, "CompareExactContent");
        C::ResourceIdentityToken invalidToken = resourceA;
        invalidToken.resourceGeneration = F::Known(F::Gen(C::IdentityKind::RepresentationGeneration, "resource.family", 42));
        r.Error("T08.generation.wrong-slot", invalidToken.Check(), C::Error::WrongIdentityKind, "ResourceIdentityToken::Check");
        C::ResourceIdentityToken generationInIncarnation = resourceA;
        generationInIncarnation.objectIncarnation = F::Known(
            F::Gen(C::IdentityKind::ResourceGeneration, "resource.family", 42).identity);
        r.Error("T08.incarnation.generation-misplaced", generationInIncarnation.Check(), C::Error::WrongIdentityKind,
                "ResourceIdentityToken::Check");
        auto objectIssuer = host->BindObject(F::Binding(C::OwnerDomain::IdentityRegistry, "object.scope"));
        r.Check("T08.object.bound", objectIssuer.has_value(), true, "HostIdentityDomain::BindObject");
        if (objectIssuer)
        {
            const auto first = objectIssuer->Incarnation(F::Event(C::OwnerDomain::IdentityRegistry, F::Key("object.scope"), 1));
            const auto second = objectIssuer->Incarnation(F::Event(C::OwnerDomain::IdentityRegistry, F::Key("object.scope"), 2));
            r.Check("T08.object.recreated", first.value.IsKnown() && second.value.IsKnown() &&
                    first.value.KnownPart()->value.value == 1 && second.value.KnownPart()->value.value == 2,
                    true, "ObjectIssuer::Incarnation");
        }

        auto resource = host->BindResource(F::Binding(C::OwnerDomain::Resource, "resource.scope"));
        auto route = host->BindRoute(F::Binding(C::OwnerDomain::StreamCoordinator, "route.scope"));
        auto history = host->BindHistory(F::Binding(C::OwnerDomain::History, "history.scope"));
        r.Check("T09.channels", resource.has_value() && route.has_value() && history.has_value(), true,
                "HostIdentityDomain::BindResource/BindRoute/BindHistory");
        if (!resource || !route || !history) return r.Finish();
        const auto ri = resource->Resource(F::Event(C::OwnerDomain::Resource, F::Key("resource.scope"), 1));
        const auto rg = resource->Structure(F::Event(C::OwnerDomain::Resource, F::Key("resource.scope"), 2));
        const auto route1 = route->CommittedChange(F::Event(C::OwnerDomain::StreamCoordinator, F::Key("route.scope"), 1));
        const auto history1 = history->AppliedReset(F::Event(C::OwnerDomain::History, F::Key("history.scope"), 1));
        r.Check("T09.initial.known", ri.value.IsKnown() && rg.value.IsKnown() && route1.value.IsKnown() && history1.value.IsKnown(),
                true, "ResourceIssuer/RouteIssuer/HistoryIssuer");
        if (!ri.value.IsKnown()) return r.Finish();
        const auto cr1 = resource->ContentWrite(F::Event(C::OwnerDomain::Resource, F::Key("resource.scope"), 3), ri.value.KnownPart()->value);
        const auto cr2 = resource->ContentWrite(F::Event(C::OwnerDomain::Resource, F::Key("resource.scope"), 4), ri.value.KnownPart()->value);
        r.Check("T09.content.explicit-writes", cr1.value.IsKnown() && cr2.value.IsKnown() &&
                cr1.value.KnownPart()->value.value == 1 && cr2.value.KnownPart()->value.value == 2, true,
                "ResourceIssuer::ContentWrite");
        const auto route2 = route->CommittedChange(F::Event(C::OwnerDomain::StreamCoordinator, F::Key("route.scope"), 2));
        const auto history2 = history->AppliedReset(F::Event(C::OwnerDomain::History, F::Key("history.scope"), 2));
        const auto structure2 = resource->Structure(F::Event(C::OwnerDomain::Resource, F::Key("resource.scope"), 5));
        r.Check("T09.content.no-route-churn", route2.value.IsKnown() && route2.value.KnownPart()->value.value == 2,
                true, "RouteIssuer::CommittedChange");
        r.Check("T09.content.no-history-churn", history2.value.IsKnown() && history2.value.KnownPart()->value.value == 2,
                true, "HistoryIssuer::AppliedReset");
        r.Check("T09.content.no-structure-churn", structure2.value.IsKnown() && structure2.value.KnownPart()->value.value == 2,
                true, "ResourceIssuer::Structure");

        auto selection = F::Selection();
        auto signature1 = std::make_unique<L::ResolvedStructuralSignature>();
        auto signatureRepublished = std::make_unique<L::ResolvedStructuralSignature>();
        auto signatureRoute = std::make_unique<L::ResolvedStructuralSignature>();
        auto signatureResource = std::make_unique<L::ResolvedStructuralSignature>();
        auto signatureRepresentation = std::make_unique<L::ResolvedStructuralSignature>();
        auto signatureRecord = std::make_unique<L::ResolvedStructuralSignature>();
        r.Status("T09.signature.first", L::BuildSignature(F::Generations(42, 7, 9, 1), selection, F::Publication(), *signature1),
                L::IdentityStatus::Ok, "BuildSignature");
        r.Status("T09.signature.republished", L::BuildSignature(F::Generations(42, 7, 9, 2), selection, F::Publication(1002), *signatureRepublished),
                L::IdentityStatus::Ok, "BuildSignature");
        r.Status("T09.signature.route-build", L::BuildSignature(F::Generations(42, 7, 10, 1), selection, F::Publication(1003), *signatureRoute),
                L::IdentityStatus::Ok, "BuildSignature");
        r.Status("T09.signature.resource-build", L::BuildSignature(F::Generations(43, 7, 9, 1), selection, F::Publication(1004), *signatureResource),
                L::IdentityStatus::Ok, "BuildSignature");
        r.Status("T09.signature.representation-build", L::BuildSignature(F::Generations(42, 8, 9, 1), selection, F::Publication(1005), *signatureRepresentation),
                L::IdentityStatus::Ok, "BuildSignature");
        auto recordSelection = F::Selection();
        if (!recordSelection.structuralRecords.Get(0)) throw std::runtime_error("Structural record fixture missing");
        *recordSelection.structuralRecords.Get(0) = F::Key("stable.stream-view-profile", 2);
        r.Status("T09.signature.record-build", L::BuildSignature(F::Generations(42, 7, 9, 1), recordSelection, F::Publication(1006), *signatureRecord),
                L::IdentityStatus::Ok, "BuildSignature");
        r.Status("T09.signature.selected-only", L::CompareSignatures(*signature1, *signatureRepublished),
                L::IdentityStatus::Ok, "CompareSignatures");
        r.Status("T09.signature.selected-route-different", L::CompareSignatures(*signature1, *signatureRoute),
                L::IdentityStatus::Different, "CompareSignatures");
        r.Status("T09.signature.selected-resource-different", L::CompareSignatures(*signature1, *signatureResource),
                L::IdentityStatus::Different, "CompareSignatures");
        r.Status("T09.signature.selected-representation-different", L::CompareSignatures(*signature1, *signatureRepresentation),
                L::IdentityStatus::Different, "CompareSignatures");
        r.Status("T09.signature.selected-record-different", L::CompareSignatures(*signature1, *signatureRecord),
                L::IdentityStatus::Different, "CompareSignatures");
        const auto selectedMatches = [&](C::IdentityKind kind, std::string_view ns, std::uint64_t valueExpected) {
            if (!signature1->generationBody) return false;
            for (const auto& token : signature1->generationBody->second.Entries())
                if (token.identity.kind == kind && token.identity.nameSpace.View() == ns &&
                    token.identity.issuer.View() == "fixture.owner" && token.identity.value == valueExpected) return true;
            return false;
        };
        r.Check("T09.signature.resource-token-exact", selectedMatches(C::IdentityKind::ResourceGeneration, "resource.family", 42),
                true, "BuildSignature selected token");
        r.Check("T09.signature.representation-token-exact", selectedMatches(C::IdentityKind::RepresentationGeneration, "representation", 7),
                true, "BuildSignature selected token");
        r.Check("T09.signature.route-token-exact", selectedMatches(C::IdentityKind::RouteGeneration, "route", 9),
                true, "BuildSignature selected token");
        if (signature1->generationBody)
            r.Equal("T09.signature.three-axes", signature1->generationBody->second.Entries().Size(), 3, "BuildSignature");

        auto newContent = resourceA; newContent.contentRevision = F::Known(C::ContentRevision{101});
        r.Status("T09.content.stale", L::CompareExactContent(resourceA, newContent, {true, true}),
                L::IdentityStatus::Different, "CompareExactContent");
        newContent.contentRevision = {};
        r.Status("T09.content.unknown", L::CompareExactContent(resourceA, newContent, {true, true}),
                L::IdentityStatus::Unknown, "CompareExactContent");
        auto zeroContent = resourceA; zeroContent.contentRevision = F::Known(C::ContentRevision{0});
        auto unknownContent = zeroContent; unknownContent.contentRevision = {};
        r.Status("T09.content.zero-equals-zero", L::CompareExactContent(zeroContent, zeroContent, {true, true}),
                L::IdentityStatus::Ok, "CompareExactContent");
        r.Status("T09.content.zero-vs-unknown", L::CompareExactContent(zeroContent, unknownContent, {true, true}),
                L::IdentityStatus::Unknown, "CompareExactContent");
        r.Status("T09.signature.content-independent", L::CompareSignatures(*signature1, *signatureRepublished),
                L::IdentityStatus::Ok, "CompareSignatures");

        C::HistoryKey h; h.owner = F::Key("history.owner"); h.slot = F::Sym("main");
        h.dependencies = signature1->publication; h.generation = F::Known(F::Id<C::HistoryGeneration>("history", 3));
        auto h2 = h; h2.dependencies = signatureRepublished->publication;
        r.Status("T09.history.compatible", L::CompareHistoryDependencies(h, *signature1, h2, *signatureRepublished),
                L::IdentityStatus::Ok, "CompareHistoryDependencies");
        auto absent = std::make_unique<L::ResolvedStructuralSignature>(*signature1); absent->generationBody.reset();
        r.Status("T09.signature.unestablished", L::CompareSignatures(*signature1, *absent),
                L::IdentityStatus::Unknown, "CompareSignatures");
        const L::GenerationAxis axis {C::IdentityKind::RepresentationGeneration, F::Sym("representation"), F::Sym("fixture.owner")};
        const C::GenerationChange change {F::Known(F::Gen(C::IdentityKind::RepresentationGeneration, "representation", 7)),
                                         F::Known(F::Gen(C::IdentityKind::RepresentationGeneration, "representation", 8))};
        const auto affected = L::ChangeAffects(*signature1, axis, change, F::Ev(77));
        r.Check("T09.reset.described-only", affected.value.IsKnown() && affected.value.KnownPart()->value, true, "ChangeAffects");
        r.Status("T09.reset.absent-not-false", L::ChangeAffects(*absent, axis, change, F::Ev(77)).status,
                L::IdentityStatus::Unknown, "ChangeAffects");
        std::uint64_t next = 88;
        r.Status("T08.successor.exhausted", L::CheckedSuccessor(18446744073709551615ULL, 18446744073709551615ULL, next),
                L::IdentityStatus::Exhausted, "CheckedSuccessor");
        r.Equal("T08.successor.no-wrap", next, 88, "CheckedSuccessor");
        return r.Finish();
    });
}
