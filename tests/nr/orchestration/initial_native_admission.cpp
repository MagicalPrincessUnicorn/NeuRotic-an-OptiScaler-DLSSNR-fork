#include "../contracts/Test002Support.h"
#include "InitialInvocationFixture.h"
using namespace NrTest002;
int main(int argc,char** argv)
{
    return Guard([&]{Report r("initial-native-admission",argc==2?std::string_view(argv[1]):std::string_view{});
        using namespace InitialNativeTests;
        Fixture f;auto active=f.Activate();
        r.Precondition("producer",active.current.has_value(),true,"actual canonical materializer");
        auto owner=std::make_shared<SyntheticInvocation>(f.root.get());auto a=Access::Admit(f.root,owner);
        r.Check("fresh_operation_authority",a.has_value(),true,"INIT-CPU-13");
        if(!a)return r.Finish();
        r.Check("claim_exactly_once",a->AcquirePrimary()&&!a->AcquirePrimary()&&a->Primary().has_value(),true,"INIT-CPU-23 actual LIFE003");
        auto guard=Access::Action(*a,P::NativeCheckPoint::BeforeModel);
        r.Check("owner_guard_spans_action",guard&&owner->guards==1,true,"INIT-CPU-28");
        f.coordinator.Revoke(O::RevocationCause::Disabled);
        r.Check("revoke_before_model_gate",guard&&!Access::Start(*a,*guard)&&!Access::Called(*a),true,"INIT-CPU-25 actual start gate");
        guard.reset();
        r.Check("stop_does_not_strand_seal",!f.root->AdvanceClosure().finalizerStopped,true,"INIT-CPU-31 outstanding unsealed primary");
        {
            Fixture win;win.Activate();auto o=std::make_shared<SyntheticInvocation>(win.root.get());auto w=Access::Admit(win.root,o);
            const bool primary=w&&w->AcquirePrimary();auto g=primary?Access::Action(*w,P::NativeCheckPoint::BeforeModel):nullptr;
            const bool started=g&&Access::Start(*w,*g);win.coordinator.Revoke(O::RevocationCause::Disabled);
            const bool called=started&&Access::Called(*w);
            r.Check("winning_start_truth",called&&!Access::Called(*w)&&!Access::Start(*w,*g),true,"INIT-CPU-26 exactly one actual seam mark");
            if(w)Access::Returned(*w);g.reset();
            auto continuation=w?Access::Action(*w,P::NativeCheckPoint::BeforeComposition):nullptr;
            r.Check("continuation_only",continuation&& !Access::Action(*w,P::NativeCheckPoint::BeforeModel),true,"INIT-CPU-27");
            continuation.reset();
            auto last=Access::Action(*w,P::NativeCheckPoint::BeforeComposition);last.reset();
            r.Check("continuation_exhausted",!Access::Action(*w,P::NativeCheckPoint::BeforeComposition),true,"INIT-CPU-27 finite action budget");
        }
        {
            Fixture fresh;fresh.Activate();auto first=std::make_shared<SyntheticInvocation>(fresh.root.get(),10);
            auto one=Access::Admit(fresh.root,first);auto next=std::make_shared<SyntheticInvocation>(fresh.root.get(),11);
            auto two=Access::Admit(fresh.root,next);
            r.Check("fresh_frames_stable_route",one&&two&&one->Product()->recipe.evaluation!=two->Product()->recipe.evaluation&&
                one->Product()->profileCertificate.header.record!=two->Product()->profileCertificate.header.record&&
                fresh.coordinator.Inspect().successfulIssues==1,true,"INIT-CPU-20");
            auto bad=std::make_shared<SyntheticInvocation>(fresh.root.get(),12);
            const auto badAttempt=[&]{const bool admitted=Access::Admit(fresh.root,bad).has_value();
                ++bad->frameNumber;return admitted;};
            bad->changePreparation=[](auto& s){++s.product.recipe.committedPlan.revision;};
            r.Check("exact_commit_reference",!badAttempt(),true,"INIT-CPU-14");
            bad->changePreparation=[](auto& s){s.product.recipe.committedPlan.recordType=Sym("ForeignPlan");};
            r.Check("exact_commit_type",!badAttempt(),true,"INIT-CPU-14 exact wire type");
            bad->changePreparation=[](auto& s){++s.product.recipe.committedPlan.record.value;};
            r.Check("exact_commit_key",!badAttempt(),true,"INIT-CPU-14 exact key");
            bad->changePreparation=[](auto& s){s.generation.nameSpace=Sym("foreign.same.number");s.leases.Get(0)->route=C::GenerationToken{s.generation.Describe()};};
            r.Check("full_identity",!badAttempt(),true,"INIT-CPU-13");
            for(unsigned i=0;i<3;++i)
            {
                bad->changePreparation=[i](auto& s){auto& b=*s.leases.Get(0);if(i==0)b.plan.reset();if(i==1)b.profile.reset();if(i==2)b.route.reset();};
                r.Check("mandatory_native_bindings."+std::to_string(i),!badAttempt(),true,"INIT-CPU-16");
            }
            bad->changePreparation=[](auto& s){s.leases.Get(0)->profile=Key("wrong-profile-record");};
            r.Check("profile_key_mapping",!badAttempt(),true,"INIT-CPU-17 C04 record identity");
            bad->changePreparation=[](auto& s){s.product.profileCertificate.purpose=Sym("RENDER.NativeStructuralBindings");};
            r.Check("structural_certificate_not_admission",!badAttempt(),true,"SP02 preparation-only C04 is not execution authority");
            bad->changePreparation={};auto b=Access::Admit(fresh.root,bad);
            if(b)
            {
                b->AcquirePrimary();bad->changeCurrent=[](auto& s){++s.recording.value;};
                r.Check("recording_isolation",!Access::Action(*b,P::NativeCheckPoint::BeforeModel),true,"INIT-CPU-19");
                bad->changeCurrent=[](auto& s){++s.consumer.value;};
                r.Check("operation_isolation",!Access::Action(*b,P::NativeCheckPoint::BeforeModel),true,"INIT-CPU-18");
                bad->changeCurrent=[](auto& s){++s.product.profileCertificate.header.revision;};
                r.Check("current_profile_revision",!Access::Action(*b,P::NativeCheckPoint::BeforeModel),true,"INIT-CPU-17 current C04 revision");
                bad->changeCurrent=[](auto& s){++s.product.recipe.evaluation.value;};
                r.Check("current_evaluation",!Access::Action(*b,P::NativeCheckPoint::BeforeModel),true,"INIT-CPU-18 exact evaluation");
                bad->changeCurrent=[](auto& s){s.leases.Get(0)->purpose=Sym("WrongPurpose");};
                r.Check("current_purpose",!Access::Action(*b,P::NativeCheckPoint::BeforeModel),true,"INIT-CPU-18 exact purpose");
                bad->changeCurrent=[](auto& s){s.metadata=std::make_shared<int>(1);};
                r.Check("current_metadata_owner",!Access::Action(*b,P::NativeCheckPoint::BeforeModel),true,
                    "AM05 equal-value foreign metadata closure cannot replace the retained invocation owner");
                bad->changeCurrent=[](auto& s){++s.reservation.value;};
                r.Check("current_reservation",!Access::Action(*b,P::NativeCheckPoint::BeforeModel),true,"INIT-CPU-19 exact reservation");
            }
        }
        {
            Fixture native;native.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;
            const auto route=native.Activate();
            r.Check("hostreturn_route_active",route.current.has_value(),true,"AM05 genuine HostReturn plan");
            auto first=std::make_shared<SyntheticInvocation>(native.root.get(),10);
            auto nativeAdmission=Access::Admit(native.root,first);
            const bool owned=nativeAdmission&&nativeAdmission->AcquirePrimary();
            r.Check("hostreturn_native_without_final_claim",owned&&nativeAdmission&&!nativeAdmission->Primary(),true,
                "AM05 Native attempt is owned without C13 reservation");
            auto duplicate=std::make_shared<SyntheticInvocation>(native.root.get(),10);
            r.Check("hostreturn_duplicate_sample_refused",!Access::Admit(native.root,duplicate),true,
                "AM04 callback wrappers cannot issue a second Native attempt");
            r.Check("hostreturn_duplicate_skips_preparation",duplicate->preparations==0,true,
                "AM05 duplicate source cannot repeat possibly effectful owner preparation before ledger reservation");
            auto action=owned?Access::Action(*nativeAdmission,P::NativeCheckPoint::BeforeModel):nullptr;
            r.Check("hostreturn_owner_guarded_model_start",action&&Access::Start(*nativeAdmission,*action)&&Access::Called(*nativeAdmission),true,
                "AM05 exact owner guard and one model start without a C13 seal");
            if(nativeAdmission)Access::Returned(*nativeAdmission);
            action.reset();nativeAdmission.reset();
            auto retiredDuplicate=std::make_shared<SyntheticInvocation>(native.root.get(),10);
            r.Check("hostreturn_retired_duplicate_refused",!Access::Admit(native.root,retiredDuplicate),true,
                "AM04 a retired callback cannot regain authority through a recycled coordinator slot");
            auto nextSample=std::make_shared<SyntheticInvocation>(native.root.get(),11);
            r.Check("hostreturn_next_sample_admitted",Access::Admit(native.root,nextSample).has_value(),true,
                "AM04 a genuinely newer source ordinal can use the retained fixed route");
        }
        {
            Fixture content;content.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;content.Activate();
            auto unknown=std::make_shared<SyntheticInvocation>(content.root.get(),10);
            unknown->changePreparation=[](auto& s){s.product.inputRepresentations.Get(0)->currentContent={};};
            r.Check("unknown_input_content_refused",!Access::Admit(content.root,unknown),true,
                "AM05 current game input needs a Resource-owned content revision before admission");
            auto stale=std::make_shared<SyntheticInvocation>(content.root.get(),11);
            stale->changePreparation=[](auto& s){s.product.inputRepresentations.Get(0)->currentContent=Known(C::ContentRevision{2});};
            r.Check("stale_input_content_refused",!Access::Admit(content.root,stale),true,
                "AM05 C12 source and current content revision must be exact");
        }
        {
            Fixture strict;strict.Activate();
            auto strictOwner=std::make_shared<SyntheticInvocation>(strict.root.get(),13);
            auto admission=Access::Admit(strict.root,strictOwner);
            bool refused=false;
            if(admission&&strictOwner->preparedRecord&&strictOwner->snapshot)
            {
                auto unknownFrameEvaluation=*strictOwner->snapshot;
                unknownFrameEvaluation.frame.evaluationId={};
                r.Check("strict_unknown_frame_evaluation_refused",
                    !P::NativeInvocationMatches(unknownFrameEvaluation,*strictOwner->preparedRecord),true,
                    "HostReturn source identity separation does not relax Strict frame identity");
                auto wrong=*strictOwner->preparedRecord;
                wrong.delivery.kind=C::NativeDeliveryKind::HostReturn;
                refused=!P::NativeInvocationMatches(*strictOwner->snapshot,wrong);
            }
            r.Check("delivery_kind_bound_to_route",refused,true,
                "AM05 identical metadata reference cannot change strict versus HostReturn authority");
        }
        return r.Finish();});
}
