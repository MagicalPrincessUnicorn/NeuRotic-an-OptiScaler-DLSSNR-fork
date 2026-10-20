// Connected NRH2 fixture; GPU utility/recorder adapted from the returned proposal.
// Real GPU source -> imported/canonical guides -> existing ProcessResident
// -> explicit test recorder -> GPU copyback -> byte check. No private NR model.
// On any failure, ExitProcess avoids attempting in-process recovery of unknown
// GPU work. This policy belongs to this disposable fixture, not product code.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d10_1.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <memory>
#include <algorithm>
#include <cstdlib>
#include <cerrno>
#include "../../addons/prepared-guides/helper/PreparedGpuHost.h"
#include "../../addons/prepared-guides/helper/PreparedGpuIo.h"
#include "../../addons/prepared-guides/helper/GpuRemoteConsumer.h"


using Microsoft::WRL::ComPtr;
namespace P=Neurotic::Feed::Prepared;
namespace G=DlssNr::PreparedGuides;
constexpr UINT W=256,H=64;
[[noreturn]] static void Fail(const char* step,HRESULT hr=E_FAIL) {
    std::fprintf(stderr,"FAIL %s hr=%08lx; no recovery/qualification claimed\n",step,(unsigned long)hr);
    std::fflush(nullptr);ExitProcess(1);
}
static void Check(bool yes,const char* step){if(!yes)Fail(step);}
static void Hr(HRESULT hr,const char* step){if(FAILED(hr))Fail(step,hr);}
static void Barrier(ID3D12GraphicsCommandList*l,ID3D12Resource*r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
    if(a==b)return;D3D12_RESOURCE_BARRIER x{};x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};l->ResourceBarrier(1,&x);
}
struct Handle {HANDLE h=nullptr;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}Handle()=default;
    Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;};
static void Wait(ID3D12Fence*f,UINT64 n){
    auto v=f->GetCompletedValue();if(v==UINT64_MAX)Fail("removed device");if(v>=n)return;
    Handle e;e.h=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(e.h!=nullptr,"event");
    Hr(f->SetEventOnCompletion(n,e.h),"SetEventOnCompletion");
    Check(WaitForSingleObject(e.h,5000)==WAIT_OBJECT_0,"bounded GPU completion");
    v=f->GetCompletedValue();Check(v!=UINT64_MAX&&v>=n,"fence value after wake");
}
struct Gpu {
    ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> d;
    ComPtr<ID3D12CommandQueue> q;ComPtr<ID3D12CommandAllocator>a;
    ComPtr<ID3D12GraphicsCommandList>l;ComPtr<ID3D12Fence>f;UINT64 n=0;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    explicit Gpu(bool warp){
        ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
        if(warp)Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP");
        else {
            UINT index=0;
            if(const char* text=std::getenv("UC_ADAPTER_INDEX")) {
                char* end=nullptr;errno=0;const auto parsed=std::strtoul(text,&end,10);
                Check(text[0] && end && !*end && errno==0 && parsed<=0xffff,"UC_ADAPTER_INDEX");
                index=static_cast<UINT>(parsed);
            }
            Hr(factory->EnumAdapters1(index,&adapter),"selected DXGI adapter");
        }
        DXGI_ADAPTER_DESC1 info{};Hr(adapter->GetDesc1(&info),"adapter metadata");
        std::printf("Adapter LUID=%08lx:%08lx (%ls); recorder fixture, no model\n",
            (unsigned long)info.AdapterLuid.HighPart,(unsigned long)info.AdapterLuid.LowPart,info.Description);
        Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&d)),"D3D12 FL12_0");
        D3D12_COMMAND_QUEUE_DESC qd{};Hr(d->CreateCommandQueue(&qd,IID_PPV_ARGS(&q)),"queue");
        Hr(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)),"allocator");
        Hr(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&l)),"list");
        Hr(l->Close(),"initial close");Hr(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)),"fence");
    }
    void Begin(){Hr(a->Reset(),"allocator reset");Hr(l->Reset(a.Get(),nullptr),"list reset");}
    void End(){Hr(l->Close(),"list close");ID3D12CommandList*c=l.Get();q->ExecuteCommandLists(1,&c);
        Hr(q->Signal(f.Get(),++n),"completion signal");Wait(f.Get(),n);uploads.clear();}
    ComPtr<ID3D12Resource> Buffer(UINT64 size,D3D12_HEAP_TYPE type){
        D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width=size;rd.Height=rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto state=type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST;
        ComPtr<ID3D12Resource> r;Hr(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(&r)),"buffer");return r;
    }
    ComPtr<ID3D12Resource> Texture(DXGI_FORMAT format,D3D12_RESOURCE_STATES state,bool shared=false){
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=W;rd.Height=H;
        rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Format=format;
        // GL interop textures are simultaneous-access; private canonical inputs are not.
        rd.Flags=shared?D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        ComPtr<ID3D12Resource> r;Hr(d->CreateCommittedResource(&hp,shared?D3D12_HEAP_FLAG_SHARED:D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(&r)),"texture");return r;
    }
    ComPtr<ID3D12Resource> Upload(DXGI_FORMAT fmt,const void*data,UINT bpp){
        auto r=Texture(fmt,D3D12_RESOURCE_STATE_COPY_DEST);auto desc=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;
        d->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&total);auto staging=Buffer(total,D3D12_HEAP_TYPE_UPLOAD);
        void* ptr=nullptr;D3D12_RANGE none{0,0};Hr(staging->Map(0,&none,&ptr),"upload map");
        for(UINT y=0;y<H;++y)std::memcpy(static_cast<BYTE*>(ptr)+fp.Offset+y*fp.Footprint.RowPitch,static_cast<const BYTE*>(data)+y*W*bpp,W*bpp);
        staging->Unmap(0,nullptr);D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=r.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource=staging.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
        l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(l.Get(),r.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        uploads.push_back(staging);return r;
    }
    ComPtr<ID3D12Resource> Import(ID3D12Resource*r){
        auto image=Texture(r->GetDesc().Format,D3D12_RESOURCE_STATE_COPY_DEST);
        Barrier(l.Get(),r,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);l->CopyResource(image.Get(),r);
        Barrier(l.Get(),r,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
        Barrier(l.Get(),image.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);return image;
    }
    std::vector<BYTE> Read(ID3D12Resource*r,D3D12_RESOURCE_STATES state){
        auto desc=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;d->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&total);
        auto b=Buffer(total,D3D12_HEAP_TYPE_READBACK);Begin();Barrier(l.Get(),r,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=b.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
        src.pResource=r;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Barrier(l.Get(),r,D3D12_RESOURCE_STATE_COPY_SOURCE,state);End();
        void* p=nullptr;Hr(b->Map(0,nullptr,&p),"readback map");std::vector<BYTE> v(W*H*4);
        for(UINT y=0;y<H;++y)std::memcpy(v.data()+y*W*4,static_cast<BYTE*>(p)+fp.Offset+y*fp.Footprint.RowPitch,W*4);
        D3D12_RANGE none{0,0};b->Unmap(0,&none);return v;
    }
};
static std::vector<BYTE> Pattern(bool flip=false){std::vector<BYTE> p(W*H*4);
    for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto i=(y*W+x)*4;p[i]=BYTE(x);p[i+1]=BYTE(flip?H-1-y:y);p[i+2]=0;p[i+3]=255;}return p;}
static void Verify(const std::vector<BYTE>&p,bool flip=false){Check(p.size()==W*H*4,"pixel extent");
    for(UINT y=0;y<H;++y)for(UINT x=0;x<W;++x){auto i=(y*W+x)*4;
        if(p[i]!=BYTE(std::min(255u,x+64u))||p[i+1]!=BYTE(flip?H-1-y:y)||p[i+2]!=255||p[i+3]!=255)Fail("returned GPU pixel");}}
// The explicit recorder consumes all four resources, not merely a completion counter.
struct Recorder {
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pso;ComPtr<ID3D12DescriptorHeap> heap;
    bool preparing=false,sealRefused=false;unsigned calls=0;
} rec;
static unsigned __cdecl Prepare(ID3D12GraphicsCommandList*){return 1;}
static unsigned __cdecl Seal(ID3D12GraphicsCommandList*){return rec.sealRefused?0:1;}
static unsigned __cdecl Record(const G::Dispatch*x){
    ++rec.calls;if(rec.preparing)return unsigned(G::Result::Preparing);
    ComPtr<ID3D12Device>d;Hr(x->list->GetDevice(IID_PPV_ARGS(&d)),"recorder device");
    if(!rec.root){
        static const char shader[]=R"(
RWTexture2D<float4> color:register(u0);
Texture2D<float> depth:register(t0);Texture2D<float2> motion:register(t1);Texture2D<float> mask:register(t2);
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){uint w,h;color.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;
float4 c=color[id.xy];float z=depth.Load(int3(id.xy,0));float2 m=motion.Load(int3(id.xy,0));float v=mask.Load(int3(id.xy,0));
color[id.xy]=float4(saturate(c.r+z),saturate(c.g+abs(m.x)/w+abs(m.y)/h),v,c.a);})";
        D3D12_DESCRIPTOR_RANGE ranges[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,1}};
        D3D12_ROOT_PARAMETER rp{};rp.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;rp.DescriptorTable={2,ranges};
        D3D12_ROOT_SIGNATURE_DESC rs{};rs.NumParameters=1;rs.pParameters=&rp;ComPtr<ID3DBlob>b,error,cs;
        Hr(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&b,&error),"root serialize");
        Hr(d->CreateRootSignature(0,b->GetBufferPointer(),b->GetBufferSize(),IID_PPV_ARGS(&rec.root)),"root");
        Hr(D3DCompile(shader,sizeof(shader),"fixture",nullptr,nullptr,"main","cs_5_0",0,0,&cs,&error),"fixture shader");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=rec.root.Get();pd.CS={cs->GetBufferPointer(),cs->GetBufferSize()};
        Hr(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&rec.pso)),"fixture PSO");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=4;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Hr(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rec.heap)),"fixture descriptor heap");
    }
    auto cpu=rec.heap->GetCPUDescriptorHandleForHeapStart();const UINT stride=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R8G8B8A8_UNORM;u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    d->CreateUnorderedAccessView(x->color,nullptr,&u,cpu);
    for(auto*r:{x->depth,x->motion,x->distrust}){cpu.ptr+=stride;D3D12_SHADER_RESOURCE_VIEW_DESC s{};s.Format=r->GetDesc().Format;
        s.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;s.Texture2D.MipLevels=1;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d->CreateShaderResourceView(r,&s,cpu);}
    ID3D12DescriptorHeap*h=rec.heap.Get();x->list->SetDescriptorHeaps(1,&h);x->list->SetComputeRootSignature(rec.root.Get());x->list->SetPipelineState(rec.pso.Get());
    x->list->SetComputeRootDescriptorTable(0,rec.heap->GetGPUDescriptorHandleForHeapStart());x->list->Dispatch(W/8,H/8,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=x->color;x->list->ResourceBarrier(1,&barrier);
    return unsigned(G::Result::Recorded);
}
static void AuthenticationNegatives(Gpu&g){
    namespace H2=nrpg::gpu;
    for(unsigned test=0;test<3;++test){std::wstring name=L"\\\\.\\pipe\\NeuRotic.NRH2.auth-test."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(test);
        Handle server,peer;server.h=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,1000,nullptr);Check(server.h!=INVALID_HANDLE_VALUE,"authentication fixture server");peer.h=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);Check(peer.h!=INVALID_HANDLE_VALUE,"authentication fixture peer");
        H2::PeerPolicy policy;policy.device=g.d;policy.processId=GetCurrentProcessId()+(test==0?1:0);policy.creationTime=H2::io::Creation(GetCurrentProcess())+(test==1?1:0);policy.nonce={11,12};auto luid=g.d->GetAdapterLuid();std::memcpy(&policy.adapterLuid,&luid,8);
        H2::Consumer consumer;consumer.process=[](const P::Descriptor&,const nrpg::CpuMainlineClient::GpuInputs&,bool,nrpg::CpuMainlineClient::GpuOutput&,std::string&)->bool{Fail("authentication must precede consumer entry");};consumer.requiresRestart=[](){return false;};
        if(test==2){H2::Hello hello;hello.nonce={99,98};hello.processId=GetCurrentProcessId();hello.processBits=sizeof(void*)*8;hello.adapterLuid=policy.adapterLuid;Check(H2::io::Write(server.h,H2::Encode(hello),1000),"wrong nonce hello send");}
        H2::PreparedGpuHost host;H2::ManagedHostSpec spec;std::string reason;Check(!host.Run(peer.h,policy,spec,consumer,reason)&&!host.RequiresRestart(),"actual PID/creation/nonce mismatch rejected before GPU reference");
    }
}
int wmain(int argc,wchar_t**argv){
    namespace H2=nrpg::gpu;
    if(argc==3&&std::wstring(argv[1])==L"--gpu-bootstrap"){
        const auto handle=std::stoull(argv[2]);
        return H2::RunGpuHostBootstrap(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(handle)),[](const H2::PeerPolicy&peer,const H2::ManagedHostSpec&spec,std::string&){
            auto client=std::make_shared<nrpg::CpuMainlineClient>(nrpg::CpuMainlineClient::TestRecorder{Prepare,Record,Seal});
            rec.preparing=spec.buildIdentity[1]==1;rec.sealRefused=spec.buildIdentity[1]==2;
            H2::Consumer c;c.capabilities=H2::PreparedExports;
            c.process=[client,luid=peer.adapterLuid](const P::Descriptor&d,const nrpg::CpuMainlineClient::GpuInputs&i,bool derive,nrpg::CpuMainlineClient::GpuOutput&o,std::string&r){if(derive){r="Recorder fixture requires declared guides";return false;}return client->ProcessResident(luid,d,i,o,r);};
            c.requiresRestart=[client](){return client->RequiresRestart();};return c;
        });
    }
    Check(argc>=2,"host executable argument");std::wstring fault=argc>2?argv[2]:L"";Gpu g(fault==L"--warp");AuthenticationNegatives(g);
    H2::ManagedHostSpec spec;spec.executable=std::filesystem::absolute(argv[1]);spec.coreModule=spec.executable;spec.buildIdentity={0x4e524832, fault==L"--preparing"?1ull:fault==L"--seal-refused"?2ull:0ull};
    H2::PeerPolicy policy;policy.device=g.d;auto luid=g.d->GetAdapterLuid();std::memcpy(&policy.adapterLuid,&luid,8);policy.episode={101,102,103,104,105,0};
    if(fault==L"--bridge"){
        H2::GpuRemoteConsumer bridge(spec);DlssNr::NativeGuides::TextureCapture capture{};capture.adapterLuid=policy.adapterLuid;capture.deriveMotion=false;auto&d=capture.description;d.producer=101;d.session=102;d.stream=103;d.device=104;d.generation=105;d.flags=P::Reset|P::HasDistrust|P::ZeroJitterPolicy;d.color=d.depth=d.motion=d.distrust={W,H,0,0,W,H};auto&in=capture.normalized;in.device=g.d;in.lease=std::make_shared<int>(0);auto color=Pattern();std::vector<float>depth(W*H,.25f);std::vector<BYTE>motion(W*H*4,0),mask(W*H,255);g.Begin();in.color=g.Upload(DXGI_FORMAT_R8G8B8A8_UNORM,color.data(),4);in.depth=g.Upload(DXGI_FORMAT_R32_FLOAT,depth.data(),4);in.motion=g.Upload(DXGI_FORMAT_R16G16_FLOAT,motion.data(),4);in.distrust=g.Upload(DXGI_FORMAT_R8_UNORM,mask.data(),1);g.End();
        auto copybackTarget=g.Texture(DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_COPY_DEST);
        for(unsigned frame=1;frame<=3;++frame){d.capture=d.maskCapture=frame;d.previousCapture=frame-1;in.producer=g.f;in.value=g.n;nrpg::CpuMainlineClient::GpuOutput output;std::string reason;Check(bridge.Process(capture,output,reason),reason.c_str());
            if(frame==1){H2::GpuRemoteConsumer competing(spec);nrpg::CpuMainlineClient::GpuOutput refused;Check(!competing.Process(capture,refused,reason)&&!refused.color&&reason.find("Another managed GPU host")!=std::string::npos,"second host cannot bypass producer allocation/ownership bound");}
            g.Begin();Barrier(g.l.Get(),output.color.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);g.l->CopyResource(copybackTarget.Get(),output.color.Get());Barrier(g.l.Get(),output.color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);g.End();
            Verify(g.Read(copybackTarget.Get(),D3D12_RESOURCE_STATE_COPY_DEST));Check(bridge.ReturnAfterCopyback(true,d.capture,d.generation,reason),reason.c_str());}
        std::printf("PASS NRH2 %u-bit local remote consumer bridge, 3 captures/two slots/exact pixels\n",unsigned(sizeof(void*)*8));return 0;
    }
    H2::PreparedGpuClient client;std::string reason;Check(client.Start(spec,policy,reason),reason.c_str());
    {H2::PreparedGpuClient second;Check(!second.Start(spec,policy,reason),"second active session refused");}
    {Handle event;event.h=CreateEventW(nullptr,FALSE,FALSE,nullptr);H2::GpuRegistration invalid;invalid.id=3;invalid.handle=reinterpret_cast<std::uintptr_t>(event.h);invalid.width=W;invalid.height=H;invalid.format=DXGI_FORMAT_R8G8B8A8_UNORM;invalid.mips=invalid.arraySize=invalid.samples=1;invalid.access=GENERIC_ALL;Check(!client.Register(invalid,reason),"non GPU NT object rejected");}
    auto color=Pattern();std::vector<float>depth(W*H,.25f);std::vector<BYTE>motion(W*H*4,0),mask(W*H,255);
    const std::array<DXGI_FORMAT,5> formats{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM};
    std::array<ComPtr<ID3D12Resource>,5> shared;
    for(unsigned i=0;i<5;++i){Handle h;H2::GpuRegistration reg;reg.id=i+3;reg.role=static_cast<H2::Role>(i+1);reg.width=W;reg.height=H;reg.format=formats[i];reg.mips=reg.arraySize=reg.samples=1;reg.access=GENERIC_ALL;
        if(fault==L"--host-created")reg.direction=H2::Direction::HostToProducer;
        else {shared[i]=g.Texture(formats[i],D3D12_RESOURCE_STATE_COMMON,true);Hr(g.d->CreateSharedHandle(shared[i].Get(),nullptr,GENERIC_ALL,nullptr,&h.h),"producer shared handle");reg.handle=reinterpret_cast<std::uintptr_t>(h.h);}
        Check(client.Register(reg,reason),reason.c_str());if(fault==L"--host-created")shared[i]=client.Resource(reg.id);}
    g.Begin();const void*data[]{color.data(),depth.data(),motion.data(),mask.data()};const unsigned bpp[]{4,4,4,1};std::array<ComPtr<ID3D12Resource>,4> sources;
    for(unsigned i=0;i<4;++i){sources[i]=g.Upload(formats[i],data[i],bpp[i]);Barrier(g.l.Get(),sources[i].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);Barrier(g.l.Get(),shared[i].Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);g.l->CopyResource(shared[i].Get(),sources[i].Get());Barrier(g.l.Get(),shared[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);}g.End();
    H2::GpuFrameRequest frame;frame.ticket={policy.episode,0,3,1,1,1,client.SessionNonce()};frame.ticket.identity.capture=1;auto&d=frame.description;d.producer=101;d.session=102;d.stream=103;d.device=104;d.generation=105;d.capture=d.maskCapture=1;d.flags=P::Reset|P::HasDistrust|P::ZeroJitterPolicy;d.color=d.depth=d.motion=d.distrust={W,H,0,0,W,H};frame.registrations={3,4,5,6,7};
    if(fault!=L"--pending")Hr(g.q->Signal(client.ProducerFence(),1),"producer signal");
    H2::GpuFrameResult result;const bool processed=client.Process(frame,result,reason);
    if(fault==L"--pending"||fault==L"--seal-refused"){Check(!processed&&client.RequiresRestart(),"uncertain generation quarantined");std::puts("PASS NRH2 submitted fault quarantined without output/restart");return 0;}
    Check(processed,reason.c_str());if(fault==L"--preparing"){Check(!(result.facts&P::Ipc::OutputValid)&&result.outcome!=P::Ipc::Outcome::CompletedSuccess,"preparing never becomes output");Check(client.Drain(reason),reason.c_str());std::puts("PASS NRH2 preparing produces no output");return 0;}
    Check(result.facts==15&&result.outputCapture==1,"exact completed output receipt");auto pixels=g.Read(shared[4].Get(),D3D12_RESOURCE_STATE_COMMON);Verify(pixels);
    H2::CopybackReceipt receipt{frame.ticket,g.f,g.n,true};auto bad=receipt;bad.restored=false;Check(!client.Release(bad,reason),"missing local restore proof rejected");Check(!client.Process(frame,result,reason),"leased slot refuses second submission");Check(client.Release(receipt,reason),reason.c_str());Check(!client.Process(frame,result,reason),"capture/timeline replay rejected");
    for(unsigned capture=2;capture<=3;++capture){frame.ticket.identity.capture=capture;frame.ticket.producerValue=frame.ticket.outputValue=frame.ticket.sequence=capture;d.capture=d.maskCapture=capture;d.previousCapture=capture-1;Hr(g.q->Signal(client.ProducerFence(),capture),"next producer signal");Check(client.Process(frame,result,reason),reason.c_str());Verify(g.Read(shared[4].Get(),D3D12_RESOURCE_STATE_COMMON));receipt={frame.ticket,g.f,g.n,true};Check(client.Release(receipt,reason),reason.c_str());}
    Check(client.Drain(reason),reason.c_str());
    std::printf("PASS NRH2 %u-bit producer -> x64 ProcessResident shader recorder -> exact GPU copyback pixels; no private-model claim\n",unsigned(sizeof(void*)*8));return 0;
}
