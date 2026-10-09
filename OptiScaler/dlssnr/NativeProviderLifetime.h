#pragma once
#include "NrGpuSafety.h"
#include "Feature18HostContract.h"
#include <nr/context/ContextMetadata.h>
#include <nr/lifecycle/NativeHistoryState.h>
#include <wrl/client.h>
#include <mutex>
#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace DlssNr
{
namespace PC=Neurotic::Contracts;
// Catalog identities, not caller-asserted booleans. A proprietary build may be
// enrolled only by a reviewed catalog extension with its exact lifetime contract.
class ProviderLifetimeContract
{
    enum class Kind { Unavailable, ControlledD3D12V1, HostRecordedFeature18V1 };
    Kind kind_=Kind::Unavailable;
    explicit ProviderLifetimeContract(Kind kind):kind_(kind){}
  public:
    static ProviderLifetimeContract NgxFeature18(){return ProviderLifetimeContract(Kind::Unavailable);}
    static ProviderLifetimeContract NgxFeature18(std::string_view matchedAdapter){
        return ProviderLifetimeContract(matchedAdapter==Feature18HostContractIdentity?
            Kind::HostRecordedFeature18V1:Kind::Unavailable);}
#ifdef NR_GPU_SAFETY_TEST
    static ProviderLifetimeContract ControlledCallerList(){return ProviderLifetimeContract(Kind::ControlledD3D12V1);}
#endif
    bool Supported()const{return kind_!=Kind::Unavailable;}
    std::string_view Identity()const{return kind_==Kind::HostRecordedFeature18V1?Feature18HostContractIdentity:
        Supported()?"ControlledD3D12CallerList/1/provider_lifetime.cpp/v1":
        "NGX.Feature18/AlphaFeature18.ABI1/API0x15/UnverifiedBuild";}
    bool operator==(const ProviderLifetimeContract&)const=default;
};
struct ProviderRegistrationBinding
{
    PC::NativeSampleIdentityV1 invocation;
    PC::RecordKey registration;
    PC::ResourceIdentityToken resource;
    PC::ProviderIncarnation feature;
    GpuSafety::Ticket recording;
    ID3D12Resource* native=nullptr;
    bool operator==(const ProviderRegistrationBinding&)const=default;
};
class NativeProviderRetirementOwner;
class ProviderTerminalReceipt
{
    friend class NativeProviderRetirementOwner;
    struct Authority {};
    std::shared_ptr<const Authority> authority_;
    ProviderRegistrationBinding binding_;
    ProviderLifetimeContract contract_;
    GpuSafety::TerminalRecordingEvidence terminal_;
    bool providerReportedSuccess_=false;
    ProviderTerminalReceipt(std::shared_ptr<const Authority> authority,ProviderRegistrationBinding binding,
        ProviderLifetimeContract contract,GpuSafety::TerminalRecordingEvidence terminal,bool success):
        authority_(std::move(authority)),binding_(std::move(binding)),contract_(contract),terminal_(std::move(terminal)),
        providerReportedSuccess_(success){}
  public:
    const auto& Binding()const{return binding_;}
    const auto& Contract()const{return contract_;}
    const auto& Terminal()const{return terminal_;}
    bool ProviderReportedSuccess()const{return providerReportedSuccess_;}
};
class ProviderInvocationUse
{
    friend class NativeProviderRetirementOwner;
    enum class State { Reserved, Entered, Returned, Cancelled, Terminal };
    State state_=State::Reserved;
    bool providerReportedSuccess_=false;
    std::vector<ProviderRegistrationBinding> bindings_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> resources_;
    std::vector<std::shared_ptr<const ProviderTerminalReceipt>> receipts_;
    std::shared_ptr<const void> authority_;
    std::size_t retentionSlot_=8192;
    ProviderInvocationUse()=default;
};
// Separate from a provider registration: encode/copy commands can use shared
// renderer storage before provider entry. Cancellation of a Reserved provider
// registration must therefore never release this recording reservation.
#define NR_NATIVE_FEATURE_RECORDING_V1 1
class NativeFeatureRecordingUse
{
    friend class NativeProviderRetirementOwner;
    PC::NativeSampleIdentityV1 invocation_;
    GpuSafety::Ticket recording_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> resources_;
    std::vector<Microsoft::WRL::ComPtr<IUnknown>> allocationIdentities_;
    std::shared_ptr<const void> authority_;
    std::shared_ptr<Neurotic::Lifecycle::NativeHistoryState> history_;
    std::size_t retentionSlot_=128;
    bool closed_=false;
    NativeFeatureRecordingUse()=default;
};
// Owner-issued closure of one prior NR writer. It is neither whole-renderer
// destruction nor release of the later FG algorithm. The terminal receipt and
// retained exact target cannot be reconstructed from serialized metadata.
class NativePriorWriterClosure
{
    friend class NativeProviderRetirementOwner;
    std::shared_ptr<const void> authority_;
    std::shared_ptr<ProviderInvocationUse> provider_;
    std::shared_ptr<NativeFeatureRecordingUse> recording_;
    GpuSafety::Ticket ticket_;
    GpuSafety::TerminalRecordingEvidence terminal_;
    Microsoft::WRL::ComPtr<ID3D12Resource> output_;
    NativePriorWriterClosure(std::shared_ptr<const void> authority,std::shared_ptr<ProviderInvocationUse> provider,
        std::shared_ptr<NativeFeatureRecordingUse> recording,GpuSafety::Ticket ticket,
        GpuSafety::TerminalRecordingEvidence terminal,Microsoft::WRL::ComPtr<ID3D12Resource> output)
        :authority_(std::move(authority)),provider_(std::move(provider)),recording_(std::move(recording)),
         ticket_(std::move(ticket)),terminal_(std::move(terminal)),output_(std::move(output)){}
};
// Renderer retirement owner for provider registrations, shared recording uses
// and the existing history companion. The coordinator/ledger own invocation slots.
class NativeProviderRetirementOwner
{
    using Use=std::shared_ptr<ProviderInvocationUse>;
    using State=ProviderInvocationUse::State;
    ProviderLifetimeContract contract_;
    PC::ProviderIncarnation feature_;
    std::shared_ptr<const ProviderTerminalReceipt::Authority> authority_=std::make_shared<ProviderTerminalReceipt::Authority>();
    mutable std::mutex mutex_;
    std::vector<Use> pending_;
    std::shared_ptr<NativeFeatureRecordingUse> recordingUse_;
    // This companion belongs to this real feature incarnation. It is not an
    // invocation's metadata history reference and never claims an applied reset.
    std::shared_ptr<Neurotic::Lifecycle::NativeHistoryState> history_;
    struct RecordingRetention
    {
        std::mutex mutex;
        std::array<std::shared_ptr<NativeFeatureRecordingUse>,128> slots;
    };
    static RecordingRetention& RetainedRecordings()
    {
        // Created only on admission, before effects. Each live capability owns
        // a slot. Abnormal owner destruction leaves that bounded slot retained;
        // no allocation or COM release is attempted by the destructor.
        static auto* retained=new RecordingRetention;return *retained;
    }
    struct ProviderRetention
    {
        std::mutex mutex;
        std::array<Use,8192> slots;
    };
    static ProviderRetention& RetainedProviders()
    {static auto* retained=new ProviderRetention;return *retained;}
    std::optional<PC::RecordKey> highWater_;
    bool closing_=false;
    bool Owns(const Use& use)const
    {return use&&std::find(pending_.begin(),pending_.end(),use)!=pending_.end();}
    static bool SameIssuer(const PC::RecordKey& a,const PC::RecordKey& b)
    {auto copy=a;copy.value=b.value;return copy==b;}
    void Remove(const Use& use)
    {
        auto& retained=RetainedProviders();std::lock_guard retentionLock(retained.mutex);
        if(use->retentionSlot_<retained.slots.size()&&retained.slots[use->retentionSlot_]==use)
            retained.slots[use->retentionSlot_].reset();
        std::erase(pending_,use);
    }
  public:
    explicit NativeProviderRetirementOwner(ProviderLifetimeContract contract,PC::ProviderIncarnation feature):
        contract_(contract),feature_(feature){}
    // Every admitted use already has a bounded retention slot. Erroneous owner
    // destruction cannot allocate, terminate on allocation failure, or free a
    // possibly live use. Normal cancellation/closure clears the exact slot.
    ~NativeProviderRetirementOwner()=default;
    const auto& Contract()const{return contract_;}
    const auto& Feature()const{return feature_;}
    bool RecordingHeld()const{std::lock_guard lock(mutex_);return bool(recordingUse_);}
    std::shared_ptr<NativeFeatureRecordingUse> ReserveRecording(const PC::NativeSampleIdentityV1& invocation,
        const GpuSafety::LocalRecordingAction& action,ID3D12GraphicsCommandList* list,
        const std::vector<ID3D12Resource*>& resources)
    {
        std::lock_guard lock(mutex_);
        if(!contract_.Supported()||closing_||recordingUse_||!pending_.empty()||
           feature_.Check()!=PC::Error::None||invocation.Check()!=PC::Error::None||
           resources.empty()||resources.size()>4||!action.Current())return {};
        const auto& ticket=action.RecordingTicket();
        for(std::size_t i=0;i<resources.size();++i)
        {
            if(!resources[i]||!GpuSafety::MatchesLocalRecording(ticket,list,resources[i]))return {};
            for(std::size_t j=0;j<i;++j)if(resources[j]==resources[i])return {};
        }
        auto use=std::shared_ptr<NativeFeatureRecordingUse>(new NativeFeatureRecordingUse);
        use->invocation_=invocation;use->recording_=ticket;use->authority_=authority_;
        // Renderer scratch uses distinct committed allocations. Pin canonical
        // COM identities before the table lock; this grants only exclusion,
        // never content, submission, completion or resource-currentness facts.
        for(auto* resource:resources)
        {
            Microsoft::WRL::ComPtr<IUnknown> identity;
            if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&identity))))return {};
            for(const auto& prior:use->allocationIdentities_)if(prior==identity)return {};
            use->resources_.emplace_back(resource);use->allocationIdentities_.push_back(std::move(identity));
        }
        auto& retained=RetainedRecordings();std::lock_guard retentionLock(retained.mutex);
        for(const auto& retainedUse:retained.slots)if(retainedUse)
            for(const auto& identity:use->allocationIdentities_)
                for(const auto& heldIdentity:retainedUse->allocationIdentities_)
                    if(identity==heldIdentity)return {};
        for(std::size_t i=0;i<retained.slots.size();++i)if(!retained.slots[i])
        {use->retentionSlot_=i;retained.slots[i]=use;break;}
        if(use->retentionSlot_==retained.slots.size())return {};
        recordingUse_=use;return use;
    }
    bool RecordingCurrent(const std::shared_ptr<NativeFeatureRecordingUse>& use,
        const PC::NativeSampleIdentityV1& invocation,const GpuSafety::Ticket& ticket,
        ID3D12Resource* resource=nullptr)const
    {
        std::lock_guard lock(mutex_);
        if(!use||use!=recordingUse_||use->closed_||closing_||use->authority_!=authority_||
           use->invocation_!=invocation||use->recording_!=ticket)return false;
        return !resource||std::any_of(use->resources_.begin(),use->resources_.end(),
            [&](const auto& held){return held.Get()==resource;});
    }
    bool RegisterOutputTarget(const std::shared_ptr<NativeFeatureRecordingUse>& use,
        const GpuSafety::LocalRecordingAction& action,ID3D12GraphicsCommandList* list,ID3D12Resource* output)
    {
        if(!output||!action.Current()||!GpuSafety::MatchesLocalRecording(action.RecordingTicket(),list,output))return false;
        Microsoft::WRL::ComPtr<ID3D12Resource> retainedOutput=output;
        Microsoft::WRL::ComPtr<IUnknown> identity;if(FAILED(output->QueryInterface(IID_PPV_ARGS(&identity))))return false;
        std::lock_guard lock(mutex_);
        if(!use||use!=recordingUse_||use->closed_||closing_||use->authority_!=authority_||
           use->recording_!=action.RecordingTicket())return false;
        for(const auto& held:use->allocationIdentities_)if(held==identity)return true;
        if(use->resources_.size()>=5)return false;
        auto& retained=RetainedRecordings();std::lock_guard retentionLock(retained.mutex);
        for(const auto& existing:retained.slots)if(existing&&existing!=use)
            for(const auto& held:existing->allocationIdentities_)if(held==identity)return false;
        use->resources_.reserve(5);use->allocationIdentities_.reserve(5);
        use->resources_.push_back(std::move(retainedOutput));use->allocationIdentities_.push_back(std::move(identity));
        return true;
    }
    std::optional<NativePriorWriterClosure> ClosePriorWriter(const Use& provider,
        const std::shared_ptr<NativeFeatureRecordingUse>& recording,const GpuSafety::Ticket& ticket,ID3D12Resource* output)
    {
        Poll();
        Microsoft::WRL::ComPtr<ID3D12Resource> retainedOutput=output;
        std::lock_guard lock(mutex_);
        if(!contract_.Supported()||!provider||!Owns(provider)||!recording||recording!=recordingUse_||
           recording->authority_!=authority_||recording->recording_!=ticket||!output||
           !std::any_of(recording->resources_.begin(),recording->resources_.end(),[&](const auto& resource){return resource.Get()==output;}))return {};
        // Permanent exclusion is set even if an earlier effect is still pending.
        closing_=true;
        if(pending_.size()!=1||provider->state_!=State::Terminal||provider->receipts_.size()!=provider->bindings_.size())return {};
        for(std::size_t i=0;i<provider->bindings_.size();++i)
            if(provider->bindings_[i].recording!=ticket||provider->bindings_[i].invocation!=recording->invocation_||
               provider->receipts_[i]->authority_!=authority_||
               provider->receipts_[i]->binding_!=provider->bindings_[i]||provider->receipts_[i]->contract_!=contract_)return {};
        const auto terminal=GpuSafety::InspectTerminalRecording(ticket);if(!terminal)return {};
        return NativePriorWriterClosure(authority_,provider,recording,ticket,*terminal,std::move(retainedOutput));
    }
    bool ValidatePriorWriter(const NativePriorWriterClosure& closure,const Use& provider,
        const std::shared_ptr<NativeFeatureRecordingUse>& recording,const GpuSafety::Ticket& ticket,ID3D12Resource* output)const
    {
        std::lock_guard lock(mutex_);
        return closing_&&closure.authority_==authority_&&closure.provider_==provider&&closure.recording_==recording&&
            closure.ticket_==ticket&&closure.output_.Get()==output&&provider&&provider->authority_==authority_&&
            provider->state_==State::Terminal&&recording&&recording->authority_==authority_&&recording->recording_==ticket;
    }
    std::shared_ptr<Neurotic::Lifecycle::NativeHistoryState> History(
        const std::shared_ptr<NativeFeatureRecordingUse>& use,const PC::RecordKey& owner,const PC::RecordKey& firstHistory)
    {
        std::lock_guard lock(mutex_);
        if(!use||use!=recordingUse_||use->closed_||use->authority_!=authority_||
           owner.Check()!=PC::Error::None||firstHistory.Check()!=PC::Error::None)return {};
        if(history_)
        {
            if(history_->Projection().owner!=owner)return {};
            use->history_=history_;return history_;
        }
        history_=std::make_shared<Neurotic::Lifecycle::NativeHistoryState>(owner,firstHistory);
        use->history_=history_;return history_;
    }
    template<class Action> bool WithHistory(const std::shared_ptr<NativeFeatureRecordingUse>& use,Action&& action)
    {
        std::lock_guard lock(mutex_);
        if(!use||use!=recordingUse_||use->closed_||use->authority_!=authority_||!history_||use->history_!=history_)return false;
        return std::forward<Action>(action)(*history_);
    }
    bool CloseRecording(const std::shared_ptr<NativeFeatureRecordingUse>& use)
    {
        std::lock_guard lock(mutex_);
        if(!use||use->authority_!=authority_)return false;
        if(use->closed_)return true;
        if(use!=recordingUse_||!pending_.empty()||(history_&&!history_->InvocationClosed())||
           !GpuSafety::InspectTerminalRecording(use->recording_))return false;
        {
            auto& retained=RetainedRecordings();std::lock_guard retentionLock(retained.mutex);
            if(use->retentionSlot_>=retained.slots.size()||retained.slots[use->retentionSlot_]!=use)return false;
            retained.slots[use->retentionSlot_].reset();
        }
        use->closed_=true;use->resources_.clear();use->allocationIdentities_.clear();recordingUse_.reset();return true;
    }
    Use Reserve(const std::vector<ProviderRegistrationBinding>& bindings,
        const GpuSafety::LocalRecordingAction& action,ID3D12GraphicsCommandList* list)
    {
        std::lock_guard lock(mutex_);
        if(!contract_.Supported()||closing_||bindings.empty()||bindings.size()>4||!action.Current())return {};
        size_t count=0;for(const auto& use:pending_)count+=use->bindings_.size();
        if(count+bindings.size()>64)return {};
        auto high=highWater_;
        for(size_t i=0;i<bindings.size();++i)
        {
            const auto& b=bindings[i];
            if(b.invocation.Check()!=PC::Error::None||b.registration.Check()!=PC::Error::None||
               b.feature.Check()!=PC::Error::None||b.feature!=feature_||!b.native||!b.recording||b.recording!=action.RecordingTicket()||
               b.invocation!=bindings[0].invocation||!Neurotic::Context::SameResourceStructure(b.resource,b.resource)||
               !GpuSafety::MatchesLocalRecording(b.recording,list,b.native))return {};
            if(high&&(!SameIssuer(*high,b.registration)||b.registration.value<=high->value))return {};
            high=b.registration;
        }
        auto use=Use(new ProviderInvocationUse);use->bindings_=bindings;use->authority_=authority_;
        for(const auto& b:bindings)use->resources_.emplace_back(b.native);
        auto& retained=RetainedProviders();std::lock_guard retentionLock(retained.mutex);
        std::size_t index=0;for(;index<retained.slots.size();++index)if(!retained.slots[index])break;
        if(index==retained.slots.size())return {};
        pending_.push_back(use);use->retentionSlot_=index;retained.slots[index]=use;
        highWater_=high;return use;
    }
    bool Reserved(const Use& use,const PC::RecordKey& key,const PC::ResourceIdentityToken& resource)const
    {
        std::lock_guard lock(mutex_);
        if(!Owns(use)||use->state_!=State::Reserved||closing_)return false;
        return std::any_of(use->bindings_.begin(),use->bindings_.end(),[&](const auto& b){
            return b.registration==key&&Neurotic::Context::SameResourceStructure(b.resource,resource);});
    }
    bool Enter(const Use& use,const GpuSafety::LocalRecordingAction& action,ProviderLifetimeContract actual)
    {
        std::lock_guard lock(mutex_);
        if(!Owns(use)||use->state_!=State::Reserved)return false;
        if(!actual.Supported()||actual!=contract_)
        {use->state_=State::Cancelled;use->resources_.clear();Remove(use);return false;}
        if(closing_||
           !action.Current()||action.RecordingTicket()!=use->bindings_[0].recording)return false;
        use->state_=State::Entered;return true;
    }
    // The caller invokes this only between activation and the literal provider
    // call. It cannot cancel a returned or otherwise possibly effectful use.
    // The independent feature recording reservation remains held for encode/copy.
    bool CancelActivatedBeforeCall(const Use& use)
    {
        std::lock_guard lock(mutex_);
        if(!Owns(use)||use->state_!=State::Entered)return false;
        use->state_=State::Cancelled;use->resources_.clear();Remove(use);return true;
    }
    bool GpuHeld(const Use& use,const PC::RecordKey& key,const PC::ResourceIdentityToken& resource)const
    {
        std::lock_guard lock(mutex_);
        if(!Owns(use)||(use->state_!=State::Entered&&use->state_!=State::Returned))return false;
        return std::any_of(use->bindings_.begin(),use->bindings_.end(),[&](const auto& b){
            return b.registration==key&&Neurotic::Context::SameResourceStructure(b.resource,resource);});
    }
    void Returned(const Use& use,bool success)
    {
        std::lock_guard lock(mutex_);
        if(Owns(use)&&use->state_==State::Entered)
        {use->providerReportedSuccess_=success;use->state_=State::Returned;}
    }
    bool Cancel(const Use& use)
    {
        std::lock_guard lock(mutex_);
        if(!Owns(use)||use->state_!=State::Reserved)return false;
        use->state_=State::Cancelled;use->resources_.clear();Remove(use);return true;
    }
    void Poll()
    {
        std::lock_guard lock(mutex_);
        for(auto& use:pending_)if(use->state_==State::Returned)
        {
            const auto terminal=GpuSafety::InspectTerminalRecording(use->bindings_[0].recording);
            if(!terminal)continue;
            std::vector<std::shared_ptr<const ProviderTerminalReceipt>> receipts;
            for(const auto& b:use->bindings_)receipts.emplace_back(new ProviderTerminalReceipt(authority_,b,contract_,*terminal,use->providerReportedSuccess_));
            use->receipts_=std::move(receipts);use->state_=State::Terminal;use->resources_.clear();
        }
        // Keep the feature use until the invocation owner closes its callback,
        // C03 leases, resource action, history and claim tails.
    }
    auto Receipts(const Use& use)const
    {
        std::lock_guard lock(mutex_);
        return use&&use->authority_==authority_?use->receipts_:std::vector<std::shared_ptr<const ProviderTerminalReceipt>>{};
    }
    bool ValidateReceipt(const Use& use,const ProviderRegistrationBinding& binding,
        const std::shared_ptr<const ProviderTerminalReceipt>& receipt)const
    {
        std::lock_guard lock(mutex_);
        return use&&receipt&&use->authority_==authority_&&use->state_==State::Terminal&&receipt->authority_==authority_&&
            receipt->binding_==binding&&receipt->contract_==contract_&&
            std::count(use->receipts_.begin(),use->receipts_.end(),receipt)==1;
    }
    bool Complete(const Use& use)const
    {
        std::lock_guard lock(mutex_);
        if(!use||use->authority_!=authority_)return false;
        if(use->state_==State::Cancelled)return true;
        if(use->state_!=State::Terminal||use->receipts_.size()!=use->bindings_.size())return false;
        for(size_t i=0;i<use->bindings_.size();++i)
            if(use->receipts_[i]->authority_!=authority_||use->receipts_[i]->binding_!=use->bindings_[i]||
               use->receipts_[i]->contract_!=contract_)return false;
        return true;
    }
    size_t Pending()const{std::lock_guard lock(mutex_);return pending_.size();}
    bool CloseInvocation(const Use& use)
    {
        if(!Complete(use))return false;
        std::lock_guard lock(mutex_);Remove(use);return true;
    }
    bool CpuActive(const Use& use)const{std::lock_guard lock(mutex_);return Owns(use)&&use->state_==State::Entered;}
    bool BeginTeardown(){std::lock_guard lock(mutex_);closing_=true;return pending_.empty()&&!recordingUse_;}
};
}
