#include "../finalizer/finalizer_fixture.h"
#include <nr/lifecycle/LifecycleEvidence.h>
#include <iostream>
using namespace NrTest002;
namespace L=Neurotic::Lifecycle;
int main()
{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto fixture=std::make_unique<FinalizerTests::Fixture>();
    auto binding=fixture->binding;binding.description.use=C::UsageKind::OrderedGpuRead;binding.description.queue={};
    binding.sameRecording=L::SameRecordingUse{Key("recording"),Key("reservation"),1,2};
    auto facts=fixture->facts;facts.current=binding;facts.holds={};facts.callbackActive=L::Fact::Yes;
    facts.dependency.queueKnown=L::Fact::Unknown;
    facts.dependency.committed=facts.dependency.uniqueSubmission=facts.dependency.completed=
        facts.dependency.nonReplayable=facts.dependency.reusable=L::Fact::No;
    facts.sameRecording=L::SameRecordingFacts{*binding.sameRecording,L::Fact::Yes,L::Fact::Yes,L::Fact::Yes,L::Fact::Yes};
    const auto baseline=facts;
    auto lease=L::BeginConsumption(binding,facts);
    check(lease&&L::ValidateForRecording(*lease,facts).allowed,"N03 exact local recording can precede unknown host queue");
    if(lease)
    {
        check(!L::ValidateForSubmit(*lease,facts).allowed,"L03 local recording cannot certify unknown submission queue");
        const auto proof=L::PublishDependencyProof(*lease,facts,NrContextTests::Header(C::ContractId::C03,C::OwnerDomain::Resource,999),
            Known(Key("recording")),{},Known(Key("reservation")),Ev(1));
        check(proof&&!proof->Value().consumerQueue.IsKnown()&&!proof->Value().producerQueue.IsKnown()&&
              !proof->Value().submission.IsKnown()&&!proof->Value().gpuCompleted.KnownPart()->value,
              "L03 local proof preserves unknown queue and absent submission");
        const auto refuse=[&](const char* name){check(!L::ValidateForRecording(*lease,facts).allowed,name);facts=baseline;};
        facts.sameRecording->activeRecording=L::Fact::No;refuse("N03 local recording identity must remain active");
        facts.sameRecording->orderEstablished=L::Fact::Unknown;refuse("N03 local barrier must be established");
        facts.sameRecording->reservationRetained=L::Fact::No;refuse("N03 local reservation must remain held");
        facts.sameRecording->conflictingUsesExcluded=L::Fact::Unknown;refuse("L03 conflicting uses must be excluded");
        facts.dependency.sameDevice=L::Fact::Unknown;refuse("N03 local resource and recording device must match");
        facts.dependency.queueKnown=L::Fact::Yes;refuse("L03 unknown queue cannot become known by boolean");
        facts.dependency.crossQueue=true;refuse("L03 local permission never establishes cross-queue order");
        facts.holds=fixture->facts.holds;refuse("L03 unresolved provider hold stays blocking");
        L::CloseAdmission(*lease);check(!L::CanOwnerReuse(*lease,facts).allowed,"L03 local recording is not physical retirement");
    }
    auto ordinary=binding;ordinary.sameRecording.reset();facts=baseline;facts.current=ordinary;facts.sameRecording.reset();
    check(!L::BeginConsumption(ordinary,facts),"N03 ordinary dependency still requires known queue");
    std::cout<<"Local recording: "<<count<<" checks, "<<failed<<" failures; CPU rights specialization only\n";
    return failed?1:0;
}
