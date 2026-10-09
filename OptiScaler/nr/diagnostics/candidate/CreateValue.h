#pragma once
#include "Observer.h"
#include <d3d12.h>
#include <cstring>
#include "../../../dlssnr/FrameTraceContract.h"

namespace DlssNr::CandidateObserver {
// Reads only documented call arguments and the permitted S_OK output-storage value.
// No method is invoked on any foreign interface.
inline Event CreateValue(bool placed,bool normal,HRESULT result,ID3D12Device* device,
                         const D3D12_RESOURCE_DESC* desc,D3D12_RESOURCE_STATES initial,
                         REFIID iid,void** output,const D3D12_HEAP_PROPERTIES* heapProperties,
                         D3D12_HEAP_FLAGS heapFlags,ID3D12Heap* heap,uint64_t offset) noexcept {
    Event e{};e.result=static_cast<uint32_t>(result);
    e.source=normal?static_cast<uint16_t>(placed?2:1):3;
    if(!normal) {e.kind=Kind::Alternate;return e;}
    const bool resourceIid=iid==__uuidof(ID3D12Resource);
    // Failed calls, capability-only calls and unknown interfaces never expose their outputs.
    const bool inspect=result==S_OK && output!=nullptr && resourceIid;
    uintptr_t bits=inspect?reinterpret_cast<uintptr_t>(*output):0;
    e.kind=Classify(true,e.result,output!=nullptr,bits!=0,resourceIid);
    if(e.kind==Kind::Committed && placed) e.kind=Kind::Placed;
    e.resourceBits=bits;e.deviceBits=reinterpret_cast<uintptr_t>(device);
    e.heapBits=reinterpret_cast<uintptr_t>(heap);e.heapOffset=offset;
    std::memcpy(e.iid,&iid,sizeof(e.iid));
    if(desc) {
        e.descriptorKnown=true;auto& d=e.descriptor;
        d.dimension=static_cast<uint32_t>(desc->Dimension);d.alignment=desc->Alignment;d.width=desc->Width;
        d.height=desc->Height;d.depth=desc->DepthOrArraySize;d.mips=desc->MipLevels;
        d.format=static_cast<uint32_t>(desc->Format);d.layout=static_cast<uint32_t>(desc->Layout);
        d.flags=static_cast<uint32_t>(desc->Flags);d.sampleCount=desc->SampleDesc.Count;
        d.sampleQuality=desc->SampleDesc.Quality;d.initialState=static_cast<uint32_t>(initial);
    }
    if(heapProperties) {
        e.heapKnown=true;e.heapType=static_cast<uint32_t>(heapProperties->Type);
        e.cpuPage=static_cast<uint32_t>(heapProperties->CPUPageProperty);
        e.memoryPool=static_cast<uint32_t>(heapProperties->MemoryPoolPreference);
        e.creationNode=heapProperties->CreationNodeMask;e.visibleNode=heapProperties->VisibleNodeMask;
    }
    e.heapFlags=static_cast<uint32_t>(heapFlags);
    e.nativeObservation=FrameTrace::nativeObservation;e.presentObservation=FrameTrace::presentObservation;
    return e;
}
}
