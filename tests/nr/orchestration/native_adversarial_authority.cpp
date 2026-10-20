#include "../contracts/Test002Support.h"
#include "InitialInvocationFixture.h"
#include "NativeAdversarialGroups.h"
#include "NativeAdversarialClosure.h"
using namespace NrTest002;
int main(int argc,char** argv)
{
    return Guard([&]{Report r("native-adversarial-authority",argc==2?std::string_view(argv[1]):std::string_view{});
        using namespace InitialNativeTests;
        {
            // The imported copy is descriptive test data. Every opportunity and
            // model gate below still comes from the actual retained kernel.
            Fixture copied;copied.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;copied.Activate();
            auto seed=std::make_shared<SyntheticInvocation>(copied.root.get(),10);
            auto seedAdmission=Access::Admit(copied.root,seed);
            const auto materialization=seed->preparedRecord;
            seedAdmission.reset();seed.reset();
            auto before=std::make_shared<SyntheticInvocation>(copied.root.get(),11);
            before->importedMaterialization=materialization;
            auto admitted=Access::Admit(copied.root,before);
            const bool primary=admitted&&admitted->AcquirePrimary();
            auto action=primary?Access::Action(*admitted,P::NativeCheckPoint::BeforeModel):nullptr;
            const bool validCopy=materialization&&before->snapshot&&
                P::NativeInvocationMatches(*before->snapshot,*materialization);
            const bool called=action&&Access::Start(*admitted,*action)&&Access::Called(*admitted);
            r.Check("copied_plan_pre_revoke_start",validCopy&&called&&!Access::Called(*admitted),true,
                "N03 same exact copied materialization permits one actual start before revoke");
            if(called)Access::Returned(*admitted);action.reset();admitted.reset();
            auto pending=std::make_shared<SyntheticInvocation>(copied.root.get(),12);
            pending->importedMaterialization=materialization;
            auto pendingAdmission=Access::Admit(copied.root,pending);
            const bool pendingPrimary=pendingAdmission&&pendingAdmission->AcquirePrimary();
            auto pendingAction=pendingPrimary?Access::Action(*pendingAdmission,P::NativeCheckPoint::BeforeModel):nullptr;
            copied.coordinator.Revoke(O::RevocationCause::Disabled);
            r.Check("copied_plan_stays_structural_after_revoke",materialization&&pending->snapshot&&
                P::NativeInvocationMatches(*pending->snapshot,*materialization),true,
                "N03 revocation does not rewrite the copied C05 or immutable descriptive closure");
            const bool staleStart=pendingAction&&Access::Start(*pendingAdmission,*pendingAction);
            const bool staleCall=pendingAdmission&&Access::Called(*pendingAdmission);
            if(staleCall)Access::Returned(*pendingAdmission);pendingAction.reset();
            auto imported=std::make_shared<SyntheticInvocation>(copied.root.get(),13);
            imported->importedMaterialization=materialization;
            auto after=Access::Admit(copied.root,imported);
            r.Check("copied_plan_post_revoke_no_authority",pendingPrimary&&!staleStart&&!staleCall&&
                !after&&imported->preparations==0&&!copied.Activate().current,true,
                "N03 structurally valid old C05 cannot import membership or permit another model start");
        }
        {
            Fixture rekeyed;rekeyed.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;rekeyed.Activate();
            auto first=std::make_shared<SyntheticInvocation>(rekeyed.root.get(),10);
            std::weak_ptr<SyntheticInvocation> firstLifetime=first;
            auto firstAdmission=Access::Admit(rekeyed.root,first);
            const auto originalSample=firstAdmission?firstAdmission->Sample():std::nullopt;
            const auto materialization=first->preparedRecord;
            const bool primary=firstAdmission&&firstAdmission->AcquirePrimary();
            auto action=primary?Access::Action(*firstAdmission,P::NativeCheckPoint::BeforeModel):nullptr;
            const bool called=action&&Access::Start(*firstAdmission,*action)&&Access::Called(*firstAdmission);
            r.Check("rekeyed_source_first_call",originalSample&&materialization&&called&&!Access::Called(*firstAdmission),true,
                "N06 original actual HostReturn opportunity calls exactly once");
            if(called)Access::Returned(*firstAdmission);action.reset();firstAdmission.reset();first.reset();
            auto evaluation=std::make_shared<SyntheticInvocation>(rekeyed.root.get(),10);
            evaluation->evaluationOverride=110;
            const auto evaluationSample=materialization?evaluation->SourceSample(*materialization):std::nullopt;
            auto evaluationAdmission=Access::Admit(rekeyed.root,evaluation);
            r.Check("rekeyed_evaluation_no_opportunity",originalSample&&evaluationSample&&
                originalSample->SameSample(*evaluationSample)&&originalSample->callback==evaluationSample->callback&&
                !evaluationAdmission&&evaluation->preparations==0,true,
                "N06 same closed source sample with a fresh coherent EvaluationId cannot reserve another opportunity");
            evaluationAdmission.reset();
            auto route=std::make_shared<SyntheticInvocation>(rekeyed.root.get(),10);
            route->evaluationOverride=111;
            route->changePreparation=[](auto& s){s.generation.nameSpace=Sym("relabeled.same.sample");
                s.leases.Get(0)->route=C::GenerationToken{s.generation.Describe()};};
            const auto routeSample=materialization?route->SourceSample(*materialization):std::nullopt;
            auto routeAdmission=Access::Admit(rekeyed.root,route);
            r.Check("rekeyed_route_label_no_opportunity",originalSample&&routeSample&&
                originalSample->SameSample(*routeSample)&&originalSample->callback==routeSample->callback&&
                !routeAdmission&&route->preparations==0,true,
                "N06 untrusted evaluation and route relabeling cannot repeat even owner preparation for the same sample");
            routeAdmission.reset();
            r.Check("rekeyed_sample_retirement_preserved",called&&firstLifetime.expired(),true,
                "N06 actual sweep closed the returned guard-free HostReturn owner without erasing sample uniqueness");
            auto fresh=std::make_shared<SyntheticInvocation>(rekeyed.root.get(),11);fresh->evaluationOverride=112;
            auto freshAdmission=Access::Admit(rekeyed.root,fresh);
            const bool freshPrimary=freshAdmission&&freshAdmission->AcquirePrimary();
            auto freshAction=freshPrimary?Access::Action(*freshAdmission,P::NativeCheckPoint::BeforeModel):nullptr;
            const bool freshCall=freshAction&&Access::Start(*freshAdmission,*freshAction)&&Access::Called(*freshAdmission);
            r.Check("genuinely_new_sample_after_closure",originalSample&&freshAdmission&&freshAdmission->Sample()&&
                !originalSample->SameSample(*freshAdmission->Sample())&&freshCall&&
                freshAdmission->Product()->recipe.evaluation==Id<C::EvaluationId>("evaluation",112)&&
                rekeyed.coordinator.Inspect().successfulIssues==1,true,
                "N06 genuinely new source sample reserves and calls after rightful closure on the retained fixed route");
            if(freshCall)Access::Returned(*freshAdmission);freshAction.reset();
        }
        NativeAdversarialGroups::Selection(r);
        NativeAdversarialGroups::ClosedTickets(r);
        NativeAdversarialGroups::SampleBindings(r);
        NativeAdversarialGroups::FailureHistory(r);
        NativeAdversarialGroups::OptionalDiagnostics(r);
        NativeAdversarialGroups::PublicationFailure(r);
        NativeAdversarialGroups::LocalQualification(r);
        NativeAdversarialGroups::FeatureReplacement(r);
        NativeAdversarialGroups::UntouchedRefusal(r);
        NativeAdversarialGroups::AdmissionCapacity(r);
        NativeAdversarialGroups::SharedClosure(r);
        NativeAdversarialGroups::CallbackMetadataTail(r);
        NativeAdversarialGroups::OrdinalIsNotAssociation(r);
        return r.Finish();});
}
