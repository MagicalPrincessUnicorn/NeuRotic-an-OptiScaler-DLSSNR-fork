#pragma once
#include "ConsumptionLeaseAdapter.h"
#include <dlssnr/NrGpuSafety.h>
#include <dlssnr/PreFg.h>

namespace Neurotic::Lifecycle
{
// Binding to an existing Pre-FG owner registration. The legacy ledger key is mapping
// evidence only; it cannot replace or issue the independent canonical C14 identities.
struct ProviderLeaseBinding
{
    C::ResourceIdentityToken resource;
    C::ProviderIncarnation provider;
    C::GenerationToken handoff;
    C::RecordKey consumer;
    DlssNr::PreFg::ConsumerKey ledgerKey;
    std::uint64_t token=0,sequence=0;
};
inline bool SameProviderBinding(const ProviderLeaseBinding& a,const ProviderLeaseBinding& b)
{
    return Context::SameResourceStructure(a.resource,b.resource)&&Context::SameContent(a.resource.contentRevision,b.resource.contentRevision)&&
        a.provider.Check()==C::Error::None&&a.provider==b.provider && a.consumer.Check()==C::Error::None&&a.consumer==b.consumer &&
        a.handoff.identity.Check()==C::Error::None&&a.handoff.identity.kind==C::IdentityKind::HandoffContractGeneration&&a.handoff==b.handoff &&
        a.ledgerKey.provider&&a.ledgerKey.generation&&a.ledgerKey.instance&&a.ledgerKey==b.ledgerKey &&
        a.token&&a.sequence&&a.token==b.token&&a.sequence==b.sequence;
}
class ProviderLeaseDependency;
inline std::optional<ProviderLeaseDependency> ClaimProviderDependency(DlssNr::PreFg::CompletionLedger&,
    ID3D12Resource*,const ProviderLeaseBinding&,const ProviderLeaseBinding&);
// Opaque existing-owner dependency, never a C03 serialized authority or a resource hold.
// It carries the fence/status already issued by CompletionLedger; no resource AddRef.
class ProviderLeaseDependency
{
    ProviderLeaseBinding binding;
    DlssNr::PreFg::CompletionDependency dependency;
    ProviderLeaseDependency(const ProviderLeaseBinding& b,DlssNr::PreFg::CompletionDependency d):binding(b),dependency(std::move(d)){}
    friend std::optional<ProviderLeaseDependency> ClaimProviderDependency(DlssNr::PreFg::CompletionLedger&,
        ID3D12Resource*,const ProviderLeaseBinding&,const ProviderLeaseBinding&);
  public:
    bool Bind(const ProviderLeaseBinding& current,ID3D12GraphicsCommandList* list) const
    {
        if(!SameProviderBinding(binding,current)||!dependency.status||dependency.status->failed.load())return false;
        return DlssNr::GpuSafety::BindExternalWait(list,dependency.fence.Get(),dependency.value,
                                                  dependency.token,dependency.sequence,dependency.status);
    }
    std::shared_ptr<const DlssNr::GpuSafety::ExternalWaitStatus> Status() const{return dependency.status;}
    Fact ProviderReleased() const{return Fact::Unknown;} // requires a separate owner release registration
};
// Caller is the existing Pre-FG/resource owner and holds its ledger/resource lock.
// current is a fresh authenticated C14/native mapping, not an echo of expected.
inline std::optional<ProviderLeaseDependency> ClaimProviderDependency(DlssNr::PreFg::CompletionLedger& ledger,
    ID3D12Resource* resource,const ProviderLeaseBinding& expected,const ProviderLeaseBinding& current)
{
    if(!resource||!SameProviderBinding(expected,current))return {};
    auto claim=ledger.Claim(resource,current.ledgerKey.provider,current.ledgerKey.generation,current.ledgerKey.instance,current.token,current.sequence);
    if(claim.result!=DlssNr::PreFg::CompletionClaimResult::Ready||claim.dependency.kind!=DlssNr::PreFg::CompletionKind::Output ||
       !claim.dependency.fence||!claim.dependency.value||!claim.dependency.reservation||!claim.dependency.status ||
       claim.dependency.token!=current.token||claim.dependency.sequence!=current.sequence||claim.dependency.status->failed.load())return {};
    return ProviderLeaseDependency(current,std::move(claim.dependency));
}
}
