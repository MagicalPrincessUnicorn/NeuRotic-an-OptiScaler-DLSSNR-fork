#pragma once
#include "NativeOwnerSet.h"
#include "ConsumptionLeaseAdapter.h"
#include <array>
#include <cstring>
#include <span>

namespace Neurotic::Lifecycle
{
// Controlled, bounded two-queue host for the supported full-copy operation.
// Owns every list/queue/allocation it operates; no borrowed game list is accepted.
// Canonical ResourceRegistry/C03/GpuSafety remain the identity, permission and
// physical-terminal authorities. These edges grant no Native lineage or seal.
class NativeOwnedContinuation
{
#ifdef NR_GPU_SAFETY_TEST
    friend class NativeOwnedContinuationTestAccess;
#endif
    using GpuTicket=DlssNr::GpuSafety::Ticket;
    template<class T>using ComPtr=Microsoft::WRL::ComPtr<T>;
  public:
    static constexpr std::size_t MaxEdges=4;
    static constexpr std::size_t MaxHosts=8;
    enum class Operation { FullCopy, Identity, Unknown };
    enum class Result { Refused, Identity, Recorded, PossibleEffect };
    struct CopyRequest
    {
        std::size_t source=0,destination=0;
        C::ResourceIdentityToken sourceVersion,destinationVersion;
        C::Rectangle rectangle;
        Operation operation=Operation::FullCopy;
    };
    struct Edge
    {
        C::ResourceIdentityToken source,destination,destinationBefore;
        C::Rectangle rectangle;
        C::RecordKey recording;
        GpuTicket recordingTicket,producerDependency;
        std::uint32_t sourceSubresource=0,destinationSubresource=0;
        std::uint64_t ordinal=0;
        Operation operation=Operation::FullCopy;
        Result result=Result::PossibleEffect;
    };
    // A proof of this private host's actual recorded pixel-copy path only.
    // It contains no Native C07 association, final boundary or C13 authority.
    class RecordedContentPath
    {
        friend class NativeOwnedContinuation;
        std::weak_ptr<NativeOwnerSet> owner_;
        C::ResourceIdentityToken source_,destination_;
        std::array<std::uint64_t,MaxEdges> ordinals_{};
        std::size_t destinationIndex_=0,count_=0;
        RecordedContentPath()=default;
      public:
        RecordedContentPath(const RecordedContentPath&)=default;
        RecordedContentPath& operator=(const RecordedContentPath&)=default;
        const C::ResourceIdentityToken& Source()const noexcept{return source_;}
        const C::ResourceIdentityToken& Destination()const noexcept{return destination_;}
        std::size_t StepCount()const noexcept{return count_;}
    };
  private:
    struct RecordingObjects
    {
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Fence> fence;
        HANDLE completionEvent=nullptr;
        C::RecordKey recording;
        C::OptionalFact<C::ObjectIncarnation> queueIdentity;
        GpuTicket ticket;
        bool submitted=false,closed=false;
        ~RecordingObjects(){if(completionEvent)CloseHandle(completionEvent);}
        void Swap(RecordingObjects& other)noexcept
        {
            queue.Swap(other.queue);allocator.Swap(other.allocator);list.Swap(other.list);fence.Swap(other.fence);
            std::swap(completionEvent,other.completionEvent);std::swap(recording,other.recording);
            std::swap(queueIdentity,other.queueIdentity);ticket.swap(other.ticket);
            std::swap(submitted,other.submitted);std::swap(closed,other.closed);
        }
        void Release()noexcept {list.Reset();allocator.Reset();queue.Reset();fence.Reset();}
    };
    struct State:RecordingObjects
    {
        std::shared_ptr<NativeOwnerSet> owners;
        std::unique_ptr<OwnerMetadataArena> metadata;
        ComPtr<ID3D12Device> device;
        RecordingObjects spare,producer;
        std::array<ComPtr<ID3D12Resource>,MaxEdges+1> textures;
        ComPtr<ID3D12Resource> upload,readback;
        std::array<C::ResourceView,MaxEdges+1> views;
        std::array<C::ContractRef<C::ContractId::C01>,MaxEdges+1> candidates;
        C::CanonicalFrameContext context;
        C::ScopeRef scope;
        C::RecordKey reservation,consumer;
        std::array<GpuTicket,MaxEdges+1> writers;
        std::array<std::array<GpuTicket,2>,MaxEdges+1> uses;
        std::array<bool,MaxEdges+1> admissionClosed{};
        std::optional<C::ResourceIdentityToken> uploadRoot;
        GpuTicket uploadTicket;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows=0;
        UINT64 rowBytes=0,totalBytes=0;
        std::array<Edge,MaxEdges> edges;
        std::array<std::optional<NativeResourceRegistry::FullCopyReservation>,MaxEdges> copyReservations;
        std::size_t capacity=0,count=0;
        UINT width=0,height=0;
        bool uploaded=false,readbackRecorded=false,failed=false,retired=false;
    };
    static bool Terminal(const State& state)
    {
        for(const auto* object:{static_cast<const RecordingObjects*>(&state),&state.producer,&state.spare})
            if(object->ticket&&!DlssNr::GpuSafety::InspectTerminalRecording(object->ticket))return false;
        return true;
    }
    static bool UniqueTerminal(const State& state)
    {
        for(const auto* object:{static_cast<const RecordingObjects*>(&state),&state.producer,&state.spare})
            if(object->ticket)
            {
                const auto observed=DlssNr::GpuSafety::InspectTerminalRecording(object->ticket);
                if(!observed||observed->submissions!=(object==&state.spare?0:1))return false;
            }
        return true;
    }
    static bool Cancel(RecordingObjects& object)
    {
        if(!object.ticket)return true;
        if(object.submitted)return false;
        if(!object.closed&&FAILED(object.list->Close()))return false;object.closed=true;
        if(FAILED(object.list->Reset(object.allocator.Get(),nullptr)))return false;
        return SUCCEEDED(object.list->Close())&&bool(DlssNr::GpuSafety::InspectTerminalRecording(object.ticket));
    }
    struct Retention
    {
        struct Slot {bool reserved=false;std::unique_ptr<State> pending;};
        std::mutex mutex;
        std::array<Slot,MaxHosts> slots;
    };
    static Retention& Retained()
    {
        // Process-lifetime storage has a fixed upper bound. This table owns
        // abandoned controlled hosts only; canonical owners still issue every
        // resource identity, use permission and terminal recording observation.
        static auto* value=new Retention;return *value;
    }
    static void ReleaseSlot(std::size_t& index)noexcept
    {
        if(index==MaxHosts)return;
        auto& retained=Retained();std::lock_guard lock(retained.mutex);
        retained.slots[index].reserved=false;index=MaxHosts;
    }
    struct SlotReservation
    {
        std::size_t index=MaxHosts;
        ~SlotReservation(){ReleaseSlot(index);}
    };
    std::unique_ptr<State> state_;
    std::size_t reservation_=MaxHosts;
    mutable std::mutex mutex_;
    explicit NativeOwnedContinuation(std::unique_ptr<State> state,std::size_t reservation):
        state_(std::move(state)),reservation_(reservation){}
    void DisarmPrivateController()noexcept
    {
        if(!state_||!state_->owners)return;
        auto& registry=state_->owners->Resources();std::lock_guard lock(registry.mutex_);
        if(registry.privateController_==this)registry.privateController_=nullptr;
        // Null only disables private detach. It cannot release a resource or
        // bypass the bounded retained State's all-ticket terminal requirement.
    }
    static void Api(HRESULT value)
    {if(FAILED(value))throw std::runtime_error("controlled continuation D3D12 operation failed");}
    static C::Symbol Symbol(std::string_view text)
    {C::Symbol result;if(!result.Assign(text))throw std::invalid_argument("controlled continuation symbol");return result;}
    template<class T>static C::OptionalFact<T> Known(T value,const C::RecordKey& event)
    {return C::OptionalFact<T>::FromKnown(value,C::EvidenceRef{event});}
    static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={resource,0,before,after};list->ResourceBarrier(1,&barrier);
    }
    bool Open()const
    {
        ID3D12Resource* anchor=nullptr;
        if(state_)for(const auto& resource:state_->textures)if(resource){anchor=resource.Get();break;}
        return state_&&!state_->retired&&!state_->failed&&!state_->closed&&!state_->submitted&&state_->owners&&
            DlssNr::GpuSafety::MatchesLocalRecording(state_->ticket,state_->list.Get(),anchor);
    }
    bool Fresh(std::size_t index,const C::ResourceIdentityToken& version)const
    {
        if(index>state_->capacity||state_->admissionClosed[index]||!state_->textures[index])return false;
        const auto* view=state_->owners->Resources().Observe(state_->textures[index].Get());
        return view&&view->identity==version&&state_->views[index].identity==version;
    }
    bool TrackUse(std::size_t index)
    {
        auto& s=*state_;if(index>s.capacity||s.admissionClosed[index])return false;
        for(auto& use:s.uses[index])if(use==s.ticket)return true;
        for(auto& use:s.uses[index])if(!use){use=s.ticket;return true;}
        return false;
    }
    bool RecordedTicket(const GpuTicket& ticket)const
    {
        const auto observed=DlssNr::GpuSafety::InspectRecording(ticket);
        if(!observed.valid||!observed.registryHealthy)return false;
        if(observed.submitted)
        {
            if(!observed.uniqueSubmission||!observed.nonReplayable)return false;
            const auto terminal=DlssNr::GpuSafety::InspectTerminalRecording(ticket);
            return terminal&&(terminal->state==DlssNr::GpuSafety::RecordingTerminalState::OwnerSeal||
                terminal->state==DlssNr::GpuSafety::RecordingTerminalState::Reset||
                terminal->state==DlssNr::GpuSafety::RecordingTerminalState::LifetimeEnded);
        }
        if(observed.nonReplayable)return false;
        ID3D12Resource* anchor=nullptr;for(const auto& resource:state_->textures)if(resource){anchor=resource.Get();break;}
        const auto* object=ticket==state_->ticket?static_cast<const RecordingObjects*>(state_.get()):
            ticket==state_->producer.ticket?&state_->producer:nullptr;
        return object&&DlssNr::GpuSafety::MatchesLocalRecording(ticket,object->list.Get(),anchor);
    }
    std::optional<RecordedContentPath> PathLocked(std::size_t index,const C::ResourceIdentityToken& expected)const
    {
        if(!state_||state_->retired||state_->failed||!state_->owners||!state_->uploadRoot||
           !Fresh(index,expected)||!RecordedTicket(state_->uploadTicket))return {};
        const auto& s=*state_;RecordedContentPath proof;proof.owner_=s.owners;
        proof.source_=*s.uploadRoot;proof.destination_=expected;proof.destinationIndex_=index;
        auto current=expected;std::uint64_t limit=s.count+1;
        while(current!=proof.source_)
        {
            if(proof.count_==MaxEdges)return {};
            const Edge* found=nullptr;
            for(std::size_t i=0;i<s.count;++i)if(s.edges[i].destination==current)
            {if(found)return {};found=&s.edges[i];}
            if(!found||found->result!=Result::Recorded||found->operation!=Operation::FullCopy||
               !found->ordinal||found->ordinal>=limit||!RecordedTicket(found->recordingTicket))return {};
            proof.ordinals_[proof.count_++]=found->ordinal;limit=found->ordinal;current=found->source;
        }
        return proof;
    }
    // Called while both the controlled host and canonical Resource action lock
    // are held. These allocations were created and remain private to this host;
    // no provider, descriptor, foreign registration or competing use exists.
    std::optional<LiveConsumptionLease> Lease(std::size_t index,bool write,
        const DlssNr::GpuSafety::LocalRecordingAction& action)
    {
        auto& s=*state_;auto& registry=s.owners->Resources();
        const auto publication=registry.PublishNativeLease(s.textures[index].Get(),s.scope);
        if(!publication||!action.Current()||action.RecordingTicket()!=s.ticket||
           !DlssNr::GpuSafety::MatchesLocalRecording(s.ticket,s.list.Get(),s.textures[index].Get()))return {};
        const auto observed=DlssNr::GpuSafety::InspectRecording(s.ticket);
        if(!observed.valid||!observed.registryHealthy||observed.submitted||observed.nonReplayable)return {};
        LeaseBinding binding;auto& d=binding.description;
        d.header=publication->header;d.context={Symbol(C::CanonicalFrameContext::WireName),s.context.header.record,1};
        d.candidate=s.candidates[index];d.resource=publication->view.identity;
        d.use=write?C::UsageKind::WriteExclusive:C::UsageKind::OrderedGpuRead;
        d.commandScope=s.scope;d.device=publication->view.device;d.generations=s.context.generations;
        d.revoked=Known(false,d.header.record);d.revocationEpoch=Known(s.reservation,d.header.record);
        d.expirationBoundary=Known(s.recording,d.header.record);d.submissionRecheckRequired=Known(true,d.header.record);
        binding.consumer=s.consumer;binding.purpose=Symbol(write?"Controlled.CopyDestination":"Controlled.CopySource");
        binding.callbackScoped=true;binding.contentBasis=write?ContentBasis::WriteDestination:ContentBasis::PhysicalRevision;
        OwnerFacts facts;
        facts.permission=facts.callbackActive=facts.registryHealthy=facts.nativeBindingMatches=facts.apiCompatible=Fact::Yes;
        facts.recordingTracked=facts.resourcesRegistered=facts.descriptorsProtected=facts.externalRegistrationsComplete=Fact::Yes;
        if(s.writers[index]&&s.writers[index]!=s.ticket)
        {
            // Only a read of our retained, uniquely submitted producer is
            // supported across queues. Its allocation is never a fresh write.
            if(write||s.writers[index]!=s.producer.ticket)return {};
            const auto dependency=DlssNr::GpuSafety::InspectRecording(s.producer.ticket,s.queue.Get(),true);
            if(!dependency.valid||!dependency.registryHealthy)return {};
            const auto fact=[](bool value){return value?Fact::Yes:Fact::No;};
            d.queue=s.queueIdentity;
            facts.dependency.ordered=fact(dependency.orderedForConsumer);
            facts.dependency.committed=fact(dependency.submitted);
            facts.dependency.uniqueSubmission=fact(dependency.uniqueSubmission);
            facts.dependency.completed=fact(dependency.completed);
            facts.dependency.nonReplayable=fact(dependency.nonReplayable);
            facts.dependency.reusable=fact(dependency.reusable);
            facts.dependency.sameDevice=fact(dependency.sameDevice);
            facts.dependency.queueKnown=fact(dependency.queueKnown);
            facts.dependency.supportedType=fact(dependency.supportedType);
            facts.dependency.crossQueue=!dependency.sameQueue;
            ExternalHold hold;hold.registration=s.producer.recording;hold.consumer=s.consumer;
            hold.target=d.resource;hold.released=Fact::No;
            if(!facts.holds.Push(hold))return {};
        }
        else
        {
            binding.sameRecording=SameRecordingUse{s.recording,s.reservation,1,2};
            facts.dependency.ordered=facts.dependency.sameDevice=facts.dependency.supportedType=Fact::Yes;
            facts.dependency.committed=facts.dependency.uniqueSubmission=facts.dependency.completed=
                facts.dependency.nonReplayable=facts.dependency.reusable=Fact::No;
            facts.sameRecording=SameRecordingFacts{*binding.sameRecording,Fact::Yes,Fact::Yes,Fact::Yes,Fact::Yes};
        }
        facts.current=binding;
        auto lease=BeginConsumption(binding,facts);
        if(!lease||!ValidateForRecording(*lease,facts).allowed)return {};
        return lease;
    }
    bool Written(std::size_t index,const DlssNr::GpuSafety::LocalRecordingAction& action)
    {
        auto& s=*state_;
        if(!s.owners->Resources().RecordWritten(s.textures[index].Get(),s.list.Get(),action))return false;
        const auto* view=s.owners->Resources().Observe(s.textures[index].Get());
        if(!view)return false;s.views[index]=*view;s.writers[index]=s.ticket;return true;
    }
  public:
    static std::size_t CollectCompleted()noexcept
    try
    {
        std::array<std::unique_ptr<State>,MaxHosts> completed;
        std::size_t count=0;
        {
            auto& retained=Retained();std::lock_guard lock(retained.mutex);
            for(auto& slot:retained.slots)
                if(slot.pending&&Terminal(*slot.pending))
                {completed[count++]=std::move(slot.pending);slot.reserved=false;}
        }
        // Release COM and canonical resource roots after dropping the table lock.
        return count;
    }
    catch(...){return 0;}
    // Transfer the complete owner set exclusively. Its publication journals are
    // owner-serialized, so independent hosts must never share this mutable root.
    // Shared retention is private to State and conveys no external write access.
    // Pass a named owner when refusal must preserve its existing lifetimes.
    // Preconditions are checked before transfer; refusal leaves that input intact.
    static std::unique_ptr<NativeOwnedContinuation> Create(ID3D12Device* device,
        std::unique_ptr<NativeOwnerSet>&& owners,UINT width,UINT height,std::size_t edgeCapacity=MaxEdges,
        const char** reason=nullptr)noexcept
    try
    {
        if(reason)*reason="invalid controlled host request";
        if(!device||!owners||!owners->Ready()||owners->Resources().Used()!=0||!width||!height||width>4096||height>4096||
           !edgeCapacity||edgeCapacity>MaxEdges)return {};
        const auto drain=owners->Finalizer()->DrainStatus();
        if(!drain||drain->unsealed||drain->sealed||drain->providerHeld||drain->retentionUnknown)return {};
        CollectCompleted();SlotReservation reservation;
        {
            auto& retained=Retained();std::lock_guard lock(retained.mutex);
            for(std::size_t i=0;i<MaxHosts;++i)if(!retained.slots[i].reserved)
            {retained.slots[i].reserved=true;reservation.index=i;break;}
        }
        if(reason)*reason="controlled retained host capacity";
        if(reservation.index==MaxHosts)return {};
        auto s=std::make_unique<State>();s->owners=std::move(owners);s->device=device;
        s->width=width;s->height=height;s->capacity=edgeCapacity;
        if(reason)*reason="controlled context metadata publication";
        const auto metadataBytes=(sizeof(C::AcquisitionCandidate)+sizeof(C::ResourceView))*(edgeCapacity+1)+
            sizeof(C::CanonicalFrameContext)+sizeof(C::FrameIdentity)+sizeof(C::EvidenceVector)+sizeof(C::MaskSet)+
            sizeof(C::GenerationVector)+sizeof(C::RasterDescription)+512;
        s->metadata=s->owners->NewInvocationMetadata(metadataBytes,32);
        auto& journal=s->owners->SourceJournal();s->scope={journal.Event().evidence.record};
        s->recording=journal.Event().evidence.record;s->spare.recording=journal.Event().evidence.record;
        s->reservation=journal.Event().evidence.record;
        s->consumer=journal.Event().evidence.record;
        s->context.header=s->owners->ContextJournal().Header(C::ContractId::C02,s->scope);
        auto publisher=s->metadata->ForOwner(C::OwnerDomain::Context);
        s->context.generations=publisher.Publish(C::GenerationVector{});
        s->context.frame=publisher.Publish(C::FrameIdentity{});
        s->context.continuity=publisher.Publish(C::EvidenceVector{});
        s->context.masks=publisher.Publish(C::MaskSet{});
        if(reason)*reason="controlled device objects";
        D3D12_COMMAND_QUEUE_DESC queue{};queue.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        for(auto* object:{static_cast<RecordingObjects*>(s.get()),&s->spare})
        {
            Api(device->CreateCommandQueue(&queue,IID_PPV_ARGS(&object->queue)));
            const auto identity=s->owners->Objects()->Incarnation(s->owners->IdentityJournal().Event());
            if(identity.status!=IdentityStatus::Ok)return {};object->queueIdentity=identity.value;
            Api(device->CreateCommandAllocator(queue.Type,IID_PPV_ARGS(&object->allocator)));
            Api(device->CreateCommandList(0,queue.Type,object->allocator.Get(),nullptr,IID_PPV_ARGS(&object->list)));
            Api(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&object->fence)));
            object->completionEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!object->completionEvent)return {};
        }
        // An unused spare is closed, unrecorded and contributes no fake pending
        // GPU ticket. All its storage is nevertheless reserved before work.
        Api(s->spare.list->Close());s->spare.closed=true;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=width;desc.Height=height;desc.DepthOrArraySize=desc.MipLevels=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        for(std::size_t i=0;i<=edgeCapacity;++i)
        {
            if(reason)*reason="controlled texture registration";
            // Separately committed allocations exclude placed/tiled aliases.
            Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,
                nullptr,IID_PPV_ARGS(&s->textures[i])));
            const auto* view=s->owners->Resources().Observe(s->textures[i].Get());if(!view)return {};
            s->views[i]=*view;C::AcquisitionCandidate candidate;
            if(reason)*reason="controlled candidate metadata publication";
            candidate.header=journal.Header(C::ContractId::C01,s->scope);candidate.provider=journal.Binding().subject;
            candidate.sourceSchema=Symbol("Controlled.RGBA8.FullCopy.v1");candidate.observationRevision=1;
            candidate.frame=s->context.frame;candidate.evidence=s->context.continuity;candidate.masks=s->context.masks;
            candidate.payload=Known(C::CandidateValue{s->metadata->ForOwner(C::OwnerDomain::Resource).Publish(*view)},candidate.header.record);
            if(!Context::ValidValues(candidate))return {};
            if(reason)*reason="controlled candidate metadata capacity";
            s->metadata->ForOwner(C::OwnerDomain::Provider).Publish(candidate);
            s->candidates[i]={Symbol(C::AcquisitionCandidate::WireName),candidate.header.record,1};
        }
        if(reason)*reason="controlled context body publication";
        s->context.renderRaster=publisher.Publish(s->views[0].raster);
        s->context.outputRaster=s->context.renderRaster;publisher.Publish(s->context);
        if(reason)*reason="controlled upload and readback allocation";
        device->GetCopyableFootprints(&desc,0,1,0,&s->footprint,&s->rows,&s->rowBytes,&s->totalBytes);
        if(s->rows!=height||s->rowBytes!=UINT64(width)*4||!s->totalBytes)return {};
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=s->totalBytes;
        buffer.Height=1;buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        heap.Type=D3D12_HEAP_TYPE_UPLOAD;
        Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&s->upload)));
        heap.Type=D3D12_HEAP_TYPE_READBACK;
        Api(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&s->readback)));
        if(reason)*reason="controlled recording observation";
        if(!DlssNr::GpuSafety::Record(s->list.Get()))return {};
        Api(s->list->Close());Api(s->list->Reset(s->allocator.Get(),nullptr));
        s->ticket=DlssNr::GpuSafety::Record(s->list.Get());if(!s->ticket)return {};
        auto host=std::unique_ptr<NativeOwnedContinuation>(new NativeOwnedContinuation(std::move(s),reservation.index));
        {
            auto& registry=host->state_->owners->Resources();std::lock_guard lock(registry.mutex_);
            registry.privateController_=host.get();
        }
        reservation.index=MaxHosts;
        if(reason)*reason=nullptr;
        return host;
    }
    catch(...){return {};}
    ~NativeOwnedContinuation()
    {
        DisarmPrivateController();
        if(state_&&!state_->retired&&!state_->submitted)CancelUnsubmitted();
        bool terminal=false;
        try
        {
            if(state_&&!state_->retired)Cancel(state_->spare);
            terminal=!state_||state_->retired||Terminal(*state_);
        }
        catch(...){}
        if(terminal){state_.reset();ReleaseSlot(reservation_);return;}
        // A reservation exists before any recording. Destruction transfers the
        // exact live state into that slot without allocation, timeout release or
        // loss of its later authentic-terminal reclamation path.
        auto& retained=Retained();std::lock_guard lock(retained.mutex);
        retained.slots[reservation_].pending=std::move(state_);reservation_=MaxHosts;
    }
    NativeOwnedContinuation(const NativeOwnedContinuation&)=delete;
    NativeOwnedContinuation& operator=(const NativeOwnedContinuation&)=delete;
    std::optional<C::ResourceView> View(std::size_t index)const noexcept
    {std::lock_guard lock(mutex_);return state_&&index<=state_->capacity&&state_->textures[index]?std::optional{state_->views[index]}:std::nullopt;}
    std::optional<RecordedContentPath> ProveRecordedPath(std::size_t index,const C::ResourceIdentityToken& expected)const noexcept
    try{std::lock_guard lock(mutex_);return PathLocked(index,expected);}catch(...){return {};}
    bool ValidateRecordedPath(const RecordedContentPath& proof)const noexcept
    try
    {
        std::lock_guard lock(mutex_);if(!state_||proof.owner_.lock()!=state_->owners)return false;
        const auto current=PathLocked(proof.destinationIndex_,proof.destination_);
        return current&&current->source_==proof.source_&&current->count_==proof.count_&&current->ordinals_==proof.ordinals_;
    }
    catch(...){return false;}
    bool TryRetireResource(std::size_t index,const C::ResourceIdentityToken& expected)noexcept
    try
    {
        std::optional<NativeResourceRegistry::DetachedPrivateAllocation> registryHold;
        ComPtr<ID3D12Resource> hostHold;
        {
            std::lock_guard lock(mutex_);
            if(!state_||state_->retired||!state_->owners||index>state_->capacity||!state_->textures[index]||
               state_->views[index].identity!=expected)return false;
            auto& s=*state_;s.admissionClosed[index]=true;
            registryHold=s.owners->Resources().DetachPrivateAllocation(this,s.textures[index].Get(),expected,s.uses[index]);
            if(!registryHold)return false;
            hostHold=std::move(s.textures[index]);
        }
        // Registry and host storage references release only after both locks.
        return true;
    }
    catch(...){return false;}
    CopyRequest Request(std::size_t source,std::size_t destination)const noexcept
    {
        std::lock_guard lock(mutex_);CopyRequest request;request.source=source;request.destination=destination;
        if(state_&&source<=state_->capacity)request.sourceVersion=state_->views[source].identity;
        if(state_&&destination<=state_->capacity)request.destinationVersion=state_->views[destination].identity;
        if(state_)request.rectangle={0,0,state_->width,state_->height};return request;
    }
    std::size_t EdgeCount()const noexcept{std::lock_guard lock(mutex_);return state_?state_->count:0;}
    std::optional<Edge> EdgeAt(std::size_t index)const noexcept
    {std::lock_guard lock(mutex_);return state_&&index<state_->count?std::optional{state_->edges[index]}:std::nullopt;}
    bool BeginCrossQueue(std::size_t source,const C::ResourceIdentityToken& version,const GpuTicket& exactProducer)noexcept
    try
    {
        std::lock_guard lock(mutex_);
        if(!state_||state_->retired||state_->failed||!state_->submitted||state_->readbackRecorded||
           state_->producer.ticket||state_->spare.ticket||!exactProducer||exactProducer!=state_->ticket||
           source>state_->capacity||state_->writers[source]!=exactProducer||state_->count==state_->capacity)return false;
        auto& s=*state_;auto resourceLock=s.owners->Resources().LockNativeAction();
        if(!Fresh(source,version)||!Context::CompleteContent(s.views[source])||
           !s.owners->Resources().PublishNativeLease(s.textures[source].Get(),s.scope))return false;
        // The opaque owner ticket supplies its actual signal fence/value. No raw
        // fence, value or replacement queue is accepted from the caller.
        const auto dependency=DlssNr::GpuSafety::InspectRecording(exactProducer,s.spare.queue.Get(),true);
        if(!dependency.valid||!dependency.registryHealthy||!dependency.submitted||!dependency.uniqueSubmission||
           !dependency.nonReplayable||!dependency.queueKnown||!dependency.supportedType||
           !dependency.sameDevice||dependency.sameQueue||!dependency.orderedForConsumer)return false;
        s.spare.ticket=DlssNr::GpuSafety::Record(s.spare.list.Get());if(!s.spare.ticket)return false;
        Api(s.spare.list->Reset(s.spare.allocator.Get(),nullptr));s.spare.closed=false;
        auto consumer=DlssNr::GpuSafety::Record(s.spare.list.Get());if(!consumer){s.failed=true;return false;}
        s.spare.ticket=std::move(consumer);s.producer.Swap(s);s.Swap(s.spare);return true;
    }
    catch(...){std::lock_guard lock(mutex_);if(state_)state_->failed=true;return false;}
    Result Upload(std::span<const std::byte> pixels)noexcept
    try
    {
        std::lock_guard lock(mutex_);if(!Open()||state_->uploaded||pixels.size()!=std::size_t(state_->width)*state_->height*4)return Result::Refused;
        auto& s=*state_;auto resourceLock=s.owners->Resources().LockNativeAction();
        auto action=DlssNr::GpuSafety::BeginLocalAction(s.ticket,s.list.Get(),s.textures[0].Get());
        auto lease=action?Lease(0,true,*action):std::nullopt;if(!lease||!TrackUse(0))return Result::Refused;
        void* address=nullptr;D3D12_RANGE noRead{0,0};if(FAILED(s.upload->Map(0,&noRead,&address)))return Result::Refused;
        for(UINT row=0;row<s.height;++row)std::memcpy(static_cast<std::byte*>(address)+s.footprint.Offset+
            std::size_t(row)*s.footprint.Footprint.RowPitch,pixels.data()+std::size_t(row)*s.width*4,std::size_t(s.width)*4);
        s.upload->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=s.upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=s.footprint;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=s.textures[0].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        s.uploaded=true;
        Transition(s.list.Get(),s.textures[0].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        s.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Transition(s.list.Get(),s.textures[0].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        const bool published=Written(0,*action);CloseAdmission(*lease);s.failed=!published;
        if(published){s.uploadRoot=s.views[0].identity;s.uploadTicket=s.ticket;}
        return published?Result::Recorded:Result::PossibleEffect;
    }
    catch(...){std::lock_guard lock(mutex_);if(state_&&state_->uploaded)state_->failed=true;return state_&&state_->uploaded?Result::PossibleEffect:Result::Refused;}
    Result Copy(const CopyRequest& request)noexcept
    try
    {
        std::lock_guard lock(mutex_);if(!Open())return Result::Refused;auto& s=*state_;
        auto resourceLock=s.owners->Resources().LockNativeAction();
        if(request.rectangle!=C::Rectangle{0,0,s.width,s.height}||request.operation==Operation::Unknown||
           !Fresh(request.source,request.sourceVersion)||!Fresh(request.destination,request.destinationVersion)||
           !Context::CompleteContent(s.views[request.source]))return Result::Refused;
        if(request.operation==Operation::Identity)
            return request.source==request.destination&&request.sourceVersion==request.destinationVersion?Result::Identity:Result::Refused;
        if(request.operation!=Operation::FullCopy||request.source==request.destination||s.count==s.capacity||s.readbackRecorded)return Result::Refused;
        auto action=DlssNr::GpuSafety::BeginLocalAction(s.ticket,s.list.Get(),s.textures[request.destination].Get());
        if(!action)return Result::Refused;
        auto read=Lease(request.source,false,*action),write=Lease(request.destination,true,*action);
        if(!read||!write||!TrackUse(request.source)||!TrackUse(request.destination))return Result::Refused;
        // All edge, revision-result and physical storage slots already exist.
        // Reserve the possible-effect edge before the first command; failed
        // publication can never erase it or an earlier recorded edge.
        auto reserved=s.owners->Resources().ReservePrivateFullCopy(this,s.textures[request.source].Get(),
            s.textures[request.destination].Get(),s.views[request.source],s.views[request.destination],s.recording,s.count+1,*action);
        if(!reserved){CloseAdmission(*read);CloseAdmission(*write);return Result::Refused;}
        s.copyReservations[s.count]=std::move(reserved);
        auto& edge=s.edges[s.count++];edge.source=request.sourceVersion;edge.destinationBefore=request.destinationVersion;
        edge.rectangle=request.rectangle;edge.recording=s.recording;edge.recordingTicket=s.ticket;edge.ordinal=s.count;
        if(s.writers[request.source]!=s.ticket)edge.producerDependency=s.writers[request.source];
        auto* from=s.textures[request.source].Get();auto* to=s.textures[request.destination].Get();
        Transition(s.list.Get(),from,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(s.list.Get(),to,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        s.list->CopyResource(to,from);
        Transition(s.list.Get(),from,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
        Transition(s.list.Get(),to,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        const bool published=Written(request.destination,*action);CloseAdmission(*read);CloseAdmission(*write);
        if(published){edge.destination=s.views[request.destination].identity;edge.result=Result::Recorded;}
        else s.failed=true;
        return edge.result;
    }
    catch(...){std::lock_guard lock(mutex_);if(state_)state_->failed=true;return Result::PossibleEffect;}
    std::optional<FinalInputAncestry> FullCopyAncestry(std::size_t edgeIndex)const
    {
        std::lock_guard lock(mutex_);if(!state_||edgeIndex>=state_->count||state_->failed)return {};
        const auto& s=*state_;const auto& edge=s.edges[edgeIndex];
        if(edge.result!=Result::Recorded||!s.copyReservations[edgeIndex])return {};
        for(const auto& view:s.views)if(view.identity==edge.destination)
            return s.owners->Resources().CommitPrivateFullCopy(this,*s.copyReservations[edgeIndex],view);
        return {};
    }
    Result RecordReadback(std::size_t index,const C::ResourceIdentityToken& version)noexcept
    try
    {
        std::lock_guard lock(mutex_);if(!Open()||state_->readbackRecorded)return Result::Refused;auto& s=*state_;
        auto resourceLock=s.owners->Resources().LockNativeAction();
        if(!Fresh(index,version)||!Context::CompleteContent(s.views[index]))return Result::Refused;
        auto action=DlssNr::GpuSafety::BeginLocalAction(s.ticket,s.list.Get(),s.textures[index].Get());
        auto lease=action?Lease(index,false,*action):std::nullopt;if(!lease||!TrackUse(index))return Result::Refused;
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=s.textures[index].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=s.readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=s.footprint;
        s.readbackRecorded=true;
        Transition(s.list.Get(),from.pResource,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        s.list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Transition(s.list.Get(),from.pResource,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
        CloseAdmission(*lease);return Result::Recorded;
    }
    catch(...){std::lock_guard lock(mutex_);if(state_)state_->failed=true;return Result::PossibleEffect;}
    bool Submit()noexcept
    try
    {
        std::lock_guard lock(mutex_);if(!Open()||!state_->uploaded)return false;auto& s=*state_;
        if(FAILED(s.list->Close())){s.failed=true;return false;}s.closed=true;
        ID3D12CommandList* lists[]={s.list.Get()};s.submitted=true;s.queue->ExecuteCommandLists(1,lists);
        if(!DlssNr::GpuSafety::SealOwnedRecording(s.list.Get())||FAILED(s.queue->Signal(s.fence.Get(),1)))
        {s.failed=true;return false;}return true;
    }
    catch(...){std::lock_guard lock(mutex_);if(state_)state_->failed=true;return false;}
    bool Complete(DWORD timeoutMs)noexcept
    try
    {
        if(timeoutMs>5000)return false;
        std::lock_guard lock(mutex_);if(!state_||state_->retired||state_->failed||!state_->submitted)return false;
        auto& s=*state_;const auto observed=s.fence->GetCompletedValue();
        if(observed==UINT64_MAX)return false;
        if(observed<1)
        {
            if(!timeoutMs)return false;
            const HRESULT registered=s.fence->SetEventOnCompletion(1,s.completionEvent);
            const DWORD waited=SUCCEEDED(registered)?WaitForSingleObject(s.completionEvent,timeoutMs):WAIT_FAILED;
            if(waited!=WAIT_OBJECT_0)return false;
        }
        return UniqueTerminal(s);
    }
    catch(...){return false;}
    bool Readback(std::span<std::byte> pixels)noexcept
    try
    {
        std::lock_guard lock(mutex_);
        if(!state_||state_->retired||state_->failed||!state_->submitted||!state_->readbackRecorded||
           pixels.size()!=std::size_t(state_->width)*state_->height*4)return false;
        auto& s=*state_;
        if(!UniqueTerminal(s)||s.fence->GetCompletedValue()==UINT64_MAX||s.fence->GetCompletedValue()<1)return false;
        void* address=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(s.totalBytes)};
        if(FAILED(s.readback->Map(0,&range,&address)))return false;
        for(UINT row=0;row<s.height;++row)std::memcpy(pixels.data()+std::size_t(row)*s.width*4,
            static_cast<const std::byte*>(address)+s.footprint.Offset+std::size_t(row)*s.footprint.Footprint.RowPitch,std::size_t(s.width)*4);
        D3D12_RANGE noWrite{0,0};s.readback->Unmap(0,&noWrite);return true;
    }
    catch(...){return false;}
    bool CancelUnsubmitted()noexcept
    try
    {
        std::lock_guard lock(mutex_);if(!state_||state_->retired||state_->submitted)return false;auto& s=*state_;
        s.failed=true;return Cancel(s);
    }
    catch(...){return false;}
    bool TryRetire()noexcept
    try
    {
        std::lock_guard lock(mutex_);if(!state_)return false;auto& s=*state_;if(s.retired)return true;
        if(!Terminal(s))return false;
        // Completed replayed recordings become releasable only after Reset or
        // true lifetime end. They never qualify the controlled readback result.
        DisarmPrivateController();
        s.Release();s.producer.Release();s.spare.Release();s.upload.Reset();s.readback.Reset();
        for(auto& resource:s.textures)resource.Reset();s.owners.reset();s.device.Reset();s.retired=true;
        ReleaseSlot(reservation_);return true;
    }
    catch(...){return false;}
};
}
