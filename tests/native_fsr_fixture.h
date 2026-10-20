#pragma once
#include <dlssnr/NativeFsr3D3D12.h>
#include <cassert>
namespace NativeFsrFixture {
inline unsigned dispatched=0;
inline Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
inline ffxReturnCode_t Create(ffxContext* c,ffxCreateContextDescHeader*,const ffxAllocationCallbacks*){*c=reinterpret_cast<void*>(1);return FFX_API_RETURN_OK;}
inline ffxReturnCode_t Destroy(ffxContext*,const ffxAllocationCallbacks*){heap.Reset();return FFX_API_RETURN_OK;}
inline ffxReturnCode_t Configure(ffxContext*,const ffxConfigureDescHeader* h){const auto* d=reinterpret_cast<const ffxConfigureDescFrameGeneration*>(h);assert(d->flags&FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY);return FFX_API_RETURN_OK;}
inline ffxReturnCode_t Dispatch(ffxContext*,const ffxDispatchDescHeader* h){
 if(h->type==FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE)return FFX_API_RETURN_OK;
 auto* d=reinterpret_cast<const ffxDispatchDescFrameGeneration*>(h);
 auto* list=static_cast<ID3D12GraphicsCommandList*>(d->commandList);
 auto* dest=static_cast<ID3D12Resource*>(d->outputs[0].resource);
 Microsoft::WRL::ComPtr<ID3D12Device> device;assert(SUCCEEDED(dest->GetDevice(IID_PPV_ARGS(&device))));
 if(!heap){D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=1;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;assert(SUCCEEDED(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap))));}
 D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
 device->CreateUnorderedAccessView(dest,nullptr,&view,heap->GetCPUDescriptorHandleForHeapStart());ID3D12DescriptorHeap* heaps[]{heap.Get()};list->SetDescriptorHeaps(1,heaps);
 const float color[]{17/255.f,33/255.f,77/255.f,1};list->ClearUnorderedAccessViewFloat(heap->GetGPUDescriptorHandleForHeapStart(),heap->GetCPUDescriptorHandleForHeapStart(),dest,color,0,nullptr);
 ++dispatched;return FFX_API_RETURN_OK;
}
inline DlssNr::NativeFg::FsrApi Api(){return {Create,Destroy,Configure,Dispatch};}
}
