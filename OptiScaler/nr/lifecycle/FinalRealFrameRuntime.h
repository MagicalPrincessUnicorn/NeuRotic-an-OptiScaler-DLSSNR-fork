#pragma once
#include "FinalRealFrameTypes.h"
namespace Neurotic::Lifecycle
{
inline bool SameFinalFrameBinding(const LeaseBinding& a,const LeaseBinding& b)
{
    return a.description==b.description&&a.consumer==b.consumer&&a.purpose==b.purpose&&a.plan==b.plan&&
        a.profile==b.profile&&a.route==b.route&&a.contentBasis==b.contentBasis&&a.cpuReadback==b.cpuReadback&&
        a.callbackScoped==b.callbackScoped&&a.dependencyRequired==b.dependencyRequired&&a.sameRecording==b.sameRecording;
}
inline bool FinalFrameProviderHold(const ExternalHold& hold,const LeaseBinding& binding,const C::ProviderIncarnation& provider,const C::HandoffContractGeneration& handoff)
{
    return hold.kind==HoldKind::Provider&&hold.consumer==binding.consumer&&hold.provider==provider&&
        hold.handoff==C::GenerationToken{handoff.Describe()}&&hold.released==Fact::No&&
        ValidHold(hold,binding.description.resource)&&Context::SameContent(hold.target.contentRevision,binding.description.resource.contentRevision);
}
inline bool ValidateFinalFrameRuntime(const FinalFrameRuntimeProof& p,const C::ResourceView& output,
    const C::ProviderIncarnation& provider,const C::HandoffContractGeneration& handoff,const C::RecordKey& plan)
{
    if(!p.lease||!p.facts||!p.dependency||!Context::ValidValues(output)||
       !ValidateForSubmit(*p.lease,*p.facts).allowed)return false;
    const auto& b=p.lease->Binding();const auto& d=*p.dependency;
    if(b.description.use!=C::UsageKind::RetainedProviderConsumption||b.purpose.View()!="ExternalProviderConsume"||
       b.plan!=plan||!Context::SameResourceStructure(b.description.resource,output.identity)||
       !Context::SameContent(b.description.resource.contentRevision,output.identity.contentRevision)||
       !Context::SameFact(b.description.device,output.device)||!Context::ValidValues(d)||
       d.header.owner!=C::OwnerDomain::Resource||!FinalizerTrue(d.orderedForConsumer)||
       !Context::Established(d.ownerDependency)||!Context::Established(d.submission)||
       !Context::SameFact(d.producerDevice,b.description.device)||
       !Context::SameFact(d.consumerQueue,b.description.queue)||d.generations!=b.description.generations||
       !Context::Established(b.description.producerDependency))return false;
    const auto& reference=b.description.producerDependency.KnownPart()->value;
    if(reference.recordType.View()!=C::DependencyProof::WireName||reference.record!=d.header.record||reference.revision!=d.header.revision)return false;
    bool registered=false;
    for(const auto& hold:p.facts->holds)
        if(hold.kind==HoldKind::Provider&&hold.consumer==b.consumer&&hold.provider==provider&&
           hold.handoff==C::GenerationToken{handoff.Describe()}&&hold.released==Fact::No&&
           ValidHold(hold,output.identity)&&Context::SameContent(hold.target.contentRevision,output.identity.contentRevision))registered=true;
    return registered; // Ordered GPU use deliberately does not require CPU completion.
}
struct FinalFrameRuntimeBinding
{
    C::FinalSealId seal;
    const LiveConsumptionLease* lease=nullptr; // Resource owner owns lifetime, no destruction/release here.
    C::RecordKey dependency,retention,consumer;
    C::ProviderIncarnation provider;
    C::HandoffContractGeneration handoff;
    C::ResourceIdentityToken resource;
};
}
