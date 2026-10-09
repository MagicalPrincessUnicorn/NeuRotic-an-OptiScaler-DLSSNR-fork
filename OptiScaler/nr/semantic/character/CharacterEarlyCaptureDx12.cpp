#ifndef CHARACTER_EARLY_CAPTURE_TEST
#include "pch.h"
#include <Logger.h>
#endif
#include "CharacterEarlyCaptureDx12.h"
#include "CharacterThumbnailBytecode.h"
#include <hooks/D3D12_Hooks.h>
#include <dlssnr/NativeIdentity.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace Neurotic::Semantic::Character {
using Microsoft::WRL::ComPtr;
namespace {
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};
    list->ResourceBarrier(1,&barrier);
}
bool Compatible(const D3D12_RESOURCE_DESC& desc,const CpuFrame& metadata){
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||desc.SampleDesc.Count!=1||
       desc.DepthOrArraySize!=1||desc.MipLevels!=1||!desc.Width||!desc.Height||
       desc.Width>32768||desc.Height>32768||(desc.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))return false;
    if(metadata.colorPolicy==0)return desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM;
    return metadata.colorPolicy==3&&std::isfinite(metadata.preExposure)&&metadata.preExposure>0&&
        (desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||desc.Format==DXGI_FORMAT_R32G32B32A32_FLOAT);
}
}
std::shared_ptr<CharacterEarlyCaptureDx12::Backing> CharacterEarlyCaptureDx12::Prepare(
    ID3D12Device* device,unsigned width,unsigned height){
    D3D12Hooks::ScopedLegacyCaptureSuppression legacyCapture;
    auto backing=std::make_shared<Backing>();backing->device=device;backing->width=width;backing->height=height;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0};
    D3D12_ROOT_PARAMETER parameters[3]{};
    for(unsigned i=0;i<2;++i){parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[i].DescriptorTable={1,&ranges[i]};}
    parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[2].Constants={0,0,6};
    D3D12_ROOT_SIGNATURE_DESC signature{3,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob,error;
    if(FAILED(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error))||
       FAILED(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&backing->root))))return {};
    // This exact private layout must be known before its setters enter the
    // journal. Unknown inserted signatures would taint the caller's recording.
    using Kind=Neurotic::D3D12::RootKind;
    if(!D3D12Hooks::RegisterNativeRootLayout(backing->root.Get(),{{Kind::Table},{Kind::Table},{Kind::Constants,6}}))return {};
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=backing->root.Get();
    pipeline.CS={g_CharacterThumbnail,sizeof(g_CharacterThumbnail)};
    if(FAILED(device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&backing->pipeline))))return {};
    D3D12_DESCRIPTOR_HEAP_DESC descriptors{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    if(FAILED(device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&backing->heap))))return {};
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=width;texture.Height=height;
    texture.DepthOrArraySize=1;texture.MipLevels=1;texture.Format=DXGI_FORMAT_R8G8B8A8_UNORM;texture.SampleDesc.Count=1;
    texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,IID_PPV_ARGS(&backing->thumbnail))))return {};
    device->GetCopyableFootprints(&texture,0,1,0,&backing->footprint,nullptr,nullptr,&backing->bytes);
    if(!backing->bytes||backing->bytes>4ull*1024*1024)return {};
    heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=backing->bytes;
    buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,IID_PPV_ARGS(&backing->readback))))return {};
    return backing;
}
void CharacterEarlyCaptureDx12::PollLocked(){
    for(auto& slot:slots_)if(slot.recorded&&!slot.complete&&DlssNr::GpuSafety::Reusable(slot.ticket)){
        const auto observation=DlssNr::GpuSafety::InspectRecording(slot.ticket);
        slot.backing->source.Reset();
        if(observation.uniqueSubmission&&DlssNr::GpuSafety::Readable(slot.ticket))slot.complete=true;
        else {slot.recorded=false;slot.ticket.reset();}
    }
}
bool CharacterEarlyCaptureDx12::TryRecord(ID3D12GraphicsCommandList* caller,ID3D12Resource* input,CpuFrame metadata){
    std::unique_lock lock(mutex_,std::try_to_lock);
    if(!lock||!caller||!input||restorationFailed_||metadata.captureGeneration!=admissionGeneration_)return false;
    const auto resolvedList=DlssNr::NativeIdentity::Resolve<ID3D12GraphicsCommandList>(caller);
    const auto resolvedSource=DlssNr::NativeIdentity::Resolve<ID3D12Resource>(input);
    auto* list=resolvedList.object.Get();auto* source=resolvedSource.object.Get();
    if(!list||!source||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT){reason_="Unsupported NGX recording";return false;}
    const auto desc=source->GetDesc();if(!Compatible(desc,metadata)){reason_="Unsupported NGX Color format";return false;}
    ComPtr<ID3D12Device> device,sourceDevice;
    if(FAILED(list->GetDevice(IID_PPV_ARGS(&device)))||FAILED(source->GetDevice(IID_PPV_ARGS(&sourceDevice)))||
       !DlssNr::NativeIdentity::CompareDevices(device.Get(),sourceDevice.Get()).equal)return false;
    const double scale=std::min(1.,960./static_cast<double>(std::max(desc.Width,static_cast<UINT64>(desc.Height))));
    const auto width=std::max(1u,static_cast<unsigned>(desc.Width*scale)),height=std::max(1u,static_cast<unsigned>(desc.Height*scale));
    requestedDevice_=device;requestedWidth_=width;requestedHeight_=height;
    // Cold enrollment observes the next real host Reset. It never resets or
    // seals a caller recording and cannot pretend its initial open state.
    auto ticket=DlssNr::GpuSafety::Record(list);
    if(!ticket){reason_="NGX recording tracking unavailable";return false;}
    Slot* ready=nullptr;
    for(auto& slot:slots_)if(!slot.recorded&&slot.backing&&slot.backing->width==width&&slot.backing->height==height&&
        DlssNr::NativeIdentity::CompareDevices(slot.backing->device.Get(),device.Get()).equal){ready=&slot;break;}
    if(!ready){reason_="Preparing early capture slots";return false;}
    std::unique_lock recording(recordingMutex_,std::try_to_lock);
    if(!recording||metadata.captureGeneration!=admissionGeneration_)return false;
    auto& slot=*ready;auto& backing=*slot.backing;
    ID3D12Resource* resources[]{source,backing.thumbnail.Get(),backing.readback.Get()};
    auto action=DlssNr::GpuSafety::BeginLocalAction(ticket,list,resources);
    if(!action){reason_="Waiting for an observed NGX recording reset";return false;}
    using Mask=Neurotic::D3D12::RestoreMask;
    Neurotic::D3D12::NativeStateCaptureDiagnostic diagnostic;
    auto saved=D3D12Hooks::CapturePostSrState(list,Mask::Compute|Mask::Pipeline|Mask::Heaps|Mask::HeapInvalidatedTables,&diagnostic);
    if(!saved){
        reason_=diagnostic.reason;
#ifndef CHARACTER_EARLY_CAPTURE_TEST
        static std::atomic<unsigned> reports{0};
        if(reports.fetch_add(1)<4){
            const auto hooks=D3D12Hooks::DiagnoseNativeRecording(list);
            LOG_WARN("CharacterInspector early refusal: reason={} parameter={} known={} required={} "
                     "hookReason={} slot={} result={:X} flags={} resets={} enrolled={} format={} input={}x{}",
                diagnostic.reason,diagnostic.parameter,diagnostic.known,diagnostic.required,
                hooks.firstRefusal,hooks.failedSlot,static_cast<unsigned>(hooks.firstResult),hooks.flags,
                hooks.resetSuccesses,hooks.enrollmentSuccesses,static_cast<unsigned>(desc.Format),desc.Width,desc.Height);
        }
#endif
        return false;
    }
    if(!DlssNr::GpuSafety::RetainDerivedUse(*action,slot.backing)){reason_="NGX capture retention unavailable";return false;}
    // Publish the complete backing before the first command. Even a later
    // failure or owner destruction cannot release a replayable recording's use.
    backing.source=source;slot.ticket=ticket;slot.recorded=true;slot.complete=false;
    metadata.width=width;metadata.height=height;metadata.stride=width*4;metadata.earlySource=true;slot.metadata=std::move(metadata);
    bool restored=false;
    struct Restore {
        const NativeStateRestorePoint& saved;bool& restored;std::atomic<bool>& failed;
        ~Restore(){try{restored=D3D12Hooks::RestorePostSrState(saved);}catch(...){restored=false;}if(!restored)failed=true;}
    };
    {
        // The native journal restores GPU state, while legacy upscaler/HUD
        // caches must never learn this private pass as the game's state.
        D3D12Hooks::ScopedLegacyCaptureSuppression legacyCapture;
        Restore restore{*saved,restored,restorationFailed_};
        auto cpu=backing.heap->GetCPUDescriptorHandleForHeapStart();auto gpu=backing.heap->GetGPUDescriptorHandleForHeapStart();
        const auto increment=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(source,&srv,cpu);cpu.ptr+=increment;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R8G8B8A8_UNORM;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(backing.thumbnail.Get(),nullptr,&uav,cpu);
        list->SetComputeRootSignature(backing.root.Get());list->SetPipelineState(backing.pipeline.Get());
        auto* heap=backing.heap.Get();list->SetDescriptorHeaps(1,&heap);
        list->SetComputeRootDescriptorTable(0,gpu);gpu.ptr+=increment;list->SetComputeRootDescriptorTable(1,gpu);
        const unsigned constants[]{static_cast<unsigned>(desc.Width),desc.Height,width,height,slot.metadata.colorPolicy,
            std::bit_cast<unsigned>(slot.metadata.preExposure)};
        list->SetComputeRoot32BitConstants(2,6,constants,0);list->Dispatch((width+7)/8,(height+7)/8,1);
        Transition(list,backing.thumbnail.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=backing.thumbnail.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource=backing.readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=backing.footprint;
        list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Transition(list,backing.thumbnail.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if(!restored||!action->Current()){restorationFailed_=true;reason_="Early capture command restoration failed";return false;}
    reason_="Captured NGX Color before upscaling";return true;
}
void CharacterEarlyCaptureDx12::PrepareRequested(){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return;
    PollLocked();if(!requestedDevice_||!requestedWidth_||!requestedHeight_)return;
    for(auto& slot:slots_)if(!slot.recorded){
        if(slot.backing&&slot.backing->width==requestedWidth_&&slot.backing->height==requestedHeight_&&
           DlssNr::NativeIdentity::CompareDevices(slot.backing->device.Get(),requestedDevice_.Get()).equal)continue;
        auto prepared=Prepare(requestedDevice_.Get(),requestedWidth_,requestedHeight_);
        if(prepared)slot.backing=std::move(prepared);else reason_="Early capture allocation failed";
    }
}
bool CharacterEarlyCaptureDx12::TryTakeNewest(CpuFrame& frame){
    std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return false;
    PollLocked();Slot* newest=nullptr;
    for(auto& slot:slots_)if(slot.complete&&(!newest||slot.metadata.key.sequence>newest->metadata.key.sequence))newest=&slot;
    if(!newest)return false;
    auto& backing=*newest->backing;void* pixels=nullptr;D3D12_RANGE read{0,static_cast<SIZE_T>(backing.bytes)};
    if(FAILED(backing.readback->Map(0,&read,&pixels))){reason_="Early capture readback mapping failed";return false;}
    struct Unmap{ID3D12Resource* resource;~Unmap(){const D3D12_RANGE written{0,0};resource->Unmap(0,&written);}} unmap{backing.readback.Get()};
    frame=newest->metadata;frame.pixels.resize(static_cast<size_t>(frame.stride)*frame.height);
    for(unsigned y=0;y<frame.height;++y)std::memcpy(frame.pixels.data()+static_cast<size_t>(y)*frame.stride,
        static_cast<unsigned char*>(pixels)+backing.footprint.Offset+static_cast<size_t>(y)*backing.footprint.Footprint.RowPitch,frame.stride);
    for(auto& slot:slots_)if(slot.complete){slot.recorded=slot.complete=false;slot.ticket.reset();}
    return true;
}
bool CharacterEarlyCaptureDx12::RetireSources(unsigned timeoutMs){
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(std::min(timeoutMs,500u));
    do {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(lock){PollLocked();bool pending=false;for(const auto& slot:slots_)if(slot.backing&&slot.backing->source)pending=true;if(!pending)return true;}
        if(lock)lock.unlock();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(std::chrono::steady_clock::now()<end);
    return false;
}
}
