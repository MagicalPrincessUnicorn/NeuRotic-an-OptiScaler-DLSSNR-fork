// Executable Windows x64 adapter fixture, adapted from the returned source proposal.
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
#include "CpuMainlineClient.h"
#include <dlssnr/connections/D3D10Connection.h>
#include <dlssnr/connections/GpuBudget.h>
#include <dlssnr/connections/PreparedInputComposition.h>
#include <dlssnr/connections/GlConnection.h>
#include <gl/GL.h>
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
        rd.Flags=shared?D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS:D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
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
    std::vector<BYTE> Read(ID3D12Resource*r,D3D12_RESOURCE_STATES state,UINT bpp=4){
        auto desc=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;d->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&total);
        auto b=Buffer(total,D3D12_HEAP_TYPE_READBACK);Begin();Barrier(l.Get(),r,state,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=b.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;
        src.pResource=r;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Barrier(l.Get(),r,D3D12_RESOURCE_STATE_COPY_SOURCE,state);End();
        void* p=nullptr;Hr(b->Map(0,nullptr,&p),"readback map");std::vector<BYTE> v(W*H*bpp);
        for(UINT y=0;y<H;++y)std::memcpy(v.data()+y*W*bpp,static_cast<BYTE*>(p)+fp.Offset+y*fp.Footprint.RowPitch,W*bpp);
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

#include <dlssnr/NativeGuideProcessor.h>
namespace C=DlssNr::Connections;
static P::Descriptor Describe(P::C::GraphicsApi api){P::Descriptor d{};d.producer=17;d.session=1;d.stream=1;d.device=7;d.generation=1;d.capture=1;d.maskCapture=1;d.sourceApi=api;d.flags=P::Reset|P::ZeroJitterPolicy;
    d.color=d.depth=d.motion=d.distrust={W,H,0,0,W,H};return d;}
static void Equal(const std::vector<BYTE>& a,const std::vector<BYTE>& b,const char* msg){Check(a==b,msg);}
static nrpg::CpuMainlineClient::GpuOutput Output(Gpu& g,const std::vector<BYTE>& pixels){
    nrpg::CpuMainlineClient::GpuOutput out;g.Begin();out.color=g.Upload(DXGI_FORMAT_R8G8B8A8_UNORM,pixels.data(),4);
    Barrier(g.l.Get(),out.color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);g.End();
    out.completion={g.f,g.n};out.lease=std::make_shared<int>(1);return out;
}
static void Canonical(Gpu& g,const C::PendingFrame& p,const std::vector<BYTE>& pixels){
    Equal(g.Read(p.textures.normalized.color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),pixels,"canonical color bytes");
    auto depth=g.Read(p.textures.normalized.depth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for(size_t n=0;n<depth.size();n+=4){float z=0;std::memcpy(&z,depth.data()+n,4);Check(z>=.24998f&&z<=.25002f,"canonical depth value");}
}
static void Run12ColorFormats(Gpu& g){
    C::D3D12Connection bridge(g.d.Get());auto pixels=Pattern();std::vector<float> z(W*H,.25f);
    for(size_t n=0;n<pixels.size();n+=4){pixels[n+2]=BYTE(n/4);pixels[n+3]=BYTE(((n/4)%4)*85);}
    // Reuse one connection across output format changes: the conversion PSO must
    // match each destination, including switching back to a previously used format.
    for(auto fmt:{DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM}){
        auto pack=[&](const std::vector<BYTE>& rgba){auto bytes=rgba;
            for(size_t n=0;n<bytes.size();n+=4){if(fmt==DXGI_FORMAT_B8G8R8A8_UNORM)std::swap(bytes[n],bytes[n+2]);
                else {auto ten=[](BYTE c){return (UINT(c)*1023+127)/255;};UINT p=ten(rgba[n])|(ten(rgba[n+1])<<10)|(ten(rgba[n+2])<<20)|(UINT((rgba[n+3]*3+127)/255)<<30);std::memcpy(bytes.data()+n,&p,4);}}
            return bytes;};
        auto encoded=pack(pixels);D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=W;desc.Height=H;
        desc.MipLevels=desc.DepthOrArraySize=desc.SampleDesc.Count=1;desc.Format=fmt;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> color;
        Hr(g.d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&color)),"converted color target");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;g.d->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&total);
        auto staging=g.Buffer(total,D3D12_HEAP_TYPE_UPLOAD);void* ptr=nullptr;Hr(staging->Map(0,nullptr,&ptr),"converted upload map");
        for(UINT y=0;y<H;++y)std::memcpy(static_cast<BYTE*>(ptr)+fp.Offset+y*fp.Footprint.RowPitch,encoded.data()+y*W*4,W*4);staging->Unmap(0,nullptr);
        g.Begin();D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=color.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.pResource=staging.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
        g.l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(g.l.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);
        auto depth=g.Upload(DXGI_FORMAT_R32_FLOAT,z.data(),4);g.End();
        C::D3D12ObservedFrame f{};f.description=Describe(P::C::GraphicsApi::D3D12);f.color=color;f.depth=depth;f.sourceQueue=g.q;f.producer=g.f;f.producerValue=g.n;
        f.colorState=D3D12_RESOURCE_STATE_PRESENT;f.depthState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;f.statesKnown=true;f.lease=std::make_shared<int>(1);
        C::PendingFrame pending;std::string reason;
        auto invalid=f;desc.Flags=D3D12_RESOURCE_FLAG_NONE;
        Hr(g.d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PRESENT,nullptr,IID_PPV_ARGS(&invalid.outputTarget)),"non-renderable destination");
        Check(!bridge.Capture(invalid,pending,reason)&&!pending.ownerLease,"converted target requires render target support");
        invalid=f;invalid.description.colorDomain=P::C::ColorDomain::DisplayLinear;
        Check(!bridge.Capture(invalid,pending,reason)&&!pending.ownerLease,"linear color domain remains refused");
        Equal(g.Read(color.Get(),D3D12_RESOURCE_STATE_PRESENT),encoded,"refused captures preserve source pixels");
        if(!bridge.Capture(f,pending,reason)){std::fprintf(stderr,"capture format=%u: %s\n",unsigned(fmt),reason.c_str());Fail("SDR converted capture");}
        Canonical(g,pending,pixels);auto changed=pixels;for(size_t n=0;n<changed.size();n+=4){changed[n]=255-changed[n];changed[n+2]=173;}
        auto output=Output(g,changed);C::CopybackReceipt receipt;Check(bridge.Finish(pending,output,receipt,reason),"converted copyback");
        Check(receipt.completed,"converted receipt");Equal(g.Read(color.Get(),D3D12_RESOURCE_STATE_PRESENT),pack(changed),"converted returned pixels");
        std::printf("PASS D3D12 format %u capture/depth/conversion/copyback\n",unsigned(fmt));
    }
}
static void Run12(Gpu& g,DXGI_FORMAT depthFormat=DXGI_FORMAT_R32_FLOAT){
    C::D3D12Connection bridge(g.d.Get());auto pixels=Pattern();std::vector<float> z(W*H,.25f);g.Begin();
    auto color=g.Upload(DXGI_FORMAT_R8G8B8A8_UNORM,pixels.data(),4),depth=g.Upload(DXGI_FORMAT_R32_FLOAT,z.data(),4);g.End();
    if(depthFormat!=DXGI_FORMAT_R32_FLOAT){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=W;desc.Height=H;desc.MipLevels=desc.DepthOrArraySize=desc.SampleDesc.Count=1;desc.Format=depthFormat;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL|D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_CLEAR_VALUE clear{};clear.Format=depthFormat;clear.DepthStencil.Depth=.25f;
        Hr(g.d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth)),"D3D12 typed depth");
        D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;ComPtr<ID3D12DescriptorHeap> heap;Hr(g.d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"depth heap");
        auto view=heap->GetCPUDescriptorHandleForHeapStart();g.d->CreateDepthStencilView(depth.Get(),nullptr,view);g.Begin();g.l->ClearDepthStencilView(view,D3D12_CLEAR_FLAG_DEPTH,.25f,0,0,nullptr);g.End();}
    C::D3D12ObservedFrame f{};f.description=Describe(P::C::GraphicsApi::D3D12);f.color=color;f.depth=depth;f.sourceQueue=g.q;f.producer=g.f;f.producerValue=g.n;
    f.colorState=f.depthState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;f.lease=std::make_shared<int>(1);
    if(depthFormat!=DXGI_FORMAT_R32_FLOAT)f.depthState=D3D12_RESOURCE_STATE_DEPTH_WRITE;
    C::PendingFrame p;std::string why;Check(!bridge.Capture(f,p,why)&&!bridge.RequiresRestart(),"unknown states refused");f.statesKnown=true;
    auto over=f;over.description.color.width=4097;Check(!bridge.Capture(over,p,why)&&!bridge.RequiresRestart(),"oversized raster refused before allocation");
    {auto budget=C::GpuBudget::Reserve(C::GpuBudget::Limit()-C::GpuBudget::Bytes());Check(bool(budget),"reserve aggregate budget");Check(!bridge.Capture(f,p,why)&&why.find("budget")!=std::string::npos&&!bridge.RequiresRestart(),"aggregate GPU budget refuses capture");}
    f.producerValue=g.n+1;Check(!bridge.Capture(f,p,why)&&!bridge.RequiresRestart(),"pending producer refused");f.producerValue=g.n;
    ComPtr<ID3D12Device> foreign;Hr(D3D12CreateDevice(g.adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&foreign)),"second logical device");C::D3D12Connection wrong(foreign.Get());
    ComPtr<IUnknown> identity,otherIdentity;Hr(g.d.As(&identity),"device identity");Hr(foreign.As(&otherIdentity),"second identity");
    if(identity!=otherIdentity)Check(!wrong.Capture(f,p,why)&&!wrong.RequiresRestart(),"foreign logical device refused");
    else std::puts("SKIP foreign-device negative: D3D12CreateDevice returned the same COM identity");
    if(!bridge.Capture(f,p,why))Fail(why.c_str());Canonical(g,p,pixels);
    C::PendingFrame second;Check(!bridge.Capture(f,second,why),"one pending capture bound");
    auto history=p.textures.normalized.lease;auto replay=p;auto changed=pixels;for(size_t n=0;n<changed.size();n+=4)changed[n+2]=255;
    auto output=Output(g,changed);auto weak=std::weak_ptr<void>(output.lease);C::CopybackReceipt receipt;
    auto invalid=output;invalid.completion.value=g.n+1;Check(!bridge.Finish(p,invalid,receipt,why)&&!receipt.completed&&invalid.lease,"uncompleted output not copied");invalid={};
    if(!bridge.Finish(p,output,receipt,why))Fail(why.c_str());Check(receipt.completed&&receipt.capture==1&&weak.expired(),"output lease retired only on completion");
    Equal(g.Read(color.Get(),f.colorState),changed,"D3D12 source copyback");Check(!bridge.Finish(replay,output,receipt,why),"replayed ticket refused");replay={};
    f.description.capture=2;if(!bridge.Capture(f,p,why))Fail(why.c_str());auto history2=p.textures.normalized.lease;Check(bridge.Cancel(p,why),"cancel completed capture");
    f.description.capture=3;Check(!bridge.Capture(f,p,why)&&!bridge.RequiresRestart(),"both history pairs leased");history.reset();
    if(!bridge.Capture(f,p,why))Fail(why.c_str());Check(bridge.Cancel(p,why),"released canonical pair reusable");
    history2.reset();std::vector<float> movement(W*H*2),mask(W*H,.5f);for(size_t n=0;n<movement.size();n+=2){movement[n]=2;movement[n+1]=-3;}
    g.Begin();f.motion=g.Upload(DXGI_FORMAT_R32G32_FLOAT,movement.data(),8);f.distrust=g.Upload(DXGI_FORMAT_R32_FLOAT,mask.data(),4);g.End();
    f.motionState=f.distrustState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;f.producerValue=g.n;f.deriveMotion=false;f.invertMotionY=true;
    f.description.capture=4;f.description.maskCapture=4;f.description.flags|=P::HasDistrust;
    if(!bridge.Capture(f,p,why))Fail(why.c_str());Check(!p.textures.deriveMotion,"supplied guides retain external mode");
    auto vectors=g.Read(p.textures.normalized.motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for(size_t n=0;n<vectors.size();n+=4){std::uint16_t x=0,y=0;std::memcpy(&x,vectors.data()+n,2);std::memcpy(&y,vectors.data()+n+2,2);Check(x==0x4000&&y==0x4200,"RG32 to RG16 guide conversion and motion Y");}
    auto distrust=g.Read(p.textures.normalized.distrust.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,1);
    for(auto v:distrust)Check(v==128,"R32 to R8 distrust conversion");Check(bridge.Cancel(p,why),"supplied capture cancel");
    f.description.motionUnits=P::C::MotionUnits::ActiveRasterUV;Check(!bridge.Capture(f,p,why)&&!bridge.RequiresRestart(),"UV supplied motion refused");
    f.description.motionUnits=P::C::MotionUnits::Pixels;f.description.capture=f.description.maskCapture=5;
    std::fill(movement.begin(),movement.end(),0.f);std::fill(mask.begin(),mask.end(),1.f);
    g.Begin();f.motion=g.Upload(DXGI_FORMAT_R32G32_FLOAT,movement.data(),8);f.distrust=g.Upload(DXGI_FORMAT_R32_FLOAT,mask.data(),4);g.End();f.producerValue=g.n;
    if(!bridge.Capture(f,p,why))Fail(why.c_str());DlssNr::NativeGuides::Processor processor({Prepare,Record,Seal},g.d.Get());
    nrpg::CpuMainlineClient::GpuOutput model;
    Check(processor.ProcessTextures(p.textures,model,why)==DlssNr::NativeGuides::Outcome::Delivered,why.c_str());
    if(!bridge.Finish(p,model,receipt,why))Fail(why.c_str());Verify(g.Read(color.Get(),f.colorState));
    std::puts("PASS D3D12 production Capture -> ProcessTextures -> ProcessResident test recorder -> exact Finish pixels; pending, replay, slot pressure");
}
static void VerifyMotion(Gpu& g,const C::PendingFrame& p){auto bytes=g.Read(p.textures.normalized.motion.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for(size_t n=0;n<bytes.size();n+=4){std::uint16_t x=0,y=0;std::memcpy(&x,bytes.data()+n,2);std::memcpy(&y,bytes.data()+n+2,2);Check(x==0x4000&&y==0xc200,"supplied RG32 motion preserved through shared RG16 normalization");}}
static void Run11(Gpu& g,bool bgra,DXGI_FORMAT depthFormat=DXGI_FORMAT_R32_FLOAT){
    ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;D3D_FEATURE_LEVEL level;
    Hr(D3D11CreateDevice(g.adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c),"D3D11 source device");
    auto pixels=Pattern(),raw=pixels;if(bgra)for(size_t n=0;n<raw.size();n+=4)std::swap(raw[n],raw[n+2]);std::vector<float>z(W*H,.25f);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=W;desc.Height=H;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    desc.Format=bgra?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;D3D11_SUBRESOURCE_DATA data{raw.data(),W*4,0};ComPtr<ID3D11Texture2D>color,depth;
    Hr(d->CreateTexture2D(&desc,&data,&color),"D3D11 source color");desc.Format=depthFormat;data.pSysMem=z.data();
    if(depthFormat==DXGI_FORMAT_R32_FLOAT)Hr(d->CreateTexture2D(&desc,&data,&depth),"D3D11 source depth");
    else{desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;Hr(d->CreateTexture2D(&desc,nullptr,&depth),"typed depth texture");ComPtr<ID3D11DepthStencilView> view;
        Hr(d->CreateDepthStencilView(depth.Get(),nullptr,&view),"typed depth view");c->ClearDepthStencilView(view.Get(),D3D11_CLEAR_DEPTH,.25f,0);}
    C::D3D11Connection bridge(g.d.Get());C::D3D11ObservedFrame f{};f.description=Describe(P::C::GraphicsApi::D3D11);f.context=c;f.color=color;f.depth=depth;f.lease=std::make_shared<int>(1);
    C::PendingFrame p;std::string why;ComPtr<ID3D11DeviceContext> deferred;Hr(d->CreateDeferredContext(0,&deferred),"deferred context");f.context=deferred;
    Check(!bridge.Capture(f,p,why)&&!bridge.RequiresRestart(),"deferred context rejected before references");f.context=c;
    D3D11_VIEWPORT vp{3,5,17,23,.1f,.9f};c->RSSetViewports(1,&vp);
    if(!bridge.Capture(f,p,why))Fail(why.c_str());Canonical(g,p,pixels);auto changed=pixels;for(size_t n=0;n<changed.size();n+=4)changed[n+2]=255;
    auto output=Output(g,changed);C::CopybackReceipt receipt;if(!bridge.Finish(p,output,receipt,why))Fail(why.c_str());Check(receipt.completed&&!output.lease,"D3D11 final copyback receipt");
    D3D11_VIEWPORT actual{};UINT count=1;c->RSGetViewports(&count,&actual);Check(count==1&&!std::memcmp(&vp,&actual,sizeof(vp)),"D3D11 viewport state retained");
    color->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=desc.MiscFlags=0;ComPtr<ID3D11Texture2D>read;
    Hr(d->CreateTexture2D(&desc,nullptr,&read),"D3D11 readback");c->CopyResource(read.Get(),color.Get());D3D11_MAPPED_SUBRESOURCE map{};Hr(c->Map(read.Get(),0,D3D11_MAP_READ,0,&map),"D3D11 readback map");
    std::vector<BYTE> got(W*H*4);for(UINT y=0;y<H;++y)std::memcpy(got.data()+y*W*4,static_cast<BYTE*>(map.pData)+y*map.RowPitch,W*4);c->Unmap(read.Get(),0);
    if(bgra)for(size_t n=0;n<got.size();n+=4)std::swap(got[n],got[n+2]);Equal(got,changed,"D3D11 output pixels");
    std::vector<float> motion(W*H*2,0.f),mask(W*H,1.f);for(size_t n=0;n<motion.size();n+=2){motion[n]=2;motion[n+1]=-3;}color->GetDesc(&desc);desc.Format=DXGI_FORMAT_R32G32_FLOAT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    data={motion.data(),W*8,0};Hr(d->CreateTexture2D(&desc,&data,&f.motion),"D3D11 supplied motion");desc.Format=DXGI_FORMAT_R32_FLOAT;data={mask.data(),W*4,0};Hr(d->CreateTexture2D(&desc,&data,&f.distrust),"D3D11 supplied distrust");
    f.deriveMotion=false;f.description.capture=f.description.maskCapture=2;f.description.flags|=P::HasDistrust;
    if(!bridge.Capture(f,p,why))Fail(why.c_str());VerifyMotion(g,p);Check(bridge.Cancel(p,why),"retire conversion test capture");std::fill(motion.begin(),motion.end(),0.f);c->UpdateSubresource(f.motion.Get(),0,nullptr,motion.data(),W*8,0);f.description.capture=f.description.maskCapture=3;
    if(!bridge.Capture(f,p,why))Fail(why.c_str());DlssNr::NativeGuides::Processor processor({Prepare,Record,Seal},g.d.Get());nrpg::CpuMainlineClient::GpuOutput model;
    Check(processor.ProcessTextures(p.textures,model,why)==DlssNr::NativeGuides::Outcome::Delivered,why.c_str());if(!bridge.Finish(p,model,receipt,why))Fail(why.c_str());
    c->CopyResource(read.Get(),color.Get());Hr(c->Map(read.Get(),0,D3D11_MAP_READ,0,&map),"D3D11 fulltuple readback");for(UINT y=0;y<H;++y)std::memcpy(got.data()+y*W*4,static_cast<BYTE*>(map.pData)+y*map.RowPitch,W*4);c->Unmap(read.Get(),0);
    if(bgra)for(size_t n=0;n<got.size();n+=4)std::swap(got[n],got[n+2]);Verify(got);
    std::puts(bgra?"PASS D3D11 BGRA capture, fulltuple processor recorder, exact copyback":"PASS D3D11 RGBA/depth capture, fulltuple processor recorder, exact copyback");
}
static void Run10(Gpu& g,bool d24=false,bool base=false){
    ComPtr<ID3D10Device> d;
    if(base)Hr(D3D10CreateDevice(g.adapter.Get(),D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_SDK_VERSION,&d),"base D3D10 source device");
    else{ComPtr<ID3D10Device1> device1;Hr(D3D10CreateDevice1(g.adapter.Get(),D3D10_DRIVER_TYPE_HARDWARE,nullptr,0,D3D10_FEATURE_LEVEL_10_1,D3D10_1_SDK_VERSION,&device1),"D3D10 source device");d=device1;}
    auto pixels=Pattern();std::vector<float> z(W*H,.25f);D3D10_TEXTURE2D_DESC desc{};desc.Width=W;desc.Height=H;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D10_USAGE_DEFAULT;desc.BindFlags=D3D10_BIND_SHADER_RESOURCE|D3D10_BIND_RENDER_TARGET;
    D3D10_SUBRESOURCE_DATA data{pixels.data(),W*4,0};ComPtr<ID3D10Texture2D> color,depth;Hr(d->CreateTexture2D(&desc,&data,&color),"D3D10 source color");
    desc.Format=DXGI_FORMAT_R32_FLOAT;data.pSysMem=z.data();
    if(!d24)Hr(d->CreateTexture2D(&desc,&data,&depth),"D3D10 source depth");
    else{desc.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;desc.BindFlags=D3D10_BIND_DEPTH_STENCIL;Hr(d->CreateTexture2D(&desc,nullptr,&depth),"D3D10 typed D24 depth");
        ComPtr<ID3D10DepthStencilView> view;Hr(d->CreateDepthStencilView(depth.Get(),nullptr,&view),"D3D10 D24 view");d->ClearDepthStencilView(view.Get(),D3D10_CLEAR_DEPTH,.25f,0);}
    C::D3D10Connection bridge(g.d.Get());C::D3D10ObservedFrame f{};f.device=d;f.description=Describe(P::C::GraphicsApi::D3D10);f.color=color;f.depth=depth;f.lease=std::make_shared<int>(1);
    C::PendingFrame p;std::string why;if(!bridge.Capture(f,p,why))Fail(why.c_str());Canonical(g,p,pixels);
    auto changed=pixels;for(size_t n=0;n<changed.size();n+=4)changed[n+2]=255;auto output=Output(g,changed);C::CopybackReceipt receipt;
    if(!bridge.Finish(p,output,receipt,why))Fail(why.c_str());Check(receipt.completed&&!output.lease,"D3D10 final game event receipt");
    color->GetDesc(&desc);desc.Usage=D3D10_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D10_CPU_ACCESS_READ;
    ComPtr<ID3D10Texture2D> read;Hr(d->CreateTexture2D(&desc,nullptr,&read),"D3D10 readback");d->CopyResource(read.Get(),color.Get());D3D10_MAPPED_TEXTURE2D map{};Hr(read->Map(0,D3D10_MAP_READ,0,&map),"D3D10 readback map");
    std::vector<BYTE> got(W*H*4);for(UINT y=0;y<H;++y)std::memcpy(got.data()+y*W*4,static_cast<BYTE*>(map.pData)+y*map.RowPitch,W*4);read->Unmap(0);Equal(got,changed,"D3D10 output pixels");
    std::vector<float> motion(W*H*2,0.f),mask(W*H,1.f);for(size_t n=0;n<motion.size();n+=2){motion[n]=2;motion[n+1]=-3;}color->GetDesc(&desc);desc.Format=DXGI_FORMAT_R32G32_FLOAT;desc.BindFlags=D3D10_BIND_SHADER_RESOURCE;
    data={motion.data(),W*8,0};Hr(d->CreateTexture2D(&desc,&data,&f.motion),"D3D10 supplied motion");desc.Format=DXGI_FORMAT_R32_FLOAT;data={mask.data(),W*4,0};Hr(d->CreateTexture2D(&desc,&data,&f.distrust),"D3D10 supplied distrust");
    f.deriveMotion=false;f.description.capture=f.description.maskCapture=2;f.description.flags|=P::HasDistrust;
    if(!bridge.Capture(f,p,why))Fail(why.c_str());VerifyMotion(g,p);Check(bridge.Cancel(p,why),"retire conversion test capture");std::fill(motion.begin(),motion.end(),0.f);d->UpdateSubresource(f.motion.Get(),0,nullptr,motion.data(),W*8,0);f.description.capture=f.description.maskCapture=3;
    if(!bridge.Capture(f,p,why))Fail(why.c_str());DlssNr::NativeGuides::Processor processor({Prepare,Record,Seal},g.d.Get());nrpg::CpuMainlineClient::GpuOutput model;
    Check(processor.ProcessTextures(p.textures,model,why)==DlssNr::NativeGuides::Outcome::Delivered,why.c_str());if(!bridge.Finish(p,model,receipt,why))Fail(why.c_str());
    d->CopyResource(read.Get(),color.Get());Hr(read->Map(0,D3D10_MAP_READ,0,&map),"D3D10 fulltuple readback");for(UINT y=0;y<H;++y)std::memcpy(got.data()+y*W*4,static_cast<BYTE*>(map.pData)+y*map.RowPitch,W*4);read->Unmap(0);Verify(got);
    std::puts("PASS D3D10 event relay, fulltuple processor recorder, exact copyback");
}
static void RunGl(Gpu& g){
    C::GlImportOwnership ownership;Check(!ownership.GlOwns()&&!ownership.ReleaseAll(),"unprobed GL imports cannot be released");ownership.ProbeCompleted();
    for(unsigned n=0;n<3;++n){Check(ownership.ReleaseAll()&&ownership.HostOwns(),"capture releases whole import set");Check(!ownership.ReleaseAll()&&!ownership.GlOwns(),"next capture blocked before full reacquire");Check(ownership.ReacquireAll()&&ownership.GlOwns(),"Finish/Cancel/clean refusal reacquire whole import set");Check(!ownership.ReacquireAll(),"duplicate reacquire refused");}
    ownership.ReleaseAll();ownership.Unknown();Check(!ownership.GlOwns()&&!ownership.ReleaseAll()&&!ownership.ReacquireAll(),"uncertain transfer forbids reuse");
    std::puts("PASS production GL ownership guard: Finish, Cancel, clean refusal, missing reacquire and uncertainty");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NeuRoticAdapterTest";wc.style=CS_OWNDC;RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"Adapter test",WS_OVERLAPPEDWINDOW,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);Check(window!=nullptr,"hidden GL fixture window");HDC dc=GetDC(window);
    PIXELFORMATDESCRIPTOR pfd{};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;
    int pf=ChoosePixelFormat(dc,&pfd);Check(pf&&SetPixelFormat(dc,pf,&pfd),"GL pixel format");HGLRC context=wglCreateContext(dc);Check(context&&wglMakeCurrent(dc,context),"GL source context");
    GLuint names[2]{};glGenTextures(2,names);auto pixels=Pattern(true);std::vector<float>depth(W*H,.25f);
    glBindTexture(GL_TEXTURE_2D,names[0]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    glBindTexture(GL_TEXTURE_2D,names[1]);glTexImage2D(GL_TEXTURE_2D,0,0x822E,W,H,0,GL_RED,GL_FLOAT,depth.data());while(glGetError()!=GL_NO_ERROR){}
    auto lease=std::make_shared<C::GlContextLease>(context,C::GlContextIdentity{1,1,GetCurrentThreadId()});C::GlObservedFrame f{};f.context=lease;f.lease=lease;f.description=Describe(P::C::GraphicsApi::OpenGL);
    f.description.rasterOrigin=P::C::RasterOrigin::BottomLeft;f.color=(std::uint64_t(GL_TEXTURE_2D)<<40)|names[0];f.depth=(std::uint64_t(GL_TEXTURE_2D)<<40)|names[1];f.depthIsR32Float=true;
    {C::GlConnection bridge(g.d.Get());C::PendingFrame pending;std::string why;
        if(!bridge.Capture(f,pending,why)){Check(!bridge.RequiresRestart(),"GL unavailable capability clean refusal");std::printf("SKIP GL positive: %s; verified clean capability refusal\n",why.c_str());}
        else{Canonical(g,pending,Pattern());auto changed=Pattern();for(size_t n=0;n<changed.size();n+=4)changed[n+2]=255;auto out=Output(g,changed);C::CopybackReceipt receipt;
            if(!bridge.Finish(pending,out,receipt,why))Fail(why.c_str());Check(receipt.completed,"GL completed copyback");for(size_t n=0;n<pixels.size();n+=4)pixels[n+2]=255;std::puts("PASS GL production completed pixel roundtrip");}
        glBindTexture(GL_TEXTURE_2D,names[0]);std::vector<BYTE> actual(W*H*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());Equal(actual,pixels,"GL source preserved or exact returned pixels");}
    lease->Invalidate();glDeleteTextures(2,names);wglMakeCurrent(nullptr,nullptr);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);
}
static void RunMixed12(Gpu& g){
    namespace M=C::Mixed;
    C::D3D12Connection bridge(g.d.Get());auto pixels=Pattern();
    std::vector<float> z(W*H,.25f),motion(W*H*2,0.f),mask(W*H,1.f);g.Begin();
    auto destinationPixels=pixels;for(size_t n=0;n<destinationPixels.size();n+=4)destinationPixels[n]^=0xff;
    auto color=g.Upload(DXGI_FORMAT_R8G8B8A8_UNORM,destinationPixels.data(),4);
    auto nativePixels=g.Upload(DXGI_FORMAT_R8G8B8A8_UNORM,pixels.data(),4);
    auto depth=g.Upload(DXGI_FORMAT_R32_FLOAT,z.data(),4);
    auto mv=g.Upload(DXGI_FORMAT_R32G32_FLOAT,motion.data(),8);
    auto distrust=g.Upload(DXGI_FORMAT_R32_FLOAT,mask.data(),4);g.End();
    C::D3D12ObservedFrame anchor{};anchor.description=Describe(P::C::GraphicsApi::D3D12);
    anchor.description.capture=2;anchor.description.previousCapture=1;anchor.description.maskCapture=2;
    anchor.description.flags|=P::HasDistrust;anchor.color=color;anchor.outputTarget=color;
    anchor.sourceQueue=g.q;anchor.producer=g.f;anchor.producerValue=g.n;
    anchor.colorState=anchor.outputState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    anchor.statesKnown=true;anchor.lease=std::make_shared<int>(1);
    M::Anchor identity{{1,1,9,1,7,2,2},M::Stage::Present,C::Source::Native,W,H,1};
    C::D3D12SignalOffer a{},b{};a.info.signal=M::Signal::Motion;a.info.source=C::Source::Native;
    a.info.frame=identity.frame;a.info.stage=identity.stage;a.info.previous=1;a.info.providerGeneration=1;
    a.info.width=W;a.info.height=H;a.info.observed=true;a.info.conventionsKnown=true;
    a.frame=anchor;a.frame.motion=mv;a.frame.distrust=distrust;a.frame.deriveMotion=false;
    a.frame.motionState=a.frame.distrustState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    b=a;b.info.signal=M::Signal::Depth;b.info.source=C::Source::ReShade;b.frame.depth=depth;
    b.frame.depthState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    b.frame.lease=std::make_shared<int>(2);std::weak_ptr<void> depthLifetime=b.frame.lease;
    auto nativeColor=a;nativeColor.info.signal=M::Signal::Color;
    nativeColor.frame=anchor;nativeColor.frame.color=nativePixels;
    nativeColor.frame.lease=std::make_shared<int>(3);std::weak_ptr<void> colorLifetime=nativeColor.frame.lease;
    C::D3D12SignalOffer offers[]{a,b,nativeColor};C::PendingFrame pending;M::Selection selected;std::string why;
    // A physically unusable preferred color must not hide another valid offer.
    {
        auto badColor=nativeColor;badColor.frame.color=g.Texture(DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        auto alternate=nativeColor;alternate.info.source=C::Source::ReShade;
        C::D3D12SignalOffer candidates[]{a,b,badColor,alternate};M::Policy prefer;prefer.color=C::Source::Native;
        Check(bridge.CaptureMixed(anchor,identity,candidates,prefer,pending,selected,why),"physically incompatible preferred color can fall back");
        Check(selected.color==3&&selected.colorReason==M::Reason::PreferenceFallback,"fallback names actual compatible color resource");
        Canonical(g,pending,pixels);Check(bridge.Cancel(pending,why),"fallback capture retires");
        prefer.strict=true;Check(!bridge.CaptureMixed(anchor,identity,candidates,prefer,pending,selected,why),"strict incompatible color refuses even with alternate");
    }
    auto savedColor=offers[2];M::Policy colorPolicy;colorPolicy.color=C::Source::External;colorPolicy.strict=true;
    Check(!bridge.CaptureMixed(anchor,identity,offers,colorPolicy,pending,selected,why)&&!pending.ownerLease,"strict unavailable color refuses before allocation");
    colorPolicy.color=C::Source::None;colorPolicy.strict=true;
    Check(!bridge.CaptureMixed(anchor,identity,offers,colorPolicy,pending,selected,why)&&!pending.ownerLease,"strict None color withholds consumer");
    offers[2].frame.producer.Reset();
    Check(!bridge.CaptureMixed(anchor,identity,offers,{},pending,selected,why),"color requires actual completed producer fence");
    offers[2]=savedColor;offers[2].frame.description.colorDomain=P::C::ColorDomain::SceneLinear;
    Check(!bridge.CaptureMixed(anchor,identity,offers,{},pending,selected,why),"unsupported HDR cannot be relabeled SDR");
    offers[2]=savedColor;offers[2].info.frame.view++;
    Check(!bridge.CaptureMixed(anchor,identity,offers,{},pending,selected,why),"color wrong view refuses");
    offers[2]=savedColor;
    auto bad=offers[1];offers[1].info.frame.view=3;
    Check(!bridge.CaptureMixed(anchor,identity,offers,{},pending,selected,why)&&!pending.ownerLease,"mixed wrong view rejected before capture");
    offers[1]=bad;offers[1].frame.producer.Reset();
    Check(!bridge.CaptureMixed(anchor,identity,offers,{},pending,selected,why)&&!pending.ownerLease,"mixed missing actual fence rejected");
    offers[1]=bad;
    offers[1].frame.producerValue=g.n+1;
    Check(!bridge.CaptureMixed(anchor,identity,offers,{},pending,selected,why)&&!pending.ownerLease,"late optional GPU work is not waited on");
    offers[1]=bad;offers[1].info.frame.generation++;
    Check(!bridge.CaptureMixed(anchor,identity,offers,{},pending,selected,why),"old generation rejected");
    offers[1]=bad;offers[1].info.stage=M::Stage::AfterPresent;
    Check(!bridge.CaptureMixed(anchor,identity,offers,{},pending,selected,why),"late stage rejected");
    offers[1]=bad;
    auto local=identity;local.frame.scope=M::IdentityScope::CoherentCapture;local.frame.scene=0;
    for(auto& offer:offers)offer.info.frame=local.frame;
    Check(!bridge.CaptureMixed(anchor,local,offers,{},pending,selected,why),"coherent capture cannot join a different content lease");
    for(auto& offer:offers)offer.info.frame=identity.frame;
    if(!bridge.CaptureMixed(anchor,identity,offers,{},pending,selected,why))Fail(why.c_str());
    Check(selected.ready&&selected.color==2&&selected.depth==1&&selected.motion==0,"mixed selected exact partial resources");
    C::PendingFrame competing;M::Selection second;
    Check(!bridge.CaptureMixed(anchor,identity,offers,{},competing,second,why)&&!competing.ownerLease,"one owner cannot start competing capture");
    offers[1].frame.lease.reset();b.frame.lease.reset();bad.frame.lease.reset();
    offers[2].frame.lease.reset();nativeColor.frame.lease.reset();savedColor.frame.lease.reset();
    Check(!colorLifetime.expired(),"selected color lease retained through consumer");
    Canonical(g,pending,pixels);
    Check(!depthLifetime.expired(),"mixed depth content lease retained through consumer");
    DlssNr::NativeGuides::Processor processor({Prepare,Record,Seal},g.d.Get());nrpg::CpuMainlineClient::GpuOutput output;
    Check(processor.ProcessTextures(pending.textures,output,why)==DlssNr::NativeGuides::Outcome::Delivered,why.c_str());
    C::CopybackReceipt receipt;if(!bridge.Finish(pending,output,receipt,why))Fail(why.c_str());
    Check(receipt.completed&&receipt.capture==2,"mixed exact capture copyback receipt");
    Verify(g.Read(color.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    Equal(g.Read(nativePixels.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),pixels,"Native source preserved; composed result delivered only to anchor");
    Check(colorLifetime.expired(),"selected color lease released after completed copyback");
    Check(depthLifetime.expired(),"mixed source lease released after completed copyback");
    // Actual production prepared-boundary adapter: user selects built-in
    // estimation while retaining captured depth from another transport.
    auto complete=anchor;complete.depth=depth;complete.motion=mv;complete.distrust=distrust;
    complete.deriveMotion=false;complete.depthState=complete.motionState=complete.distrustState=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    complete.description.capture=complete.description.maskCapture=3;complete.description.previousCapture=2;
    C::SignalStatusWireV2 status;M::Policy policy;policy.motion=C::Source::BuiltIn;
    auto physicallyRejected=complete;
    physicallyRejected.outputTarget=g.Texture(DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_COMMON);
    Check(!C::CapturePreparedInputs(bridge,physicallyRejected,C::Source::ReShade,C::Source::ReShade,C::Source::External,policy,pending,status,why),"physical copyback format rejection");
    Check(!status.depth&&!status.motion&&!status.consumed&&!pending.ownerLease,"rejected physical acquisition publishes no usable/consumed signals");
    for(bool motion:{false,true}){
        M::Policy softNone;(motion?softNone.motion:softNone.depth)=C::Source::None;
        Check(C::CapturePreparedInputs(bridge,complete,C::Source::ReShade,C::Source::ReShade,C::Source::External,softNone,pending,status,why),"soft None prepared input falls back");
        Check((motion?status.motionReason:status.depthReason)==uint32_t(M::Reason::PreferenceFallback),"prepared fallback is explicit");
        Check(bridge.Cancel(pending,why),"soft None capture retires cleanly");
    }
    const auto beforeNone=C::GpuBudget::Bytes();
    for(bool motion:{false,true}){
        M::Policy withheld;withheld.strict=true;(motion?withheld.motion:withheld.depth)=C::Source::None;
        Check(!C::CapturePreparedInputs(bridge,complete,C::Source::ReShade,C::Source::ReShade,C::Source::External,withheld,pending,status,why),"None refuses production prepared boundary");
        Check(!pending.ownerLease&&!status.consumed&&!status.depth&&!status.motion&&C::GpuBudget::Bytes()==beforeNone,"None has no allocation, estimator or consumed packet");
        Check((motion?status.motionReason:status.depthReason)==uint32_t(M::Reason::IntentionallyDisabled),"None publishes intentional refusal");
    }
    Check(C::CapturePreparedInputs(bridge,complete,C::Source::ReShade,C::Source::ReShade,C::Source::External,policy,pending,status,why),why.c_str());
    Check(pending.textures.deriveMotion&&status.depth==3&&status.motion==2,"production boundary honors per-signal estimator request");
    Check(bridge.Cancel(pending,why),"production boundary clean cancel");
    Check(C::GpuBudget::Bytes()>0,"reusable bounded cache remains owned until bridge retirement");
    const auto budget=C::GpuBudget::Bytes();
    for(unsigned n=0;n<8;++n){
        complete.description.flags&=~P::Reset;
        complete.description.previousCapture=complete.description.capture;
        ++complete.description.capture;complete.description.maskCapture=complete.description.capture;
        ++policy.revision;
        Check(C::CapturePreparedInputs(bridge,complete,C::Source::ReShade,C::Source::ReShade,C::Source::External,policy,pending,status,why),why.c_str());
        Check((pending.textures.description.flags&P::Reset)!=0,"policy revision resets temporal history");
        Check(bridge.Cancel(pending,why),"repeated clean cancellation");
        Check(C::GpuBudget::Bytes()==budget,"same dimensions reuse bounded pool");
    }
    std::puts("PASS mixed partial providers -> canonical textures -> NR consumer recorder -> exact GPU pixels; lease retirement");
}
int main(int argc,char** argv){std::string api=argc>1?argv[1]:"d3d12";bool warp=argc>2&&std::string(argv[2])=="--warp";Gpu g(warp);
    if(api=="mixed-d3d12"){RunMixed12(g);Check(C::GpuBudget::Bytes()==0,"mixed pool fully released");return 0;}
    if(api=="d3d12-color-formats")Run12ColorFormats(g);else if(api=="d3d12")Run12(g);else if(api=="d3d12-d24")Run12(g,DXGI_FORMAT_D24_UNORM_S8_UINT);else if(api=="d3d11")Run11(g,false);else if(api=="bgra")Run11(g,true);else if(api=="d16")Run11(g,false,DXGI_FORMAT_D16_UNORM);else if(api=="d24")Run11(g,false,DXGI_FORMAT_D24_UNORM_S8_UINT);else if(api=="d32")Run11(g,false,DXGI_FORMAT_D32_FLOAT);else if(api=="d3d10")Run10(g);else if(api=="d3d10-d24")Run10(g,true);else if(api=="d3d10-base")Run10(g,false,true);else if(api=="opengl")RunGl(g);else Fail("unknown test API");Check(C::GpuBudget::Bytes()==0,"all retired adapter/processor allocation tokens released");return 0;}
