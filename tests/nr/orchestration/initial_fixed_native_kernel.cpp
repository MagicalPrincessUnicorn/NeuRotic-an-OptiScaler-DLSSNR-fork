#include "../contracts/Test002Support.h"
#if __has_include(<nr/orchestration/StreamCoordinatorKernel.h>)
#include "InitialNativeFixture.h"
#define NR_INIT_KERNEL 1
#endif
using namespace NrTest002;
int main(int argc,char** argv)
{
    return Guard([&]{Report r("initial-fixed-native-kernel",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_INIT_KERNEL
        using namespace InitialNativeTests;
        {
            Fixture f;O::InitialNativeTestAccess::DiagnosticNoise(*f.root);auto first=f.Activate();
            r.Check("diagnostic_independence",first.current&&f.coordinator.Inspect().successfulIssues==1,true,"INIT-CPU-35 unrelated journals are not authority");
            auto other=f.root->Coordinator();auto second=other.TryActivate();
            r.Check("global_stop_topology",second.current&&first.current&&second.current->Plan()==first.current->Plan()&&
                other.Inspect().publications==1,true,"INIT-CPU-34 every facade shares the one private root/finalizer");
        }
        {
            Fixture f;const auto s=f.coordinator.Inspect();
            r.Check("cold_empty",s.phase==O::RoutePhase::Empty&&s.successfulIssues==0&&s.publications==0,true,"INIT-CPU-01");
            f.owner->enabled=false;
            r.Check("disabled_non_native",f.Activate().status==O::InitStatus::Disabled&&f.coordinator.Inspect().successfulIssues==0,true,"INIT-CPU-02");
            f.owner->enabled=true;f.owner->selectionWrong=true;
            r.Check("fixed_lineage_only",!f.Activate().current&&f.coordinator.Inspect().successfulIssues==0,true,"INIT-CPU-03");
        }
        {
            Fixture f;
            for(std::size_t i=0;i<O::InitialOwnerCount;++i)
            {
                f.owner->missingOwner=i;
                r.Check("every_required_owner."+std::to_string(i),!f.Activate().current&&f.coordinator.Inspect().successfulIssues==0,true,"INIT-CPU-04");
            }
            f.owner->missingOwner.reset();f.owner->legacyAttempted=true;
            r.Check("legacy_boundary_excluded",!f.Activate().current&&f.coordinator.Inspect().successfulIssues==0,true,"INIT-CPU-05");
            f.owner->legacyAttempted=false;auto activated=f.Activate();
            const auto counts=f.coordinator.Inspect();
            r.Check("real_initial_commit",activated.status==O::InitStatus::Activated&&activated.current&&
                counts.successfulIssues==1&&counts.publications==1,true,"INIT-CPU-06 actual producer/issuer");
            if(activated.current)
            {
                const auto& p=activated.current->Plan();
                r.Check("real_commit_owner",p.header.owner==C::OwnerDomain::StreamCoordinator&&p.committed.IsKnown()&&
                    p.committed.KnownPart()->value&&p.routeGeneration.IsKnown(),true,"Canonical C05 fields");
                const auto again=f.Activate();
                r.Check("one_slot_one_budget",again.status==O::InitStatus::ExistingReadOnly&&
                    f.coordinator.Inspect().successfulIssues==1&&f.coordinator.Inspect().publications==1,true,"INIT-CPU-11");
                auto facade=f.root->Coordinator();facade.Revoke(O::RevocationCause::Disabled);
                r.Check("known_revoked_record",!facade.TryReadCurrent().current&&p.committed.KnownPart()->value,true,"INIT-CPU-15 immutable history");
                r.Check("no_reincarnation",!f.Activate().current&&f.coordinator.Inspect().successfulIssues==1,true,"INIT-CPU-12");
            }
        }
        {
            Fixture f;f.owner->metadataMissing=true;
            r.Check("preissue_failures",!f.Activate().current&&f.coordinator.Inspect().successfulIssues==0,true,"INIT-CPU-08");
            f.owner->metadataMissing=false;
            r.Check("preissue_cleanup_retry",f.Activate().current.has_value(),true,"Fresh owner preparation after proven cleanup");
        }
        {
            Fixture f;f.SetFault(O::TestFault::IssuerBusy);
            r.Check("busy_is_not_issue",f.Activate().status==O::InitStatus::Busy&&f.coordinator.Inspect().successfulIssues==0,true,"INIT-CPU-09 controlled nonissuance branch");
            f.SetFault(O::TestFault::None);
            r.Check("busy_fresh_retry",f.Activate().current.has_value()&&f.coordinator.Inspect().successfulIssues==1,true,"Nonissuing retry only");
        }
        {
            Fixture f;f.SetFault(O::TestFault::AfterIssue);
            r.Check("issued_failure_terminal",!f.Activate().current&&f.coordinator.Inspect().successfulIssues==1,true,"INIT-CPU-10");
            f.SetFault(O::TestFault::None);
            r.Check("issued_failure_no_retry",!f.Activate().current&&f.coordinator.Inspect().successfulIssues==1,true,"No generation-2 recovery");
        }
        {
            Fixture f;auto active=f.Activate();f.coordinator.Revoke(O::RevocationCause::SessionClose);
            f.owner->drainUnknown=true;
            r.Check("aggregate_not_finalizer_only",!f.root->AdvanceClosure().closed,true,"INIT-CPU-33");
            r.Check("unknown_unsealed_quarantine",f.coordinator.Inspect().phase==O::RoutePhase::Quarantined,true,"INIT-CPU-32");
            f.owner->drainUnknown=false;
            r.Check("owner_proven_close",f.root->AdvanceClosure().closed,true,"Actual synthetic external-owner proof; production root");
            r.Check("closed_history_readable",active.current&&active.current->Plan().committed.KnownPart()->value,true,"INIT-CPU-29/30 retained immutable history");
        }
#else
        r.Check("cold_empty",false,true,"INIT-CPU-01: retained production coordinator absent");
#endif
        return r.Finish();});
}
