#include "../finalizer/finalizer_fixture.h"
#if __has_include(<nr/lifecycle/SameRecordingUse.h>)
#include <nr/lifecycle/SameRecordingUse.h>
#include <nr/lifecycle/LifecycleEvidence.h>
#define NR_SAME_RECORDING 1
#endif
using namespace NrTest002;
namespace L=Neurotic::Lifecycle;
template<class Facts>void Compatible(Facts& facts,const C::RecordKey& registration)
{if constexpr(requires{facts.compatibleHolds.Push(registration);})facts.compatibleHolds.Push(registration);}
template<class Use>void ProviderUse(Use& use,const C::RecordKey& registration)
{if constexpr(requires{use.providerRegistration=registration;})use.providerRegistration=registration;}
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-same-recording",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_SAME_RECORDING
    auto fixture=std::make_unique<FinalizerTests::Fixture>();
    auto b=fixture->binding;b.description.use=C::UsageKind::OrderedGpuRead;
    b.sameRecording=L::SameRecordingUse{Key("recording"),Key("reservation"),1,2};
    auto f=fixture->facts;f.current=b;f.holds={};f.callbackActive=L::Fact::Yes;
    f.dependency.committed=L::Fact::No;f.dependency.uniqueSubmission=L::Fact::No;
    f.dependency.nonReplayable=L::Fact::No;f.dependency.reusable=L::Fact::No;
    f.sameRecording=L::SameRecordingFacts{*b.sameRecording,L::Fact::Yes,L::Fact::Yes,L::Fact::Yes,L::Fact::Yes};
    const auto baseline=f;
    auto lease=L::BeginConsumption(b,f);
    r.Check("T16.local-order-without-submission",lease&&L::ValidateForRecording(*lease,f).allowed,true,"same-recording admission");
    if(!lease)return r.Finish();
    r.Check("T16.local-order-not-reuse",!L::CanOwnerReuse(*lease,f).allowed,true,"CanOwnerReuse");
    const auto reject=[&](std::string_view name){r.Check(name,!L::ValidateForRecording(*lease,f).allowed,true,"same-recording recheck");f=baseline;};
    f.sameRecording->use.recording=Key("other-recording");reject("T17.local-wrong-recording");
    f.sameRecording->use.reservation=Key("other-reservation");reject("T16.local-wrong-reservation");
    f.sameRecording->use.producerOrdinal=2;reject("T16.local-unordered-producer");
    f.sameRecording->activeRecording=L::Fact::No;reject("T17.local-reset-recording");
    f.sameRecording->orderEstablished=L::Fact::Unknown;reject("T16.local-unproven-barrier");
    f.sameRecording->reservationRetained=L::Fact::No;reject("T17.local-reservation-lost");
    f.sameRecording->conflictingUsesExcluded=L::Fact::No;reject("T19.local-conflicting-consumer");
    f.dependency.crossQueue=true;reject("T18.local-cross-queue");
    f.dependency.committed=L::Fact::Yes;reject("T17.local-unexpected-submission");
    f.dependency.uniqueSubmission=L::Fact::Yes;reject("T17.local-submission-claim");
    f.callbackActive=L::Fact::No;reject("T16.local-callback-expired");
    f.current.description.resource.contentRevision=Known(C::ContentRevision{999});reject("T16.local-content-changed");
    f.holds=fixture->facts.holds;reject("T19.local-provider-held");
    auto write=b;write.description.use=C::UsageKind::WriteExclusive;f=baseline;f.current=write;
    auto writeLease=L::BeginConsumption(write,f);
    r.Check("T16.local-exclusive-write",writeLease&&L::ValidateForRecording(*writeLease,f).allowed,true,"same-recording write");
    if(writeLease){L::CloseAdmission(*writeLease);r.Check("T17.local-write-not-storage-reuse",!L::CanOwnerReuse(*writeLease,f).allowed,true,"CanOwnerReuse");}
    auto invalid=b;invalid.description.use=C::UsageKind::RetainedProviderConsumption;f=baseline;f.current=invalid;
    r.Check("T19.local-not-provider-proof",!L::BeginConsumption(invalid,f),true,"same-recording admission");
    invalid=b;invalid.cpuReadback=true;f.current=invalid;
    r.Check("T16.local-not-cpu-readback",!L::BeginConsumption(invalid,f),true,"same-recording admission");
    f=baseline;f.holds=fixture->facts.holds;
    Compatible(*f.sameRecording,f.holds.Get(0)->registration);
    auto compatible=L::BeginConsumption(b,f);
    r.Check("T19.local-compatible-held-read",compatible&&L::ValidateForRecording(*compatible,f).allowed,true,"same-recording admission");
    if(compatible){L::CloseAdmission(*compatible);r.Check("T19.local-compatible-hold-not-reuse",!L::CanOwnerReuse(*compatible,f).allowed,true,"CanOwnerReuse");}
    f.holds.Get(0)->released=L::Fact::Unknown;
    r.Check("T19.local-compatible-unknown-refused",!L::BeginConsumption(b,f),true,"same-recording admission");
    f=baseline;f.holds=fixture->facts.holds;Compatible(*f.sameRecording,Key("wrong-registration"));
    r.Check("T19.local-unrelated-compatibility-refused",!L::BeginConsumption(b,f),true,"same-recording admission");
    auto retained=b;retained.description.use=C::UsageKind::RetainedProviderConsumption;
    ProviderUse(*retained.sameRecording,fixture->facts.holds.Get(0)->registration);
    f=baseline;f.current=retained;f.sameRecording->use=*retained.sameRecording;f.holds=fixture->facts.holds;
    Compatible(*f.sameRecording,f.holds.Get(0)->registration);
    const auto providerFacts=f;
    auto retainedLease=L::BeginConsumption(retained,f);
    r.Check("T19.local-exact-provider-consumption",retainedLease&&L::ValidateForRecording(*retainedLease,f).allowed,true,"same-recording admission");
    f.holds.Get(0)->consumer=Key("foreign-provider-consumer");
    r.Check("T19.local-provider-consumer-mismatch",!L::BeginConsumption(retained,f),true,"same-recording admission");
    f=providerFacts;f.holds.Get(0)->released=L::Fact::Yes;f.holds.Get(0)->releaseEvidence=Key("release");
    r.Check("T19.local-provider-registration-already-released",!L::BeginConsumption(retained,f),true,"same-recording admission");
    f=baseline;f.current=retained;f.sameRecording->use=*retained.sameRecording;
    f.providerReservation=L::OwnerFacts::ProviderReservation{*retained.sameRecording->providerRegistration,
        retained.description.resource,Id<C::ProviderIncarnation>("renderer-feature",1)};
    r.Check("SP03.reserved-not-active",L::BeginConsumption(retained,f).has_value()&&f.holds.Size()==0,true,"reserved provider registration");
    auto reserved=f;
    f.providerReservation->registration=Key("wrong-registration");
    r.Check("SP03.wrong-reservation",!L::BeginConsumption(retained,f),true,"reserved provider registration");
    f=reserved;f.providerReservation->target.resourceGeneration={};
    r.Check("SP03.wrong-reserved-generation",!L::BeginConsumption(retained,f),true,"reserved provider registration");
    f=reserved;f.providerReservation->feature={};
    r.Check("SP03.missing-reserved-feature",!L::BeginConsumption(retained,f),true,"reserved provider registration");
    f=reserved;f.providerReservation.reset();
    r.Check("SP03.revoked-reservation",!L::BeginConsumption(retained,f),true,"reserved provider registration");
    f=reserved;f.callbackActive=L::Fact::No;
    r.Check("SP03.reservation-after-callback",!L::BeginConsumption(retained,f),true,"reserved provider registration");
    f=baseline;const auto header=NrContextTests::Header(C::ContractId::C03,C::OwnerDomain::Resource,999);
    const auto proof=L::PublishDependencyProof(*lease,f,header,Known(Key("recording")),{},Known(Key("reservation")),Ev(1));
    r.Check("T18.local-c03-no-invented-submit",proof&&proof->Value().orderedForConsumer.KnownPart()->value&&!proof->Value().submission.IsKnown()&&!proof->Value().gpuCompleted.KnownPart()->value,true,"PublishDependencyProof");
    r.Check("T18.local-c03-wrong-recording",!L::PublishDependencyProof(*lease,f,header,Known(Key("other-recording")),{},Known(Key("reservation")),Ev(1)),true,"PublishDependencyProof");
    r.Check("T18.local-c03-false-submission",!L::PublishDependencyProof(*lease,f,header,Known(Key("recording")),Known(Key("submission")),Known(Key("reservation")),Ev(1)),true,"PublishDependencyProof");
    f.dependency.committed=L::Fact::Yes;f.dependency.uniqueSubmission=L::Fact::Yes;
    f.sameRecording->activeRecording=L::Fact::No;
    L::CloseAdmission(*lease);
    const auto published=[&]{return L::PublishDependencyProof(*lease,f,header,Known(Key("recording")),Known(Key("submission")),Known(Key("reservation")),Ev(1));};
    auto submitted=published();
    r.Check("T18.local-later-submission-evidence",submitted&&submitted->Value().submission.IsKnown()&&!L::ValidateForRecording(*lease,f).allowed,true,"PublishDependencyProof");
    f.dependency.completed=L::Fact::Yes;
    auto completed=published();
    r.Check("T17.local-later-completion-not-reuse",completed&&completed->Value().gpuCompleted.KnownPart()->value&&!completed->Value().storageReusable.KnownPart()->value,true,"PublishDependencyProof");
    f.dependency.nonReplayable=L::Fact::Yes;f.dependency.reusable=L::Fact::Yes;
    f.consumerTicketsRetired=L::Fact::Yes;f.ownerCanRecycle=L::Fact::Yes;
    auto retired=published();
    r.Check("T17.local-later-retirement-evidence",retired&&retired->Value().storageReusable.KnownPart()->value&&!L::ValidateForRecording(*lease,f).allowed,true,"PublishDependencyProof");
    f.dependency.committed=L::Fact::No;f.dependency.uniqueSubmission=L::Fact::No;f.dependency.completed=L::Fact::No;
    f.dependency.ordered=L::Fact::No;
    const auto canceled=L::PublishDependencyProof(*lease,f,header,Known(Key("recording")),{},Known(Key("reservation")),Ev(1));
    r.Check("T17.local-canceled-retirement-evidence",canceled&&!canceled->Value().submission.IsKnown()&&canceled->Value().storageReusable.KnownPart()->value&&!L::ValidateForRecording(*lease,f).allowed,true,"PublishDependencyProof");
#else
    r.Check("T16.same-recording-production",false,true,"SameRecordingUse unavailable");
#endif
    return r.Finish();});
}
