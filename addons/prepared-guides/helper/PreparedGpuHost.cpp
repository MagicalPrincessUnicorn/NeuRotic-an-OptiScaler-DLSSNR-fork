#include "PreparedGpuHost.h"
#include "PreparedGpuIo.h"
#include <dlssnr/connections/GpuBudget.h>
#include <dxgi1_4.h>
#include <iostream>
namespace nrpg::gpu {
using Microsoft::WRL::ComPtr;
namespace B=DlssNr::Connections::GpuBudget;
struct PreparedGpuHost::Impl {
    struct Registration {GpuRegistration wire;std::shared_ptr<B::Reservation> allocation;ComPtr<ID3D12Resource> image;std::shared_ptr<io::Handle> handle;};
    struct Canonical {std::array<std::shared_ptr<B::Reservation>,4> allocations;std::array<ComPtr<ID3D12Resource>,4> images;};
    struct Slot {std::shared_ptr<Canonical> canonical;CpuMainlineClient::GpuOutput output;T::FrameTicket ticket{};bool leased=false;};
    PeerPolicy peer;ManagedHostSpec spec;Consumer consumer;io::Handle process;
    ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> producer,output,completed;std::uint64_t value=0;std::array<Registration,14> registered{};unsigned count=0;std::uint32_t lastId=2;
    std::array<Slot,2> slots{};T::FrameTicket latest{};bool uncertain=false;
    bool Refuse(std::string&r,const char*s,bool quarantine=false){uncertain|=quarantine;return io::Fail(r,s);}
    bool Initialize(std::string&r){B::ClampLimit(512ull*1024*1024);D3D12_COMMAND_QUEUE_DESC q{};
        return (SUCCEEDED(peer.device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))&&SUCCEEDED(peer.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))&&SUCCEEDED(peer.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))&&SUCCEEDED(list->Close())&&SUCCEEDED(peer.device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&producer)))&&SUCCEEDED(peer.device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&output)))&&SUCCEEDED(peer.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&completed))))||Refuse(r,"NRH2 host D3D12 initialization refused");}
    bool ExportFence(HANDLE pipe,ID3D12Fence*f,Role role,std::uint32_t id,std::string&r){io::Handle local;HANDLE remote=nullptr;
        if(FAILED(peer.device->CreateSharedHandle(f,nullptr,GENERIC_ALL,nullptr,&local.value))||!DuplicateHandle(GetCurrentProcess(),local.value,process.value,&remote,0,FALSE,DUPLICATE_SAME_ACCESS))return Refuse(r,"NRH2 shared fence export failed");
        GpuRegistration wire;wire.nonce=peer.nonce;wire.identity=peer.episode;wire.id=id;wire.role=role;wire.kind=ObjectKind::Fence;wire.handle=reinterpret_cast<std::uintptr_t>(remote);wire.access=GENERIC_ALL;wire.direction=Direction::HostToProducer;
        return io::Write(pipe,Encode(wire),spec.timeoutMs)||Refuse(r,"NRH2 shared fence delivery failed");}
    bool Register(GpuRegistration&w,std::string&r){
        if(!B::AdmitRaster(w.width,w.height))return Refuse(r,"NRH2 registration exceeds raster budget");
        std::shared_ptr<B::Reservation> allocation;
        if(w.nonce!=peer.nonce||!T::detail::Prefix(w.identity,peer.episode)||w.id<=lastId||count==registered.size()||w.kind!=ObjectKind::Texture||static_cast<unsigned>(w.role)>5||(w.access!=GENERIC_ALL&&w.access!=(DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE))||latest.identity.capture)return Refuse(r,"NRH2 registration rights/identity/phase refused");
        const DXGI_FORMAT formats[]{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM};
        if(w.format!=static_cast<unsigned>(formats[static_cast<unsigned>(w.role)-1]))return Refuse(r,"NRH2 registration format refused");
        for(unsigned i=0;i<count;++i)if(registered[i].wire.slot==w.slot&&registered[i].wire.role==w.role)return Refuse(r,"NRH2 duplicate resource role refused");
        // Numeric value is interpreted only in this authenticated target process.
        if(w.handle>UINTPTR_MAX)return Refuse(r,"NRH2 local handle width refused");
        HANDLE handle=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(w.handle));ComPtr<ID3D12Resource> resource;
        for(unsigned i=0;i<count;++i)if(handle&&registered[i].handle->value==handle)return Refuse(r,"NRH2 already-owned local handle value refused");
        if(w.direction==Direction::HostToProducer){
            if(w.handle||w.access!=GENERIC_ALL)return Refuse(r,"NRH2 host allocation request must have zero handle");
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w.width;d.Height=w.height;d.DepthOrArraySize=d.MipLevels=1;d.Format=static_cast<DXGI_FORMAT>(w.format);d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
            allocation=B::Reserve(B::TextureBytes(peer.device.Get(),d));if(!allocation)return Refuse(r,"NRH2 host aggregate GPU budget exhausted");
            if(FAILED(peer.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource)))||FAILED(peer.device->CreateSharedHandle(resource.Get(),nullptr,GENERIC_ALL,nullptr,&handle)))return Refuse(r,"NRH2 host texture allocation/export failed");
        }else if(FAILED(peer.device->OpenSharedHandle(handle,IID_PPV_ARGS(&resource))))return Refuse(r,"NRH2 target NT texture import refused");
        auto ownedHandle=std::make_shared<io::Handle>(handle);const auto d=resource->GetDesc();D3D12_HEAP_PROPERTIES heap{};D3D12_HEAP_FLAGS flags{};
        if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Width!=w.width||d.Height!=w.height||d.Format!=static_cast<DXGI_FORMAT>(w.format)||d.MipLevels!=1||d.DepthOrArraySize!=1||d.SampleDesc.Count!=1||d.SampleDesc.Quality||FAILED(resource->GetHeapProperties(&heap,&flags))||!(flags&D3D12_HEAP_FLAG_SHARED))return Refuse(r,"NRH2 imported object shape/shared allocation refused");
        using CompareFn=BOOL(WINAPI*)(HANDLE,HANDLE);auto compare=reinterpret_cast<CompareFn>(GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),"CompareObjectHandles"));if(!compare)return Refuse(r,"NRH2 shared object identity comparison unavailable");
        for(unsigned i=0;i<count;++i)if(compare(registered[i].handle->value,handle))return Refuse(r,"NRH2 alias registration refused");
        if(w.direction==Direction::HostToProducer){HANDLE remote=nullptr;if(!DuplicateHandle(GetCurrentProcess(),handle,process.value,&remote,0,FALSE,DUPLICATE_SAME_ACCESS))return Refuse(r,"NRH2 host texture target duplication failed");w.handle=reinterpret_cast<std::uintptr_t>(remote);}
        if(!allocation)allocation=B::Reserve(B::TextureBytes(peer.device.Get(),d));if(!allocation)return Refuse(r,"NRH2 imported allocation exceeds aggregate GPU budget");
        registered[count++]={w,std::move(allocation),resource,ownedHandle};lastId=w.id;return true;
    }
    Registration* Find(std::uint32_t id,Role role,const GpuFrameRequest&f){for(unsigned i=0;i<count;++i){auto&r=registered[i];if(r.wire.id==id&&r.wire.role==role&&r.wire.slot==f.ticket.slot&&r.wire.width==f.description.color.width&&r.wire.height==f.description.color.height)return &r;}return nullptr;}
    bool Begin(std::string&r){return (SUCCEEDED(allocator->Reset())&&SUCCEEDED(list->Reset(allocator.Get(),nullptr)))||Refuse(r,"NRH2 recorder reset failed");}
    bool Submit(std::string&r){if(FAILED(list->Close()))return Refuse(r,"NRH2 command close failed");ID3D12CommandList* lists[]{list.Get()};uncertain=true;queue->ExecuteCommandLists(1,lists);if(FAILED(queue->Signal(completed.Get(),++value))||!io::Wait(completed.Get(),value,spec.timeoutMs))return Refuse(r,"NRH2 GPU completion unknown; generation quarantined",true);uncertain=false;return true;}
    bool Frame(const GpuFrameRequest&f,GpuFrameResult&result,std::string&r){
        result.ticket=f.ticket;auto&slot=slots[f.ticket.slot];
        // A single outstanding submitted/output frame is permitted across both slots.
        if(uncertain||slots[0].leased||slots[1].leased||(slot.canonical&&slot.canonical.use_count()!=1))return Refuse(r,"NRH2 bounded slots busy or retained by history");
        std::array<Registration*,5> regs{};for(unsigned i=0;i<5;++i){if(i>=2&&i<=3&&!(f.ticket.flags&2))continue;regs[i]=Find(f.registrations[i],static_cast<Role>(i+1),f);if(!regs[i])return Refuse(r,"NRH2 frame registration mismatch");}
        const bool reuse=bool(slot.canonical);auto canonical=reuse?slot.canonical:std::make_shared<Canonical>();for(unsigned i=0;i<4;++i){if(!regs[i])continue;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;auto d=regs[i]->image->GetDesc();d.Flags=D3D12_RESOURCE_FLAG_NONE;
            if(reuse){const auto old=canonical->images[i]?canonical->images[i]->GetDesc():D3D12_RESOURCE_DESC{};if(old.Width!=d.Width||old.Height!=d.Height||old.Format!=d.Format)return Refuse(r,"NRH2 canonical resize requires clean new generation");}
            else {auto charged=B::Reserve(B::TextureBytes(peer.device.Get(),d));if(!charged)return Refuse(r,"NRH2 canonical aggregate GPU budget exhausted");if(FAILED(peer.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&canonical->images[i]))))return Refuse(r,"NRH2 canonical texture allocation failed");canonical->allocations[i]=std::move(charged);}}
        slot.canonical=canonical;slot.ticket=f.ticket;latest=f.ticket;
        if(!Begin(r))return false;
        // The queue wait is a GPU reference even before ExecuteCommandLists.
        uncertain=true;if(FAILED(queue->Wait(producer.Get(),f.ticket.producerValue)))return Refuse(r,"NRH2 producer GPU dependency failed",true);
        for(unsigned i=0;i<4;++i)if(regs[i]){if(reuse)io::Barrier(list.Get(),canonical->images[i].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);io::Barrier(list.Get(),regs[i]->image.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyResource(canonical->images[i].Get(),regs[i]->image.Get());io::Barrier(list.Get(),regs[i]->image.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);io::Barrier(list.Get(),canonical->images[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
        if(!Submit(r))return false;
        CpuMainlineClient::GpuInputs input;input.device=peer.device;input.color=canonical->images[0];input.depth=canonical->images[1];input.motion=canonical->images[2];input.distrust=canonical->images[3];input.producer=completed;input.value=value;input.lease=canonical;
        uncertain=true;
        if(!consumer.process(f.description,input,!(f.ticket.flags&2),slot.output,r)){if(consumer.requiresRestart())return Refuse(r,"NRH2 consumer completion unknown; quarantined",true);uncertain=false;result.reason=1;return true;}
        auto&o=slot.output;const auto d=o.color?o.color->GetDesc():D3D12_RESOURCE_DESC{};
        if(!o.lease||!io::SameDevice(peer.device.Get(),o.color.Get())||!io::SameDevice(peer.device.Get(),o.completion.fence.Get())||!io::Wait(o.completion.fence.Get(),o.completion.value,spec.timeoutMs)||d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||d.Width!=f.description.color.width||d.Height!=f.description.color.height||d.DepthOrArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1)return Refuse(r,"NRH2 consumer output proof invalid",true);
        if(!Begin(r))return Refuse(r,"NRH2 output recording failed",true);
        io::Barrier(list.Get(),o.color.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);io::Barrier(list.Get(),regs[4]->image.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(regs[4]->image.Get(),o.color.Get());io::Barrier(list.Get(),regs[4]->image.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);io::Barrier(list.Get(),o.color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if(!Submit(r))return false;
        uncertain=true;if(FAILED(queue->Signal(output.Get(),f.ticket.outputValue))||!io::Wait(output.Get(),f.ticket.outputValue,spec.timeoutMs))return Refuse(r,"NRH2 output fence completion unknown",true);uncertain=false;
        slot.leased=true;result.outcome=P::Ipc::Outcome::CompletedSuccess;result.facts=P::Ipc::Recorded|P::Ipc::Submitted|P::Ipc::GpuCompleted|P::Ipc::OutputValid;result.outputCapture=f.ticket.identity.capture;result.completedValue=f.ticket.outputValue;return true;
    }
};
PreparedGpuHost::PreparedGpuHost():p_(std::make_unique<Impl>()){}
PreparedGpuHost::~PreparedGpuHost(){if(p_->uncertain||p_->slots[0].leased||p_->slots[1].leased)(void)p_.release();}
bool PreparedGpuHost::RequiresRestart()const{return p_->uncertain;}
bool PreparedGpuHost::Run(HANDLE pipe,const PeerPolicy&peer,const ManagedHostSpec&spec,Consumer consumer,std::string&r){auto&h=*p_;if(h.peer.device)return h.Refuse(r,"NRH2 host cannot be restarted");h.peer=peer;h.spec=spec;h.consumer=std::move(consumer);ULONG pid=0;
    if(!peer.device||!h.consumer.process||!h.consumer.requiresRestart||!GetNamedPipeServerProcessId(pipe,&pid)||pid!=peer.processId)return h.Refuse(r,"NRH2 actual control peer mismatch");
    h.process.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_DUP_HANDLE|SYNCHRONIZE,FALSE,pid);if(!h.process.value||io::Creation(h.process.value)!=peer.creationTime)return h.Refuse(r,"NRH2 peer creation identity mismatch");
    BOOL wow64=FALSE;if(!IsWow64Process(h.process.value,&wow64))return h.Refuse(r,"NRH2 peer architecture unavailable");
    Bytes bytes;Hello hello;if(!io::Read(pipe,bytes,spec.timeoutMs)||!Decode(bytes,hello)||hello.nonce!=peer.nonce||hello.processId!=pid||hello.processBits!=(wow64?32U:64U)||hello.adapterLuid!=peer.adapterLuid||hello.consumerMode!=1||hello.buildIdentity!=spec.buildIdentity||!h.Initialize(r))return h.Refuse(r,"NRH2 hello/device admission failed");
    hello.processId=GetCurrentProcessId();hello.processBits=64;hello.capabilities=h.consumer.capabilities|SharedTextures|SharedFences;
    if(!io::Write(pipe,Encode(hello,Kind::HelloAck),spec.timeoutMs)||!h.ExportFence(pipe,h.producer.Get(),Role::ProducerFence,1,r)||!h.ExportFence(pipe,h.output.Get(),Role::OutputFence,2,r))return false;
    T::TicketSequence sequence(peer.episode,peer.nonce);
    for(;;){DWORD available=0;if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr))break;
        if(!available){if(WaitForSingleObject(h.process.value,0)!=WAIT_TIMEOUT)break;Sleep(10);continue;}
        if(!io::Read(pipe,bytes,spec.timeoutMs))break;Kind kind{};if(!Header(bytes,kind))break;
        if(kind==Kind::Register){GpuRegistration reg;if(!Decode(bytes,reg)||!h.Register(reg,r)||!io::Write(pipe,Encode(reg),spec.timeoutMs))break;continue;}
        if(kind==Kind::Frame){GpuFrameRequest frame;GpuFrameResult result;if(!Decode(bytes,frame)||!sequence.Accept(frame.ticket)||!h.Frame(frame,result,r))break;if(!io::Write(pipe,Encode(result),spec.timeoutMs))break;continue;}
        if(kind==Kind::Release||kind==Kind::Drain||kind==Kind::Cancel||kind==Kind::Close){Command command;if(!Decode(bytes,command,kind)||command.ticket!=h.latest)break;auto&s=h.slots[command.ticket.slot];
            if(kind==Kind::Release){if(!s.leased||s.ticket!=command.ticket)break;s.output={};s.leased=false;}
            else if(h.slots[0].leased||h.slots[1].leased||h.uncertain)break;
            if(!io::Write(pipe,bytes,spec.timeoutMs))break;if(kind==Kind::Close)return true;continue;}
        break;
    }
    h.uncertain|=h.slots[0].leased||h.slots[1].leased;
    // A pre-frame shutdown or clean idle EOF is not a GPU retirement claim.
    return !h.uncertain;
}
int RunGpuHostBootstrap(HANDLE bootstrap,ConsumerFactory factory){try{
    io::Handle boot(bootstrap);Bytes b(io::BootstrapBytes);DWORD read=0;if(!ReadFile(bootstrap,b.data(),static_cast<DWORD>(b.size()),&read,nullptr)||read!=b.size())return 10;
    Hello hello;if(!Decode(std::span<const std::uint8_t>(b).first(80),hello)||hello.consumerMode!=1)return 11;
    PeerPolicy peer;peer.nonce=hello.nonce;peer.adapterLuid=hello.adapterLuid;peer.processId=hello.processId;peer.creationTime=Get(b,80);peer.episode=Prefix(std::span<const std::uint8_t>(b),88);
    ManagedHostSpec spec;spec.buildIdentity=hello.buildIdentity;spec.timeoutMs=static_cast<std::uint32_t>(Get(b,128,4));if(!spec.timeoutMs||spec.timeoutMs>30000)return 12;
    auto name=io::Text(b,136,128);spec.coreModule=io::Text(b,392,512);if(name.rfind(L"\\\\.\\pipe\\NeuRotic.NRH2.",0)!=0)return 13;
    ComPtr<IDXGIFactory4> dxgi;ComPtr<IDXGIAdapter1> adapter;LUID luid{};std::memcpy(&luid,&peer.adapterLuid,8);if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi)))||FAILED(dxgi->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)))||FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&peer.device))))return 14;
    std::string reason;auto consumer=factory(peer,spec,reason);if(!consumer.process){std::cerr<<reason<<'\n';return 15;}
    io::Handle pipe(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));if(pipe.value==INVALID_HANDLE_VALUE)return 16;
    PreparedGpuHost host;bool ok=host.Run(pipe.value,peer,spec,std::move(consumer),reason);if(!ok)std::cerr<<reason<<'\n';return ok?0:17;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 18;}}
}
