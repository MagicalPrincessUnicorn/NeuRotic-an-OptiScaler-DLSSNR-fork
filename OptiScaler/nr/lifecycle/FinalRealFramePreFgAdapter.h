#pragma once
#include "FinalRealFrameFinalizer.h"
#include "ProviderLeaseBinding.h"

namespace Neurotic::Lifecycle
{
// The existing PreFg/Resource owner supplies an authenticated, fresh mapping under
// its lock. Provider-local token/sequence only locate its dependency; they never
// become canonical frame identity or prove host-rendering content lineage.
template<class Reader> bool FinalPreFgBindingMatches(const FinalPacketSnapshot& snapshot,const ProviderLeaseBinding& mapping,
    const FinalFrameRuntimeProof& proof,const Reader& reader)
{
    const auto& packet=snapshot.packet;
    if(!Context::ValidValues(packet)||!SameProviderBinding(mapping,mapping)||!proof.lease||!proof.facts||!proof.dependency||
       !proof.lease->Binding().plan||mapping.consumer!=proof.lease->Binding().consumer)return false;
    const auto* output=Context::ResolveMetadata(packet.output,reader);
    const auto* generations=Context::ResolveMetadata(packet.generations,reader);
    if(!output||!generations||!Context::SameResourceStructure(mapping.resource,output->identity)||
       !Context::SameContent(mapping.resource.contentRevision,output->identity.contentRevision))return false;
    bool handoffKnown=false;
    for(const auto& generation:generations->Entries())if(generation==mapping.handoff)handoffKnown=true;
    if(!handoffKnown||packet.dependency.recordType.View()!=C::DependencyProof::WireName||
       packet.dependency.record!=proof.dependency->header.record||packet.dependency.revision!=proof.dependency->header.revision)return false;
    const auto& id=mapping.handoff.identity;C::HandoffContractGeneration handoff{id.nameSpace,id.issuer,id.value};
    if(!ValidateFinalFrameRuntime(proof,*output,mapping.provider,handoff,*proof.lease->Binding().plan))return false;
    const C::BoundedList<C::RetentionRegistration,16>* registrations=nullptr;
    if(!Context::ResolveList(packet.retentions,snapshot,registrations)||!registrations||registrations->Size()==0)return false;
    std::size_t matched=0;
    for(const auto& hold:proof.facts->holds)if(FinalFrameProviderHold(hold,proof.lease->Binding(),mapping.provider,handoff))
    {
        std::size_t count=0;
        for(const auto& registration:*registrations)
            if(registration.consumer==hold.consumer&&Context::Established(registration.registration)&&
               registration.registration.KnownPart()->value==hold.registration)++count;
        if(count!=1)return false;++matched;
    }
    return matched==registrations->Size();
}
inline bool FinalPreFgClaimMatches(FinalRealFrameFinalizer& finalizer,const PrimaryNrClaimHandle& claim,const C::FinalSealId& seal)
{const auto packet=finalizer.Snapshot(claim);return packet&&packet->packet.seal==seal;}
inline std::uint64_t ReserveFinalPreFg(DlssNr::PreFg::CompletionLedger& ledger,ID3D12Resource* resource,const ProviderLeaseBinding& mapping)
{
    if(!resource||!SameProviderBinding(mapping,mapping))return 0;
    return ledger.Reserve(resource,mapping.ledgerKey.provider,mapping.ledgerKey.generation,mapping.ledgerKey.instance,mapping.token,mapping.sequence);
}
// Invoked by the existing submission owner only after its successful queue signal.
// Failure retains the existing reservation: submitted work is never canceled here.
inline bool PublishFinalPreFg(DlssNr::PreFg::CompletionLedger& ledger,std::uint64_t reservation,ID3D12Resource* resource,
    ID3D12Fence* fence,std::uint64_t value,const ProviderLeaseBinding& expected,const ProviderLeaseBinding& current)
{
    return SameProviderBinding(expected,current)&&ledger.Commit(reservation,resource,fence,value);
}
template<class Reader> std::optional<ProviderLeaseDependency> BindFinalPreFg(FinalRealFrameFinalizer& finalizer,
    const PrimaryNrClaimHandle& claim,const C::FinalSealId& seal,const FinalFrameRuntimeProof& proof,
    const ProviderLeaseBinding& expected,const ProviderLeaseBinding& current,const Reader& reader,
    DlssNr::PreFg::CompletionLedger& ledger,ID3D12Resource* resource,ID3D12GraphicsCommandList* providerList,const FinalizerEvent& event)
{
    if(!FinalPreFgClaimMatches(finalizer,claim,seal))return {}; // Refuse a foreign pair before changing either handoff state.
    auto packet=finalizer.Snapshot(seal);
    if(!packet||!SameProviderBinding(expected,current)||!FinalPreFgBindingMatches(*packet,current,proof,reader))return {};
    const auto offered=finalizer.OfferToFg(claim,proof,event);
    if(!offered.accepted||!offered.packet||offered.packet->seal!=seal)return {};
    auto dependency=ClaimProviderDependency(ledger,resource,expected,current);
    if(!dependency||!dependency->Bind(current,providerList))return {};
    // This proves order binding only. The actual provider owner separately records
    // C13 acceptance and release after observing those operations. No CPU fence wait.
    return dependency;
}
}
