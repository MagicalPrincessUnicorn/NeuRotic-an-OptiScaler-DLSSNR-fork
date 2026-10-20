#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>
#include <cstring>
#include <stdexcept>
struct NativeGpuFixture {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D12Device> device;Ptr<ID3D12CommandQueue> queue;Ptr<ID3D12CommandAllocator> allocator;
    Ptr<ID3D12GraphicsCommandList> list;Ptr<ID3D12Fence> fence;uint64_t serial=0;HANDLE event=nullptr;
    static void Require(bool ok){if(!ok)throw std::runtime_error("GPU fixture operation failed");}
    NativeGpuFixture(){Ptr<IDXGIFactory4> factory;Ptr<IDXGIAdapter1> adapter;
        Require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))&&
            SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))));
        D3D12_COMMAND_QUEUE_DESC q{};Require(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))&&
            SUCCEEDED(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)))&&
            SUCCEEDED(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))&&SUCCEEDED(list->Close())&&
            SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr);
    }
    ~NativeGpuFixture(){if(event)CloseHandle(event);}
    uint64_t Luid()const{const auto id=device->GetAdapterLuid();uint64_t value=0;std::memcpy(&value,&id,8);return value;}
    void Begin(){Require(SUCCEEDED(allocator->Reset())&&SUCCEEDED(list->Reset(allocator.Get(),nullptr)));}
    void Finish(){Require(SUCCEEDED(list->Close()));ID3D12CommandList* l=list.Get();queue->ExecuteCommandLists(1,&l);
        Require(SUCCEEDED(queue->Signal(fence.Get(),++serial))&&SUCCEEDED(fence->SetEventOnCompletion(serial,event))&&WaitForSingleObject(event,5000)==WAIT_OBJECT_0&&fence->GetCompletedValue()==serial);}
    Ptr<ID3D12Resource> Buffer(uint64_t bytes,D3D12_HEAP_TYPE type=D3D12_HEAP_TYPE_DEFAULT){
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=(bytes+3)&~3ull;d.Height=d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(type==D3D12_HEAP_TYPE_DEFAULT)d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES h{};h.Type=type;Ptr<ID3D12Resource> resource;
        Require(SUCCEEDED(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:
            type==D3D12_HEAP_TYPE_READBACK?D3D12_RESOURCE_STATE_COPY_DEST:D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&resource))));return resource;
    }
    void Upload(ID3D12Resource* destination,const std::vector<uint8_t>& bytes){auto upload=Buffer(bytes.size(),D3D12_HEAP_TYPE_UPLOAD);void* p=nullptr;D3D12_RANGE none{0,0};Require(SUCCEEDED(upload->Map(0,&none,&p)));std::memcpy(p,bytes.data(),bytes.size());upload->Unmap(0,nullptr);
        Begin();DlssNr::NativeGuides::GpuPixels::Transition(list.Get(),destination,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(destination,0,upload.Get(),0,bytes.size());DlssNr::NativeGuides::GpuPixels::Transition(list.Get(),destination,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);Finish();}
    std::vector<uint8_t> Read(ID3D12Resource* source,size_t bytes){auto readback=Buffer(bytes,D3D12_HEAP_TYPE_READBACK);Begin();
        DlssNr::NativeGuides::GpuPixels::Transition(list.Get(),source,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyBufferRegion(readback.Get(),0,source,0,bytes);
        DlssNr::NativeGuides::GpuPixels::Transition(list.Get(),source,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);Finish();
        std::vector<uint8_t> out(bytes);void* p=nullptr;D3D12_RANGE range{0,bytes};Require(SUCCEEDED(readback->Map(0,&range,&p)));std::memcpy(out.data(),p,bytes);D3D12_RANGE none{0,0};readback->Unmap(0,&none);return out;}
};
