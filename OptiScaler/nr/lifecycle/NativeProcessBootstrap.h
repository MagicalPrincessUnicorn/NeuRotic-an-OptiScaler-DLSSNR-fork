#pragma once
#include <algorithm>
#include "NativeSourceEnrollment.h"
#include "NativeSourcePublication.h"
#include "NativeInitialMaterialization.h"
#include "NativeInvocationOwner.h"
#include "NativeControlledSceneProducer.h"
#include "Fsr3SourceBoundConsumerPublication.h"
#include "Fsr3SourceBoundPreflight.h"
#include "SelectedFsr3NativeLeaseOwner.h"
#include <dlssnr/NativeControlledFgBridge.h>
#include <dlssnr/NativeNgxCallCapture.h>
#include <dlssnr/NativeRendererPreparation.h>
#include <dlssnr/NativeTeardownObservation.h>
#include <dlssnr/NativeRendererRetirement.h>
#include <dlssnr/NativeSelectedSrLifetimeLedger.h>
#include <objbase.h>
#include <memory>
#include <mutex>
#include <functional>
#include <thread>
#include <nr/d3d12/NativeRecordingState.h>
#define NR_NATIVE_RUNTIME_ATTACHMENT_V1 1
#define NR_NATIVE_SOURCE_RETURN_V1 1
#pragma comment(lib,"ole32.lib")

namespace DlssNr { class NativeDx12Source; }
namespace Neurotic::Lifecycle
{
// Process-owned, bounded source attachment for the initial fixed Native scope.
// Construction/capture are private to the actual NGX owner. No caller can open
// a session from serialized identities, an arbitrary namespace or a ready port.
// The process retains this root; source closure alone never frees downstream
// GPU/provider/finalizer ownership. Coordinator materialization consumes this
// same owner set when the genuine renderer preparation is available.
class NativeProcessBootstrap
{
    friend class DlssNr::NativeDx12Source;
#ifdef NR_SPECTRE_SOURCE_TESTING
    friend class NativeProcessTestAccess;
#endif
    struct PinBase {virtual ~PinBase()=default;virtual bool Current()const=0;
        virtual std::shared_ptr<const DlssNr::NativeFeatureLifetime> Lifetime()const=0;
        virtual bool MarkOpaqueEntry()=0;
        virtual bool WithCurrent(const std::function<bool()>&)const=0;};
    template<class Feature>struct OwnedPin final:PinBase
    {
        typename DlssNr::NativeFeatureRegistry<Feature>::CallbackPin value;
        explicit OwnedPin(typename DlssNr::NativeFeatureRegistry<Feature>::CallbackPin&& pin):value(std::move(pin)){}
        bool Current()const override{return value.Current();}
        std::shared_ptr<const DlssNr::NativeFeatureLifetime> Lifetime()const override{return value.Lifetime();}
        bool MarkOpaqueEntry()override{return value.MarkOpaqueEntry();}
        bool WithCurrent(const std::function<bool()>& action)const override{return value.WithCurrent(action);}
    };
    struct SourceBase {virtual ~SourceBase()=default;virtual void Close()noexcept=0;
        virtual bool Reopen(const DlssNr::NativeSrRetirementReceipt&)=0;};
    template<class Feature>struct Source final:SourceBase
    {
        DlssNr::NativeFeatureRegistry<Feature>& registry;
        NativeSourceEnrollment<Feature> enrollment;
        Source(NativeOwnerSet& owners,DlssNr::NativeFeatureRegistry<Feature>& source,
            const typename DlssNr::NativeFeatureRegistry<Feature>::CallbackPin& pin):
            registry(source),enrollment(owners,source,pin){}
        void Close()noexcept override{enrollment.Close();registry.StopCallbacks();}
        bool Reopen(const DlssNr::NativeSrRetirementReceipt& receipt)override{return registry.ReopenAfterRetirement(receipt);}
    };
    struct OpaqueSrOperation;
    using ControlledPublication=Fsr3SourceBoundConsumerPublication<NativeInitialMaterialization::Store>;
    struct ControlledFg
    {
        std::recursive_mutex mutex;
        std::shared_ptr<const void> module;
        std::shared_ptr<const void> moduleFile;
        std::shared_ptr<ffx::nr::OwnedOutputLease> output;
        std::shared_ptr<ffx::nr::AlgorithmLease> algorithm;
        FfxNrOwnedOutputHandleV1 outputHandle{};FfxNrAlgorithmHandleV1 algorithmHandle{};
        C::NativeFinalConsumerContractV1 consumer;
        C::RecordKey enrollment;
        std::function<bool(ID3D12GraphicsCommandList*,ID3D12Resource*)> writerCurrent;
        std::shared_ptr<NativeInvocationOwner> invocation;
        std::shared_ptr<NativeSourceTransactionObservation> transaction;
        std::optional<SourceBoundClaimHandle> claim;
        std::shared_ptr<ControlledPublication> publication;
        std::optional<SelectedFsr3NativeLeaseOwner::Selection> selection;
        DlssNr::NativeControlledFgObservationV1 observation;
        bool consumerAttempted=false,submitActive=false;
        std::shared_ptr<void> submitGuard;
        std::thread::id submitThread;
        std::thread::id consumerThread;
        FfxNrDispatchTicketV1 dispatch{};
    };
    struct State
    {
        std::mutex mutex;
        std::mutex completionMutex;
        std::unique_ptr<NativeOwnerSet> owners;
        std::shared_ptr<NativeSessionLifetime> lifetime;
        std::shared_ptr<NativeInitialMaterialization> initialPort;
        std::shared_ptr<NativeInitialMaterialization::Store> initialization;
        std::array<std::shared_ptr<NativeInitialMaterialization::Store>,16> initializationHistory;
        std::size_t initializationCount=0;
        // Unknown external SR effects have no enrolled terminal contract.
        // Retain their exact call/resource/list closure conservatively.
        std::array<std::shared_ptr<OpaqueSrOperation>,16> opaqueEffects;
        // The fixed producer records real GPU work. Retain its resource/PSO
        // closure before entry, including partial failures and replayable lists.
        std::array<std::shared_ptr<NativeControlledSceneProducer::Declaration>,16> controlledProducers;
        C::OptionalFact<C::ProviderIncarnation> runtimeIncarnation;
        void* runtimeFeature=nullptr;
        NativeResourceRegistry* resources=nullptr; // same owner set; stable across its later ownership transfer
        std::unique_ptr<SourceBase> source;
        std::size_t callbacks=0;
        bool closed=false,preparing=false,sourceClosureApplied=false;
        bool retired=false,recreationAttempted=false;
        DlssNr::NativeTeardownAttempt rendererTeardownAttempt;
        std::shared_ptr<DlssNr::NativeSelectedSrLifetimeLedger> srLifetime=std::make_shared<DlssNr::NativeSelectedSrLifetimeLedger>();
        std::shared_ptr<DlssNr::NativeProviderRetirementOwner> rendererOwner;
        std::shared_ptr<const DlssNr::NativeRendererRetirement> rendererRetirement;
        DlssNr::NativeHostReturnObservationV1 latestReturn;
        std::weak_ptr<NativeInvocationOwner> latestReturnOwner;
        bool returnObservationAmbiguous=false;
        std::uintptr_t controlledFeature=0;
        unsigned controlledFeatureId=0;
        std::uint64_t controlledFeatureGeneration=0;
        bool priorWritersClosing=false;
        std::shared_ptr<ControlledFg> controlledFg;
        NativeOwnerSet* Owner()const noexcept{return owners?owners.get():lifetime?lifetime->owners_.get():nullptr;}
    };
    std::shared_ptr<State> state_=std::make_shared<State>();
    static Orchestration::NativeDrainReadiness ObserveOwnerRetirement(State& state)
    {
        using R=Orchestration::Retirement;using O=Orchestration::InitialOwner;
        Orchestration::NativeDrainReadiness result;
        std::lock_guard lock(state.mutex);
        const auto observed=[](bool done){return done?R::Retired:R::Outstanding;};
        const bool cpu=state.closed&&!state.callbacks&&!state.preparing;
        const bool captures=std::none_of(state.opaqueEffects.begin(),state.opaqueEffects.end(),[](const auto& p){return bool(p);})&&
            std::none_of(state.controlledProducers.begin(),state.controlledProducers.end(),[](const auto& p){return bool(p);});
        const bool renderer=state.rendererRetirement&&state.rendererRetirement->Covers(state.rendererOwner);
        const auto sr=state.srLifetime->Retirement();
        const bool provider=renderer&&sr&&sr->Matches(*state.srLifetime);
        const auto resource=state.resources?std::optional{state.resources->ObserveRetirement()}:std::nullopt;
        auto* owner=state.Owner();auto* finalizer=owner?owner->Finalizer():nullptr;
        const auto tails=finalizer?finalizer->DrainStatus():std::nullopt;
        const bool final=tails&&tails->tailsRetired;
        auto set=[&](O domain,bool done){result.owners[static_cast<std::size_t>(domain)]=observed(done);};
        // These CPU publication domains own immutable records, not foreign
        // objects. The kernel calls this only after every invocation and its
        // prepared reservation has relinquished execution rights.
        set(O::Topology,cpu);set(O::Configuration,cpu);set(O::Strategy,cpu);
        set(O::Profile,cpu);set(O::Metadata,cpu);
        set(O::Resource,cpu&&provider&&captures&&resource&&resource->Quiescent());
        set(O::Recording,cpu&&provider&&captures);
        set(O::Provider,cpu&&provider);set(O::FrameGeneration,cpu&&final);
        set(O::History,cpu&&renderer);set(O::LegacyExclusion,cpu&&renderer);
        result.preparation=observed(cpu);result.callbacks=observed(state.closed&&!state.callbacks);
        result.unsealedClaims=observed(final);
        return result;
    }
    class SelectedResourceRead final:public NativeSourceTransactionObservation::SelectedOutputRead
    {
        // Retain the canonical owner set across its transfer to the session.
        std::shared_ptr<State> root_;
        NativeResourceRegistry::ReadBorrow read_;
      public:
        SelectedResourceRead(std::shared_ptr<State> root,NativeResourceRegistry::ReadBorrow read)
            :root_(std::move(root)),read_(std::move(read)){}
        bool Current()const noexcept override{return read_.Current();}
        ID3D12Resource* Native()const noexcept override{return read_.Native();}
        const C::ResourceView& View()const noexcept override{return read_.View();}
    };
    NativeSourceTransactionObservation::ReadFactory SelectedReadFactory(ID3D12Resource* native,
        const C::ResourceView& view)
    {
        // Weak publication avoids a State -> publication -> State ownership
        // cycle. A successful borrow retains that same State independently.
        return [weak=std::weak_ptr<State>(state_),native,view](ID3D12Resource* exact)
            ->std::unique_ptr<NativeSourceTransactionObservation::SelectedOutputRead>
        {
            if(exact!=native)return {};
            auto state=weak.lock();if(!state)return {};
            NativeResourceRegistry* registry=nullptr;
            {std::lock_guard lock(state->mutex);if(!state->closed)registry=state->resources;}
            if(!registry)return {};
            auto read=registry->BorrowCurrentRead(exact,view);
            if(!read||!read->Current())return {};
            return std::make_unique<SelectedResourceRead>(std::move(state),std::move(*read));
        };
    }
    NativeProcessBootstrap()=default;
    static bool ControlledCreation(const DlssNr::NativeNgxCreationParameters& creation,unsigned inverted)
    {
        const auto width=creation.Value("Width"),height=creation.Value("Height");
        return inverted<=1&&creation.Captured()&&creation.Value("DLSS.Feature.Create.Flags")==
            std::optional<unsigned>{3u|(inverted?8u:0u)}&&
            width&&height&&NativeControlledSceneProducer::SupportedExtent(*width,*height)&&
            creation.Value("OutWidth")==width&&creation.Value("OutHeight")==height&&
            creation.Value("PerfQualityValue")==std::optional<unsigned>{5}; // selected DLAA creation ABI
    }
    bool EnsureOwners()
    {
        std::lock_guard lock(state_->mutex);
        if(state_->closed)return false;
        if(!state_->Owner())
        {
            state_->owners=std::make_unique<NativeOwnerSet>(NewNamespace());
            state_->resources=&state_->owners->Resources();
        }
        return state_->resources!=nullptr;
    }
    std::shared_ptr<NativeControlledSceneProducer::Declaration> RecordControlledScene(
        ID3D12GraphicsCommandList* list,const NativeControlledSceneProducer::Images& images,
        std::uint64_t generation,unsigned frame,bool inverted,
        const NativeControlledSceneProducer::Observe& observe,
        const NativeControlledSceneProducer::RegisterLayout& registerLayout)
    {
        if(!EnsureOwners())return {};
        NativeResourceRegistry* resources=nullptr;
        {std::lock_guard lock(state_->mutex);if(!state_->closed)resources=state_->resources;}
        if(!resources)return {};
        auto producer=NativeControlledSceneProducer::Prepare(*resources,list,images,generation,frame,inverted,registerLayout);
        if(!producer)return {};
        std::array<std::shared_ptr<NativeControlledSceneProducer::Declaration>,16> retired;
        {
            std::lock_guard lock(state_->mutex);
            if(state_->closed)return {};
            bool retained=false;
            for(std::size_t i=0;i<state_->controlledProducers.size();++i)
            {
                auto& held=state_->controlledProducers[i];
                if(held&&held->Terminal())retired[i]=std::move(held);
                if(!held&&!retained){held=producer;retained=true;}
            }
            if(!retained)return {};
        }
        // No callback lifetime or wall-clock age can retire this closure.
        if(!producer->Record(*resources,observe))return {};
        return producer;
    }
    unsigned QueryNativeReturn(std::uint64_t after,std::uint64_t handle,std::uint64_t list,
        std::uint64_t output,DlssNr::NativeHostReturnObservationV1& result)noexcept
    try
    {
        result={};
        std::shared_ptr<NativeInvocationOwner> owner;
        {
            std::lock_guard lock(state_->mutex);
            if(state_->closed||state_->returnObservationAmbiguous||
               !DlssNr::MatchesNativeReturnObservation(state_->latestReturn,after,handle,list,output))return 0;
            result=state_->latestReturn;owner=state_->latestReturnOwner.lock();
        }
        if(result.status==2&&owner&&owner->closed_&&!owner->returnTailActive_&&owner->return_&&owner->providerOwner_)
        {
            auto resourceLock=owner->resources_->LockNativeAction();
            owner->providerOwner_->WithHistory(owner->recordingUse_,[&](NativeHistoryState&){
                owner->return_->ObserveForHost(result);
                const auto current=owner->resources_->CurrentRegisteredView(owner->call_->Resource("Output"));
                const auto source=owner->store_->source->TransactionObservation();
                const auto selected=source?source->SelectedOutput():std::nullopt;
                if(current&&selected&&*current==selected->view)
                    owner->return_->ObserveOutputForHost(*owner->store_,*current,selected->recordingIncarnation,result);
                return true;});
        }
        // A callback may have advanced while the owner snapshot was read.
        std::lock_guard lock(state_->mutex);
        if(state_->closed||state_->returnObservationAmbiguous||state_->latestReturn.sequence!=result.sequence)
        {result={};return 0;}
        return 1;
    }
    catch(...){result={};return 0;}
    static C::Symbol NewNamespace()
    {
        GUID guid{};
        if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Native OS namespace unavailable");
        wchar_t wide[40]{};
        const int size=StringFromGUID2(guid,wide,40);
        if(size!=39||wide[0]!=L'{'||wide[37]!=L'}')throw std::runtime_error("Native OS namespace invalid");
        std::string text="native.";
        // Keep every UUID digit/hyphen; the API's display braces are not legal
        // C14 Symbol characters. This is encoding, never a truncated/hash ID.
        for(int i=1;i<size-2;++i)
        {
            if(wide[i]>127)throw std::runtime_error("Native OS namespace encoding");
            text.push_back(static_cast<char>(wide[i]));
        }
        C::Symbol result;if(!result.Assign(text))throw std::runtime_error("Native OS namespace capacity");
        return result;
    }
  public:
    static constexpr std::size_t MaximumCallbacks=16;
    class Callback
    {
        friend class NativeProcessBootstrap;
#ifdef NR_SPECTRE_SOURCE_TESTING
        friend class NativeProcessTestAccess;
#endif
      public:
        struct RequiredSrReturnValue
        {
            std::uint32_t result=0;
            bool succeeded=false;
            bool callerBindingMatches=false;
        };
        struct CallerOutputTargetValue
        {
            // The registered allocation carries the Resource-owner generation;
            // region is the caller's requested subrectangle. Neither is a
            // projected write view or a content revision.
            DlssNr::NativeNgxCallCapture::CallerOutputRegionValue region;
            C::ResourceView view;
        };
      private:
        std::shared_ptr<State> state_;
        std::shared_ptr<PinBase> pin_;
        std::shared_ptr<DlssNr::NativeNgxCallCapture> call_;
        std::shared_ptr<NativeSourcePublication> publication_;
        std::shared_ptr<NativeInitialMaterialization::Store> currentPreparation_;
        std::shared_ptr<DlssNr::NativeRendererInvocationBorrow> rendererBorrow_;
        std::shared_ptr<NativeInvocationOwner> invocation_;
        bool nativeCpuRestored_=false,nativeCommandRestored_=false;
        std::array<const C::ResourceView*,DlssNr::NativeNgxCallCapture::ResourceNames().size()> resources_{};
        bool callCaptured_=false;
        bool hostOutputRecorded_=false;
        bool srLocalRecordingObserved_=false;
        const void* parameterIdentity_=nullptr;
        std::shared_ptr<const NativeProducerColorBinding> producerColor_;
        OpaqueSrEvaluationReceipt::Class srReceiptClass_=OpaqueSrEvaluationReceipt::Class::Unavailable;
        bool runtimeAttempted_=false;
        bool invocationAttempted_=false;
        std::optional<RequiredSrReturnValue> srReturn_;
        DlssNr::NativePreparationStatus runtimeStatus_=DlssNr::NativePreparationStatus::Refused;
        C::Symbol runtimeReason_;
        C::NativeSampleIdentityV1 sample_;
        std::shared_ptr<const C::InterceptedSourceTransactionV1> transaction_;
        std::shared_ptr<NativeSourceTransactionObservation> transactionObservation_;
        Orchestration::NativeScopeEnrollment scope_;
        C::OptionalFact<C::ProviderIncarnation> incarnation_;
        std::uint64_t generation_=0;
        std::uint32_t featureHandle_=0;
        DlssNr::NativeNgxCreationParameters creation_;
        Callback(std::shared_ptr<State> state,std::unique_ptr<PinBase> pin,
            const C::NativeSampleIdentityV1& sample,const Orchestration::NativeScopeEnrollment& scope,
            const C::OptionalFact<C::ProviderIncarnation>& incarnation,std::uint64_t generation,
            const DlssNr::NativeNgxCreationParameters& creation,
            std::shared_ptr<const C::InterceptedSourceTransactionV1> transaction,std::uint32_t featureHandle):
            state_(std::move(state)),pin_(std::move(pin)),sample_(sample),transaction_(std::move(transaction)),scope_(scope),incarnation_(incarnation),generation_(generation),featureHandle_(featureHandle),creation_(creation){}
        void Drop(std::optional<bool> returned={})noexcept
        {
            if(!state_)return;
            const bool hadInvocation=bool(invocation_);
            if(invocation_)invocation_->CloseCallback();
            invocation_.reset();
            currentPreparation_.reset();
            rendererBorrow_.reset();
            publication_.reset();
            producerColor_.reset();
            call_.reset();
            pin_.reset();
            {
                std::lock_guard lock(state_->mutex);--state_->callbacks;
                auto& observed=state_->latestReturn;
                if(observed.sequence==sample_.producerOrdinal)
                {
                    observed.status=hadInvocation?2u:1u;
                    observed.restorationFlags=(nativeCpuRestored_?1u:0u)|(nativeCommandRestored_?2u:0u)|
                        (returned?4u:0u)|(returned&&*returned?8u:0u);
                    observed.runtimePreparation=static_cast<std::uint32_t>(runtimeStatus_);
                    if(srReturn_)observed.hostResult=srReturn_->result;
                    const auto reason=runtimeReason_.View();std::memset(observed.reason,0,sizeof(observed.reason));
                    std::memcpy(observed.reason,reason.data(),(std::min)(reason.size(),sizeof(observed.reason)-1));
                }
            }
            // Complete only after callback-owned cleanup. A normal API return
            // must not first be marked abandoned by the cleanup it requires.
            if(transactionObservation_)
            {
                if(returned)transactionObservation_->Returned(*returned);
                else transactionObservation_->Abandon();
            }
            state_.reset();
        }
      public:
        ~Callback(){Drop();}
        Callback(const Callback&)=delete;
        Callback& operator=(const Callback&)=delete;
        Callback(Callback&&)noexcept=default;
        Callback& operator=(Callback&& other)noexcept
        {
            if(this!=&other){Drop();state_=std::move(other.state_);pin_=std::move(other.pin_);
                call_=std::move(other.call_);callCaptured_=other.callCaptured_;resources_=other.resources_;other.resources_={};
                transaction_=std::move(other.transaction_);transactionObservation_=std::move(other.transactionObservation_);
                publication_=std::move(other.publication_);incarnation_=other.incarnation_;generation_=other.generation_;
                featureHandle_=other.featureHandle_;
                currentPreparation_=std::move(other.currentPreparation_);
                rendererBorrow_=std::move(other.rendererBorrow_);
                invocation_=std::move(other.invocation_);
                nativeCpuRestored_=other.nativeCpuRestored_;nativeCommandRestored_=other.nativeCommandRestored_;
                runtimeAttempted_=other.runtimeAttempted_;runtimeStatus_=other.runtimeStatus_;
                invocationAttempted_=other.invocationAttempted_;
                srReturn_=other.srReturn_;
                runtimeReason_=other.runtimeReason_;
                hostOutputRecorded_=other.hostOutputRecorded_;
                srLocalRecordingObserved_=other.srLocalRecordingObserved_;
                parameterIdentity_=other.parameterIdentity_;srReceiptClass_=other.srReceiptClass_;
                producerColor_=std::move(other.producerColor_);
                sample_=other.sample_;scope_=other.scope_;creation_=other.creation_;}return *this;
        }
        bool Current()const
        {if(!state_||!pin_)return false;std::lock_guard lock(state_->mutex);return !state_->closed&&pin_->Current();}
        bool MarkOpaqueEntry()noexcept
        {try{return pin_&&pin_->MarkOpaqueEntry();}catch(...){if(state_)state_->srLifetime->MarkUnknown();return false;}}
        const C::NativeSampleIdentityV1& Sample()const noexcept{return sample_;}
        const std::shared_ptr<NativeSourceTransactionObservation>& TransactionObservation()const noexcept{return transactionObservation_;}
        const Orchestration::NativeScopeEnrollment& Scope()const noexcept{return scope_;}
        const DlssNr::NativeNgxCallCapture* OriginalCall()const noexcept{return call_.get();}
        const DlssNr::NativeNgxCreationParameters& OriginalCreation()const noexcept{return creation_;}
        const std::optional<RequiredSrReturnValue>& RequiredSrReturn()const noexcept{return srReturn_;}
        OpaqueSrEvaluationReceipt::Class SrReceiptClass()const noexcept{return srReceiptClass_;}
        std::optional<DlssNr::NativeNgxCallCapture::CallerOutputRegionValue> CallerOutputRegion()const noexcept
        {
            const auto width=creation_.Value("OutWidth"),height=creation_.Value("OutHeight");
            if(!call_||!width||!height||!Current()||!RegisteredResource("Output"))return {};
            return call_->CallerOutputRegion(*width,*height);
        }
        std::optional<CallerOutputTargetValue> CallerOutputTarget()const noexcept
        {
            const auto region=CallerOutputRegion();const auto* registered=RegisteredResource("Output");
            if(!region||!registered||!Current()||!registered->identity.resourceGeneration.IsKnown())return {};
            return CallerOutputTargetValue{*region,*registered};
        }
        // This is only a candidate SR input after the actual selected operation
        // owner recorded the caller Output write. The opaque NGX result alone
        // cannot issue that Resource revision.
        std::optional<CallerOutputTargetValue> RequiredSrInput()const noexcept
        {
            if(srReceiptClass_!=OpaqueSrEvaluationReceipt::Class::RecordedOpaqueWrite||
               !srReturn_||!srReturn_->succeeded||!srReturn_->callerBindingMatches||
               !hostOutputRecorded_)return {};
            auto target=CallerOutputTarget();
            return target&&Context::CompleteContent(target->view)?target:std::nullopt;
        }
        struct InvocationGaps
        {
            bool gameInputContent=true;
            bool hostRecordingUse=true;
            bool invocationInputProvenance=true;
        };
        // Physical input versions can remain unknown while the exact callback
        // publication supports ordered input reads. A recorded local action is
        // not a host one-shot grant, submission or a non-replayability claim.
        InvocationGaps InvocationEvidenceGaps()const noexcept
        {
            InvocationGaps gaps;gaps.hostRecordingUse=!srLocalRecordingObserved_;
            if(!Current()||!publication_)return gaps;
            gaps.invocationInputProvenance=false;gaps.gameInputContent=false;
            for(const auto* key:{"Color","Depth","MotionVectors"})
            {
                const auto* view=RegisteredResource(key);
                if(!view||!Context::CompleteContent(*view))gaps.gameInputContent=true;
                if(!view||!publication_->DescribesInput(sample_,key,*view))gaps.invocationInputProvenance=true;
            }
            return gaps;
        }
        // Historical descriptive metadata remains inspectable after source closure.
        // Current() and downstream qualification/admission are separate obligations.
        const NativeSourcePublication* ObservedSource()const noexcept{return publication_.get();}
        DlssNr::NativePreparationStatus RuntimePreparationStatus()const noexcept{return runtimeStatus_;}
        const C::Symbol& RuntimePreparationReason()const noexcept{return runtimeReason_;}
        const C::ResourceView* RegisteredResource(std::string_view key)const noexcept
        {
            if(!state_||!call_)return nullptr;
            const auto& names=DlssNr::NativeNgxCallCapture::ResourceNames();
            for(std::size_t i=0;i<names.size();++i)if(key==names[i])return resources_[i];
            return nullptr;
        }
    };
    NativeProcessBootstrap(const NativeProcessBootstrap&)=delete;
    NativeProcessBootstrap& operator=(const NativeProcessBootstrap&)=delete;
  private:
    // Production calls this only from the actual NativeDx12Source factory.
    // The template permits an explicitly synthetic renderer in isolated tests;
    // there is no public readiness port or serialized-scope entry point.
    template<class Snapshot,class Factory>Orchestration::InitResult Materialize(Callback& callback,
        const Snapshot& settings,Factory&& acquire)
    {
        using Status=DlssNr::NativePreparationStatus;
        std::shared_ptr<NativeInitialMaterialization::Store> store;
        // RequiredSrInput rechecks callback currentness under the State lock.
        // Capture it before acquiring that same nonrecursive lock below.
        const auto selectedSr=callback.RequiredSrInput();
        {
            std::lock_guard lock(state_->mutex);
            if(callback.state_!=state_||state_->closed||state_->priorWritersClosing||state_->preparing||callback.runtimeAttempted_||
               !callback.pin_||!callback.pin_->Current()||!callback.call_||!callback.publication_||
               !callback.publication_->Complete()||!state_->Owner()||
               !settings.DlssNrNativeProtocol.value_or_default()||settings.DlssNrRoute.value_or_default()!=0||
               !settings.GetDlssNrRuntimeSnapshot().enabled)return {};
            callback.runtimeAttempted_=true;
            try
            {
                auto& owners=*state_->Owner();
                store=std::make_shared<NativeInitialMaterialization::Store>(owners);
                store->source=callback.publication_;store->call=callback.call_;
                store->DeclareRuntime();if(!store->CaptureSettings(settings))return {};
                store->request.placement=settings.DlssNrRunBeforeSr.value_or_default()?C::Placement::NativeBefore:C::Placement::NativeAfter;
                if(store->request.placement==C::Placement::NativeAfter)
                    if(selectedSr)
                    {store->selectedSrSample_=callback.sample_;store->selectedSrOutput_=selectedSr->view;}
                if(!state_->initialPort)state_->initialPort=std::shared_ptr<NativeInitialMaterialization>(new NativeInitialMaterialization(
                    [weak=std::weak_ptr<State>(state_)]{auto state=weak.lock();return state?ObserveOwnerRetirement(*state):Orchestration::NativeDrainReadiness{};}));
                if(!state_->lifetime)state_->lifetime=NativeSessionLifetime::Adopt(state_->owners,callback.scope_,state_->initialPort);
                // All root/kernel/I06 failure storage and the original input
                // closure exist before calling any renderer/provider owner.
                if(!state_->initialization)state_->initialization=store;
                state_->preparing=true;
            }
            catch(...){callback.runtimeStatus_=Status::Refused;return {};}
        }
        struct End
        {
            std::shared_ptr<State> state;
            std::shared_ptr<NativeInitialMaterialization::Store> store;Callback& callback;
            ~End(){callback.runtimeReason_=store->reason;std::lock_guard lock(state->mutex);state->preparing=false;if(state->initialPort)state->initialPort->EndBorrow();}
        }end{state_,store,callback};
        try
        {
            store->reason.Assign("Native.Initialization.Preflight");auto renderer=acquire();
            if(!renderer||!renderer->PreflightCurrent()||!callback.Current())return {};
            const auto encode=renderer->Encode();if(!encode)return {};store->encode=*encode;
            auto policy=std::make_unique<Context::SemanticProfile>();
            const auto profile=Protocol::BuildProfileKey(*Protocol::LookupProfile(Protocol::Family::LegacyCompatibleNativeDlssNr,1),
                store->runtime,C::GraphicsApi::D3D12,store->request.placement,Protocol::Symbol("NativeTemporal"));
            if(!profile)return {};policy->key=*profile;store->request.requestedProfile=*profile;
            policy->purpose=Protocol::Symbol("RENDER.NativeSemantics");policy->rule=Protocol::Symbol("CTX.Semantics");
            {std::lock_guard lock(state_->mutex);if(state_->closed)return {};policy->publication=state_->Owner()->ContextJournal().Event().evidence.record;}
            policy->nativeSampleDomainVersion=1;policy->boundary=C::BoundaryKind::BeforeUpscale;
            policy->acceptedEvidence.Push(C::SourceClass::Native);
            policy->sourceSchemas.Push({Protocol::Symbol("NGX"),{1,0},Context::SourceSchemaDomain::NativeNgxAdapter});
            for(const auto field:{"color.domain","depth.device","motion.native","jitter"})policy->requirements.Push({Protocol::Symbol(field),true});
            store->reason.Assign("Native.Initialization.Context");const auto* context=PrepareContext(callback,*policy);
            if(!context||!context->context||!context->certificate||context->certificate->eligibility!=C::Eligibility::Eligible)return {};
            store->reason.Assign("Native.Initialization.C12");const auto* representations=PrepareRepresentations(callback,store->request.placement,*encode);
            if(!representations||!representations->bundle||!callback.Current()||!renderer->PreflightCurrent())return {};
            if(renderer->NeedsModelInitialization())
            {
                std::lock_guard lock(state_->mutex);
                if(state_->closed||state_->initializationCount==state_->initializationHistory.size())return {};
                // Bind the exact current callback/settings closure BEFORE entry.
                // A prior harmless refusal is not this attempt's failure record;
                // every possibly effectful record remains retained separately.
                state_->initializationHistory[state_->initializationCount++]=store;
                state_->initialization=store;store->possibleEffects=true;
            }
            store->reason.Assign("Native.Initialization.ModelPreparation");
            if(!renderer->PrepareModel(*state_->resources,callback.runtimeStatus_))return {};
            const auto* resources=renderer->Inspect();
            if(callback.runtimeStatus_!=Status::Ready||!resources||!resources->feature||!renderer->Current()||!callback.Current())return {};
            const auto* output=state_->resources->Observe(resources->modelOutput);
            const auto ownership=state_->resources->Ownership(output);if(!ownership)return {};
            // Model/recording borrows still exclude changes. This tail performs
            // only CPU metadata/identity publication under source reservation.
            std::lock_guard lock(state_->mutex);if(state_->closed)return {};
            if(resources->providerUses)
            {
                if(state_->rendererOwner&&state_->rendererOwner!=resources->providerUses)return {};
                state_->rendererOwner=resources->providerUses;
            }
            // Selected warmup may prepare the real model, but must not commit
            // a HostReturn route that would later be relabeled for FG.
            if(state_->controlledFeature&&!state_->controlledFg)
            {store->reason.Assign("controlled-fg-warmup-pending");return {};}
            if(state_->controlledFg)
            {
                auto selected=state_->controlledFg;
                std::unique_lock selectedLock(selected->mutex,std::try_to_lock);
                if(!selectedLock.owns_lock()||callback.featureHandle_!=state_->controlledFeatureId||
                   callback.generation_!=state_->controlledFeatureGeneration)return {};
                if(selected->invocation||selected->observation.failed)return {};
                store->selectedConsumer_=selected->consumer;store->selectedEnrollment_=selected->enrollment;
                store->selectedWriterCurrent_=selected->writerCurrent;
            }
            Orchestration::InitResult result;
            const bool published=callback.pin_->WithCurrent([&]{
                auto& owners=*state_->Owner();
                store->reason.Assign("Native.Initialization.RuntimeIdentity");
                if(!Context::Established(state_->runtimeIncarnation))
                {
                    const auto issued=owners.RuntimeProvider()->Incarnation(owners.RuntimeJournal().Event());
                    if(issued.status!=IdentityStatus::Ok)return false;
                    state_->runtimeIncarnation=issued.value;state_->runtimeFeature=resources->feature;
                }
                store->reason.Assign("Native.Initialization.ProfileInputs");
                if(state_->runtimeFeature!=resources->feature||!store->BindProfile(callback.scope_,state_->runtimeIncarnation,*ownership))return false;
                if(!state_->initialPort->Stage(callback.scope_,store))return false;
                store->reason.Assign("Native.Initialization.Coordinator");
                result=state_->lifetime->Coordinator().TryActivate();if(result.current)store->reason={};return true;
            });
            if(published&&result.current)
            {
                callback.currentPreparation_=store;
                if constexpr(requires{renderer->Transfer();})
                {
                    callback.rendererBorrow_=renderer->Transfer();
                    if(!callback.rendererBorrow_)return {};
                }
            }
            return published?result:Orchestration::InitResult{};
        }
        catch(...){if(store->reason.Empty())store->reason.Assign("Native.Initialization.Refused");return {};}
    }
    Orchestration::InitResult PrepareRuntime(Callback&,const NrConfigSnapshot<Config>&);
    Protocol::NativeProtocolResult RunNativeBefore(Callback&,void*,const NrConfigSnapshot<Config>&);
    Protocol::NativeProtocolResult RecordNativeResult(Callback& callback,Protocol::NativeProtocolResult result)noexcept
    {
        if(callback.state_!=state_)return result;
        // An unavailable invocation may be the consequence of a precise
        // preparation refusal. Preserve that cause in both returned views.
        if(result.reason.View()=="Native.InitializationUnavailable"&&!callback.runtimeReason_.Empty())
            result.reason=callback.runtimeReason_;
        callback.runtimeReason_=result.reason;
        return result;
    }
    Protocol::NativeProtocolResult RecordCommandStateUnavailable(Callback& callback,
        const D3D12::NativeRecordingObservation& recording,
        C::Symbol detail = Protocol::Symbol("Native.CommandStateUnavailable"))noexcept
    {
        Protocol::NativeProtocolResult result;
        result.reason=!recording.nativeList||!recording.active?Protocol::Symbol("Native.RecordingUnavailable"):
            !recording.completeCoverage?Protocol::Symbol("Native.HookCoverageIncomplete"):detail;
        return RecordNativeResult(callback,result);
    }
    void ObserveNativeRestoration(Callback& callback,const Protocol::NativeProtocolResult& result,
        bool commandRestored,const D3D12::NativeRecordingObservation& recording)noexcept
    {
        callback.nativeCpuRestored_=!result.facts.outputRestoreFailed;
        callback.nativeCommandRestored_=commandRestored&&!result.facts.commandStateRestoreFailed;
        try
        {
            const auto owner=callback.invocation_;
            if(!callback.nativeCpuRestored_||!callback.nativeCommandRestored_||!callback.Current()||
               !owner||!owner->snapshot_||owner->snapshot_->product.recipe.placement!=C::Placement::NativeAfter||
               !owner->publishedOutput_||!result.evaluation||result.evaluation->stage!=C::OutcomeStage::Produced||
               result.evaluation->evaluation!=owner->snapshot_->product.recipe.evaluation||
               !Context::Established(result.evaluation->output)||!callback.transactionObservation_||
               !recording.active||!recording.completeCoverage||recording.nativeList!=callback.call_->CommandList())return;
            const auto* content=Context::ResolveMetadata(*owner->publishedOutput_,*owner->store_);
            const auto* view=content?Context::ResolveNativeOutput(*content,*owner->store_):nullptr;
            if(!view||content->recording!=owner->seeds_.recording||
               content->view!=result.evaluation->output.KnownPart()->value||
               owner->outputTarget_!=callback.call_->Resource("Output"))return;
            auto read=owner->resources_->BorrowCurrentRead(owner->outputTarget_,*view);
            if(!read)return;
            auto lock=owner->resources_->LockNativeAction();
            if(read->Current())callback.transactionObservation_->AdvanceSelectedOutput(read->View(),
                recording.incarnation,recording.workOrdinal,SelectedReadFactory(owner->outputTarget_,read->View()));
        }
        catch(...){/* Source export remains unavailable; local HostReturn is independent. */}
    }
    void FinishNativeReturn(Callback&,std::uint32_t,bool)noexcept;
    bool BeginControlledSelection(std::uintptr_t,std::uint64_t);
    bool EnrollControlled(const DlssNr::NativeControlledFgEnrollmentV1&);
    bool FinishControlledWriters(const std::function<bool()>&);
    FfxNrStatusV1 BeginControlledConsumer(FfxNrOwnedOutputHandleV1,FfxNrAlgorithmHandleV1,const FfxNrDispatchTicketV1&,void**);
    void EndControlledConsumer(void*,std::int32_t);
    FfxNrStatusV1 BeginControlledSubmit(const FfxNrDispatchTicketV1&,void**);
    void EndControlledSubmit(void*,const FfxNrSubmitResultV1&);
    bool ObserveControlled(DlssNr::NativeControlledFgObservationV1&,bool);
    const NativeRepresentationPreparation* PrepareRepresentations(Callback& callback,C::Placement placement,
        const Protocol::NativeEncodeControls& controls)
    {
        std::lock_guard lock(state_->mutex);
        if(callback.state_!=state_||state_->closed||!callback.pin_||!callback.pin_->Current()||!state_->Owner()||
           !callback.call_||!callback.publication_||!callback.publication_->PrepareRepresentations(*state_->Owner(),*callback.call_,placement,controls)||
           !callback.pin_->Current())return nullptr;
        return callback.publication_->RepresentationPreparation();
    }
    const Context::NativeContextResult* PrepareContext(Callback& callback,const Context::SemanticProfile& policy)
    {
        std::lock_guard lock(state_->mutex);
        if(callback.state_!=state_||state_->closed||!callback.pin_||!callback.pin_->Current()||
           !state_->Owner()||!callback.publication_||!callback.publication_->Complete()||
           !callback.publication_->PrepareContext(*state_->Owner(),policy)||!callback.pin_->Current())return nullptr;
        return callback.publication_->ContextPreparation();
    }
    template<class Parameters,class Result>bool BindCall(Callback& callback,ID3D12GraphicsCommandList* list,
        Parameters* parameters,Result success,const NativeControlledSceneProducer::Observe& observe={})noexcept
    {
        // One original capture per outer sample, including a refused attempt.
        // Failure does not invoke a candidate or replace the original SR service.
        if(callback.state_!=state_||callback.callCaptured_||!callback.Current())return false;
        callback.callCaptured_=true;
        callback.call_=DlssNr::NativeNgxCallCapture::Capture(list,parameters,success);
        if(!callback.Current()){callback.call_.reset();return false;}
        if(!callback.call_)return false;
        NativeResourceRegistry* resources=nullptr;
        {std::lock_guard lock(state_->mutex);if(!state_->closed)resources=state_->resources;}
        if(!resources)return false;
        decltype(callback.resources_) registered{};
        const auto& names=DlssNr::NativeNgxCallCapture::ResourceNames();
        for(std::size_t i=0;i<names.size();++i)
            if(auto* native=callback.call_->Resource(names[i]))
            {registered[i]=resources->Observe(native);if(!registered[i])return false;}
        if(!callback.Current()){callback.call_.reset();return false;}
        callback.resources_=registered;
        callback.parameterIdentity_=parameters;
        try
        {
            // Parameter/resource capture can call foreign code. Check the live
            // span AFTER those reads, never reuse a pre-capture observation.
            const auto flags=callback.creation_.Value("DLSS.Feature.Create.Flags");
            if(observe&&flags&&ControlledCreation(callback.creation_,(*flags&8u)?1u:0u))
                callback.producerColor_=NativeControlledSceneProducer::Consume(*callback.call_,callback.sample_,
                    callback.generation_,parameters,observe(list),registered);
            // The only replay is our immutable CPU Get adapter. Never enter
            // external COM/parameters while this source/journal lock is held.
            std::lock_guard lock(state_->mutex);
            if(state_->closed||!callback.pin_->Current()||!state_->Owner())return false;
            state_->latestReturn={};state_->latestReturnOwner.reset();
            state_->returnObservationAmbiguous=state_->callbacks!=1;
            auto& observed=state_->latestReturn;observed.status=2;
            observed.sequence=callback.sample_.producerOrdinal;observed.featureHandle=callback.featureHandle_;
            observed.commandList=reinterpret_cast<std::uintptr_t>(list);
            observed.outputResource=reinterpret_cast<std::uintptr_t>(callback.call_->Resource("Output"));
            observed.callbackRecord=callback.sample_.callback.value;
            const auto sourceNamespace=callback.sample_.callback.nameSpace.View();
            const auto sourceIssuer=callback.sample_.callback.issuer.View();
            if(sourceNamespace.size()>=sizeof(observed.sourceNamespace)||sourceIssuer.size()>=sizeof(observed.sourceIssuer))
                state_->returnObservationAmbiguous=true;
            std::memcpy(observed.sourceNamespace,sourceNamespace.data(),
                (std::min)(sourceNamespace.size(),sizeof(observed.sourceNamespace)-1));
            std::memcpy(observed.sourceIssuer,sourceIssuer.data(),
                (std::min)(sourceIssuer.size(),sizeof(observed.sourceIssuer)-1));
            callback.publication_=std::shared_ptr<NativeSourcePublication>(new NativeSourcePublication(*state_->Owner()));
            const bool published=callback.publication_->Publish(*state_->Owner(),callback.sample_,callback.scope_.scope,
                callback.incarnation_,callback.generation_,callback.creation_,*callback.call_,registered,
                callback.transaction_.get(),callback.producerColor_.get());
            if(published&&callback.transaction_&&callback.transaction_->sample==callback.sample_)
            {
                auto subject=*callback.transaction_;
                if(registered[0])subject.device=registered[0]->device;
                callback.transactionObservation_=std::shared_ptr<NativeSourceTransactionObservation>(
                    new NativeSourceTransactionObservation(std::move(subject),callback.call_->SourceFrame()));
                callback.publication_->transaction_=callback.transactionObservation_;
            }
            return published&&callback.pin_->Current();
        }
        catch(...){return false;}
    }
    template<class Parameters,class Result>bool ObserveSrReturn(Callback& callback,Parameters* parameters,
        Result result,Result success)noexcept
    {
        if(callback.state_!=state_||!callback.call_||!callback.Current()||callback.srReturn_)return false;
        Callback::RequiredSrReturnValue observed;
        observed.result=static_cast<std::uint32_t>(result);
        observed.succeeded=result==success;
        // A returned API result does not prove an Output write. This only
        // checks that the original caller binding survived the opaque call.
        try
        {
            void* original=nullptr,*current=nullptr;
            observed.callerBindingMatches=parameters&&callback.CallerOutputTarget()&&
                callback.call_->Get("Output",&original)==0&&
                parameters->Get("Output",&current)==success&&current==original;
        }
        catch(...){observed.callerBindingMatches=false;}
        callback.srReturn_=observed;
        return true;
    }
    struct OpaqueSrOperation
    {
        std::shared_ptr<DlssNr::NativeNgxCallCapture> call;
        std::unique_ptr<DlssNr::GpuSafety::LocalRecordingAction> action;
        std::shared_ptr<DlssNr::NativeRendererInvocationBorrow> joinedBorrow;
        std::optional<NativeResourceRegistry::OpaqueWriteReservation> reservation;
        std::unique_ptr<OpaqueSrEvaluationReceipt> receipt;
        std::optional<DlssNr::NativeSelectedSrLifetimeLedger::Registration> lifetimeRegistration;
        bool entered=false;
        std::atomic<bool> committed{false};
        const DlssNr::GpuSafety::LocalRecordingAction* Action()const
        {return joinedBorrow?&joinedBorrow->RecordingAction():action.get();}
    };
    void RetireSuccessfulSrCaptures()noexcept
    {
        std::array<std::shared_ptr<OpaqueSrOperation>,16> observed,retired;
        {std::lock_guard lock(state_->mutex);observed=state_->opaqueEffects;}
        for(std::size_t i=0;i<observed.size();++i)
        {
            const auto& operation=observed[i];
            const bool transferred=operation&&operation->lifetimeRegistration&&
                state_->srLifetime->RetainsTerminal(*operation->lifetimeRegistration);
            if(operation&&(transferred||(operation->committed&&operation->reservation&&
               DlssNr::GpuSafety::InspectTerminalRecording(operation->reservation->recording))))
            {
                std::lock_guard lock(state_->mutex);
                if(state_->opaqueEffects[i]==operation)retired[i]=std::move(state_->opaqueEffects[i]);
            }
        }
        // Release duplicate captures outside source locks. Canonical Resource
        // retention and provider/feature lifetime owners are unaffected.
    }
    ID3D12Resource* BeforeSrInput(Callback& callback)noexcept
    try
    {
        auto owner=callback.invocation_;
        if(callback.state_!=state_||!callback.Current()||!owner||!owner->return_||
           !owner->snapshot_||owner->snapshot_->product.recipe.placement!=C::Placement::NativeBefore||
           !owner->publishedOutput_||!owner->LiveRecording()||!owner->return_->HistoryAwaitSafe())return nullptr;
        const auto* content=Context::ResolveMetadata(*owner->publishedOutput_,*owner->store_);
        const auto* view=content?Context::ResolveNativeOutput(*content,*owner->store_):nullptr;
        const auto current=state_->resources->CurrentRegisteredView(owner->outputTarget_);
        const auto* original=callback.RegisteredResource("Color");
        if(!view||!current||!original||current->identity!=view->identity||
           !Context::SemanticEqual(current->descriptor,original->descriptor)||
           !Context::SemanticEqual(current->raster,original->raster))return nullptr;
        return owner->outputTarget_;
    }
    catch(...){return nullptr;}
    bool TransitionSrInput(Callback& callback,ID3D12Resource* input,bool reading)noexcept
    try
    {
        if(!input||BeforeSrInput(callback)!=input)return false;
        auto borrow=callback.invocation_->LiveBorrow();
        auto token=borrow?borrow->IssueWriteToken(input):nullptr;
        auto* list=callback.call_->CommandList();if(!token||!token->CurrentFor(input,list))return false;
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=input;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=reading?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter=reading?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        list->ResourceBarrier(1,&barrier);return borrow->Current();
    }
    catch(...){return false;}
    void ObserveSrInputRestoration(Callback& callback,bool restored)noexcept
    {callback.nativeCpuRestored_=callback.nativeCpuRestored_&&restored;}
    // The actual outer EvaluateFeature wrapper opens this scope immediately
    // before calling NGX. The API boundary is an operation observation; only
    // the Resource owner may publish its reserved output revision.
    template<class Parameters,class Result>std::shared_ptr<OpaqueSrOperation> BeginOpaqueSr(
        Callback& callback,const void* feature,Parameters* parameters,Result success,
        const D3D12::NativeRecordingObservation& recording)noexcept
    try
    {
        RetireSuccessfulSrCaptures();
        auto operation=std::make_shared<OpaqueSrOperation>();
        // The original SR call still runs if tracking cannot be retained. Once
        // reserved, its destination must stop exposing an older content fact.
        struct UnretainedReservation
        {
            OpaqueSrOperation& operation;NativeResourceRegistry* registry;bool retained=false;
            ~UnretainedReservation()
            {
                if(!retained&&registry&&operation.reservation)
                    registry->InvalidateSelectedSrWrite(*operation.reservation);
            }
        } pending{*operation,state_->resources};
        operation->call=callback.call_;
        OpaqueSrEvaluationReceipt::Entry entry;
        entry.featureHandle=feature;entry.featureGeneration=callback.generation_;
        entry.parameters=parameters;entry.entrySequence=OpaqueSrEvaluationReceipt::NextSequence();
        if(callback.state_==state_&&callback.Current()&&callback.call_&&
           callback.parameterIdentity_==parameters&&callback.CallerOutputTarget())
        {
            auto* list=callback.call_->CommandList();
            const auto& observed=recording;
            entry.nativeList=observed.nativeList;entry.recordingIncarnation=observed.incarnation;
            entry.workOrdinal=observed.workOrdinal;
            entry.completeCoverage=observed.active&&observed.completeCoverage&&observed.nativeList==list;
            const auto target=callback.CallerOutputTarget();
            entry.output={target->region.resource,target->view.identity};
            entry.outputRegion={target->region.x,target->region.y,target->region.width,target->region.height};
            entry.outputSubresource=target->region.subresource;
            constexpr std::array<std::string_view,5> keys={"Color","Depth","MotionVectors",
                "ExposureTexture","DLSS.Input.Bias.Current.Color.Mask"};
            auto* nativeInput=BeforeSrInput(callback);
            for(std::size_t i=0;i<keys.size();++i)
                if(const auto* view=callback.RegisteredResource(keys[i]))
                    entry.inputs[i]={callback.call_->Resource(keys[i]),view->identity};
            if(nativeInput)
                if(const auto view=state_->resources->CurrentRegisteredView(nativeInput))
                    entry.inputs[0]={nativeInput,view->identity};
            // Recheck every live binding at entry. The immutable CPU capture
            // alone may be older than the parameters passed to this call.
            bool bindingsCurrent=callback.call_->MatchesEvaluationScalars(parameters,success);
            for(std::size_t i=0;i<keys.size();++i)
            {
                const auto& key=keys[i];
                void* raw=nullptr,*original=nullptr;
                const bool live=parameters->Get(key.data(),&raw)==success&&raw;
                const bool captured=callback.call_->Get(key.data(),&original)==0&&original;
                if(i==0&&nativeInput)original=nativeInput;
                if(live!=captured||(live&&raw!=original)){bindingsCurrent=false;break;}
            }
            void* rawOutput=nullptr,*originalOutput=nullptr;
            bindingsCurrent=bindingsCurrent&&parameters->Get("Output",&rawOutput)==success&&
                callback.call_->Get("Output",&originalOutput)==0&&rawOutput==originalOutput;
            unsigned x=0,y=0;
            bindingsCurrent=bindingsCurrent&&
                parameters->Get("DLSS.Output.Subrect.Base.X",&x)==success&&
                parameters->Get("DLSS.Output.Subrect.Base.Y",&y)==success&&
                x==target->region.x&&y==target->region.y;
            NativeResourceRegistry* registry=nullptr;
            {std::lock_guard lock(state_->mutex);if(!state_->closed)registry=state_->resources;}
            bool inputsPublished=callback.publication_&&callback.publication_->Complete();
            for(std::size_t i=0;i<keys.size();++i)
            {
                if(i==0&&nativeInput)continue;
                const auto* view=callback.RegisteredResource(keys[i]);
                if((i<3||view)&&(!view||!callback.publication_||
                   !callback.publication_->DescribesInput(callback.sample_,keys[i],*view)))inputsPublished=false;
            }
            if(inputsPublished)entry.invocationPublication=callback.publication_->Source()->evidence.record;
            // The supported output is the full registered subresource. A partial
            // write cannot turn the rest of an unknown allocation into content.
            const auto extent=target->view.raster.active;
            const bool full=Context::Established(extent)&&!target->region.x&&!target->region.y&&
                !target->region.subresource&&extent.KnownPart()->value.x==0&&extent.KnownPart()->value.y==0&&
                extent.KnownPart()->value.width==target->region.width&&extent.KnownPart()->value.height==target->region.height;
            if(bindingsCurrent&&entry.completeCoverage&&inputsPublished&&full&&registry&&list&&target->region.resource)
            {
                const auto ticket=DlssNr::GpuSafety::Record(list);
                if(nativeInput)operation->joinedBorrow=callback.invocation_->LiveBorrow();
                else operation->action=DlssNr::GpuSafety::BeginLocalAction(ticket,list,target->region.resource);
                if(const auto* action=operation->Action())
                    operation->reservation=registry->ReserveSelectedSrWrite(target->region.resource,list,*action);
                if(operation->reservation)
                {
                    entry.output.identity=operation->reservation->before;
                    entry.reservedRevision=operation->reservation->revision;
                }
            }
        }
        operation->receipt=std::make_unique<OpaqueSrEvaluationReceipt>(std::move(entry));
        if(operation->Action()&&operation->reservation&&operation->Action()->Current())
            operation->receipt->EnrollNgxSuperSampling();
        if(operation->reservation&&callback.pin_)
            operation->lifetimeRegistration=state_->srLifetime->Register(callback.pin_->Lifetime(),
                operation->receipt->Original(),operation->reservation->recording,operation->call);
        if(!operation->lifetimeRegistration)state_->srLifetime->MarkUnknown();
        {
            std::lock_guard lock(state_->mutex);
            if(state_->closed)return {};
            auto slot=std::find(state_->opaqueEffects.begin(),state_->opaqueEffects.end(),nullptr);
            if(slot==state_->opaqueEffects.end())return {};
            *slot=operation; // before the wrapper may enter the external API
            pending.retained=true;
        }
        return operation;
    }
    catch(...){return {};}
    template<class Parameters,class Result>void SealOpaqueSr(Callback& callback,OpaqueSrOperation& operation,
        const void* feature,Parameters* parameters,Result result,Result success,
        const D3D12::NativeRecordingObservation& recording)noexcept
    try
    {
        struct ReleaseAction
        {
            OpaqueSrOperation& operation;NativeResourceRegistry* registry;
            ~ReleaseAction()
            {
                if(registry&&!operation.committed&&operation.reservation&&operation.receipt&&operation.receipt->PossibleEffects())
                    registry->InvalidateSelectedSrWrite(*operation.reservation);
                operation.action.reset();operation.joinedBorrow.reset();
            }
        } release{operation,state_->resources};
        if(!operation.receipt)return;
        OpaqueSrEvaluationReceipt::Return observed;
        observed.featureHandle=feature;observed.featureGeneration=callback.generation_;
        observed.parameters=parameters;observed.returnSequence=OpaqueSrEvaluationReceipt::NextSequence();
        observed.ngxResult=static_cast<std::uint32_t>(result);
        observed.ngxSuccess=static_cast<std::uint32_t>(success);
        if(callback.state_==state_&&callback.Current()&&callback.call_)
        {
            observed.nativeList=recording.nativeList;observed.recordingIncarnation=recording.incarnation;
            observed.workOrdinal=recording.workOrdinal;
            observed.completeCoverage=recording.active&&recording.completeCoverage;
            observed.parameterValuesMatch=callback.call_->MatchesEvaluationScalars(parameters,success);
            const auto target=callback.CallerOutputTarget();
            if(target)
            {
                observed.output={target->region.resource,target->view.identity};
                observed.outputRegion={target->region.x,target->region.y,target->region.width,target->region.height};
                observed.outputSubresource=target->region.subresource;
            }
            constexpr std::array<std::string_view,5> keys={"Color","Depth","MotionVectors",
                "ExposureTexture","DLSS.Input.Bias.Current.Color.Mask"};
            for(std::size_t i=0;i<keys.size();++i)
            {
                void* raw=nullptr,*original=nullptr;
                const bool live=parameters&&parameters->Get(keys[i].data(),&raw)==success&&raw;
                const bool captured=callback.call_->Get(keys[i].data(),&original)==0&&original;
                const auto& expected=operation.receipt->Original().inputs[i];
                if(i==0&&callback.invocation_&&expected.native==callback.invocation_->outputTarget_)
                {
                    const auto current=state_->resources->CurrentRegisteredView(expected.native);
                    if(live&&raw==expected.native&&current)observed.inputs[i]={expected.native,current->identity};
                    continue;
                }
                if(live!=captured||(live&&raw!=original))observed.inputs[i].native=
                    reinterpret_cast<ID3D12Resource*>(raw?raw:original);
                else if(captured)
                    if(const auto* view=callback.RegisteredResource(keys[i]))
                        observed.inputs[i]={callback.call_->Resource(keys[i]),view->identity};
            }
            void* raw=nullptr,*original=nullptr;
            if(!parameters||parameters->Get("Output",&raw)!=success||
               callback.call_->Get("Output",&original)!=0||raw!=original)
                observed.output.native=nullptr;
            unsigned x=0,y=0;
            if(!parameters||parameters->Get("DLSS.Output.Subrect.Base.X",&x)!=success||
               parameters->Get("DLSS.Output.Subrect.Base.Y",&y)!=success||
               x!=observed.outputRegion.x||y!=observed.outputRegion.y)
                observed.outputRegion={};
        }
        if(operation.lifetimeRegistration)state_->srLifetime->ObserveReturn(*operation.lifetimeRegistration,observed);
        else state_->srLifetime->MarkUnknown();
        operation.receipt->Seal(observed);
        callback.srReceiptClass_=operation.receipt->Classification();
        if(!operation.receipt->PossibleEffects())
        {
            std::lock_guard lock(state_->mutex);
            for(auto& held:state_->opaqueEffects)if(held.get()==&operation){held.reset();break;}
        }
        // Only the selected operation adapter can publish; an ordinary opaque
        // call continues to retain possible effects without a write revision.
        if(!operation.receipt->PublishedRevision())return;
        if(callback.srReceiptClass_!=OpaqueSrEvaluationReceipt::Class::RecordedOpaqueWrite||
           !operation.Action()||!operation.reservation||!callback.srReturn_||
           !callback.srReturn_->succeeded||!callback.srReturn_->callerBindingMatches)return;
        NativeResourceRegistry* registry=nullptr;
        {std::lock_guard lock(state_->mutex);if(!state_->closed)registry=state_->resources;}
        if(!registry||!registry->CommitSelectedSrWrite(*operation.reservation,
            operation.reservation->native,operation.reservation->list,*operation.Action(),*operation.receipt))
        {callback.srReceiptClass_=OpaqueSrEvaluationReceipt::Class::Unavailable;return;}
        operation.committed=true;
        callback.srLocalRecordingObserved_=true;
        // Pin this exact committed Resource publication across the return tail.
        // The pin excludes owned bookkeeping changes without holding a lock
        // across any foreign call. It grants no C03 use or host GPU authority.
        const auto output=registry->CurrentRegisteredView(operation.reservation->native);
        auto expected=operation.reservation->before;
        expected.contentRevision=operation.reservation->revision;
        if(!output||output->identity!=expected)
        {callback.srReceiptClass_=OpaqueSrEvaluationReceipt::Class::Unavailable;return;}
        auto outputRead=registry->BorrowCurrentRead(operation.reservation->native,*output);
        if(!outputRead||!outputRead->Current())
        {callback.srReceiptClass_=OpaqueSrEvaluationReceipt::Class::Unavailable;return;}
        if(callback.producerColor_)
        {
            auto outputColor=callback.producerColor_->SelectedOutput(*operation.receipt,outputRead->View());
            // Keep input-only declaration if this exact SR adapter cannot
            // establish the output; publication then leaves Output unknown.
            if(outputColor)callback.producerColor_=std::move(outputColor);
        }
        if(callback.invocation_&&callback.invocation_->snapshot_&&
           callback.invocation_->snapshot_->product.recipe.placement==C::Placement::NativeBefore)
        {
            if(!callback.invocation_->PublishSrReturn(outputRead->View(),*operation.receipt))
            {callback.srReceiptClass_=OpaqueSrEvaluationReceipt::Class::Unavailable;return;}
        }
        std::lock_guard lock(state_->mutex);
        if(state_->closed||!callback.pin_||!callback.pin_->Current()||!state_->Owner()||
           !callback.publication_||!callback.publication_->Complete()||callback.hostOutputRecorded_)return;
        // Publish reads registered input/output views and may project rectangles.
        // Serialize this CPU-only snapshot with emergency invalidation too; a
        // later Current() check alone cannot make racing raw-view reads safe.
        auto metadataLock=registry->LockNativeAction();
        if(!outputRead->Current())
        {callback.srReceiptClass_=OpaqueSrEvaluationReceipt::Class::Unavailable;return;}
        auto fresh=std::shared_ptr<NativeSourcePublication>(new NativeSourcePublication(*state_->Owner()));
        if(fresh->Publish(*state_->Owner(),callback.sample_,callback.scope_.scope,callback.incarnation_,
            callback.generation_,callback.creation_,*callback.call_,callback.resources_,callback.transaction_.get(),
            callback.producerColor_.get())&&outputRead->Current())
        {
            fresh->transaction_=callback.transactionObservation_;
            callback.publication_=std::move(fresh);callback.hostOutputRecorded_=true;
            if(callback.transactionObservation_)callback.transactionObservation_->SelectedOutput(
                outputRead->View(),observed.recordingIncarnation,observed.workOrdinal,
                SelectedReadFactory(operation.reservation->native,outputRead->View()));
        }
        else callback.srReceiptClass_=OpaqueSrEvaluationReceipt::Class::Unavailable;
    }
    catch(...){callback.srReceiptClass_=OpaqueSrEvaluationReceipt::Class::Unavailable;}
    // The enrolled selected SR operation owner supplies a live action only
    // after its actual caller Output write is recorded. An opaque SR success
    // code alone cannot call this path. Until subrect writes have an exact
    // issuer, only a full-origin, single-subresource write may be published.
    bool RefreshHostOutput(Callback& callback,const DlssNr::GpuSafety::LocalRecordingAction& action)noexcept
    {
        try
        {
            const auto target=callback.CallerOutputTarget();
            if(!target||!callback.srReturn_||!callback.srReturn_->succeeded||
               !callback.srReturn_->callerBindingMatches)return false;
            const auto& region=target->region;
            const auto description=region.resource->GetDesc();
            if(region.x||region.y||region.subresource||region.width!=description.Width||
               region.height!=description.Height||!action.CurrentFor(region.resource))return false;
            NativeResourceRegistry* registry=nullptr;
            {
                std::lock_guard lock(state_->mutex);
                if(callback.state_!=state_||state_->closed||!callback.pin_||!callback.pin_->Current()||
                   !callback.call_||!callback.publication_||!callback.publication_->Complete()||
                   callback.runtimeAttempted_||callback.hostOutputRecorded_||!state_->Owner()||!state_->resources)return false;
                callback.hostOutputRecorded_=true; // no retry after a possibly effectful write
                registry=state_->resources;
            }
            auto* list=callback.call_->CommandList();auto* output=callback.call_->Resource("Output");
            if(!list||!output||!callback.Current()||!callback.RegisteredResource("Output"))return false;
            if(!registry->RecordWritten(output,list,action))return false;
            std::lock_guard lock(state_->mutex);
            if(state_->closed||!callback.pin_->Current()||!state_->Owner())return false;
            auto fresh=std::shared_ptr<NativeSourcePublication>(new NativeSourcePublication(*state_->Owner()));
            if(!fresh->Publish(*state_->Owner(),callback.sample_,callback.scope_.scope,callback.incarnation_,
                callback.generation_,callback.creation_,*callback.call_,callback.resources_,callback.transaction_.get()))return false;
            fresh->transaction_=callback.transactionObservation_;
            callback.publication_=std::move(fresh);
            return callback.pin_->Current();
        }
        catch(...){return false;}
    }
    template<class Feature>std::optional<Callback> Capture(DlssNr::NativeFeatureRegistry<Feature>& registry,
        typename DlssNr::NativeFeatureRegistry<Feature>::CallbackPin&& sourcePin)
    {
        // Consume even a refused pin; no hidden retained callback remains in a
        // moved-from optional at the wrapper's early-return boundary.
        auto pin=std::make_unique<OwnedPin<Feature>>(std::move(sourcePin));
        std::lock_guard lock(state_->mutex);
        if(state_->closed||state_->priorWritersClosing||state_->callbacks==MaximumCallbacks||!pin->value.BelongsTo(registry)||!pin->Current()||
           (state_->controlledFeature&&(pin->value.Handle()!=state_->controlledFeatureId||pin->value.Value().generation!=state_->controlledFeatureGeneration)))return {};
        if(!state_->srLifetime->TrackFeature(pin->Lifetime()))return {};
        if(!state_->source)
        {
            try
            {
                if(!state_->Owner())
                {
                    state_->owners=std::make_unique<NativeOwnerSet>(NewNamespace());
                    state_->resources=&state_->owners->Resources();
                }
                state_->source=std::make_unique<Source<Feature>>(*state_->Owner(),registry,pin->value);
            }
            catch(...){state_->closed=true;throw;} // partial canonical issue cannot be retried
        }
        auto* source=dynamic_cast<Source<Feature>*>(state_->source.get());
        if(!source||&source->registry!=&registry)return {};
        const auto sample=source->enrollment.Observe(pin->value);
        if(!sample)return {};
        ++state_->callbacks;
        const auto generation=pin->value.Value().generation;
        const auto creation=pin->value.Value().originalCreation;
        const auto handle=pin->value.Handle();
        return Callback(state_,std::move(pin),*sample,source->enrollment.Enrollment(),source->enrollment.Incarnation(),generation,creation,source->enrollment.Transaction(),handle);
    }
    DlssNr::NativeTeardownObservationV1 PrepareTeardown()noexcept
    try
    {
        CloseSource();
        DlssNr::NativeTeardownObservationV1 result;
        std::shared_ptr<NativeSessionLifetime> lifetime;
        {std::lock_guard lock(state_->mutex);
         result.sourceClosed=state_->closed;result.callbacks=static_cast<std::uint32_t>(state_->callbacks);
         lifetime=state_->lifetime;}
        // Active callbacks may still mutate captures; never sweep their state.
        if(result.callbacks)return result;
        state_->srLifetime->Poll();
        if(lifetime)
        {
            const auto closure=lifetime->AdvanceClosure();
            result.scopeClosed=closure.closed;result.finalizerStopped=closure.finalizerStopped;
        }
        RetireSuccessfulSrCaptures();
        std::array<std::shared_ptr<NativeControlledSceneProducer::Declaration>,16> retired;
        {
            std::lock_guard lock(state_->mutex);
            for(std::size_t i=0;i<state_->controlledProducers.size();++i)
            {
                auto& held=state_->controlledProducers[i];
                if(held&&held->Terminal())retired[i]=std::move(held);
                if(held)++result.controlledProducers;
            }
            for(const auto& held:state_->opaqueEffects)if(held)++result.opaqueCaptures;
        }
        // Capture destruction is outside source locks. Scope metadata remains
        // retained: unavailable aggregate owner retirement never becomes closed.
        return result;
    }
    catch(...){return {};}
    void CloseSource()noexcept
    {
        std::shared_ptr<NativeSessionLifetime> lifetime;
        {std::lock_guard lock(state_->mutex);if(state_->sourceClosureApplied)return;
         state_->sourceClosureApplied=true;state_->closed=true;state_->srLifetime->CloseAdmission();
         if(state_->source)state_->source->Close();
         if(state_->resources)state_->resources->CloseAdmissions();lifetime=state_->lifetime;}
        if(lifetime)lifetime->Coordinator().Revoke(Orchestration::RevocationCause::SessionClose);
    }
    std::shared_ptr<DlssNr::NativeSelectedSrLifetimeLedger> SrLifetime()const noexcept{return state_->srLifetime;}
    template<class Shutdown>void AttemptRendererTeardown(DlssNr::NativeTeardownObservationV1& result,Shutdown&& shutdown)noexcept
    {state_->rendererTeardownAttempt.Observe(result,std::forward<Shutdown>(shutdown));}
    DlssNr::NativeTeardownObservationV1 CompleteTeardown(std::shared_ptr<const DlssNr::NativeRendererRetirement> renderer)noexcept
    try
    {
        std::unique_lock completion(state_->completionMutex,std::try_to_lock);
        if(!completion.owns_lock())return {};
        auto result=PrepareTeardown();
        // V1 scopeClosed is the controlled aggregate completion observation,
        // including physical detachment. Kernel closure alone is insufficient
        // for the worker to proceed to final core shutdown.
        result.scopeClosed=0;
        if(!DlssNr::SourceCapturesDrained(result))return result;
        std::shared_ptr<NativeSessionLifetime> lifetime;
        {
            std::lock_guard lock(state_->mutex);
            if(!renderer||!renderer->Covers(state_->rendererOwner))return result;
            if(state_->rendererRetirement&&state_->rendererRetirement!=renderer)return result;
            state_->rendererRetirement=std::move(renderer);lifetime=state_->lifetime;
            result.nrShutdownAttempted=1;result.nrShutdownSucceeded=1;
            if(state_->retired){result.scopeClosed=1;result.finalizerStopped=1;return result;}
        }
        const auto sr=state_->srLifetime->Retirement();
        if(!sr||!sr->Matches(*state_->srLifetime)||!lifetime)return result;
        const auto closure=lifetime->AdvanceClosure();
        result.finalizerStopped=closure.finalizerStopped;
        if(!closure.closed||!closure.finalizerStopped)return result;
        // No live execution rights remain. Detach COM captures outside source
        // locks; immutable evidence remains readable in the retired scope.
        if(state_->initialPort)state_->initialPort->ReleasePhysicalCapture();
        if(state_->initialization)state_->initialization->call.reset();
        for(auto& store:state_->initializationHistory)if(store)store->call.reset();
        if(!state_->srLifetime->ReleaseRetiredCaptures(*sr)||!state_->resources->ReleaseAfterScopeDrain())return result;
        {std::lock_guard lock(state_->mutex);state_->retired=true;}
        // Aggregate retirement is the first point where all retained callback,
        // algorithm, Resource and History tails may detach their native holds.
        std::shared_ptr<ControlledFg> selected;
        {std::lock_guard lock(state_->mutex);selected=state_->controlledFg;}
        if(selected)
        {
            std::lock_guard selectedLock(selected->mutex);
            if(selected->observation.terminal&&!selected->submitActive)
            {
                if(selected->invocation&&selected->invocation->store_)
                    selected->invocation->store_->selectedWriterCurrent_={};
                selected->publication.reset();selected->selection.reset();selected->invocation.reset();
                selected->transaction.reset();selected->writerCurrent={};selected->algorithm.reset();selected->output.reset();selected->module.reset();selected->moduleFile.reset();
            }
        }
        result.scopeClosed=1;
        return result;
    }
    catch(...){return {};}
    template<class Publish>bool RecreateScope(Publish&& publish)noexcept
    try
    {
        auto fresh=std::shared_ptr<NativeProcessBootstrap>(new NativeProcessBootstrap);
        fresh->state_->closed=true;
        {
            std::lock_guard lock(state_->mutex);
            if(!state_->retired||state_->recreationAttempted||!state_->closed||state_->callbacks||state_->preparing||!state_->source)return false;
            const auto sr=state_->srLifetime->Retirement();if(!sr)return false;
            state_->recreationAttempted=true;
            // The old root remains published and permanently closed until the
            // fresh root is ready. The factory's global-close latch decides
            // publication atomically under its pointer mutex.
            if(!state_->source->Reopen(*sr))return false;
            {std::lock_guard freshLock(fresh->state_->mutex);fresh->state_->closed=false;}
            bool published=false;try{published=publish(fresh);}catch(...){/* no published root */}
            if(!published)
            {
                {std::lock_guard freshLock(fresh->state_->mutex);fresh->state_->closed=true;}
                state_->source->Close();return false;
            }
        }
        return true;
    }
    catch(...){return false;}
};
}
