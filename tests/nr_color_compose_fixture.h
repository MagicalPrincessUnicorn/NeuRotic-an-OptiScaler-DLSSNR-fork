#pragma once
#include "nr_color_warp_fixture.h"
#include <shaders/dlssnr/DlssNr_Common.h>
#include <shaders/dlssnr/precompile/DlssNr_Shader.h>
struct Compose : Shader_Dx12 {
 FrameDescriptorHeap heap;
 ComPtr<ID3D12Resource> constants;
 Compose(Warp& w,const DlssNrConstants& c,ID3D12Resource* src,ID3D12Resource* model,ID3D12Resource* original,ID3D12Resource* out,ID3D12Resource* keep,ID3D12Resource* exposure=nullptr):Shader_Dx12("composition test",w.d.Get()){
 D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
 assert(SetupRootSignature(w.d.Get(),5,2,1,0,0,1,&sampler));assert(CreateComputePipeline(w.d.Get(),&_pipelineState,DlssNr_cso,sizeof(DlssNr_cso),nullptr));assert(InitHeaps(w.d.Get(),&heap,1));
 ID3D12Resource* sources[]={src,model?model:src,original?original:src,src,exposure?exposure:src};for(UINT i=0;i<5;++i)CreateShaderResourceView(w.d.Get(),sources[i],heap.GetSrvCPU(i));CreateUnorderedAccessView(w.d.Get(),out,heap.GetUavCPU(0),0);CreateUnorderedAccessView(w.d.Get(),keep?keep:out,heap.GetUavCPU(1),0);
 constants=w.buffer(sizeof(c),D3D12_HEAP_TYPE_UPLOAD);void* p;HR(constants->Map(0,nullptr,&p));memcpy(p,&c,sizeof(c));constants->Unmap(0,nullptr);D3D12_CONSTANT_BUFFER_VIEW_DESC cb{constants->GetGPUVirtualAddress(),sizeof(c)};w.d->CreateConstantBufferView(&cb,heap.GetCbvCPU(0));}
 void dispatch(ID3D12GraphicsCommandList* l,UINT width,UINT height){ID3D12DescriptorHeap* h[]={heap.GetHeapCSU()};l->SetDescriptorHeaps(1,h);l->SetComputeRootSignature(_rootSignature);l->SetPipelineState(_pipelineState);l->SetComputeRootDescriptorTable(0,heap.GetTableGPUStart());l->Dispatch((width+7)/8,(height+7)/8,1);}
 ~Compose(){if(_pipelineState)_pipelineState->Release();if(_rootSignature)_rootSignature->Release();_pipelineState=nullptr;_rootSignature=nullptr;}
};
