#pragma once
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dx/d3dx12.h>
#include <DirectXPackedVector.h>
#include <functional>
#include <iostream>
#include <vector>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
inline void HR(HRESULT hr){if(FAILED(hr)){std::cerr<<"HRESULT "<<std::hex<<hr<<'\n';std::exit(1);}}
struct AfnrWarp {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    struct Pending {ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;};
    std::vector<Pending> pending;
    void Submit(const std::function<void(ID3D12GraphicsCommandList*)>& record,bool hostReset=false,bool keepReplayable=false) {
        const auto type=queue->GetDesc().Type;
        Pending p;HR(device->CreateCommandAllocator(type,IID_PPV_ARGS(&p.allocator)));
        HR(device->CreateCommandList(0,type,p.allocator.Get(),nullptr,IID_PPV_ARGS(&p.list)));
        if(!DlssNr::GpuSafety::PrepareRecordingHooks(p.list.Get()))std::exit(2);
        HR(p.list->Close());HR(p.list->Reset(p.allocator.Get(),nullptr));
        auto ticket=DlssNr::GpuSafety::Record(p.list.Get());
        record(p.list.Get());HR(p.list->Close());ID3D12CommandList* lists[]{p.list.Get()};queue->ExecuteCommandLists(1,lists);
        if(hostReset) {HR(p.list->Reset(p.allocator.Get(),nullptr));HR(p.list->Close());}
        else if(!keepReplayable&&!DlssNr::GpuSafety::SealOwnedRecording(p.list.Get()))std::exit(3);
        pending.push_back(std::move(p));
    }
    void Wait() {
        ComPtr<ID3D12Fence> fence;HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HR(queue->Signal(fence.Get(),1));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)std::exit(4);HR(fence->SetEventOnCompletion(1,event));
        if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)std::exit(5);CloseHandle(event);pending.clear();
        DlssNr::GpuSafety::Pending();HR(device->GetDeviceRemovedReason());
    }
    AfnrWarp(D3D12_COMMAND_LIST_TYPE type=D3D12_COMMAND_LIST_TYPE_DIRECT){
        ComPtr<ID3D12Debug> debug;HR(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> f;HR(CreateDXGIFactory1(IID_PPV_ARGS(&f)));ComPtr<IDXGIAdapter> a;HR(f->EnumWarpAdapter(IID_PPV_ARGS(&a)));
        HR(D3D12CreateDevice(a.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=type;HR(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
    }
    ComPtr<ID3D12Resource> Buffer(UINT64 bytes,D3D12_HEAP_TYPE type){
        ComPtr<ID3D12Resource> r;auto heap=CD3DX12_HEAP_PROPERTIES(type);auto d=CD3DX12_RESOURCE_DESC::Buffer(bytes);
        HR(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));return r;
    }
    ComPtr<ID3D12Resource> Texture(DXGI_FORMAT format,unsigned w,unsigned h){
        ComPtr<ID3D12Resource> r;auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto d=CD3DX12_RESOURCE_DESC::Tex2D(format,w,h,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        HR(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));return r;
    }
    static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
        if(from==to)return;auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(resource,from,to);list->ResourceBarrier(1,&barrier);
    }
    void Run(const std::function<void(ID3D12GraphicsCommandList*)>& record){
        const auto type=queue->GetDesc().Type;
        ComPtr<ID3D12CommandAllocator> a;HR(device->CreateCommandAllocator(type,IID_PPV_ARGS(&a)));
        ComPtr<ID3D12GraphicsCommandList> l;HR(device->CreateCommandList(0,type,a.Get(),nullptr,IID_PPV_ARGS(&l)));
        if(!DlssNr::GpuSafety::PrepareRecordingHooks(l.Get()))std::exit(2);
        HR(l->Close());HR(l->Reset(a.Get(),nullptr));
        auto ticket=DlssNr::GpuSafety::Record(l.Get());record(l.Get());HR(l->Close());ID3D12CommandList* lists[]{l.Get()};queue->ExecuteCommandLists(1,lists);
        if(!DlssNr::GpuSafety::SealOwnedRecording(l.Get()))std::exit(3);
        ComPtr<ID3D12Fence> fence;HR(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HR(queue->Signal(fence.Get(),1));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)std::exit(4);HR(fence->SetEventOnCompletion(1,event));
        if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)std::exit(5);CloseHandle(event);HR(device->GetDeviceRemovedReason());
    }
    void Upload(ID3D12Resource* r,const void* data,unsigned pixelBytes,D3D12_RESOURCE_STATES arrival){
        auto d=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;
        device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto b=Buffer(bytes,D3D12_HEAP_TYPE_UPLOAD);void* p;HR(b->Map(0,nullptr,&p));
        for(unsigned y=0;y<d.Height;++y)memcpy(static_cast<char*>(p)+y*fp.Footprint.RowPitch,static_cast<const char*>(data)+y*d.Width*pixelBytes,static_cast<size_t>(d.Width)*pixelBytes);
        b->Unmap(0,nullptr);Run([&](auto* l){CD3DX12_TEXTURE_COPY_LOCATION src(b.Get(),fp),dst(r,0);l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(l,r,D3D12_RESOURCE_STATE_COPY_DEST,arrival);});
    }
    std::vector<unsigned char> Read(ID3D12Resource* r,unsigned pixelBytes){
        auto d=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes;
        device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto b=Buffer(bytes,D3D12_HEAP_TYPE_READBACK);
        Run([&](auto* l){Barrier(l,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
            CD3DX12_TEXTURE_COPY_LOCATION src(r,0),dst(b.Get(),fp);l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);Barrier(l,r,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);});
        void* p;HR(b->Map(0,nullptr,&p));std::vector<unsigned char> out(static_cast<size_t>(d.Width)*d.Height*pixelBytes);
        for(unsigned y=0;y<d.Height;++y)memcpy(out.data()+y*d.Width*pixelBytes,static_cast<char*>(p)+y*fp.Footprint.RowPitch,static_cast<size_t>(d.Width)*pixelBytes);
        b->Unmap(0,nullptr);return out;
    }
    void Clean(){ComPtr<ID3D12InfoQueue> q;HR(device.As(&q));for(UINT64 i=0;i<q->GetNumStoredMessages();++i){
        SIZE_T size=0;q->GetMessage(i,nullptr,&size);std::vector<char>b(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(b.data());HR(q->GetMessage(i,message,&size));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING){std::cerr<<message->pDescription<<'\n';std::exit(6);}}
    }
};
