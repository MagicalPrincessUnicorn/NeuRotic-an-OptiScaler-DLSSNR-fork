#include "GpuRemoteConsumer.h"
#include "PreparedGpuIo.h"
#include <dlssnr/connections/GpuBudget.h>
namespace nrpg::gpu {
using Microsoft::WRL::ComPtr;
namespace B=DlssNr::Connections::GpuBudget;
namespace {std::atomic<unsigned> remoteOwners{0};struct RemoteOwner {~RemoteOwner(){remoteOwners.fetch_sub(1);}};}
struct GpuRemoteConsumer::Impl {
    // One managed host per producer module. A quarantined Impl retains this
    // reservation; allocating another host cannot bypass the combined budget.
    std::unique_ptr<RemoteOwner> remoteOwner;
    std::vector<std::shared_ptr<B::Reservation>> allocations;
    ManagedHostSpec spec;PreparedGpuClient client;PeerPolicy peer;
    ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
    std::array<std::array<ComPtr<ID3D12Resource>,5>,2> shared;
    std::array<ComPtr<ID3D12Resource>,2> privateOutput;
    CpuMainlineClient::GpuInputs retained;
    std::weak_ptr<void> returnedLease;
    GpuFrameRequest frame;unsigned width=0,height=0;std::uint64_t localValue=0,sequence=0;
    bool initialized=false,pending=false,uncertain=false,started=false;
    explicit Impl(ManagedHostSpec s):spec(std::move(s)){}
    bool Fail(std::string&r,const char*s,bool quarantine=false){uncertain|=quarantine;return io::Fail(r,s);}
    bool Begin(std::string&r){return (SUCCEEDED(allocator->Reset())&&SUCCEEDED(list->Reset(allocator.Get(),nullptr)))||Fail(r,"Remote GPU recorder reset failed");}
    bool Submit(std::string&r){if(FAILED(list->Close()))return Fail(r,"Remote GPU recorder close failed",true);uncertain=true;ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);if(FAILED(queue->Signal(fence.Get(),++localValue))||!io::Wait(fence.Get(),localValue,spec.timeoutMs))return Fail(r,"Remote local GPU completion unknown; retained",true);uncertain=false;return true;}
    bool Initialize(const DlssNr::NativeGuides::TextureCapture&capture,std::string&r){
        if(!remoteOwner){unsigned free=0;if(!remoteOwners.compare_exchange_strong(free,1))return Fail(r,"Another managed GPU host owns this producer");
            try{remoteOwner=std::make_unique<RemoteOwner>();}catch(...){remoteOwners.fetch_sub(1);throw;}}
        B::ClampLimit(512ull*1024*1024);
        if(!B::AdmitRaster(capture.description.color.width,capture.description.color.height,capture.deriveMotion))return Fail(r,"Remote raster exceeds allocation contract");
        if(started)return Fail(r,"Remote GPU setup cannot restart this owner");started=true;
        const auto&d=capture.description;peer.device=capture.normalized.device;peer.adapterLuid=capture.adapterLuid;peer.episode={d.producer,d.session,d.stream,d.device,d.generation,0};width=d.color.width;height=d.color.height;
        D3D12_COMMAND_QUEUE_DESC q{};
        if(FAILED(peer.device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))||FAILED(peer.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))||FAILED(peer.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))||FAILED(list->Close())||FAILED(peer.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))))return Fail(r,"Remote local D3D12 owner unavailable");
        if(!client.Start(spec,peer,r))return false;
        const std::array<DXGI_FORMAT,5> formats{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM};
        for(unsigned slot=0;slot<2;++slot)for(unsigned role=0;role<5;++role){
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=width;image.Height=height;image.DepthOrArraySize=image.MipLevels=1;image.SampleDesc.Count=1;image.Format=formats[role];image.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
            auto charged=B::Reserve(B::TextureBytes(peer.device.Get(),image));if(!charged)return Fail(r,"Remote aggregate GPU budget exhausted");
            if(FAILED(peer.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&image,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&shared[slot][role]))))return Fail(r,"Remote bounded NT image pool unavailable");
            allocations.push_back(std::move(charged));
            io::Handle handle;if(FAILED(peer.device->CreateSharedHandle(shared[slot][role].Get(),nullptr,GENERIC_ALL,nullptr,&handle.value)))return Fail(r,"Remote NT image sharing unavailable");
            GpuRegistration registration;registration.id=3+slot*5+role;registration.slot=slot;registration.role=static_cast<Role>(role+1);registration.handle=reinterpret_cast<std::uintptr_t>(handle.value);registration.width=width;registration.height=height;registration.format=formats[role];registration.mips=registration.arraySize=registration.samples=1;registration.access=GENERIC_ALL;
            if(!client.Register(registration,r))return false;
            if(role==4){image.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;auto outputCharge=B::Reserve(B::TextureBytes(peer.device.Get(),image));if(!outputCharge)return Fail(r,"Remote output GPU budget exhausted");if(FAILED(peer.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&privateOutput[slot]))))return Fail(r,"Remote private output pool unavailable");allocations.push_back(std::move(outputCharge));}
        }
        initialized=true;return true;
    }
};
GpuRemoteConsumer::GpuRemoteConsumer(ManagedHostSpec s):p_(std::make_unique<Impl>(std::move(s))){}
GpuRemoteConsumer::~GpuRemoteConsumer(){if(p_->uncertain||p_->pending||p_->client.RequiresRestart())(void)p_.release();}
bool GpuRemoteConsumer::RequiresRestart()const{return p_->uncertain||p_->client.RequiresRestart();}
bool GpuRemoteConsumer::Process(const DlssNr::NativeGuides::TextureCapture&capture,CpuMainlineClient::GpuOutput&output,std::string&r){
    auto&p=*p_;output={};const auto&g=capture.normalized;const auto&d=capture.description;
    if(p.uncertain||p.pending||!p.returnedLease.expired()||p.client.RequiresRestart())return p.Fail(r,"Remote bounded generation busy/quarantined");
    if(!g.device||!g.lease||!io::SameDevice(g.device.Get(),g.producer.Get())||!io::Wait(g.producer.Get(),g.value,0)||!T::AdmitRaster(d.color.width,d.color.height)||!Neurotic::Context::NormalizePreparedGuides(d))return p.Fail(r,"Remote canonical input identity/completion refused");
    const std::array<ID3D12Resource*,4> resources{g.color.Get(),g.depth.Get(),g.motion.Get(),g.distrust.Get()};const std::array<DXGI_FORMAT,4> formats{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R8_UNORM};
    for(unsigned i=0;i<(capture.deriveMotion?2u:4u);++i){if(!io::SameDevice(g.device.Get(),resources[i]))return p.Fail(r,"Remote input logical device mismatch");const auto image=resources[i]->GetDesc();if(image.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||image.Width!=d.color.width||image.Height!=d.color.height||image.Format!=formats[i]||image.MipLevels!=1||image.DepthOrArraySize!=1||image.SampleDesc.Count!=1)return p.Fail(r,"Remote canonical texture shape/format refused");}
    if(!p.initialized&&!p.Initialize(capture,r))return false;
    T::Identity identity{d.producer,d.session,d.stream,d.device,d.generation,d.capture};if(!T::detail::Prefix(identity,p.peer.episode)||p.width!=d.color.width||p.height!=d.color.height||!io::SameDevice(p.peer.device.Get(),g.color.Get())||capture.adapterLuid!=p.peer.adapterLuid)return p.Fail(r,"Remote resize/device/episode requires a drained new owner");
    if(p.sequence>=UINT64_MAX-1)return p.Fail(r,"Remote timeline exhausted");const unsigned slot=static_cast<unsigned>(p.sequence%2);++p.sequence;p.frame={};p.frame.description=d;p.frame.ticket={identity,slot,(d.flags&P::Reset?1u:0u)|(capture.deriveMotion?0u:2u),p.sequence,p.sequence,p.sequence,p.client.SessionNonce()};
    for(unsigned i=0;i<5;++i)p.frame.registrations[i]=(i==2||i==3)&&capture.deriveMotion?0:3+slot*5+i;
    p.retained=g;if(!p.Begin(r))return false;
    for(unsigned i=0;i<(capture.deriveMotion?2u:4u);++i){io::Barrier(p.list.Get(),resources[i],D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);io::Barrier(p.list.Get(),p.shared[slot][i].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);p.list->CopyResource(p.shared[slot][i].Get(),resources[i]);io::Barrier(p.list.Get(),p.shared[slot][i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);io::Barrier(p.list.Get(),resources[i],D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
    if(!p.Submit(r))return false;p.retained={};
    p.uncertain=true;if(FAILED(p.queue->Signal(p.client.ProducerFence(),p.sequence)))return p.Fail(r,"Remote producer timeline signal failed",true);
    GpuFrameResult result;if(!p.client.Process(p.frame,result,r))return false;p.uncertain=false;
    if(!(result.facts&P::Ipc::OutputValid)){r="Remote consumer preparing/refused without valid output";return false;}
    p.pending=true;if(!p.Begin(r))return p.Fail(r,"Remote output lease acquired but local recording failed",true);
    io::Barrier(p.list.Get(),p.shared[slot][4].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);io::Barrier(p.list.Get(),p.privateOutput[slot].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);p.list->CopyResource(p.privateOutput[slot].Get(),p.shared[slot][4].Get());io::Barrier(p.list.Get(),p.privateOutput[slot].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);io::Barrier(p.list.Get(),p.shared[slot][4].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
    if(!p.Submit(r))return false;output.color=p.privateOutput[slot];output.completion={p.fence,p.localValue};output.lease=std::make_shared<int>(0);p.returnedLease=output.lease;return true;
}
bool GpuRemoteConsumer::ReturnAfterCopyback(bool actualCompleted,std::uint64_t capture,std::uint64_t generation,std::string&r){auto&p=*p_;
    if(!p.pending||p.uncertain||!actualCompleted||p.frame.ticket.identity.capture!=capture||p.frame.ticket.identity.generation!=generation)return p.Fail(r,"Remote actual game copyback proof missing/mismatched",p.pending);
    p.uncertain=true;if(FAILED(p.queue->Signal(p.fence.Get(),++p.localValue))||!io::Wait(p.fence.Get(),p.localValue,p.spec.timeoutMs))return p.Fail(r,"Remote post-copyback retirement fence unknown",true);p.uncertain=false;
    CopybackReceipt receipt{p.frame.ticket,p.fence,p.localValue,true};if(!p.client.Release(receipt,r))return false;p.pending=false;return true;
}
}
