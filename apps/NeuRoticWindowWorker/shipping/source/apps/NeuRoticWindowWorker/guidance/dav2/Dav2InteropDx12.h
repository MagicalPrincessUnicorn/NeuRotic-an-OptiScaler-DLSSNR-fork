#pragma once
#include "../../WorkerContracts.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <array>
#include <stdexcept>
namespace nrw::depth {
inline void DxCheck(HRESULT h,const char* message){if(FAILED(h))throw std::runtime_error(std::string(message)+" ("+std::to_string(uint32_t(h))+")");}
inline D3D12_RESOURCE_DESC BufferDescription(uint64_t bytes,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=1;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.Flags=flags;return d;}
inline ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,uint64_t bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE,D3D12_HEAP_FLAGS heapFlags=D3D12_HEAP_FLAG_NONE){
 D3D12_HEAP_PROPERTIES heap{};heap.Type=type;heap.CreationNodeMask=heap.VisibleNodeMask=1;auto desc=BufferDescription(bytes,flags);ComPtr<ID3D12Resource> resource;DxCheck(device->CreateCommittedResource(&heap,heapFlags,&desc,state,nullptr,IID_PPV_ARGS(&resource)),"Create depth buffer");return resource;
}
inline void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){if(before==after)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);}
inline void UavBarrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=resource;list->ResourceBarrier(1,&b);}
// One immutable descriptor set per dispatch owner. Caller retains it and all resources until its fence completes.
class ComputeKernel {
 ComPtr<ID3D12RootSignature> root_;ComPtr<ID3D12PipelineState> pipeline_;ComPtr<ID3D12DescriptorHeap> heap_;uint32_t stride_=0;
public:
 void Create(ID3D12Device* device,const void* shader,size_t bytes){
  D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,3,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0,0,0};
  D3D12_ROOT_PARAMETER params[3]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants={0,0,12};
  for(unsigned i=0;i<2;++i){params[i+1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i+1].DescriptorTable={1,&ranges[i]};}
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=params;
  ComPtr<ID3DBlob> blob,error;DxCheck(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"Serialize depth root signature");
  DxCheck(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_)),"Create depth root signature");
  D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=root_.Get();p.CS={shader,bytes};DxCheck(device->CreateComputePipelineState(&p,IID_PPV_ARGS(&pipeline_)),"Create depth compute pipeline");
  D3D12_DESCRIPTOR_HEAP_DESC h{};h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;h.NumDescriptors=5;h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;DxCheck(device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&heap_)),"Create depth descriptor heap");stride_=device->GetDescriptorHandleIncrementSize(h.Type);
  D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};nullSrv.Format=DXGI_FORMAT_R32_FLOAT;nullSrv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;nullSrv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;nullSrv.Texture2D.MipLevels=1;
  D3D12_UNORDERED_ACCESS_VIEW_DESC nullUav{};nullUav.Format=DXGI_FORMAT_R32_FLOAT;nullUav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
  for(unsigned i=0;i<3;++i)device->CreateShaderResourceView(nullptr,&nullSrv,Cpu(i));for(unsigned i=3;i<5;++i)device->CreateUnorderedAccessView(nullptr,nullptr,&nullUav,Cpu(i));
 }
 D3D12_CPU_DESCRIPTOR_HANDLE Cpu(unsigned i)const{auto h=heap_->GetCPUDescriptorHandleForHeapStart();h.ptr+=size_t(i)*stride_;return h;}
 void TextureSrv(ID3D12Device* device,unsigned slot,ID3D12Resource* r,DXGI_FORMAT format){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=format;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;device->CreateShaderResourceView(r,&d,Cpu(slot));}
 void TextureUav(ID3D12Device* device,unsigned slot,ID3D12Resource* r){D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=DXGI_FORMAT_R32_FLOAT;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(r,nullptr,&d,Cpu(3+slot));}
 void BufferSrv(ID3D12Device* device,unsigned slot,ID3D12Resource* r,uint32_t elements,bool structured=false){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=structured?DXGI_FORMAT_UNKNOWN:DXGI_FORMAT_R32_TYPELESS;d.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Buffer.NumElements=elements;d.Buffer.StructureByteStride=structured?16:0;d.Buffer.Flags=structured?D3D12_BUFFER_SRV_FLAG_NONE:D3D12_BUFFER_SRV_FLAG_RAW;device->CreateShaderResourceView(r,&d,Cpu(slot));}
 void BufferUav(ID3D12Device* device,unsigned slot,ID3D12Resource* r,uint32_t elements,bool structured=false){D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=structured?DXGI_FORMAT_UNKNOWN:DXGI_FORMAT_R32_TYPELESS;d.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;d.Buffer.NumElements=elements;d.Buffer.StructureByteStride=structured?16:0;d.Buffer.Flags=structured?D3D12_BUFFER_UAV_FLAG_NONE:D3D12_BUFFER_UAV_FLAG_RAW;device->CreateUnorderedAccessView(r,nullptr,&d,Cpu(3+slot));}
 void Dispatch(ID3D12GraphicsCommandList* list,const std::array<uint32_t,12>& constants,unsigned x,unsigned y=1){auto* heap=heap_.Get();list->SetDescriptorHeaps(1,&heap);list->SetComputeRootSignature(root_.Get());list->SetPipelineState(pipeline_.Get());list->SetComputeRoot32BitConstants(0,12,constants.data(),0);auto srv=heap_->GetGPUDescriptorHandleForHeapStart(),uav=srv;uav.ptr+=uint64_t(stride_)*3;list->SetComputeRootDescriptorTable(1,srv);list->SetComputeRootDescriptorTable(2,uav);list->Dispatch(x,y,1);}
};
}
