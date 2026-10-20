#include "CpuMainlineClient.h"
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
using Microsoft::WRL::ComPtr;
namespace P=Neurotic::Feed::Prepared;
namespace G=DlssNr::PreparedGuides;
static void Check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
static bool refusePrepare=false,refuseRecord=false,refuseSeal=false,removeAtSeal=false,preparingRecord=false;
static unsigned calls=0,seals=0;
static P::Descriptor seen;
static ComPtr<ID3D12Resource> observed;
static ComPtr<ID3D12DescriptorHeap> clearHeap;
static std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,4> layouts;
static unsigned __cdecl Prepare(ID3D12GraphicsCommandList*){return !refusePrepare;}
static unsigned __cdecl Seal(ID3D12GraphicsCommandList* list){++seals;if(removeAtSeal){ComPtr<ID3D12Device5> device;Check(SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))),"test device removal interface");device->RemoveDevice();}return !refuseSeal;}
static unsigned __cdecl Record(const G::Dispatch* d){
    ++calls;seen=d->source;
    Check(d->source.depthOrigin==P::C::SourceClass::HostObserved&&d->source.motionOrigin==P::C::SourceClass::Derived,"capture provenance preserved");
    Check(d->color->GetDesc().Format==DXGI_FORMAT_R8G8B8A8_UNORM&&d->motion->GetDesc().Format==DXGI_FORMAT_R32G32_FLOAT,"typed owned resources");
    ComPtr<ID3D12Device> dev;Check(SUCCEEDED(d->list->GetDevice(IID_PPV_ARGS(&dev))),"recorder device");
    const std::array<ID3D12Resource*,4> inputs{d->color,d->depth,d->motion,d->distrust};
    UINT64 size=0;
    for(unsigned i=0;i<4;++i){auto desc=inputs[i]->GetDesc();size=(size+511)&~511ull;UINT64 bytes=0;dev->GetCopyableFootprints(&desc,0,1,size,&layouts[i],nullptr,nullptr,&bytes);size+=bytes;}
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=size;buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;observed.Reset();
    Check(SUCCEEDED(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&observed))),"recorder readback allocation");
    for(unsigned i=0;i<4;++i){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={inputs[i],0,i?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};d->list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=observed.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=layouts[i];D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=inputs[i];src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;d->list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);d->list->ResourceBarrier(1,&b);}
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heapDesc.NumDescriptors=1;heapDesc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    clearHeap.Reset();Check(SUCCEEDED(dev->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&clearHeap))),"recorder clear heap");
    D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;dev->CreateUnorderedAccessView(d->color,nullptr,&view,clearHeap->GetCPUDescriptorHandleForHeapStart());
    ID3D12DescriptorHeap* bound=clearHeap.Get();d->list->SetDescriptorHeaps(1,&bound);const float resultColor[]={.25f,.5f,.75f,1.f};
    d->list->ClearUnorderedAccessViewFloat(clearHeap->GetGPUDescriptorHandleForHeapStart(),clearHeap->GetCPUDescriptorHandleForHeapStart(),d->color,resultColor,0,nullptr);
    return unsigned(refuseRecord?G::Result::Refused:preparingRecord?G::Result::Preparing:G::Result::Recorded);
}
static P::Descriptor Descriptor(unsigned capture=1){P::Descriptor d;d.producer=1;d.session=2;d.stream=3;d.device=4;d.generation=5;d.capture=capture;d.previousCapture=capture-1;d.maskCapture=capture;d.flags=P::ZeroJitterPolicy|P::HasDistrust|(capture==1?P::Reset:0);d.color=d.depth=d.motion=d.distrust={3,2,0,0,3,2};return d;}
static std::vector<std::byte> Payload(){std::vector<std::byte> p(120);for(unsigned i=0;i<6;++i){const std::uint8_t color[]={10,20,30,255};std::memcpy(p.data()+i*4,color,4);const float depth=.25f,motion[]={2.5f,-3.f},mask=.5f;std::memcpy(p.data()+24+i*4,&depth,4);std::memcpy(p.data()+48+i*8,motion,8);std::memcpy(p.data()+96+i*4,&mask,4);}return p;}
int main(){
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;Check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))),"WARP adapter available");DXGI_ADAPTER_DESC1 info{};adapter->GetDesc1(&info);std::uint64_t luid=0;std::memcpy(&luid,&info.AdapterLuid,8);
    nrpg::CpuMainlineClient client({Prepare,Record,Seal});std::string reason;std::vector<std::uint8_t> output;auto p=Payload();
    nrpg::CpuMainlineClient initializing({Prepare,Record,Seal});preparingRecord=true;
    auto preparationSeals=seals;
    Check(!initializing.Process(luid,Descriptor(),p,output,reason)&&initializing.Preparing()&&output.empty()&&!initializing.RequiresRestart()&&seals==preparationSeals+1,
        "successful sealed initialization is retryable without claiming output");
    const auto initializationCalls=calls;
    Check(!initializing.Process(luid,Descriptor(),p,output,reason)&&!initializing.Preparing()&&calls==initializationCalls,
        "initialization capture cannot be replayed or keep stale preparing status");
    Check(!initializing.Process(luid,Descriptor(2),p,output,reason)&&initializing.Preparing()&&!initializing.RequiresRestart()&&output.empty(),
        "multiple feature initialization lists remain retryable without output");
    preparingRecord=false;
    Check(initializing.Process(luid,Descriptor(3),p,output,reason)&&!initializing.Preparing()&&(seen.flags&P::Reset)&&seen.previousCapture==0&&output.size()==24,
        "initialization retries with actual GPU output and reset delivery history");
    Check(initializing.Process(luid,Descriptor(4),p,output,reason)&&!(seen.flags&P::Reset),"delivered output after initialization establishes continuity");
    preparingRecord=true;
    Check(!initializing.Process(luid,Descriptor(5),p,output,reason)&&initializing.Preparing()&&output.empty()&&!initializing.RequiresRestart(),
        "retirement preparation after delivery is retryable and cannot publish old output");
    preparingRecord=false;
    Check(initializing.Process(luid,Descriptor(6),p,output,reason)&&(seen.flags&P::Reset)&&seen.previousCapture==0,
        "replacement output after retirement resets previous delivery history");
    preparingRecord=true;refuseSeal=true;nrpg::CpuMainlineClient unsealedPreparation({Prepare,Record,Seal});
    Check(!unsealedPreparation.Process(luid,Descriptor(),p,output,reason)&&!unsealedPreparation.Preparing()&&unsealedPreparation.RequiresRestart()&&output.empty(),
        "initialization seal failure retains completed slot and refuses retry");
    refuseSeal=false;preparingRecord=false;
    const bool first=client.Process(luid,Descriptor(),p,output,reason);if(!first)std::fprintf(stderr,"bridge: %s\n",reason.c_str());Check(first,"completed WARP upload/core-recorder/readback accepted");
    Check(output.size()==24&&output[0]==191&&output[1]==128&&output[2]==64&&output[3]==255&&output[20]==191,"actual GPU-modified BGRA readback including second pitched row");
    void* mapped=nullptr;Check(SUCCEEDED(observed->Map(0,nullptr,&mapped)),"completed recorder map");auto* bytes=static_cast<unsigned char*>(mapped);Check(bytes[layouts[0].Offset]==30&&bytes[layouts[0].Offset+2]==10,"BGRA input converted to RGBA for core");float depth=0,motion[2]{};std::memcpy(&depth,bytes+layouts[1].Offset,4);std::memcpy(motion,bytes+layouts[2].Offset,8);Check(depth==.25f&&motion[0]==2.5f&&motion[1]==-3.f&&bytes[layouts[3].Offset]==128,"guide upload and distrust quantization");observed->Unmap(0,nullptr);
    Check(!client.Process(luid,Descriptor(),p,output,reason)&&output.empty(),"stale frame cannot return prior output");
    Check(!client.Process(luid,Descriptor(2),std::span(p).first(119),output,reason)&&output.empty(),"truncated payload refused");
    Check(client.Process(luid,Descriptor(2),p,output,reason)&&!(seen.flags&P::Reset),"contiguous source preserves history");
    auto gap=Descriptor(4);Check(client.Process(luid,gap,p,output,reason)&&(seen.flags&P::Reset)&&seen.previousCapture==0,"capture gap resets history");
    auto invalid=Descriptor(5);invalid.maskCapture=4;Check(!client.Process(luid,invalid,p,output,reason)&&output.empty(),"stale mask refused");
    auto nan=p;float bad=std::numeric_limits<float>::quiet_NaN();std::memcpy(nan.data()+48,&bad,4);Check(!client.Process(luid,Descriptor(5),nan,output,reason),"nonfinite packed motion refused");
    auto resized=Descriptor(5);resized.color=resized.depth=resized.motion=resized.distrust={2,3,0,0,2,3};++resized.generation;
    Check(client.Process(luid,resized,p,output,reason)&&(seen.flags&P::Reset)&&output.size()==24&&!client.RequiresRestart(),"completed resize and generation transition reallocates and resets history");
    auto session=Descriptor();++session.session;Check(client.Process(luid,session,p,output,reason)&&(seen.flags&P::Reset)&&!client.RequiresRestart(),"new session can restart capture numbers after completion");
    Check(!client.Process(luid+1,Descriptor(5),p,output,reason)&&client.RequiresRestart()&&output.empty(),"adapter change quarantines existing slot");
    nrpg::CpuMainlineClient rejected({Prepare,Record,Seal});refuseRecord=true;auto sealsBefore=seals;Check(!rejected.Process(luid,Descriptor(),p,output,reason)&&output.empty()&&rejected.RequiresRestart()&&seals==sealsBefore+1,"partial rejected recording is submitted and sealed without returning output");refuseRecord=false;
    nrpg::CpuMainlineClient unsealed({Prepare,Record,Seal});refuseSeal=true;Check(!unsealed.Process(luid,Descriptor(),p,output,reason)&&output.empty()&&unsealed.RequiresRestart(),"seal failure quarantines output");refuseSeal=false;
    nrpg::CpuMainlineClient unprepared({Prepare,Record,Seal});refusePrepare=true;const auto before=calls;Check(!unprepared.Process(luid,Descriptor(),p,output,reason)&&calls==before&&output.empty(),"prepare refusal prevents core recording");
    refusePrepare=false;nrpg::CpuMainlineClient missingAdapter({Prepare,Record,Seal});Check(!missingAdapter.Process(0,Descriptor(),p,output,reason)&&calls==before&&output.empty(),"unknown source adapter has no default fallback");
    nrpg::CpuMainlineClient removed({Prepare,Record,Seal});removeAtSeal=true;preparingRecord=true;
    Check(!removed.Process(luid,Descriptor(),p,output,reason)&&!removed.Preparing()&&removed.RequiresRestart()&&output.empty(),
        "initialization device loss never becomes retryable preparation");removeAtSeal=false;preparingRecord=false;
    std::puts("PASS: real WARP CPU mainline transport, admission, continuity and failure checks (test recorder only; no model/display claim)");return 0;
}
