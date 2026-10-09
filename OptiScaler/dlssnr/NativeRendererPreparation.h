#pragma once
#include "NrGpuSafety.h"
#include "NativeProviderLifetime.h"
#include <nr/protocol/NativeEncodeControls.h>
#include <memory>

class Config;
class DlssNr_Dx12;
template<class Source>struct NrConfigSnapshot;
namespace Neurotic::Lifecycle { class NativeProcessBootstrap; class NativeResourceRegistry; class NativeInvocationOwner; }
namespace DlssNr
{
#define NR_NATIVE_SR_PASSTHROUGH_SELECTION_V1 1
// Route selection only. The returned handle and all authority still come from
// the actual selected NGX provider's create and evaluate calls.
inline bool UseNativeSrPassthrough(bool protocol,bool enabled,unsigned route,bool superSampling,
    bool explicitDlss)noexcept
{return protocol&&enabled&&route==0&&superSampling&&explicitDlss;}
class NativeRendererInvocationBorrow;
class NativeRendererWriteToken;
enum class NativePreparationStatus
{ Refused, CreationAttempted, CreationRecorded, AwaitingCreationCompletion, Ready };
// A synchronous borrow of the existing renderer's actual owners. Raw handles
// are local observations, not C14 identities or C03 permissions. This guard
// does not encode inputs, evaluate a model, publish a route, or certify output.
class NativeRendererPreparation
{
    friend class NativeRendererInvocationBorrow;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit NativeRendererPreparation(std::unique_ptr<Impl>);
  public:
    struct Resources
    {
        void* feature=nullptr;
        ID3D12Resource *modelInput=nullptr,*modelOutput=nullptr,*originalCopy=nullptr;
        unsigned width=0,height=0;
        ID3D12Resource* preSrScratch=nullptr;
        std::shared_ptr<NativeProviderRetirementOwner> providerUses;
    };
    ~NativeRendererPreparation();
    NativeRendererPreparation(const NativeRendererPreparation&)=delete;
    NativeRendererPreparation& operator=(const NativeRendererPreparation&)=delete;
    bool Current()const;
    bool PreflightCurrent()const;
    bool NeedsModelInitialization()const;
    std::optional<Neurotic::Protocol::NativeEncodeControls> Encode()const;
    const Resources* Inspect()const;
    const GpuSafety::LocalRecordingAction& RecordingAction()const;
    bool PrepareModel(Neurotic::Lifecycle::NativeResourceRegistry&,NativePreparationStatus&);
  private:
    friend class Neurotic::Lifecycle::NativeProcessBootstrap;
    friend class Neurotic::Lifecycle::NativeInvocationOwner;
    friend class NativeRendererWriteToken;
    std::unique_ptr<NativeRendererInvocationBorrow> Transfer();
    static std::unique_ptr<NativeRendererPreparation> TryAcquire(ID3D12GraphicsCommandList*,
        ID3D12Resource*,const NrConfigSnapshot<Config>&,bool preSr,bool declaredLinearHdr);
};
// Noncopyable callback-local continuation of the preparation's exact recording
// action. It holds no renderer mutex while the typed Dispatch acquires one.
class NativeRendererInvocationBorrow
{
    friend class NativeRendererPreparation;
    friend class Neurotic::Lifecycle::NativeProcessBootstrap;
    friend class Neurotic::Lifecycle::NativeInvocationOwner;
    friend class NativeRendererWriteToken;
    friend class ::DlssNr_Dx12;
    std::unique_ptr<NativeRendererPreparation::Impl> impl_;
    explicit NativeRendererInvocationBorrow(std::unique_ptr<NativeRendererPreparation::Impl>);
    bool CurrentLocked()const;
    bool SnapshotOwns(ID3D12Resource*)const;
    class OwnerScope
    {
        friend class ::DlssNr_Dx12;
        NativeRendererInvocationBorrow* prior_=nullptr;
        explicit OwnerScope(NativeRendererInvocationBorrow*);
      public:
        ~OwnerScope();
        OwnerScope(const OwnerScope&)=delete;
        OwnerScope& operator=(const OwnerScope&)=delete;
    };
  public:
    ~NativeRendererInvocationBorrow();
    NativeRendererInvocationBorrow(const NativeRendererInvocationBorrow&)=delete;
    NativeRendererInvocationBorrow& operator=(const NativeRendererInvocationBorrow&)=delete;
    bool Current()const;
    bool Owns(ID3D12GraphicsCommandList*,ID3D12Resource*)const;
    bool RendererOutput(ID3D12Resource*)const;
    ID3D12GraphicsCommandList* CommandList()const;
    const GpuSafety::LocalRecordingAction& RecordingAction()const;
    const NativeRendererPreparation::Resources* Inspect()const;
  private:
    std::unique_ptr<NativeRendererWriteToken> IssueWriteToken(ID3D12Resource*)const;
};
// Issued only for an exact renderer allocation enrolled by the live borrow.
// This is a CPU write-publication token, never permission for game resources.
class NativeRendererWriteToken
{
    friend class NativeRendererInvocationBorrow;
    const NativeRendererInvocationBorrow* borrow_=nullptr;
    ID3D12Resource* resource_=nullptr;
    NativeRendererWriteToken(const NativeRendererInvocationBorrow* borrow,ID3D12Resource* resource)
        :borrow_(borrow),resource_(resource){}
  public:
    NativeRendererWriteToken(const NativeRendererWriteToken&)=delete;
    NativeRendererWriteToken& operator=(const NativeRendererWriteToken&)=delete;
    NativeRendererWriteToken(NativeRendererWriteToken&&)=default;
    bool CurrentFor(ID3D12Resource*,ID3D12GraphicsCommandList*)const;
};
}
