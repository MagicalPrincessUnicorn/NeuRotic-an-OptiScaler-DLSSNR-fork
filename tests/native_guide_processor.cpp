#include "nr_reshade_device_fixture.h"
#include <dlssnr/NativeGuideProcessor.h>
#ifndef _WIN64
int main(){
    std::vector<std::uint8_t> output{1};std::string reason;
    const auto result=DlssNr::NativeGuides::Process({},output,reason);
    return result==DlssNr::NativeGuides::Outcome::Unavailable&&output.empty()&&!reason.empty()?0:1;
}
#else
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <array>
#include <cmath>
#include <thread>
#include <chrono>
#include <dlssnr/NativeGpuPixels.h>
#include "native_gpu_fixture.h"
#include "native_fsr_fixture.h"
using Microsoft::WRL::ComPtr;
namespace N=DlssNr::NativeGuides;
namespace P=Neurotic::Feed::Prepared;
namespace G=DlssNr::PreparedGuides;
// Address-only linkage probes force both ABI families into this executable.
extern "C" void ffxOpticalflowContextCreate();
extern "C" void nrNative_ffxOpticalflowContextCreate();
extern "C" void ffxGetPermutationBlobByIndex();
extern "C" void nrNative_ffxGetPermutationBlobByIndex();
extern "C" void ffxFsr2GetInterfaceDX12();
static void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static P::Descriptor seen;
static unsigned calls=0;
static bool preparing=false,refuseSeal=false,refuseRecord=false,removeAtSeal=false;
static ComPtr<ID3D12Resource> observed;
static std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,3> layouts;
static unsigned __cdecl Detail(char* out,unsigned capacity){const char* text="fixture model refusal detail";const auto n=(std::min)(size_t(capacity?capacity-1:0),std::strlen(text));if(capacity){std::memcpy(out,text,n);out[n]=0;}return unsigned(n);}
static unsigned __cdecl Prepare(ID3D12GraphicsCommandList*){return 1;}
static unsigned __cdecl Seal(ID3D12GraphicsCommandList* list){
    if(removeAtSeal){ComPtr<ID3D12Device5> device;Check(SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&device))),"device removal interface");device->RemoveDevice();}
    return !refuseSeal;
}
static bool expectGpuMotion=true;
static nrpg::CpuMainlineClient::GpuInputs residentFixture;
static unsigned __cdecl Record(const G::Dispatch* d){
    Check(d->motion->GetDesc().Format==(expectGpuMotion?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R32G32_FLOAT),"motion format matches selected GPU or CPU transport");
    ++calls;seen=d->source;ComPtr<ID3D12Device> device;Check(SUCCEEDED(d->list->GetDevice(IID_PPV_ARGS(&device))),"recorder device");
    residentFixture.device=device;residentFixture.color=d->color;residentFixture.depth=d->depth;
    residentFixture.motion=d->motion;residentFixture.distrust=d->distrust;
    const std::array<ID3D12Resource*,3> inputs{d->depth,d->motion,d->distrust};UINT64 total=0;
    for(unsigned i=0;i<3;++i){auto desc=inputs[i]->GetDesc();total=(total+511)&~511ull;UINT64 bytes=0;
        device->GetCopyableFootprints(&desc,0,1,total,&layouts[i],nullptr,nullptr,&bytes);total+=bytes;}
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;
    buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;observed.Reset();
    Check(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&observed))),"recorder guide readback");
    for(unsigned i=0;i<3;++i){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={inputs[i],0,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};d->list->ResourceBarrier(1,&b);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=observed.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=layouts[i];
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=inputs[i];src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;d->list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        std::swap(b.Transition.StateBefore,b.Transition.StateAfter);d->list->ResourceBarrier(1,&b);}
    return unsigned(preparing?G::Result::Preparing:refuseRecord?G::Result::Refused:G::Result::Recorded);
}
int main(){
    Check(&ffxOpticalflowContextCreate!=&nrNative_ffxOpticalflowContextCreate&&
        &ffxGetPermutationBlobByIndex!=&nrNative_ffxGetPermutationBlobByIndex,"native flow ABI isolated from existing FSR optical flow and shader backend");
    Check(&ffxFsr2GetInterfaceDX12!=&nrNative_ffxGetPermutationBlobByIndex,
        "existing FSR2 DX12 backend links alongside isolated native flow");
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;
    Check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))),"WARP available");
    DXGI_ADAPTER_DESC1 info{};adapter->GetDesc1(&info);std::uint64_t luid=0;std::memcpy(&luid,&info.AdapterLuid,8);
    {
        ComPtr<ID3D12Device> native;Check(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&native))),"fixture device");
        ComPtr<ReShadeDeviceProxy> proxy;proxy.Attach(new ReShadeDeviceProxy(native.Get(),ReShadeDeviceProxy::Unsupported));
        N::Processor recovery({Prepare,Record,Seal},proxy.Get());
        std::vector<std::uint8_t> rgba(256*64*4,127),out;std::vector<float> z(256*64,.25f);std::string detail;
        N::Capture c{luid,100,200,1,256,64,true,false,rgba,z};
        Check(recovery.Process(c,out,detail)==N::Outcome::Unavailable&&out.empty(),"clean flow admission rejection must not quarantine native owner");
        proxy->mode=ReShadeDeviceProxy::Normal;
        Check(recovery.Process(c,out,detail)==N::Outcome::Delivered&&out==rgba,"native owner recovers after identity proof becomes available");
        Check(proxy->commandListsCreated>0&&proxy->fencesCreated>0,"native execution retains device proxy");
        // Negative admission uses real resource/fence objects but never records
        // or executes their uninitialized texture contents.
        nrpg::CpuMainlineClient consumer({Prepare,Record,Seal});
        nrpg::CpuMainlineClient::GpuInputs inputs;inputs.device=native;
        const std::array<DXGI_FORMAT,3> formats{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R8_UNORM};
        const std::array<ComPtr<ID3D12Resource>*,3> textures{&inputs.color,&inputs.motion,&inputs.distrust};
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        for(unsigned i=0;i<3;++i){D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=c.width;desc.Height=c.height;desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Format=formats[i];
            Check(SUCCEEDED(native->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(textures[i]->GetAddressOf()))),"negative GPU admission texture");}
        Check(SUCCEEDED(native->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&inputs.producer))),"negative GPU admission fence");
        inputs.value=1;inputs.lease=std::make_shared<ComPtr<ID3D12Device>>(native);
        nrpg::CpuMainlineClient::Completion completion;const auto before=calls;
        Check(!consumer.ProcessGpu(c.adapterLuid,seen,inputs,c.depth,out,completion,detail)&&
              detail.find("completion not established")!=std::string::npos&&!completion.fence&&calls==before&&!consumer.RequiresRestart(),
              "incomplete producer refused before core recording without quarantine");
        Check(SUCCEEDED(inputs.producer->Signal(1)),"complete negative fixture fence");
        inputs.device=proxy;proxy->mode=ReShadeDeviceProxy::Unsupported;
        Check(!consumer.ProcessGpu(c.adapterLuid,seen,inputs,c.depth,out,completion,detail)&&
              detail.find("another device")!=std::string::npos&&calls==before&&!consumer.RequiresRestart(),
              "unrecognized resource ownership refused before core recording");
        proxy->mode=ReShadeDeviceProxy::Normal;
        nrpg::CpuMainlineClient compatibility({Prepare,Record,Seal});
        const auto pixels=std::size_t(c.width)*c.height;std::vector<std::byte> packed(pixels*20);
        std::memcpy(packed.data(),c.bgra.data(),pixels*4);std::memcpy(packed.data()+pixels*4,c.depth.data(),pixels*4);
        for(std::size_t i=0;i<pixels;++i){const float one=1;std::memcpy(packed.data()+pixels*16+i*4,&one,4);}
        expectGpuMotion=false;
        Check(compatibility.Process(c.adapterLuid,seen,packed,out,detail)&&out==rgba,"existing CPU transport retains exact color and valid core input");
        expectGpuMotion=true;
        observed.Reset();calls=0;
    }
    N::Processor processor({Prepare,Record,Seal});
    std::vector<std::uint8_t> color(256*64*4,127),output;std::vector<float> depth(256*64,.25f);std::string reason;
    for(std::size_t i=0;i<color.size();i+=4){color[i]=10;color[i+1]=20;color[i+2]=30;color[i+3]=255;}
    N::Capture capture{luid,10,20,1,256,64,true,false,color,depth};
    auto result=processor.Process(capture,output,reason);if(result!=N::Outcome::Delivered)std::fprintf(stderr,"processor: %s\n",reason.c_str());
    Check(result==N::Outcome::Delivered&&output==color,"actual FFX WARP flow and core upload/readback deliver exact color");
    Check(calls==1&&seen.session==10&&seen.generation==20&&seen.capture==1&&seen.previousCapture==0,"observed capture identity survives native processing");
    void* mapped=nullptr;Check(SUCCEEDED(observed->Map(0,nullptr,&mapped)),"completed guides readable");
    const auto* bytes=static_cast<const unsigned char*>(mapped);float z=0;std::uint16_t motion[2]{};
    std::memcpy(&z,bytes+layouts[0].Offset,4);std::memcpy(motion,bytes+layouts[1].Offset,4);
    Check(z==.25f&&motion[0]==0&&motion[1]==0&&bytes[layouts[2].Offset]==255,"captured depth and reset distrust reach actual core GPU inputs");observed->Unmap(0,nullptr);
    Check(seen.sourceApi==P::C::GraphicsApi::Vulkan&&seen.depthOrigin==P::C::SourceClass::HostObserved&&seen.motionOrigin==P::C::SourceClass::Derived,"captured depth and derived motion provenance");
    {
        // All inputs are actual completed textures from the preceding WARP run.
        auto input=residentFixture;D3D12_COMMAND_QUEUE_DESC q{};
        ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;
        Check(SUCCEEDED(input.device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))&&
            SUCCEEDED(input.device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&alloc)))&&
            SUCCEEDED(input.device->CreateCommandList(0,q.Type,alloc.Get(),nullptr,IID_PPV_ARGS(&list))),"resident fixture recording");
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={input.color.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
        list->ResourceBarrier(1,&barrier);Check(SUCCEEDED(list->Close()),"resident fixture close");
        Check(SUCCEEDED(input.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&input.producer))),"resident fixture fence");
        ID3D12CommandList* command=list.Get();queue->ExecuteCommandLists(1,&command);Check(SUCCEEDED(queue->Signal(input.producer.Get(),1)),"resident fixture signal");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event&&SUCCEEDED(input.producer->SetEventOnCompletion(1,event))&&WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"resident fixture completion");CloseHandle(event);
        input.value=1;input.lease=std::make_shared<int>(1);
        nrpg::CpuMainlineClient resident({Prepare,Record,Seal});nrpg::CpuMainlineClient::GpuOutput gpuOutput;
        const auto prior=seen;Check(resident.ProcessResident(luid,prior,input,gpuOutput,reason),"resident depth and completed GPU output without CPU payload");
        Check(gpuOutput.color&&gpuOutput.lease&&gpuOutput.completion.fence&&gpuOutput.completion.fence->GetCompletedValue()>=gpuOutput.completion.value,"resident output carries actual completion and reuse lease");
        auto next=prior;++next.capture;next.maskCapture=next.capture;nrpg::CpuMainlineClient::GpuOutput refused;
        Check(!resident.ProcessResident(luid,next,input,refused,reason),"leased output cannot be overwritten");
        gpuOutput={};Check(resident.ProcessResident(luid,next,input,gpuOutput,reason),"released output permits completed reuse");gpuOutput={};
        {
            N::Processor textures({Prepare,Record,Seal});
            N::TextureCapture captureTextures;captureTextures.description=prior;
            auto& description=captureTextures.description;
            description.sourceApi=P::C::GraphicsApi::D3D11;
            description.producer=121;description.session=122;description.stream=123;description.device=124;
            description.generation=125;description.capture=1;description.previousCapture=0;description.maskCapture=1;
            description.flags|=P::Reset;
            captureTextures.adapterLuid=luid;captureTextures.normalized=input;captureTextures.deriveMotion=false;
            Check(textures.ProcessTextures(captureTextures,gpuOutput,reason)==N::Outcome::Delivered,"completed canonical texture tuple reaches mainline");
            Check(seen.sourceApi==P::C::GraphicsApi::D3D11&&seen.device==124&&seen.stream==123,"texture entry preserves source API and device incarnation");
            auto held=gpuOutput;gpuOutput={};description.capture=2;description.maskCapture=2;
            Check(textures.ProcessTextures(captureTextures,gpuOutput,reason)==N::Outcome::Unavailable&&!gpuOutput.color,"leased output prevents next texture dispatch");
            held={};captureTextures.deriveMotion=true;captureTextures.normalized.motion.Reset();captureTextures.normalized.distrust.Reset();
            Check(textures.ProcessTextures(captureTextures,gpuOutput,reason)==N::Outcome::Delivered,"canonical color and depth derive real GPU motion");
            Check(seen.stream==123&&seen.capture==2&&seen.motionOrigin==P::C::SourceClass::Derived,"derived flow uses actual stream identity");gpuOutput={};
            Check(textures.ProcessTextures(captureTextures,gpuOutput,reason)==N::Outcome::Unavailable,"texture capture replay refused");
            description.capture=3;
            auto completedFence=captureTextures.normalized.producer;
            Check(SUCCEEDED(input.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&captureTextures.normalized.producer))),"pending texture producer");
            const auto beforeCalls=calls;
            Check(textures.ProcessTextures(captureTextures,gpuOutput,reason)==N::Outcome::Unavailable&&calls==beforeCalls,"pending producer rejected before flow or model");
            captureTextures.normalized.producer=completedFence;
            description.colorOrigin=static_cast<P::C::SourceClass>(255);
            Check(textures.ProcessTextures(captureTextures,gpuOutput,reason)==N::Outcome::Unavailable&&calls==beforeCalls,"invalid source semantics rejected before flow");
            std::vector<std::uint8_t> switched;
            Check(textures.Process(capture,switched,reason)==N::Outcome::Unavailable&&switched.empty()&&calls==beforeCalls,
                "raw entry cannot replace a committed texture owner or its flow history");
        }
        Check(SUCCEEDED(alloc->Reset())&&SUCCEEDED(list->Reset(alloc.Get(),nullptr)),"restore fixture recording");
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);Check(SUCCEEDED(list->Close()),"restore fixture close");
        queue->ExecuteCommandLists(1,&command);Check(SUCCEEDED(queue->Signal(input.producer.Get(),2)),"restore fixture signal");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event&&SUCCEEDED(input.producer->SetEventOnCompletion(2,event))&&WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"restore fixture completion");CloseHandle(event);
        seen=prior;calls=1;
    }
    Check(processor.Process(capture,output,reason)==N::Outcome::Unavailable&&output.empty()&&calls==1,"replay rejected without model dispatch");
    capture.capture=2;capture.reset=false;
    Check(processor.Process(capture,output,reason)==N::Outcome::Delivered&&output==color,"second immutable capture accepted after reader completion");
    preparing=true;capture.capture=3;
    Check(processor.Process(capture,output,reason)==N::Outcome::Preparing&&output.empty(),"completed initialization frame returns preparing without output");preparing=false;
    capture.capture=4;
    Check(processor.Process(capture,output,reason)==N::Outcome::Delivered&&output==color&&(seen.flags&P::Reset),"frame after initialization can deliver safely");
    for(unsigned i=5;i<=8;++i){capture.capture=i;Check(processor.Process(capture,output,reason)==N::Outcome::Delivered,"software flow history advances after warmup");}
    Check(!(seen.flags&P::Reset)&&seen.previousCapture==7&&seen.capture==8,"completed software flow preserves real pair after warmup");
    capture.capture=10;
    Check(processor.Process(capture,output,reason)==N::Outcome::Delivered&&(seen.flags&P::Reset)&&seen.previousCapture==0,
        "skipped capture resets flow and mainline history without caller reset");
    auto invalid=capture;invalid.capture=3;invalid.depth=std::span(depth).first(depth.size()-1);
    Check(processor.Process(invalid,output,reason)==N::Outcome::Unavailable&&output.empty(),"incomplete depth rejected");
    depth[0]=std::numeric_limits<float>::quiet_NaN();capture.capture=3;
    Check(processor.Process(capture,output,reason)==N::Outcome::Unavailable&&output.empty(),"nonfinite depth rejected");depth[0]=.25f;
    capture.session=11;capture.capture=1;
    Check(processor.Process(capture,output,reason)==N::Outcome::Delivered&&(seen.flags&P::Reset),"session change resets capture pairing");
    capture.generation=21;capture.capture=1;capture.width=512;color.resize(512*64*4,127);depth.resize(512*64,.25f);capture.bgra=color;capture.depth=depth;
    Check(processor.Process(capture,output,reason)==N::Outcome::Delivered&&output==color&&(seen.flags&P::Reset),"completed resize reallocates and resets");
    invalid=capture;invalid.adapterLuid=0;
    Check(processor.Process(invalid,output,reason)==N::Outcome::Unavailable&&output.empty(),"unknown adapter never chooses default");
    invalid=capture;invalid.width=0;invalid.capture=2;
    Check(processor.Process(invalid,output,reason)==N::Outcome::Unavailable&&output.empty(),"zero dimensions rejected before work");
    depth[0]=1.1f;invalid=capture;invalid.capture=2;
    Check(processor.Process(invalid,output,reason)==N::Outcome::Unavailable&&output.empty(),"out-of-range device depth rejected");depth[0]=.25f;
    capture.capture=2;capture.depthInverted=true;
    Check(processor.Process(capture,output,reason)==N::Outcome::Delivered&&(seen.flags&P::Reset)&&seen.depthReversed==1,"observed reversed-depth change resets history");
    // A refused seal can hide retained provider use even after our fence. The
    // native singleton cannot allocate another owner around that uncertainty.
    refuseSeal=true;capture.capture=3;
    Check(processor.Process(capture,output,reason)==N::Outcome::Unsafe&&output.empty(),"seal refusal quarantines native owner");refuseSeal=false;
    auto before=calls;capture.capture=4;
    Check(processor.Process(capture,output,reason)==N::Outcome::Unsafe&&output.empty()&&calls==before,"quarantined owner cannot dispatch or return stale output");
    N::Processor rejected({Prepare,Record,Seal,Detail});refuseRecord=true;
    Check(rejected.Process(capture,output,reason)==N::Outcome::Unsafe&&output.empty(),"partial refused recording quarantines native owner");
    Check(reason.find("record result=Refused")!=std::string::npos&&reason.find("fixture model refusal detail")!=std::string::npos,"native processor preserves specific core result");refuseRecord=false;
    {
        NativeGpuFixture f;N::Processor shared({Prepare,Record,Seal},f.device.Get(),NativeFsrFixture::Api());
        constexpr unsigned w=256,h=64;const size_t sharedBytes=w*h*4;
        auto rawColor=f.Buffer(sharedBytes),rawDepth=f.Buffer(sharedBytes),rawOutput=f.Buffer(sharedBytes),rawGenerated=f.Buffer(sharedBytes);bool generatedReady=false;
        std::vector<uint8_t> colors(sharedBytes),depths(sharedBytes);for(size_t i=0;i<sharedBytes;i+=4){colors[i]=10;colors[i+1]=71;colors[i+2]=193;colors[i+3]=255;float sampleDepth=.25f;std::memcpy(depths.data()+i,&sampleDepth,4);}
        f.Upload(rawColor.Get(),colors);f.Upload(rawDepth.Get(),depths);
        ComPtr<ID3D12Fence> outputFence;Check(SUCCEEDED(f.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&outputFence))),"shared output fence");
        N::Capture c{f.Luid(),800,900,1,w,h,true,false,{},{}};
        N::SharedCapture raw{rawColor.Get(),rawDepth.Get(),rawOutput.Get(),f.fence.Get(),outputFence.Get(),f.serial,32,true,rawGenerated.Get(),&generatedReady};
        auto outcome=shared.ProcessShared(c,raw,reason);if(outcome!=N::Outcome::Delivered)std::fprintf(stderr,"shared: %s\n",reason.c_str());
        Check(outcome==N::Outcome::Delivered&&outputFence->GetCompletedValue()>=raw.value,"shared raw buffers reach flow/core and completed GPU output");
        ComPtr<ID3D12Resource> firstNormalizedDepth=residentFixture.depth;
        Check(f.Read(rawOutput.Get(),sharedBytes)==colors,"shared flow/core/pack output is exact without CPU pixel payload");
        // Submitted producer work may still be pending when the consumer records.
        // An actual GPU queue wait must protect decoding, including early refusal.
        ComPtr<ID3D12Fence> pending;
        Check(SUCCEEDED(f.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&pending))),"pending producer fence");
        raw.inputFence=pending.Get();raw.value=f.serial+1;++c.capture;
        colors[0]=47;
        std::thread producer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(60));f.Upload(rawColor.Get(),colors);pending->Signal(raw.value);});
        outcome=shared.ProcessShared(c,raw,reason);producer.join();
        Check(outcome==N::Outcome::Delivered&&outputFence->GetCompletedValue()>=raw.value,"pending producer reaches completed output through GPU dependency");
        ComPtr<ID3D12Resource> secondNormalizedDepth=residentFixture.depth;
        Check(firstNormalizedDepth.Get()!=secondNormalizedDepth.Get(),"flow history uses distinct normalized slots");
        Check(f.Read(rawOutput.Get(),sharedBytes)==colors,"consumer observes delayed producer pixels rather than stale buffer contents");
        ++raw.value;++c.capture;auto invalidShared=c;invalidShared.width=0;
        bool returnedEarly=false;
        std::thread refusedProducer([&]{std::this_thread::sleep_for(std::chrono::milliseconds(60));returnedEarly=outputFence->GetCompletedValue()>=raw.value;pending->Signal(raw.value);});
        outcome=shared.ProcessShared(invalidShared,raw,reason);refusedProducer.join();
        Check(outcome==N::Outcome::Unavailable&&!returnedEarly&&outputFence->GetCompletedValue()>=raw.value,"early refusal cannot return buffers before pending producer completes");
        f.serial=raw.value;f.Begin();f.Finish();raw.inputFence=f.fence.Get();raw.value=f.serial;
        preparing=true;++c.capture;raw.value=f.serial;c.reset=false;
        Check(shared.ProcessShared(c,raw,reason)==N::Outcome::Preparing&&outputFence->GetCompletedValue()>=raw.value,"shared preparing frame signals safe resource return");preparing=false;
        Check(residentFixture.depth.Get()==firstNormalizedDepth.Get(),"completed normalized slot reused after history lease released");
        f.Begin();f.Finish();raw.value=f.serial;++c.capture;
        Check(shared.ProcessShared(c,raw,reason)==N::Outcome::Delivered&&(seen.flags&P::Reset),"shared model preparation recovers with history reset");
        Check(residentFixture.depth.Get()==secondNormalizedDepth.Get(),"second normalized slot reused without per-frame allocation");
        Check(!generatedReady,"shared FG history reset emits only real frame");
        for(unsigned i=0;i<8;++i){f.Begin();f.Finish();raw.value=f.serial;++c.capture;c.reset=false;
            Check(shared.ProcessShared(c,raw,reason)==N::Outcome::Delivered,"shared NR then FSR fixture completed");}
        if(!generatedReady)std::fprintf(stderr,"FG shared detail: %s; dispatches=%u\n",reason.c_str(),NativeFsrFixture::dispatched);
        Check(generatedReady&&NativeFsrFixture::dispatched>=2,"resident NR guides reach FSR and completed generated publication");
        Check(f.Read(rawOutput.Get(),sharedBytes)==colors,"real NR output retains distinct pixels while generated output is packed");
        auto generatedPixels=f.Read(rawGenerated.Get(),sharedBytes);for(size_t i=0;i<sharedBytes;i+=4)Check(generatedPixels[i]==77&&generatedPixels[i+1]==33&&generatedPixels[i+2]==17&&generatedPixels[i+3]==255,"generated GPU pixels retain separate packing descriptors");
        float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(depths.data(),&nan,4);f.Upload(rawDepth.Get(),depths);raw.value=f.serial;++c.capture;
        const auto callCount=calls;Check(shared.ProcessShared(c,raw,reason)==N::Outcome::Unavailable&&calls==callCount&&outputFence->GetCompletedValue()>=raw.value,"invalid GPU depth never reaches model and returns completed transport");
        float quarter=.25f;std::memcpy(depths.data(),&quarter,4);f.Upload(rawDepth.Get(),depths);raw.value=f.serial;++c.capture;
        c.width=512;c.height=64;
        colors.resize(size_t(c.width)*c.height*4,127);depths.resize(colors.size());
        for(size_t i=0;i<depths.size();i+=4)std::memcpy(depths.data()+i,&quarter,4);
        rawColor=f.Buffer(colors.size());rawDepth=f.Buffer(colors.size());rawOutput=f.Buffer(colors.size());rawGenerated=f.Buffer(colors.size());raw.generated=rawGenerated.Get();
        f.Upload(rawColor.Get(),colors);f.Upload(rawDepth.Get(),depths);
        raw.color=rawColor.Get();raw.depth=rawDepth.Get();raw.output=rawOutput.Get();raw.value=f.serial;
        Check(shared.ProcessShared(c,raw,reason)==N::Outcome::Delivered&&(seen.flags&P::Reset),"shared resize retires old pool after flow completion");
        Check(residentFixture.depth->GetDesc().Width==512&&residentFixture.depth.Get()!=firstNormalizedDepth.Get()&&residentFixture.depth.Get()!=secondNormalizedDepth.Get(),"resized capture allocates correctly shaped replacement slots");
        Check(f.Read(rawOutput.Get(),colors.size())==colors,"resized pooled output preserves exact current pixels");
        auto texture=[&](DXGI_FORMAT format){ComPtr<ID3D12Resource> result;D3D12_RESOURCE_DESC d{};
            d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=c.width;d.Height=c.height;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Format=format;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            Check(SUCCEEDED(f.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&result))),"export texture allocation");return result;};
        auto guideDepth=texture(DXGI_FORMAT_R32_FLOAT),guideMotion=texture(DXGI_FORMAT_R16G16_FLOAT);
        raw.generated=nullptr;raw.guideDepth=guideDepth.Get();raw.guideMotion=guideMotion.Get();unsigned exported=0;
        raw.consumeGuides=[&](const N::ExportedGuides& g,std::string&){
            Check(outputFence->GetCompletedValue()>=raw.value,"guide copies complete before Vulkan consumer entry");
            Check(g.capture==c.capture&&g.stream==c.session&&g.generation==c.generation&&g.previous<c.capture,"export preserves actual flow pair");
            ++exported;return N::Outcome::Delivered;};
        f.Begin();f.Finish();raw.value=f.serial;++c.capture;
        auto beforeExport=calls;Check(shared.ProcessShared(c,raw,reason)==N::Outcome::Delivered&&exported==1&&calls==beforeExport,"export path bypasses D3D12 model and returns completed guides");
        auto readTexture=[&](ID3D12Resource* source){
            auto description=source->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,total=0;
            f.device->GetCopyableFootprints(&description,0,1,0,&footprint,&rows,&rowBytes,&total);
            auto staging=f.Buffer(total,D3D12_HEAP_TYPE_READBACK);f.Begin();
            N::GpuPixels::Transition(f.list.Get(),source,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=source;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=staging.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
            f.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            N::GpuPixels::Transition(f.list.Get(),source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);f.Finish();
            void* mapped=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(total)};
            Check(SUCCEEDED(staging->Map(0,&range,&mapped)),"map completed guide export");
            std::vector<uint8_t> pixels(static_cast<size_t>(rowBytes)*rows);
            for(UINT row=0;row<rows;++row)std::memcpy(pixels.data()+row*rowBytes,static_cast<uint8_t*>(mapped)+footprint.Offset+row*footprint.Footprint.RowPitch,static_cast<size_t>(rowBytes));
            D3D12_RANGE none{0,0};staging->Unmap(0,&none);return pixels;};
        const auto exportedDepth=readTexture(guideDepth.Get()),exportedMotion=readTexture(guideMotion.Get());
        Check(exportedDepth.size()==depths.size()&&exportedMotion.size()==depths.size(),"exported texture footprints match current capture");
        for(size_t i=0;i<exportedDepth.size();i+=4){float sample=0;std::memcpy(&sample,exportedDepth.data()+i,4);
            Check(sample==quarter,"exported depth preserves measured device-depth pixels");}
        for(size_t i=0;i<exportedMotion.size();i+=2){uint16_t sample=0;std::memcpy(&sample,exportedMotion.data()+i,2);
            Check((sample&0x7c00)!=0x7c00,"exported estimated motion contains finite half-float samples");}
        raw.consumeGuides=[&](const N::ExportedGuides&,std::string&){++exported;return N::Outcome::Unavailable;};
        f.Begin();f.Finish();raw.value=f.serial;++c.capture;
        Check(shared.ProcessShared(c,raw,reason)==N::Outcome::Unavailable&&exported==2&&calls==beforeExport,"refused Vulkan consumer never retries D3D12 model");
        raw.guideDepth=rawOutput.Get();f.Begin();f.Finish();raw.value=f.serial;++c.capture;
        Check(shared.ProcessShared(c,raw,reason)==N::Outcome::Unavailable&&exported==2&&calls==beforeExport,"wrong export shape rejected before consumer");
        raw.guideDepth=nullptr;raw.guideMotion=nullptr;raw.consumeGuides={};raw.generated=rawGenerated.Get();c.reset=true;
        f.Begin();f.Finish();
        raw.value=f.serial;++c.capture;
        refuseSeal=true;Check(shared.ProcessShared(c,raw,reason)==N::Outcome::Unsafe,"shared model seal refusal quarantines owners");refuseSeal=false;
        Check(outputFence->GetCompletedValue()<raw.value,"unsafe shared work never signals reusable output");
    }
    N::Processor lost({Prepare,Record,Seal});removeAtSeal=true;
    Check(lost.Process(capture,output,reason)==N::Outcome::Unsafe&&output.empty(),"actual mainline device removal never publishes native output");removeAtSeal=false;
    std::puts("PASS: native FFX WARP capture-to-core transport and admission (explicit recorder; no private model/game claim)");
}
#endif
