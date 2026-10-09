#ifndef CHARACTER_CAPTURE_TEST
#include "pch.h"
#endif
#include "CharacterCaptureDx12.h"
#include "CharacterThumbnailBytecode.h"
#include <dlssnr/NativeIdentity.h>
#include <d3dcompiler.h>
#include <cstring>
#include <chrono>
#include <thread>
#ifdef CHARACTER_CAPTURE_TEST
#include <iostream>
#endif
namespace Neurotic::Semantic::Character {
using Microsoft::WRL::ComPtr;
namespace {

void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
bool Done(ID3D12Fence* f,UINT64 value){if(!f||!value)return false;auto v=f->GetCompletedValue();return v!=UINT64_MAX&&v>=value;}
}
bool CharacterCaptureDx12::Initialize(ID3D12Device* device){
    if(device_.Get()==device && pipeline_)return true;
    // Never replace resources for an unretired generation.
    for(auto& s:slots_)if(s.state.phase==CapturePhase::Submitted)return false;
    for(auto& s:slots_)s=Slot{};
    pipeline_.Reset();root_.Reset();device_=device;
    ComPtr<ID3DBlob> error,signature;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0};
    D3D12_ROOT_PARAMETER params[3]{};
    for(unsigned i=0;i<2;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};}
    params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,6};
    D3D12_ROOT_SIGNATURE_DESC rs{3,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    if(Failed(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error),"SerializeRootSignature") ||
       Failed(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root_)),"CreateRootSignature"))return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root_.Get();pso.CS={g_CharacterThumbnail,sizeof(g_CharacterThumbnail)};
#ifdef CHARACTER_CAPTURE_TEST
    std::cerr<<"capture pipeline create\n";
#endif
    return !Failed(device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&pipeline_)),"CreateComputePipelineState");
}
bool CharacterCaptureDx12::Prepare(Slot& s,ID3D12Device* device,unsigned w,unsigned h,DXGI_FORMAT){
    if(s.thumbnail && s.metadata.width==w && s.metadata.height==h)return true;
    // The slot is free here; no submitted resource can be replaced.
    s=Slot{};
    if(Failed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&s.allocator)),"CreateCommandAllocator") ||
       Failed(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator.Get(),nullptr,IID_PPV_ARGS(&s.list)),"CreateCommandList") ||
       Failed(s.list->Close(),"PrepareClose") || Failed(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.fence)),"CreateFence"))return false;
    D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    if(Failed(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&s.heap)),"CreateDescriptorHeap"))return false;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=w;td.Height=h;
    td.DepthOrArraySize=1;td.MipLevels=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if(Failed(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&s.thumbnail)),"CreateThumbnail"))return false;
    device->GetCopyableFootprints(&td,0,1,0,&s.footprint,nullptr,nullptr,&s.bytes);
    if(!s.bytes||s.bytes>16*1024*1024)return false;
    hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rb{};rb.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rb.Width=s.bytes;rb.Height=1;rb.DepthOrArraySize=1;rb.MipLevels=1;rb.SampleDesc.Count=1;rb.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(Failed(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rb,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&s.readback)),"CreateReadback"))return false;
    s.metadata.width=w;s.metadata.height=h;return true;
}
bool CharacterCaptureDx12::TrySubmit(ID3D12CommandQueue* queue,ID3D12Resource* source,CpuFrame metadata){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock||!queue||!source)return false;
    return SubmitLocked(queue,source,std::move(metadata));
}
bool CharacterCaptureDx12::TrySubmitSwapchain(ID3D12CommandQueue* queue,IDXGISwapChain3* chain,CpuFrame metadata){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock||!queue||!chain||metadata.captureGeneration!=admissionGeneration_)return false;
    // Acquisition and all temporary references share the retirement lock. No
    // paused Present can retain an old buffer after a resize gate has completed.
    ComPtr<ID3D12Resource> source;const auto acquired=chain->GetBuffer(chain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&source));
    if(Failed(acquired,"GetBuffer"))return false;
    // Resolve the acquired application resource for a native command list;
    // never replace the application proxy swapchain with its physical output.
    const auto nativeSource=DlssNr::NativeIdentity::Resolve<ID3D12Resource>(source.Get());
    if(!nativeSource.object)return false;
    return SubmitLocked(queue,nativeSource.object.Get(),std::move(metadata));
}
bool CharacterCaptureDx12::SubmitLocked(ID3D12CommandQueue* queue,ID3D12Resource* source,CpuFrame metadata){
    if(metadata.captureGeneration!=admissionGeneration_)return false;
    ComPtr<ID3D12Device> device,sourceDevice;
    if(Failed(queue->GetDevice(IID_PPV_ARGS(&device)),"QueueGetDevice")||Failed(source->GetDevice(IID_PPV_ARGS(&sourceDevice)),"SourceGetDevice")||device.Get()!=sourceDevice.Get())return false;
    const auto desc=source->GetDesc();
    sourceFormat_=static_cast<unsigned>(desc.Format);sourceFlags_=static_cast<unsigned>(desc.Flags);
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.SampleDesc.Count!=1||desc.DepthOrArraySize!=1||desc.MipLevels!=1||
       !desc.Width||!desc.Height||desc.Width>32768||desc.Height>32768||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))return false;
    const bool sdr=metadata.colorPolicy==0&&(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM||desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM);
    const bool pq=metadata.colorPolicy==1&&desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM;
    const bool sc=metadata.colorPolicy==2&&desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(!sdr&&!pq&&!sc)return false;
    if(queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
    const double scale=std::min(1.,960./static_cast<double>(std::max(desc.Width,static_cast<UINT64>(desc.Height))));
    const auto w=std::max(1u,static_cast<unsigned>(desc.Width*scale)),h=std::max(1u,static_cast<unsigned>(desc.Height*scale));
    requestedDevice_=device;requestedWidth_=w;requestedHeight_=h;
    if(device_.Get()!=device.Get()||!pipeline_)return false;
    Slot* slot=nullptr;for(auto& ready:slots_)if(ready.state.phase==CapturePhase::Free&&ready.thumbnail&&ready.readback&&ready.metadata.width==w&&ready.metadata.height==h){slot=&ready;break;}if(!slot)return false;
    auto& s=*slot;
    if(Failed(s.allocator->Reset(),"AllocatorReset")||Failed(s.list->Reset(s.allocator.Get(),pipeline_.Get()),"ListReset"))return false;
    s.state.Record();
    metadata.width=w;metadata.height=h;metadata.stride=w*4;s.metadata=std::move(metadata);s.source=source;
    auto cpu=s.heap->GetCPUDescriptorHandleForHeapStart();auto gpu=s.heap->GetGPUDescriptorHandleForHeapStart();
    const auto increment=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
    device->CreateShaderResourceView(source,&srv,cpu);cpu.ptr+=increment;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R8G8B8A8_UNORM;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(s.thumbnail.Get(),nullptr,&uav,cpu);
    Transition(s.list.Get(),source,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    s.list->SetComputeRootSignature(root_.Get());auto* heap=s.heap.Get();s.list->SetDescriptorHeaps(1,&heap);
    s.list->SetComputeRootDescriptorTable(0,gpu);gpu.ptr+=increment;s.list->SetComputeRootDescriptorTable(1,gpu);
    const struct { unsigned sw,sh,tw,th,policy;float exposure; } sizes{
        static_cast<unsigned>(desc.Width),desc.Height,w,h,s.metadata.colorPolicy,1.f};
    s.list->SetComputeRoot32BitConstants(2,6,&sizes,0);
    s.list->Dispatch((w+7)/8,(h+7)/8,1);
    Transition(s.list.Get(),source,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_PRESENT);
    Transition(s.list.Get(),s.thumbnail.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=s.readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=s.footprint;
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=s.thumbnail.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    s.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    Transition(s.list.Get(),s.thumbnail.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(Failed(s.list->Close(),"SubmitClose")){s.source.Reset();s.state.Cancel();return false;}
    std::unique_lock submission(submissionMutex_,std::try_to_lock);
    if(!submission||s.metadata.captureGeneration!=admissionGeneration_){s.source.Reset();s.state.Cancel();return false;}
    auto* list=static_cast<ID3D12CommandList*>(s.list.Get());s.state.Submit();
    queue->ExecuteCommandLists(1,&list);++s.completion;
    const auto signaled=queue->Signal(s.fence.Get(),s.completion);
    if(Failed(signaled,"QueueSignal")){s.completion=UINT64_MAX;return false;} // Preserve submitted resources.
    return true;
}
bool CharacterCaptureDx12::TryTakeNewest(CpuFrame& frame){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return false;
    Slot* newest=nullptr;
    for(auto& s:slots_)if(s.state.phase==CapturePhase::Submitted&&Done(s.fence.Get(),s.completion)){
        s.state.Complete();s.source.Reset();
    }
    for(auto& s:slots_)if(s.state.phase==CapturePhase::Complete&&(!newest||s.metadata.key.sequence>newest->metadata.key.sequence))newest=&s;
    if(!newest)return false;
    auto& s=*newest;void* pixels=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(s.bytes)};
    const auto mappedResult=s.readback->Map(0,&range,&pixels);
    if(Failed(mappedResult,"ReadbackMap"))return false;
    struct UnmapOnExit{ID3D12Resource* resource;~UnmapOnExit(){const D3D12_RANGE empty{0,0};resource->Unmap(0,&empty);}} mapped{s.readback.Get()};
    frame=s.metadata;frame.pixels.resize(static_cast<std::size_t>(frame.stride)*frame.height);
    for(unsigned y=0;y<frame.height;++y)std::memcpy(frame.pixels.data()+static_cast<std::size_t>(y)*frame.stride,
        static_cast<unsigned char*>(pixels)+s.footprint.Offset+static_cast<std::size_t>(y)*s.footprint.Footprint.RowPitch,frame.stride);
    for(auto& ready:slots_)if(ready.state.phase==CapturePhase::Complete)ready.state.Release();
    return true;
}
void CharacterCaptureDx12::PollRetirement(){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return;
    for(auto& s:slots_)if(s.state.phase==CapturePhase::Submitted&&Done(s.fence.Get(),s.completion)){s.source.Reset();s.state.Complete();}
}
CaptureDiagnostics CharacterCaptureDx12::TryDiagnostics(){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return {};
    CaptureDiagnostics result{true,sourceFormat_,sourceFlags_,0,lastError_,errorOperation_};
    for(const auto& slot:slots_)if(slot.state.phase==CapturePhase::Submitted)++result.pending;
    return result;
}
void CharacterCaptureDx12::PrepareRequested(){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock||!requestedDevice_||!requestedWidth_||!requestedHeight_)return;
#ifdef CHARACTER_CAPTURE_TEST
    if(preparationTestHook_)preparationTestHook_();
#endif
    for(auto& s:slots_)if(s.state.phase==CapturePhase::Submitted&&Done(s.fence.Get(),s.completion)){s.source.Reset();s.state.Complete();}
    if(!Initialize(requestedDevice_.Get()))return;
    for(auto& s:slots_)if(s.state.phase==CapturePhase::Free)Prepare(s,requestedDevice_.Get(),requestedWidth_,requestedHeight_,DXGI_FORMAT_R8G8B8A8_UNORM);
}
bool CharacterCaptureDx12::RetireSources(unsigned timeoutMs){
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(std::min(timeoutMs,500u));
    do{
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(lock){bool pending=false;
            for(auto& s:slots_)if(s.source){
                if(s.state.phase==CapturePhase::Submitted&&Done(s.fence.Get(),s.completion)){s.source.Reset();s.state.Complete();}
                else pending=true;
            }
            if(!pending)return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(std::chrono::steady_clock::now()<end);
    return false; // Pending source/allocator/readback ownership remains intact.
}
}
