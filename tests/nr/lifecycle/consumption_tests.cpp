#include "../context/ContextFixtureStore.h"
#if __has_include(<nr/lifecycle/ConsumptionLeaseAdapter.h>)
#include <nr/lifecycle/ConsumptionLeaseAdapter.h>
#define HAS_LEASE 1
#endif
#if __has_include(<nr/lifecycle/LifecycleEvidence.h>)
#include <nr/lifecycle/LifecycleEvidence.h>
#define HAS_LIFECYCLE_EVIDENCE 1
#endif
using namespace NrContextTests;
using namespace NrTest002;
namespace L=Neurotic::Lifecycle;
#ifdef HAS_LEASE
static NrContextTests::Store metadata;
static L::LeaseBinding Binding()
{
    L::LeaseBinding b;
    b.description.header=NrContextTests::Header(C::ContractId::C03,C::OwnerDomain::Resource,701);
    b.description.context={Sym(C::CanonicalFrameContext::WireName),Key("context"),1};
    b.description.candidate={Sym(C::AcquisitionCandidate::WireName),Key("candidate"),1};
    b.description.resource=Resource(10,{}).identity;
    b.description.use=C::UsageKind::OrderedGpuRead;b.description.commandScope.key=Key("callback");
    b.description.device=Known(Id<C::ObjectIncarnation>("device",1));
    b.description.queue=Known(Id<C::ObjectIncarnation>("queue",2));
    b.description.revoked=Known(false);b.description.revocationEpoch=Known(Key("revocation",1));
    b.description.expirationBoundary=Known(Key("callback-end",1));
    b.description.submissionRecheckRequired=Known(true);
    b.description.generations=metadata.Publish(C::GenerationVector{},C::OwnerDomain::Resource);
    b.consumer=Key("consumer");b.purpose=Sym("NativeTemporal");
    b.plan=Key("plan");b.profile=Key("profile");
    b.route=Gen(C::IdentityKind::RouteGeneration,"route",1);
    return b;
}
static L::OwnerFacts Facts(const L::LeaseBinding& b)
{
    L::OwnerFacts f;f.current=b;f.permission=L::Fact::Yes;f.callbackActive=L::Fact::Yes;
    f.apiCompatible=L::Fact::Yes;f.registryHealthy=L::Fact::Yes;f.nativeBindingMatches=L::Fact::Yes;
    f.dependency.ordered=L::Fact::Yes;f.dependency.committed=L::Fact::Yes;
    f.dependency.sameDevice=L::Fact::Yes;f.dependency.queueKnown=L::Fact::Yes;
    f.dependency.uniqueSubmission=L::Fact::Yes;f.dependency.nonReplayable=L::Fact::No;
    f.dependency.completed=L::Fact::No;f.dependency.reusable=L::Fact::No;
    f.dependency.supportedType=L::Fact::Yes;
    f.recordingTracked=L::Fact::Yes;f.resourcesRegistered=L::Fact::Yes;
    f.descriptorsProtected=L::Fact::Yes;f.consumerTicketsRetired=L::Fact::No;
    f.externalRegistrationsComplete=L::Fact::Yes;f.ownerCanRecycle=L::Fact::Yes;
    return f;
}
#endif
int main(int argc,char** argv)
{
    return Guard([&]{Report r("consumption-leases",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef HAS_LEASE
        auto b=Binding();auto f=Facts(b);auto lease=L::BeginConsumption(b,f);
        if(!lease)throw std::runtime_error("valid owner fixture was rejected");
        auto record=[&]{return L::ValidateForRecording(*lease,f).allowed;};
        auto submit=[&]{return L::ValidateForSubmit(*lease,f).allowed;};
        auto reuse=[&]{return L::CanOwnerReuse(*lease,f).allowed;};
        r.Check("T16.ordered-before-completion",record(),true,"ValidateForRecording");
        r.Check("T16.submit-ordered",submit(),true,"ValidateForSubmit");
        r.Check("T16.ordered-not-reusable",reuse(),false,"CanOwnerReuse");
        f.dependency.completed=L::Fact::Yes;
        r.Check("T17.first-completion",reuse(),false,"CanOwnerReuse");
        L::CloseAdmission(*lease);
        r.Check("T17.closed-replayable",reuse(),false,"CanOwnerReuse");
        f.dependency.nonReplayable=L::Fact::Yes;f.dependency.reusable=L::Fact::Yes;
        r.Check("T16.consumer-pending",reuse(),false,"CanOwnerReuse");
        f.consumerTicketsRetired=L::Fact::Yes;
        r.Check("T16.fully-retired",reuse(),true,"CanOwnerReuse");
        f.current.description.revoked=Known(true);f.permission=L::Fact::No;
        r.Check("T16.revoked-safe-retirement",reuse(),true,"CanOwnerReuse");
        f.current.description.revoked=Known(false);f.permission=L::Fact::Yes;
        f.registryHealthy=L::Fact::No;
        r.Check("T16.failed-registry-no-recycle",reuse(),false,"CanOwnerReuse");
        f.registryHealthy=L::Fact::Yes;
        r.Check("T16.closed-no-admission",record(),false,"ValidateForRecording");
        f.descriptorsProtected=L::Fact::No;
        r.Check("T17.descriptor-retirement",reuse(),false,"CanOwnerReuse");
        f.descriptorsProtected=L::Fact::Yes;
        L::ExternalHold hold;hold.registration=Key("registration");hold.consumer=Key("provider");
        hold.target=b.description.resource;hold.kind=L::HoldKind::Provider;hold.released=L::Fact::No;
        hold.provider=Id<C::ProviderIncarnation>("provider",1);hold.handoff=Gen(C::IdentityKind::HandoffContractGeneration,"handoff",1);
        f.holds.Push(hold);
        r.Check("T19.active-provider",reuse(),false,"CanOwnerReuse");
        hold.released=L::Fact::Unknown;f.holds={};f.holds.Push(hold);
        r.Check("T19.unknown-release",reuse(),false,"CanOwnerReuse");
        hold.released=L::Fact::Yes;f.holds={};f.holds.Push(hold);
        r.Check("T19.release-token-missing",reuse(),false,"CanOwnerReuse");
        hold.releaseEvidence=Key("released");f.holds={};f.holds.Push(hold);
        r.Check("T19.explicit-release",reuse(),true,"CanOwnerReuse");
        hold.target.resourceIncarnation={};f.holds={};f.holds.Push(hold);
        r.Check("T19.wrong-target-release",reuse(),false,"CanOwnerReuse");
        f.holds={};f.externalRegistrationsComplete=L::Fact::Unknown;
        r.Check("T19.missing-registration",reuse(),false,"CanOwnerReuse");
        f=Facts(b);lease=L::BeginConsumption(b,f);
        const auto reject=[&](std::string_view id){r.Check(id,record()||submit(),false,"recording and submit recheck");f=Facts(b);};
        f.current.description.resource.objectIncarnation={};reject("T16.object-recheck");
        f.current.description.resource.resourceIncarnation={};reject("T16.resource-recheck");
        f.current.description.resource.resourceViewIncarnation={};reject("T16.view-recheck");
        f.current.description.resource.resourceGeneration={};reject("T16.resource-generation");
        f.current.description.resource.representationGeneration={};reject("T16.representation-generation");
        f.current.description.resource.contentRevision=Known(C::ContentRevision{2});reject("T16.content-recheck");
        ++f.current.description.context.revision;reject("T16.context-recheck");
        ++f.current.description.candidate.revision;reject("T16.candidate-recheck");
        f.current.consumer=Key("other-consumer");reject("T16.consumer-recheck");
        f.current.purpose=Sym("other-purpose");reject("T16.purpose-recheck");
        f.current.plan=Key("other-plan");reject("T16.plan-recheck");
        f.current.profile=Key("other-profile");reject("T16.profile-recheck");
        f.current.route={};reject("T16.route-recheck");
        f.current.description.device={};reject("T18.device-unknown");
        f.current.description.queue={};reject("T18.queue-unknown");
        f.current.description.revoked=Known(true);reject("T16.revoked");
        f.current.description.revocationEpoch=Known(Key("revocation",2));reject("T16.revocation-epoch");
        f.nativeBindingMatches=L::Fact::No;reject("T16.native-owner-map");
        f.recordingTracked=L::Fact::No;reject("T17.record-before-use");
        f.resourcesRegistered=L::Fact::No;reject("T17.register-resource");
        f.descriptorsProtected=L::Fact::Unknown;reject("T17.register-descriptor");
        f.registryHealthy=L::Fact::No;reject("T18.registry-failed");
        f.dependency.uniqueSubmission=L::Fact::No;reject("T17.replay-not-unique");
        f.dependency.committed=L::Fact::No;reject("T18.reserved-not-committed");
        f.dependency.sameDevice=L::Fact::No;reject("T18.cross-device");
        f.dependency.queueKnown=L::Fact::Unknown;reject("T18.unknown-queue");
        f.dependency.supportedType=L::Fact::No;reject("T18.copy-list");
        f.dependency.crossQueue=true;reject("T18.cross-queue-replayable");
        f.dependency.crossQueue=true;f.dependency.nonReplayable=L::Fact::Yes;
        r.Check("T18.sealed-cross-queue",submit(),true,"ValidateForSubmit");
        f=Facts(b);f.dependency.ordered=L::Fact::Unknown;reject("T18.unknown-dependency");
        f=Facts(b);b.description.use=C::UsageKind::CallbackRead;f.current=b;
        lease=L::BeginConsumption(b,f);if(!lease)throw std::runtime_error("callback fixture rejected");
        f.callbackActive=L::Fact::No;
        r.Check("T16.expired-callback",record(),false,"ValidateForRecording");
        f=Facts(b);b.description.use=C::UsageKind::RetainedProviderConsumption;f.current=b;
        r.Check("T19.no-provider-hold",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        b=Binding();b.contentBasis=L::ContentBasis::InvocationPublication;f=Facts(b);
        r.Check("T16.pixel-read-needs-content",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        b=Binding();b.description.use=C::UsageKind::WriteExclusive;b.contentBasis=L::ContentBasis::WriteDestination;
        b.description.resource.contentRevision={};f=Facts(b);
        f.dependency.reusable=L::Fact::Yes;f.dependency.nonReplayable=L::Fact::Yes;
        f.consumerTicketsRetired=L::Fact::Yes;
        r.Check("T16.scratch-no-content",L::BeginConsumption(b,f).has_value(),true,"BeginConsumption");
        b=Binding();b.description.resource.contentRevision={};b.callbackScoped=true;
        b.contentBasis=L::ContentBasis::InvocationPublication;f=Facts(b);
        r.Check("SP03.invocation-owner-current",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        f.invocationPublicationCurrent=L::Fact::Yes;
        r.Check("SP03.invocation-current",L::BeginConsumption(b,f).has_value(),true,"BeginConsumption");
        f.current.description.candidate.revision++;
        r.Check("SP03.wrong-candidate",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        f.current=b;f.callbackActive=L::Fact::No;
        r.Check("SP03.callback-closed",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        f=Facts(b);f.invocationPublicationCurrent=L::Fact::Yes;
        f.current.contentBasis=L::ContentBasis::WriteDestination;
        r.Check("SP03.content-basis-substitution",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        f=Facts(b);
        f.invocationPublicationCurrent=L::Fact::No;
        r.Check("SP03.invocation-revoked",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        b.contentBasis=L::ContentBasis::WriteDestination;f=Facts(b);
        r.Check("SP03.read-as-write-target",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        b.description.use=C::UsageKind::WriteExclusive;f=Facts(b);
        r.Check("SP03.future-target",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        hold.target=b.description.resource;hold.released=L::Fact::No;f.holds={};f.holds.Push(hold);
        r.Check("T19.write-held",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        b=Binding();b.description.use=static_cast<C::UsageKind>(999);f=Facts(b);
        r.Check("T16.unknown-use",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        b=Binding();b.description.context.recordType=Sym("other");f=Facts(b);
        r.Check("T16.wrong-context-type",L::BeginConsumption(b,f).has_value(),false,"BeginConsumption");
        b=Binding();f=Facts(b);lease=L::BeginConsumption(b,f);
        auto moved=std::move(*lease);
        r.Check("T16.moved-from-closed",L::ValidateForRecording(*lease,f).allowed,false,"ValidateForRecording");
        r.Check("T16.moved-to-active",L::ValidateForRecording(moved,f).allowed,true,"ValidateForRecording");
#ifdef HAS_LIFECYCLE_EVIDENCE
        auto receiptHeader=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::Resource,901);
        auto evidence=L::PublishLifecycleEvidence(moved,f,L::LeaseEvent::OrderedForConsumer,receiptHeader,1,Ev(1),metadata);
        r.Check("T16.owner-receipt",evidence.has_value(),true,"PublishLifecycleEvidence");
        r.Check("T16.no-false-completion",L::PublishLifecycleEvidence(moved,f,L::LeaseEvent::Completed,receiptHeader,1,Ev(1),metadata).has_value(),false,"PublishLifecycleEvidence");
        r.Check("T16.no-false-reuse",L::PublishLifecycleEvidence(moved,f,L::LeaseEvent::StorageReusable,receiptHeader,1,Ev(1),metadata).has_value(),false,"PublishLifecycleEvidence");
        receiptHeader.owner=C::OwnerDomain::Diagnostics;
        r.Check("T16.diagnostics-cannot-author",L::PublishLifecycleEvidence(moved,f,L::LeaseEvent::OrderedForConsumer,receiptHeader,1,Ev(1),metadata).has_value(),false,"PublishLifecycleEvidence");
        r.Check("T16.evidence-optional",L::ValidateForRecording(moved,f).allowed,true,"ValidateForRecording");
        r.Check("T16.c03-description",L::PublishLeaseDescription(moved,f,Ev(1),metadata).has_value(),true,"PublishLeaseDescription");
        auto proofHeader=NrContextTests::Header(C::ContractId::C03,C::OwnerDomain::Resource,902);
        auto proof=L::PublishDependencyProof(moved,f,proofHeader,Known(Key("recording")),Known(Key("submission")),Known(Key("dependency")),Ev(1));
        r.Check("T18.c03-dependency",proof.has_value(),true,"PublishDependencyProof");
        r.Check("T18.c03-order-not-completion",proof&&proof->Value().orderedForConsumer.KnownPart()->value&&!proof->Value().gpuCompleted.KnownPart()->value,true,"PublishDependencyProof");
        r.Check("T18.c03-missing-submission",L::PublishDependencyProof(moved,f,proofHeader,Known(Key("recording")),{},Known(Key("dependency")),Ev(1)).has_value(),false,"PublishDependencyProof");
#else
        r.Check("T16.evidence-owner-missing",false,true,"LifecycleEvidence unavailable");
#endif
#else
        r.Check("T16.owner-missing",false,true,"ConsumptionLeaseAdapter unavailable");
#endif
        return r.Finish();});
}
