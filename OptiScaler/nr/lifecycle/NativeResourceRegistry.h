#pragma once
#include "NativeAllocationRecord.h"
#include "OwnerPublicationJournal.h"
#include "OpaqueSrEvaluationReceipt.h"
#include "FinalInputAncestry.h"
#include <dlssnr/NativeIdentity.h>
#include <dlssnr/NrGpuSafety.h>
#include <dlssnr/NativeRendererPreparation.h>
#include <dlssnr/NativeOutputPassToken.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>

namespace Neurotic::Lifecycle
{
class NativeOwnedContinuation;
// One bounded Resource-owner registry in the canonical NativeOwnerSet. COM
// identity is only a local lookup while its real lifetime is retained. Neither
// first observation nor retention establishes a write, C03, or image lineage.
class NativeResourceRegistry
{
    friend class NativeOwnerSet;
    friend class NativeOwnedContinuation;
    friend class NativeProcessBootstrap;
#ifdef NR_SPECTRE_SOURCE_TESTING
    friend class NativeResourceRegistryTestAccess;
#endif
  public:
    static constexpr std::size_t Capacity=16;
    static constexpr std::size_t ReadBorrowsPerAllocation=16;
    static constexpr std::size_t ProjectionsPerAllocation=8;
    struct AllocationOwner
    {
        OwnerPublicationJournal journal;
        std::optional<ResourceIssuer> resource;
        std::optional<RepresentationIssuer> representation;
    };
  private:
    // Bounded state for pins of this registry's canonical publication. A pin
    // owns revocation state and a COM reference, never a raw registry pointer.
    struct ReadBorrowState
    {std::atomic<std::size_t> readers{0};std::atomic<bool> current{true};};
  public:
    class ReadBorrow
    {
        friend class NativeResourceRegistry;
        std::shared_ptr<ReadBorrowState> state_;
        Microsoft::WRL::ComPtr<ID3D12Resource> native_;
        C::ResourceView view_;
        ReadBorrow(std::shared_ptr<ReadBorrowState> state,Microsoft::WRL::ComPtr<ID3D12Resource>&& native,
            const C::ResourceView& view):state_(std::move(state)),native_(std::move(native)),view_(view){}
        void Release()noexcept
        {if(state_)state_->readers.fetch_sub(1,std::memory_order_acq_rel);state_.reset();native_.Reset();}
      public:
        ReadBorrow(const ReadBorrow&)=delete;
        ReadBorrow& operator=(const ReadBorrow&)=delete;
        ReadBorrow(ReadBorrow&& other)noexcept:
            state_(std::move(other.state_)),native_(std::move(other.native_)),view_(std::move(other.view_)){}
        ReadBorrow& operator=(ReadBorrow&& other)noexcept
        {
            if(this!=&other){Release();state_=std::move(other.state_);native_=std::move(other.native_);view_=std::move(other.view_);}
            return *this;
        }
        ~ReadBorrow(){Release();}
        bool Current()const noexcept{return state_&&state_->current.load(std::memory_order_acquire);}
        ID3D12Resource* Native()const noexcept{return native_.Get();}
        const C::ResourceView& View()const noexcept{return view_;}
    };
  private:
    struct Slot
    {
        std::shared_ptr<ReadBorrowState> reads=std::make_shared<ReadBorrowState>();
        struct Projection {C::Rectangle requested;std::optional<C::ResourceView> view;};
        AllocationOwner owner;
        Microsoft::WRL::ComPtr<ID3D12Resource> native;
        Microsoft::WRL::ComPtr<IUnknown> identity;
        IUnknown* retiredIdentity=nullptr; // refusal-only tombstone; never authorizes pointer reuse
        std::optional<NativeAllocationRecord> record;
        std::array<std::unique_ptr<Projection>,ProjectionsPerAllocation> projections;
        std::size_t projectionCount=0;
    };
    std::array<Slot,Capacity> slots_;
    OwnerPublicationJournal deviceJournal_;
    std::optional<ObjectIssuer> deviceIssuer_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<IUnknown> deviceIdentity_;
    C::OptionalFact<C::ObjectIncarnation> deviceFact_;
    struct QueueObservation
    {
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> native;
        Microsoft::WRL::ComPtr<IUnknown> identity;
        C::OptionalFact<C::ObjectIncarnation> incarnation;
    };
    std::array<QueueObservation,Capacity> queues_;
    mutable std::recursive_mutex mutex_;
    std::size_t used_=0;
    bool active_=false,closed_=false,retired_=false;
    const NativeOwnedContinuation* privateController_=nullptr;
    class FullCopyReservation
    {
        friend class NativeResourceRegistry;
        C::ResourceView source_,before_;
        ID3D12Resource* destination_=nullptr;
        C::RecordKey recording_;
        DlssNr::GpuSafety::Ticket ticket_;
        std::uint64_t ordinal_=0;
        FullCopyReservation()=default;
      public:
        FullCopyReservation(const FullCopyReservation&)=default;
    };
    std::optional<FullCopyReservation> ReservePrivateFullCopy(const NativeOwnedContinuation* controller,
        ID3D12Resource* source,ID3D12Resource* destination,const C::ResourceView& expectedSource,
        const C::ResourceView& expectedDestination,const C::RecordKey& recording,std::uint64_t ordinal,
        const DlssNr::GpuSafety::LocalRecordingAction& action)
    {
        std::lock_guard lock(mutex_);
        if(!controller||privateController_!=controller||!active_||closed_||source==destination||!ordinal||
           recording.Check()!=C::Error::None||!action.CurrentFor(destination)||!Context::CompleteContent(expectedSource))return {};
        const auto s=CurrentRegisteredView(source),d=CurrentRegisteredView(destination);
        if(!s||!d||*s!=expectedSource||*d!=expectedDestination||
           !Context::SameFact(s->descriptor.allocation,d->descriptor.allocation)||
           !Context::SameFact(s->descriptor.format,d->descriptor.format)||
           !Context::SameFact(s->descriptor.sampleCount,d->descriptor.sampleCount)||
           !Context::SameFact(s->device,d->device)||!Context::Established(s->raster.active)||
           !Context::Established(d->raster.active)||s->raster.active.KnownPart()->value!=d->raster.active.KnownPart()->value||
           !Context::Established(s->descriptor.allocation)||
           s->raster.active.KnownPart()->value!=C::Rectangle{0,0,s->descriptor.allocation.KnownPart()->value.width,
                s->descriptor.allocation.KnownPart()->value.height})return {};
        FullCopyReservation r;r.source_=*s;r.before_=*d;r.destination_=destination;r.recording_=recording;
        r.ticket_=action.RecordingTicket();r.ordinal_=ordinal;return r;
    }
    std::optional<FinalInputAncestry> CommitPrivateFullCopy(const NativeOwnedContinuation* controller,
        const FullCopyReservation& reservation,const C::ResourceView& endpoint)
    {
        std::lock_guard lock(mutex_);
        if(!controller||privateController_!=controller||!active_||closed_||!Context::CompleteContent(endpoint)||
           !Context::SameResourceStructure(reservation.before_.identity,endpoint.identity)||
           Context::SameContent(reservation.before_.identity.contentRevision,endpoint.identity.contentRevision))return {};
        const auto current=CurrentRegisteredView(reservation.destination_);
        const auto submitted=DlssNr::GpuSafety::InspectRecording(reservation.ticket_);
        if(!current||*current!=endpoint||!submitted.valid||!submitted.registryHealthy||!submitted.submitted||
           !submitted.uniqueSubmission)return {};
        return FinalInputAncestry(reservation.source_,endpoint,reservation.recording_,reservation.ordinal_);
    }
    struct DetachedPrivateAllocation
    {Microsoft::WRL::ComPtr<ID3D12Resource> native;Microsoft::WRL::ComPtr<IUnknown> identity;};
    std::optional<DetachedPrivateAllocation> DetachPrivateAllocation(const NativeOwnedContinuation* controller,
        ID3D12Resource* exact,const C::ResourceIdentityToken& version,
        const std::array<DlssNr::GpuSafety::Ticket,2>& uses)noexcept
    {
        std::lock_guard lock(mutex_);
        if(!controller||privateController_!=controller||!active_||!exact||!uses[0])return {};
        for(const auto& ticket:uses)if(ticket&&!DlssNr::GpuSafety::InspectTerminalRecording(ticket))return {};
        for(auto& slot:slots_)
            if(slot.native.Get()==exact&&slot.record&&slot.record->IsCurrent(exact)&&slot.record->View()->identity==version)
            {
                // The private controller has closed this allocation's admission
                // and supplied every recorded use. Revoke before detaching COM;
                // caller destroys this bundle after all controller locks end.
                if(slot.reads->readers.load(std::memory_order_acquire))return {};
                slot.reads->current.store(false,std::memory_order_release);
                slot.record->Revoke();slot.retiredIdentity=slot.identity.Get();
                return DetachedPrivateAllocation{std::move(slot.native),std::move(slot.identity)};
            }
        return {};
    }
    // Pins owned publication bookkeeping only: no C03 lease, GPU dependency,
    // provider release or protection from arbitrary host writes is implied.
    // exact must be a live canonical native pointer. No COM call holds mutex_.
    std::optional<ReadBorrow> BorrowCurrentRead(ID3D12Resource* exact,const C::ResourceView& expected)noexcept
    try
    {
        if(!exact||!Context::CompleteContent(expected))return {};
        Microsoft::WRL::ComPtr<ID3D12Resource> retained=exact;
        std::lock_guard lock(mutex_);
        if(!active_||closed_)return {};
        for(auto& slot:slots_)
        {
            if(slot.native.Get()!=exact||!slot.record||!slot.record->IsCurrent(exact)||slot.record->opaqueReserved_||
               !slot.reads->current.load(std::memory_order_acquire))continue;
            bool matches=*slot.record->View()==expected;
            for(std::size_t i=0;!matches&&i<slot.projectionCount;++i)
                matches=slot.projections[i]&&slot.projections[i]->view&&*slot.projections[i]->view==expected;
            if(!matches||slot.reads->readers.load(std::memory_order_acquire)>=ReadBorrowsPerAllocation)return {};
            ReadBorrow result(slot.reads,std::move(retained),expected);
            slot.reads->readers.fetch_add(1,std::memory_order_acq_rel);
            return result;
        }
        return {};
    }
    catch(...){return {};}
    // A recorded or unavoidable original write cannot preserve old freshness.
    // Quarantine retains lifetime; it does not cancel work or infer release.
    static void QuarantineWrite(Slot& slot)noexcept
    {
        slot.reads->current.store(false,std::memory_order_release);
        if(slot.record)
        {
            if(slot.record->view_)slot.record->view_->identity.contentRevision={};
            slot.record->Revoke();
        }
        for(std::size_t i=0;i<slot.projectionCount;++i)
            if(slot.projections[i]&&slot.projections[i]->view)slot.projections[i]->view->identity.contentRevision={};
    }
    NativeResourceRegistry()=default;
    template<class T>static C::OptionalFact<T> Known(const T& value,const OwnerEvent& event)
    {return C::OptionalFact<T>::FromKnown(value,event.evidence);}
    static C::Symbol Symbol(const std::string& text)
    {C::Symbol value;if(!value.Assign(text))throw std::invalid_argument("Native resource descriptor symbol");return value;}
    C::ResourceView Physical(const D3D12_RESOURCE_DESC& d,const LUID& adapter,const OwnerEvent& event)const
    {
        C::ResourceView view;const C::Extent extent{static_cast<std::uint32_t>(d.Width),d.Height};
        view.descriptor.allocation=Known(extent,event);
        view.descriptor.format=Known(Symbol("DXGI.Format."+std::to_string(static_cast<unsigned>(d.Format))),event);
        view.descriptor.sampleCount=Known(d.SampleDesc.Count,event);
        view.descriptor.mipLevels=Known(std::uint32_t{d.MipLevels},event);
        view.descriptor.arrayLayers=Known(std::uint32_t{d.DepthOrArraySize},event);
        view.descriptor.api=Known(C::GraphicsApi::D3D12,event);view.device=deviceFact_;
        view.adapter=Known(Symbol("DXGI.LUID."+std::to_string(static_cast<std::uint32_t>(adapter.HighPart))+"."+
            std::to_string(adapter.LowPart)),event);
        view.mip=Known(std::uint32_t{0},event);view.arrayLayer=Known(std::uint32_t{0},event);view.plane=Known(std::uint32_t{0},event);
        // Physical texel coordinates only; no color, motion or sampling lineage.
        view.raster.allocation=Known(extent,event);view.raster.active=Known(C::Rectangle{0,0,extent.width,extent.height},event);
        view.raster.origin=Known(C::RasterOrigin::TopLeft,event);view.raster.pixelCenter=Known(C::Vec2{.5,.5},event);
        view.raster.axisSigns=Known(C::Vec2{1,1},event);return view;
    }
    void ReleaseRetained()noexcept
    {
        // The containing owner must already have proved scope drain. Close
        // observation first; Release may reenter while all members still live.
        CloseAdmissions();
        for(auto& slot:slots_){slot.native.Reset();slot.identity.Reset();}
        for(auto& queue:queues_){queue.native.Reset();queue.identity.Reset();}
        device_.Reset();deviceIdentity_.Reset();
    }
    struct RetirementObservation
    {
        bool admissionsClosed=false,privateController=false,retired=false;
        std::size_t readers=0,opaqueWrites=0,allocations=0;
        bool Quiescent()const noexcept{return admissionsClosed&&!privateController&&!readers&&!opaqueWrites;}
    };
    RetirementObservation ObserveRetirement()const noexcept
    {
        std::lock_guard lock(mutex_);RetirementObservation result;
        result.admissionsClosed=closed_;result.privateController=privateController_!=nullptr;result.retired=retired_;
        for(const auto& slot:slots_)
        {
            result.readers+=slot.reads->readers.load(std::memory_order_acquire);
            if(slot.record&&slot.record->opaqueReserved_)++result.opaqueWrites;
            if(slot.native)++result.allocations;
        }
        return result;
    }
    // Only the owning bootstrap calls this after the existing kernel has
    // closed the session using matched renderer/source-provider retirement.
    // Keep descriptive records, detach actual COM references outside the lock.
    bool ReleaseAfterScopeDrain()noexcept
    {
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,Capacity> resources;
        std::array<Microsoft::WRL::ComPtr<IUnknown>,Capacity> identities;
        std::array<QueueObservation,Capacity> queues;
        Microsoft::WRL::ComPtr<ID3D12Device> device;Microsoft::WRL::ComPtr<IUnknown> identity;
        {
            std::lock_guard lock(mutex_);if(retired_)return true;
            if(!ObserveRetirement().Quiescent())return false;
            for(std::size_t i=0;i<slots_.size();++i)
            {
                auto& slot=slots_[i];if(slot.record)slot.record->Revoke();
                resources[i]=std::move(slot.native);identities[i]=std::move(slot.identity);
            }
            device=std::move(device_);identity=std::move(deviceIdentity_);active_=false;retired_=true;
            queues=std::move(queues_);
        }
        return true;
    }
  public:
    ~NativeResourceRegistry(){ReleaseRetained();}
    NativeResourceRegistry(const NativeResourceRegistry&)=delete;
    NativeResourceRegistry& operator=(const NativeResourceRegistry&)=delete;
    const C::ResourceView* Observe(IUnknown* borrowed)noexcept
    try
    {
        {std::lock_guard lock(mutex_);if(!active_||closed_)return nullptr;}
        // Every external call and temporary reference precedes the lock. On
        // return, the lock is destroyed before these temporaries release COM.
        auto resource=DlssNr::NativeIdentity::Resolve<ID3D12Resource>(borrowed).object;
        Microsoft::WRL::ComPtr<IUnknown> identity,deviceIdentity;
        Microsoft::WRL::ComPtr<ID3D12Device> reportedDevice;
        if(!resource||FAILED(resource.As(&identity))||FAILED(resource->GetDevice(IID_PPV_ARGS(&reportedDevice))))return nullptr;
        auto device=DlssNr::NativeIdentity::Resolve<ID3D12Device>(reportedDevice.Get()).object;
        if(!device||FAILED(device.As(&deviceIdentity)))return nullptr;
        const auto descriptor=resource->GetDesc();const auto adapter=device->GetAdapterLuid();
        if(descriptor.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||!descriptor.Width||
           descriptor.Width>(std::numeric_limits<std::uint32_t>::max)()||!descriptor.Height||
           descriptor.MipLevels!=1||descriptor.DepthOrArraySize!=1||descriptor.SampleDesc.Count!=1||
           descriptor.Format==DXGI_FORMAT_UNKNOWN)return nullptr;
        D3D12_FEATURE_DATA_FORMAT_INFO format{descriptor.Format,0};
        if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,&format,sizeof(format)))||format.PlaneCount!=1)return nullptr;
        std::lock_guard lock(mutex_);
        if(!active_||closed_)return nullptr;
        if(deviceIdentity_&&deviceIdentity_.Get()!=deviceIdentity.Get())return nullptr;
        for(std::size_t i=0;i<used_;++i)
        {
            if(slots_[i].retiredIdentity==identity.Get())return nullptr;
            if(slots_[i].identity.Get()==identity.Get())return slots_[i].record?slots_[i].record->View():nullptr;
        }
        if(used_==Capacity)return nullptr;
        if(!deviceIdentity_)
        {
            const auto issued=deviceIssuer_->Incarnation(deviceJournal_.Event());
            if(issued.status!=IdentityStatus::Ok){closed_=true;return nullptr;}
            deviceFact_=issued.value;device_=std::move(device);deviceIdentity_=std::move(deviceIdentity);
        }
        // Reserve before any allocation identity issue. A partial failure keeps
        // this physical lifetime/slot and cannot retry it or recycle its issuer.
        auto& slot=slots_[used_++];slot.native=std::move(resource);slot.identity=std::move(identity);
        slot.record.emplace(*slot.owner.resource,*slot.owner.representation,slot.owner.journal.Binding());
        const auto event=slot.owner.journal.Event();
        if(!slot.record->Registered(slot.native.Get(),Physical(descriptor,adapter,event),event))return nullptr;
        return slot.record->View();
    }
    catch(...){return nullptr;}
    const C::ResourceView* ProjectRectangle(const C::ResourceView* registered,const C::Rectangle& rect)noexcept
    try
    {
        std::lock_guard lock(mutex_);
        if(!active_||closed_||!registered||!rect.width||!rect.height)return nullptr;
        for(std::size_t i=0;i<used_;++i)
        {
            auto& slot=slots_[i];
            if(!slot.record||slot.record->View()!=registered)continue;
            if(!slot.record->IsCurrent(slot.native.Get()))return nullptr;
            const auto size=registered->descriptor.allocation.KnownPart()->value;
            if(rect.x>size.width||rect.y>size.height||rect.width>size.width-rect.x||rect.height>size.height-rect.y)return nullptr;
            if(rect==registered->raster.active.KnownPart()->value)return registered;
            for(std::size_t j=0;j<slot.projectionCount;++j)if(slot.projections[j]->requested==rect)
                return slot.projections[j]->view?&*slot.projections[j]->view:nullptr;
            if(slot.projectionCount==ProjectionsPerAllocation)return nullptr;
            auto projection=std::make_unique<Slot::Projection>();projection->requested=rect;
            auto& retained=slot.projections[slot.projectionCount++];retained=std::move(projection);
            retained->view=slot.record->ProjectRectangle(rect,slot.owner.journal.Event());
            return retained->view?&*retained->view:nullptr;
        }
        return nullptr;
    }
    catch(...){return nullptr;}
    std::size_t Used()const noexcept{std::lock_guard lock(mutex_);return used_;}
    struct NativeLeasePublication
    {C::RecordHeader header;C::ResourceView view;};
    // Exact retained queue identity only. Consumption permission still comes
    // from the live SDK allocation/action owner, never this observation.
    C::OptionalFact<C::ObjectIncarnation> ObserveQueue(ID3D12CommandQueue* exact)noexcept
    try
    {
        if(!exact)return {};
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> native=exact;
        Microsoft::WRL::ComPtr<IUnknown> identity,deviceIdentity;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if(FAILED(native.As(&identity))||FAILED(native->GetDevice(IID_PPV_ARGS(&device)))||
           FAILED(device.As(&deviceIdentity)))return {};
        const auto description=native->GetDesc();
        if(description.Type!=D3D12_COMMAND_LIST_TYPE_DIRECT&&description.Type!=D3D12_COMMAND_LIST_TYPE_COMPUTE)return {};
        std::lock_guard lock(mutex_);
        if(!active_||closed_||!deviceIssuer_||deviceIdentity!=deviceIdentity_)return {};
        for(const auto& queue:queues_)if(queue.identity==identity)return queue.incarnation;
        for(auto& queue:queues_)if(!queue.identity)
        {
            auto issued=deviceIssuer_->Incarnation(deviceJournal_.Event());if(!issued.value.IsKnown())return {};
            queue={std::move(native),std::move(identity),issued.value};return queue.incarnation;
        }
        return {};
    }
    catch(...){return {};}
    // The action owner holds this lock through its admitted CPU operation.
    // This does not itself grant C03 permission or recording rights.
    std::unique_lock<std::recursive_mutex> LockNativeAction()
    {return std::unique_lock<std::recursive_mutex>(mutex_);}
    std::optional<NativeLeasePublication> PublishNativeLease(ID3D12Resource* exact,const C::ScopeRef& scope)noexcept
    try
    {
        std::lock_guard lock(mutex_);
        if(!active_||closed_||!exact||!scope.key)return {};
        for(std::size_t i=0;i<used_;++i)
        {
            auto& slot=slots_[i];
            if(slot.native.Get()!=exact||!slot.record||!slot.record->IsCurrent(exact))continue;
            return NativeLeasePublication{slot.owner.journal.Header(C::ContractId::C03,scope),*slot.record->View()};
        }
        return {};
    }
    catch(...){return {};}
  private:
    bool MarkWritten(ID3D12Resource* native)noexcept
    {
        std::lock_guard lock(mutex_);
        if(!active_||closed_)return false;
        for(std::size_t i=0;i<used_;++i)
        {
            auto& slot=slots_[i];
            if(slot.native.Get()!=native||!slot.record||!slot.record->IsCurrent(native))continue;
            if(slot.reads->readers.load(std::memory_order_acquire)){QuarantineWrite(slot);return false;}
            if(!slot.record->Written(native,slot.owner.journal.Event()))return false;
            const auto revision=slot.record->View()->identity.contentRevision;
            for(std::size_t j=0;j<slot.projectionCount;++j)
                if(slot.projections[j]&&slot.projections[j]->view)
                    slot.projections[j]->view->identity.contentRevision=revision;
            return true;
        }
        return false;
    }
  public:
    struct OpaqueWriteReservation
    {
        ID3D12Resource* native=nullptr;
        ID3D12GraphicsCommandList* list=nullptr;
        DlssNr::GpuSafety::Ticket recording;
        C::ResourceIdentityToken before;
        C::OptionalFact<C::ContentRevision> revision;
    };
    // CPU-only snapshot of an already enrolled canonical allocation. Used in
    // the return tail where COM discovery while holding source locks is forbidden.
    std::optional<C::ResourceView> CurrentRegisteredView(ID3D12Resource* native)const
    {
        std::lock_guard lock(mutex_);
        if(!active_||closed_||!native)return {};
        for(std::size_t i=0;i<used_;++i)
            if(slots_[i].native.Get()==native&&slots_[i].record&&slots_[i].record->IsCurrent(native))
                return *slots_[i].record->View();
        return {};
    }
  private:
    template<class Permission>std::optional<OpaqueWriteReservation> ReserveWrite(ID3D12Resource* borrowed,
        ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::LocalRecordingAction& action,
        Permission&& permitted,bool unavoidableWrite=false)noexcept
    try
    {
        if(!borrowed||!list||!permitted(borrowed,list)||
           !DlssNr::GpuSafety::MatchesLocalRecording(action.RecordingTicket(),list,borrowed))return {};
        auto native=DlssNr::NativeIdentity::Resolve<ID3D12Resource>(borrowed).object;
        auto nativeList=DlssNr::NativeIdentity::Resolve<ID3D12GraphicsCommandList>(list).object;
        if(!native||!nativeList||!permitted(native.Get(),nativeList.Get()))return {};
        std::lock_guard lock(mutex_);
        if(!active_||closed_)return {};
        for(std::size_t i=0;i<used_;++i)
        {
            auto& slot=slots_[i];
            if(slot.native.Get()!=native.Get()||!slot.record||!slot.record->IsCurrent(native.Get()))continue;
            if(slot.reads->readers.load(std::memory_order_acquire))
            {if(unavoidableWrite)QuarantineWrite(slot);return {};}
            const auto before=slot.record->View()->identity;
            const auto revision=slot.record->ReserveOpaqueWrite(native.Get(),slot.owner.journal.Event());
            if(!revision)return {};
            return OpaqueWriteReservation{native.Get(),nativeList.Get(),action.RecordingTicket(),before,*revision};
        }
        return {};
    }
    catch(...){return {};}
  public:
    std::optional<OpaqueWriteReservation> ReserveOpaqueWrite(ID3D12Resource* borrowed,
        ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::LocalRecordingAction& action)noexcept
    {
        return ReserveWrite(borrowed,list,action,[&](ID3D12Resource* exact,ID3D12GraphicsCommandList*){
            return action.CurrentFor(exact);});
    }
    std::optional<OpaqueWriteReservation> ReserveRendererOutput(ID3D12Resource* borrowed,
        ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::LocalRecordingAction& action,
        const DlssNr::NativeRendererWriteToken& internal)noexcept
    {
        return ReserveWrite(borrowed,list,action,[&](ID3D12Resource* exact,ID3D12GraphicsCommandList* recording){
            return internal.CurrentFor(exact,recording);});
    }
    // The existing Resource owner consumes a capability from the actual renderer
    // pass. This does not broaden the internal-allocation write token.
    bool CommitRendererOutput(const OpaqueWriteReservation& reservation,
        const DlssNr::NativeRendererInvocationBorrow& borrow,
        const DlssNr::NativeOutputPassToken& pass,const DlssNr::NativeRendererWriteToken* internal=nullptr)noexcept
    try
    {
        if(!pass.Matches(borrow,reservation.list,reservation.native)||
           !(internal?internal->CurrentFor(reservation.native,reservation.list):borrow.RecordingAction().CurrentFor(reservation.native))||
           borrow.RecordingAction().RecordingTicket()!=reservation.recording)return false;
        std::lock_guard lock(mutex_);
        if(!active_||closed_)return false;
        for(std::size_t i=0;i<used_;++i)
        {
            auto& slot=slots_[i];
            if(slot.native.Get()!=reservation.native||!slot.record)continue;
            if(!slot.record->CommitOpaqueWrite(reservation.native,reservation.before,reservation.revision))return false;
            for(std::size_t j=0;j<slot.projectionCount;++j)
                if(slot.projections[j]&&slot.projections[j]->view)
                    slot.projections[j]->view->identity.contentRevision=reservation.revision;
            return true;
        }
        return false;
    }
    catch(...){return false;}
  private:
    // The selected public SR invocation authorizes its captured Output. Reuse
    // a held recording action even when NR originally borrowed Color; this
    // private port grants no general-purpose write token for host resources.
    void InvalidateSelectedSrWrite(const OpaqueWriteReservation& reservation)noexcept
    {
        std::lock_guard lock(mutex_);
        for(auto& slot:slots_)
            if(slot.native.Get()==reservation.native&&slot.record&&
               slot.record->InvalidateOpaqueWrite(reservation.native,reservation.before,reservation.revision))
            {
                for(std::size_t j=0;j<slot.projectionCount;++j)
                    if(slot.projections[j]&&slot.projections[j]->view)
                        slot.projections[j]->view->identity.contentRevision={};
                return;
            }
    }
    std::optional<OpaqueWriteReservation> ReserveSelectedSrWrite(ID3D12Resource* output,
        ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::LocalRecordingAction& action)noexcept
    {
        // The original selected SR call still executes after capture refusal.
        // Check/quarantine under the same lock as pin and write reservation.
        return ReserveWrite(output,list,action,[&](ID3D12Resource*,ID3D12GraphicsCommandList*){return action.Current();},true);
    }
    bool CommitSelectedSrWrite(const OpaqueWriteReservation& reservation,ID3D12Resource* output,
        ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::LocalRecordingAction& action,
        const OpaqueSrEvaluationReceipt& receipt)noexcept
    {return CommitOpaqueWriteImpl(reservation,output,list,action,receipt,action.Current());}
    bool CommitOpaqueWriteImpl(const OpaqueWriteReservation& reservation,ID3D12Resource* borrowed,
        ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::LocalRecordingAction& action,
        const OpaqueSrEvaluationReceipt& receipt,bool permitted)noexcept
    try
    {
        if(!receipt.PublishedRevision()||
           receipt.Classification()!=OpaqueSrEvaluationReceipt::Class::RecordedOpaqueWrite||
           receipt.ReservedRevision()!=reservation.revision||!borrowed||!list||
           !permitted||action.RecordingTicket()!=reservation.recording||
           !DlssNr::GpuSafety::MatchesLocalRecording(action.RecordingTicket(),list,borrowed))return false;
        auto native=DlssNr::NativeIdentity::Resolve<ID3D12Resource>(borrowed).object;
        auto nativeList=DlssNr::NativeIdentity::Resolve<ID3D12GraphicsCommandList>(list).object;
        if(!native||!nativeList||native.Get()!=reservation.native||nativeList.Get()!=reservation.list||
           receipt.Original().nativeList!=reservation.list||
           receipt.Original().output.native!=reservation.native||
           receipt.Original().output.identity!=reservation.before)return false;
        std::lock_guard lock(mutex_);
        if(!active_||closed_)return false;
        for(std::size_t i=0;i<used_;++i)
        {
            auto& slot=slots_[i];
            if(slot.native.Get()!=native.Get()||!slot.record||!slot.record->IsCurrent(native.Get()))continue;
            if(!slot.record->CommitOpaqueWrite(native.Get(),reservation.before,reservation.revision))return false;
            const auto revision=slot.record->View()->identity.contentRevision;
            for(std::size_t j=0;j<slot.projectionCount;++j)
                if(slot.projections[j]&&slot.projections[j]->view)
                    slot.projections[j]->view->identity.contentRevision=revision;
            return true;
        }
        return false;
    }
    catch(...){return false;}
  public:
    bool CommitOpaqueWrite(const OpaqueWriteReservation& reservation,ID3D12Resource* borrowed,
        ID3D12GraphicsCommandList* list,const DlssNr::GpuSafety::LocalRecordingAction& action,
        const OpaqueSrEvaluationReceipt& receipt)noexcept
    {return CommitOpaqueWriteImpl(reservation,borrowed,list,action,receipt,action.CurrentFor(borrowed));}
    std::optional<C::OwnerValueReference> Ownership(const C::ResourceView* view)noexcept
    try
    {
        std::lock_guard lock(mutex_);if(!active_||closed_||!view)return {};
        for(auto& slot:slots_)if(slot.record&&slot.record->View()==view&&slot.record->IsCurrent(slot.native.Get()))
        {
            C::Symbol type;type.Assign("Native.RetainedAllocation");
            return C::OwnerValueReference{C::OwnerDomain::Resource,type,{1,0},slot.owner.journal.Event().evidence.record,1};
        }
        return {};
    }
    catch(...){return {};}
    // Called by the existing pass owner only after its actual command has been
    // recorded. The recording borrow excludes Close/Reset while the Resource
    // owner issues the new content revision; observation alone never does so.
    bool RecordWritten(ID3D12Resource* borrowed,ID3D12GraphicsCommandList* list,
        const DlssNr::GpuSafety::LocalRecordingAction& action)noexcept
    try
    {
        if(!borrowed||!list||!action.CurrentFor(borrowed)||
           !DlssNr::GpuSafety::MatchesLocalRecording(action.RecordingTicket(),list,borrowed))return false;
        auto native=DlssNr::NativeIdentity::Resolve<ID3D12Resource>(borrowed).object;
        if(!native||!action.CurrentFor(native.Get()))return false;
        return MarkWritten(native.Get());
    }
    catch(...){return false;}
    bool RecordRendererWritten(ID3D12Resource* borrowed,ID3D12GraphicsCommandList* list,
        const DlssNr::NativeRendererWriteToken& token)noexcept
    try
    {
        if(!borrowed||!list||!token.CurrentFor(borrowed,list))return false;
        return MarkWritten(borrowed);
    }
    catch(...){return false;}
    void CloseAdmissions()noexcept
    {
        std::lock_guard lock(mutex_);closed_=true;
        for(auto& slot:slots_)slot.reads->current.store(false,std::memory_order_release);
    }
    // No recycle API: later real recording/provider retirement must establish
    // release separately. This destructor is only valid after owner-scope drain.
};
}
