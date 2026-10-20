// GPL-3.0. Five timestamps, one existing direct-queue submission. No profiling waits.
#pragma once
#include "../PerformanceMetrics.h"
namespace nrw {
class GpuTimings {
    ComPtr<ID3D12QueryHeap> heap;
    ComPtr<ID3D12Resource> readback;
    uint64_t frequency=0;
    unsigned mask=0;
public:
    void Initialize(ID3D12Device* device,ID3D12CommandQueue* queue) {
        D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=5;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width=5*sizeof(uint64_t);rd.Height=rd.DepthOrArraySize=rd.MipLevels=1;
        rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(queue->GetTimestampFrequency(&frequency)) || !frequency ||
           FAILED(device->CreateQueryHeap(&q,IID_PPV_ARGS(&heap))) ||
           FAILED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,
             D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)))) {
            heap.Reset();readback.Reset();frequency=0;
        }
    }
    void Begin(){mask=0;}
    void Mark(ID3D12GraphicsCommandList* list,unsigned index) {
        if(!heap || index>=5)return;
        list->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,index);mask|=1u<<index;
    }
    void Resolve(ID3D12GraphicsCommandList* list) {
        if(mask==31)list->ResolveQueryData(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,5,readback.Get(),0);
    }
    void Collect(ID3D12Fence* fence,uint64_t required,NrMeasurements& out) {
        out.gpu={};out.timestampFrequency=frequency;out.completionFence=required;
        if(mask!=31 || !fence)return;
        auto completed=fence->GetCompletedValue();
        if(!TimestampSpan(0,0,frequency,completed,required))return; // check before Map, never wait
        D3D12_RANGE range{0,5*sizeof(uint64_t)};uint64_t* ticks=nullptr;
        if(FAILED(readback->Map(0,&range,reinterpret_cast<void**>(&ticks))))return;
        for(unsigned i=0;i<4;++i)out.gpu[i]=TimestampSpan(ticks[i],ticks[i+1],frequency,completed,required);
        D3D12_RANGE noWrite{};readback->Unmap(0,&noWrite);
    }
};
}
