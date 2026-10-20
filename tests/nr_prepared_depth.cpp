#include <cstdio>
#if !__has_include("../OptiScaler/dlssnr/NrPreparedDepth.h")
int main() { std::puts("FAIL: Native multi-mip prepared depth is missing"); return 1; }
#else
#define NR_GPU_SAFETY_TEST
#include "../OptiScaler/dlssnr/NrGpuSafety.cpp"
#include "../OptiScaler/dlssnr/NrPreparedDepth.h"
#if __has_include("../OptiScaler/dlssnr/NrPreparedDepthDiagnostics.h")
#include "../OptiScaler/dlssnr/NrPreparedDepthDiagnostics.h"
#define HAS_DEPTH_DIAGNOSTICS
#endif
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <cassert>
#include <vector>
#include <limits>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace P = DlssNr::PreparedDepth;
namespace S = DlssNr::GpuSafety;
static unsigned checks=0;
static void Check(bool value,unsigned line) { ++checks; if(!value){std::fprintf(stderr,"FAIL check %u at line %u\n",checks,line);std::fflush(stderr);std::abort();} }
#define Expect(value) Check((value),__LINE__)
static void CheckHr(HRESULT value,unsigned line) { if(FAILED(value))std::fprintf(stderr,"HRESULT %08lx at line %u\n",value,line); Check(SUCCEEDED(value),line); }
#define Hr(value) CheckHr((value),__LINE__)
int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--policy") == 0)
    {
        const bool choice = std::strcmp(argv[2], "1") == 0;
        const bool expected = std::strcmp(argv[3], "1") == 0;
        Expect(P::Enabled(choice) == expected);
        std::puts("PASS: process environment overrides saved experimental depth choice");
        return 0;
    }
    Expect(!P::ParseOptIn("")); Expect(!P::ParseOptIn("true"));
    Expect(!P::ParseOptIn("01")); Expect(P::ParseOptIn("1"));
    D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=8;desc.Height=4;desc.DepthOrArraySize=1;desc.MipLevels=3;
    desc.Format=DXGI_FORMAT_R32_FLOAT;desc.SampleDesc.Count=1;
    P::Rect rect{0,0,8,4};
    Expect(P::Decide(false,desc,rect).kind==P::Kind::Direct);
    Expect(P::Decide(true,desc,rect).kind==P::Kind::Prepared);
    for(auto field:{0,1,2,3,4,5,6,7}){
        auto bad=desc;
        switch(field){case 0:bad.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;break;
        case 1:bad.DepthOrArraySize=2;break;case 2:bad.SampleDesc.Count=4;break;
        case 3:bad.SampleDesc.Quality=1;break;case 4:bad.Width=0;break;
        case 5:bad.MipLevels=0;break;case 6:bad.Format=DXGI_FORMAT_R8G8B8A8_UNORM;break;
        case 7:bad.Flags=D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;break;}
        Expect(P::Decide(true,bad,rect).kind==P::Kind::Refused);
    }
    Expect(P::Decide(true,desc,{7,0,2,4}).kind==P::Kind::Refused);
    Expect(P::Decide(true,desc,{0,0,0,4}).kind==P::Kind::Refused);
    Expect(P::Decide(true,desc,{UINT_MAX,0,2,4}).kind==P::Kind::Refused);
    auto single=desc;single.MipLevels=1;Expect(P::Decide(true,single,rect).kind==P::Kind::Direct);

#ifndef HAS_DEPTH_DIAGNOSTICS
    std::puts("FAIL: one-mip depth bypass has no bounded eligibility diagnostics"); return 1;
#else
    namespace D = P::Diagnostics;
    Expect(D::Classify(false,false,false,desc,rect).reason==std::string_view("policy-off"));
    Expect(D::Classify(true,true,false,desc,rect).reason==std::string_view("private-command-list"));
    Expect(D::Classify(true,false,false,single,rect).reason==std::string_view("single-mip"));
    Expect(D::Classify(true,false,true,single,rect).reason==std::string_view("single-mip"));
    Expect(D::Classify(true,false,true,desc,rect).reason==std::string_view("protocol-representation-not-enrolled"));
    Expect(D::Classify(true,false,false,desc,rect).kind==D::EntryKind::Candidate);
    auto wilds=single;wilds.Format=DXGI_FORMAT_R32G8X24_TYPELESS;
    Expect(D::Classify(true,false,false,wilds,rect).reason==std::string_view("single-mip"));
    wilds.MipLevels=3;
    Expect(D::Classify(true,false,false,wilds,rect).reason==std::string_view("depth-format"));
    Expect(D::Classify(true,false,false,desc,{7,0,2,4}).reason==std::string_view("active-rect"));
    D::Snapshot snapshot{true,false,false,1,single,rect}; D::Monitor monitor;
    Expect(monitor.Observe(snapshot)); Expect(!monitor.Observe(snapshot));
    snapshot.enabled=false;Expect(monitor.Observe(snapshot));
    snapshot.privateList=true;Expect(monitor.Observe(snapshot));
    snapshot.protocolObserver=true;Expect(monitor.Observe(snapshot));
    ++snapshot.generation;Expect(monitor.Observe(snapshot));
    ++snapshot.active.x;Expect(monitor.Observe(snapshot));
    ++snapshot.descriptor.MipLevels;Expect(monitor.Observe(snapshot));
    snapshot.descriptor.Format=DXGI_FORMAT_R32G8X24_TYPELESS;Expect(monitor.Observe(snapshot));
    for(unsigned i=monitor.count;i<D::Monitor::Limit;++i){++snapshot.generation;Expect(monitor.Observe(snapshot));}
    Expect(monitor.count==D::Monitor::Limit);++snapshot.generation;
    Expect(!monitor.Observe(snapshot));Expect(monitor.count==D::Monitor::Limit);
#endif

    ComPtr<ID3D12Debug> debug;Hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> info;Hr(device.As(&info));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC qd{};Hr(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    auto ticket=S::Record(list.Get());Expect(ticket!=nullptr);
    Hr(list->Close());Hr(allocator->Reset());Hr(list->Reset(allocator.Get(),nullptr));ticket=S::Record(list.Get());
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> source;Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&source)));
    auto buffer=[&](D3D12_HEAP_TYPE type){
        auto h=heap;h.Type=type;D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        b.Width=2048;b.Height=b.DepthOrArraySize=b.MipLevels=1;b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> r;Hr(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&b,
          type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));return r;};
    auto upload=buffer(D3D12_HEAP_TYPE_UPLOAD),readback=buffer(D3D12_HEAP_TYPE_READBACK);
    float* pixels=nullptr;Hr(upload->Map(0,nullptr,reinterpret_cast<void**>(&pixels)));
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x)pixels[y*64+x]=float(y*8+x)/32;
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x)pixels[256+y*64+x]=99;
    upload->Unmap(0,nullptr);
    auto textureCopy=[&](ID3D12Resource* image,UINT mip,ID3D12Resource* bytes,UINT64 offset,bool write){
        D3D12_TEXTURE_COPY_LOCATION t{},b{};t.pResource=image;t.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;t.SubresourceIndex=mip;
        b.pResource=bytes;b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint.Offset=offset;
        b.PlacedFootprint.Footprint={image->GetDesc().Format,8u>>mip,4u>>mip,1,256};
        list->CopyTextureRegion(write?&t:&b,0,0,0,write?&b:&t,nullptr);};
    textureCopy(source.Get(),0,upload.Get(),0,true);textureCopy(source.Get(),1,upload.Get(),1024,true);
    P::Transition(list.Get(),source.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // Mip 1 stays COPY_DEST throughout preparation; an all-mips barrier will trigger the debug layer.
    P::Slot slot{};UINT64 sequence=0;
    P::Request request{true,device.Get(),list.Get(),source.Get(),ticket,1,rect};
    request.arrivalKnown=true;
    unsigned parks=0,allocations=0;
    struct Retired{ID3D12Resource* resource;S::CompletionSet completion;};std::vector<Retired> retired;
    auto park=[&](P::Slot& old){++parks;retired.push_back({old.resource,S::Pending()});old={};};
    auto allocate=[&](ID3D12Device* d,const D3D12_RESOURCE_DESC& wanted){++allocations;return P::Allocate(d,wanted);};
    auto disabled=request;disabled.enabled=false;disabled.source=nullptr;disabled.list=nullptr;
    auto off=P::Prepare(disabled,slot,sequence,allocate,park);Expect(off.kind==P::Kind::Direct&&allocations==0&&parks==0);
    auto missing=request;missing.ticket={};Expect(P::Prepare(missing,slot,sequence,allocate,park).kind==P::Kind::Refused);
    auto stale=request;stale.generation=0;Expect(P::Prepare(stale,slot,sequence,allocate,park).kind==P::Kind::Refused);
    auto unknown=request;unknown.arrivalKnown=false;Expect(P::Prepare(unknown,slot,sequence,allocate,park).kind==P::Kind::Refused);
    unknown=request;unknown.arrival=D3D12_RESOURCE_STATE_COPY_SOURCE;Expect(P::Prepare(unknown,slot,sequence,allocate,park).kind==P::Kind::Refused);
    auto configured=request;
#ifdef NR_PREPARED_DEPTH_NATIVE_ARRIVAL_V1
    P::SetNativeArrival(configured,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);
#endif
    // A selected SR adapter restores an explicit depth barrier before this seam.
    // It must not be overwritten by an ordinary NGX default.
    Expect(P::Prepare(configured,slot,sequence,allocate,park).kind==P::Kind::Refused&&allocations==0);
#ifdef NR_PREPARED_DEPTH_NATIVE_ARRIVAL_V1
    P::SetNativeArrival(configured,std::nullopt,false);
    Expect(!configured.arrivalKnown&&P::Prepare(configured,slot,sequence,allocate,park).kind==P::Kind::Refused);
    P::SetNativeArrival(configured,std::nullopt,true);
    Expect(configured.arrivalKnown&&configured.arrival==D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    P::SetNativeArrival(configured,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,false);
    Expect(configured.arrivalKnown&&configured.arrival==D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
#endif
    ComPtr<ID3D12Device> otherDevice;
    for(UINT i=0;;++i){ComPtr<IDXGIAdapter1> candidateAdapter;
        if(FAILED(factory->EnumAdapters1(i,&candidateAdapter)))break;
        ComPtr<ID3D12Device> candidate;
        if(SUCCEEDED(D3D12CreateDevice(candidateAdapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&candidate)))&&
           !DlssNr::NativeIdentity::CompareDevices(candidate.Get(),device.Get()).equal){otherDevice=candidate;break;}}
    if(otherDevice){auto wrongDevice=request;wrongDevice.device=otherDevice.Get();
        Expect(P::Prepare(wrongDevice,slot,sequence,allocate,park).kind==P::Kind::Refused);}
    else std::puts("SKIP: distinct D3D12 device unavailable; hardware provider not executed");
    UINT64 exhaustedSequence=UINT64_MAX;Expect(P::Prepare(request,slot,exhaustedSequence,allocate,park).kind==P::Kind::Refused&&allocations==0);
    auto wrongAllocation=[&](ID3D12Device* d,const D3D12_RESOURCE_DESC& wanted){auto bad=wanted;bad.Width=4;return P::Allocate(d,bad);};
    Expect(P::Prepare(request,slot,sequence,wrongAllocation,park).kind==P::Kind::Refused&&slot.resource==nullptr);
    auto first=P::Prepare(request,slot,sequence,allocate,park);
    Expect(first.kind==P::Kind::Prepared&&first.allocated&&first.action&&first.action->Current());
    unsigned providerCalls=0;
    auto testProvider=[&](const P::Result& prepared){if(P::CanUse(prepared)){++providerCalls;Expect(prepared.input!=source.Get());}};
    testProvider(off);Expect(providerCalls==0);testProvider(first);Expect(providerCalls==1);
    Expect(first.input!=source.Get()&&first.input==slot.resource&&slot.resource->GetDesc().MipLevels==1);
    Expect(slot.resource->GetDesc().Width==8&&slot.resource->GetDesc().Height==4);
    const auto firstId=slot.id;auto* firstResource=slot.resource;
    auto overlapping=P::Prepare(request,slot,sequence,allocate,park);
    Expect(overlapping.kind==P::Kind::Refused&&slot.resource==firstResource&&slot.id==firstId);
    testProvider(overlapping);Expect(providerCalls==1);
    first.action.reset();
    testProvider(first);Expect(providerCalls==1);
    auto noAllocation=[](ID3D12Device*,const D3D12_RESOURCE_DESC&)->ID3D12Resource*{return nullptr;};
    auto failed=P::Prepare(request,slot,sequence,noAllocation,park);
    Expect(failed.kind==P::Kind::Refused&&slot.resource==firstResource&&slot.id==firstId&&parks==0);
    testProvider(failed);Expect(providerCalls==1);
    auto exhausted=request;exhausted.pendingRetirements=P::RetirementLimit;
    Expect(P::Prepare(exhausted,slot,sequence,allocate,park).kind==P::Kind::Refused&&parks==0);
    P::Transition(list.Get(),slot.resource,0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    textureCopy(slot.resource,0,readback.Get(),0,false);
    P::Transition(list.Get(),slot.resource,0,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    P::Transition(list.Get(),source.Get(),1,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    textureCopy(source.Get(),1,readback.Get(),1024,false);
    P::Transition(list.Get(),source.Get(),1,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    auto second=P::Prepare(request,slot,sequence,allocate,park);
    Expect(second.kind==P::Kind::Prepared&&second.allocated&&parks==1&&slot.id!=firstId);
    Expect(!S::Reusable(retired[0].completion));second.action.reset();
    ComPtr<ID3D12Fence> gate;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    Hr(queue->Wait(gate.Get(),1));Hr(list->Close());ID3D12CommandList* command=list.Get();queue->ExecuteCommandLists(1,&command);
    Expect(!S::Reusable(retired[0].completion));Hr(gate->Signal(1));
    Expect(S::Drain(5000));Expect(!S::Reusable(retired[0].completion)); // Completion alone cannot retire a replayable list.
    Hr(readback->Map(0,nullptr,reinterpret_cast<void**>(&pixels)));
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x)Expect(pixels[y*64+x]==float(y*8+x)/32);
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x)Expect(pixels[256+y*64+x]==99);
    readback->Unmap(0,nullptr);
    Hr(allocator->Reset());Hr(list->Reset(allocator.Get(),nullptr));Expect(S::Reusable(retired[0].completion));
    Expect(P::Prepare(request,slot,sequence,allocate,park).kind==P::Kind::Refused); // Old ticket after Reset.
    request.ticket=S::Record(list.Get());
    auto reuse=P::Prepare(request,slot,sequence,allocate,park);Expect(reuse.kind==P::Kind::Prepared&&!reuse.allocated);reuse.action.reset();
    request.generation=2;
    auto generation=P::Prepare(request,slot,sequence,allocate,park);Expect(generation.kind==P::Kind::Prepared&&generation.allocated);generation.action.reset();
    Hr(list->Close());command=list.Get();queue->ExecuteCommandLists(1,&command);Expect(S::Drain(5000));
    for(auto format:{DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_R16_UNORM})
    {
        if(format==DXGI_FORMAT_R16_FLOAT){Hr(allocator->Reset());Hr(list->Reset(allocator.Get(),nullptr));}
        auto narrowDesc=desc;narrowDesc.Format=format;ComPtr<ID3D12Resource> narrowSource;
        Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&narrowDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&narrowSource)));
        unsigned short* narrowPixels=nullptr;Hr(upload->Map(0,nullptr,reinterpret_cast<void**>(&narrowPixels)));
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x)narrowPixels[y*128+x]=static_cast<unsigned short>(0x3000+y*8+x);
        upload->Unmap(0,nullptr);textureCopy(narrowSource.Get(),0,upload.Get(),0,true);
        P::Transition(list.Get(),narrowSource.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        request.source=narrowSource.Get();request.ticket=S::Record(list.Get());request.generation=3;
        auto narrow=P::Prepare(request,slot,sequence,allocate,park);
        Expect(narrow.kind==P::Kind::Prepared&&narrow.copyRecorded&&narrow.input->GetDesc().Format==format);
        P::Transition(list.Get(),narrow.input,0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        textureCopy(narrow.input,0,readback.Get(),0,false);
        P::Transition(list.Get(),narrow.input,0,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        narrow.action.reset();Hr(list->Close());command=list.Get();queue->ExecuteCommandLists(1,&command);Expect(S::Drain(5000));
        Hr(readback->Map(0,nullptr,reinterpret_cast<void**>(&narrowPixels)));
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x)Expect(narrowPixels[y*128+x]==0x3000+y*8+x);
        readback->Unmap(0,nullptr);
        // Reset seals the recording before narrowSource's last local reference is released.
        Hr(allocator->Reset());Hr(list->Reset(allocator.Get(),nullptr));
    }
    // Deterministic nested A/B seam: each invocation retains its own identity,
    // even while B publishes a replacement and parks A through the owner.
    request.source=source.Get();request.generation=4;request.ticket=S::Record(list.Get());
    auto invocationA=P::Prepare(request,slot,sequence,allocate,park);
    Expect(P::CanUse(invocationA));const auto aId=invocationA.id;auto* aInput=invocationA.input;
    ComPtr<ID3D12CommandAllocator> allocatorB;Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocatorB)));
    ComPtr<ID3D12GraphicsCommandList> listB;Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocatorB.Get(),nullptr,IID_PPV_ARGS(&listB)));
    auto ticketB=S::Record(listB.Get());Hr(listB->Close());Hr(allocatorB->Reset());Hr(listB->Reset(allocatorB.Get(),nullptr));
    auto requestB=request;requestB.list=listB.Get();requestB.ticket=S::Record(listB.Get());
    auto invocationB=P::Prepare(requestB,slot,sequence,allocate,park);
    Expect(P::CanUse(invocationA)&&P::CanUse(invocationB)&&invocationB.id!=aId&&invocationB.input!=aInput);
    Expect(invocationA.id==aId&&invocationA.input==aInput&&slot.resource==invocationB.input);
    Expect(!S::Reusable(retired.back().completion));
    invocationB.action.reset();Hr(listB->Close());Hr(allocatorB->Reset());Hr(listB->Reset(allocatorB.Get(),nullptr));listB.Reset();
    invocationA.action.reset();Hr(list->Close());Hr(allocator->Reset());Hr(list->Reset(allocator.Get(),nullptr));
    Expect(!P::CanUse(invocationA)&&!P::CanUse(invocationB));list.Reset();
    for(auto& old:retired){Expect(S::Reusable(old.completion));old.resource->Release();}
    slot.resource->Release();slot={};
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n=0;info->GetMessage(i,nullptr,&n);std::vector<char> storage(n);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Hr(info->GetMessage(i,message,&n));if(message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING){std::printf("DEBUG: %s\n",message->pDescription);Expect(false);}}
    std::printf("PASS: prepared depth %u checks; WARP mip-zero pixels, retirement and debug validation\n",checks);
}
#endif
