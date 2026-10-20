#pragma once
#include "pch.h"
#include <shaders/format_transfer/FT_Dx12.h>
#include <d3d12sdklayers.h>
#include <DirectXPackedVector.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <functional>
GpuTime_Dx12::GpuTime_Dx12(ID3D12Device*, bool) {}
GpuTime_Dx12::~GpuTime_Dx12() = default;
void GpuTime_Dx12::Start(ID3D12GraphicsCommandList*) {}
void GpuTime_Dx12::End(ID3D12GraphicsCommandList*) {}
ID3DBlob* CompileShader(const char*, const char*, const char*) { return nullptr; }
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
inline void HR(HRESULT h) { if(FAILED(h)){std::cerr<<"HRESULT "<<std::hex<<h<<'\n';std::exit(1);} }
struct Warp {
 ComPtr<ID3D12Device> d; ComPtr<ID3D12CommandQueue> q;
 Warp(){ ComPtr<ID3D12Debug> debug; HR(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
 ComPtr<IDXGIFactory4> f;HR(CreateDXGIFactory1(IID_PPV_ARGS(&f)));ComPtr<IDXGIAdapter> a;HR(f->EnumWarpAdapter(IID_PPV_ARGS(&a)));
 HR(D3D12CreateDevice(a.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d)));D3D12_COMMAND_QUEUE_DESC desc{};HR(d->CreateCommandQueue(&desc,IID_PPV_ARGS(&q))); }
 ComPtr<ID3D12Resource> buffer(UINT64 n,D3D12_HEAP_TYPE type){ComPtr<ID3D12Resource> r;auto h=CD3DX12_HEAP_PROPERTIES(type);auto desc=CD3DX12_RESOURCE_DESC::Buffer(n);
 HR(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&desc,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));return r;}
 ComPtr<ID3D12Resource> texture(DXGI_FORMAT fmt,UINT w,UINT h,D3D12_RESOURCE_STATES state){ComPtr<ID3D12Resource> r;auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);auto desc=CD3DX12_RESOURCE_DESC::Tex2D(fmt,w,h,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
 HR(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)));return r;}
 static void barrier(ID3D12GraphicsCommandList* l,ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){auto b=CD3DX12_RESOURCE_BARRIER::Transition(r,from,to);l->ResourceBarrier(1,&b);}
 void run(std::function<void(ID3D12GraphicsCommandList*)> record){ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;
 HR(d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)));HR(d->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&l)));record(l.Get());HR(l->Close());ID3D12CommandList* ls[]={l.Get()};q->ExecuteCommandLists(1,ls);
 ComPtr<ID3D12Fence> f;HR(d->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f)));HR(q->Signal(f.Get(),1));HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(e);HR(f->SetEventOnCompletion(1,e));assert(WaitForSingleObject(e,10000)==WAIT_OBJECT_0);CloseHandle(e);HR(d->GetDeviceRemovedReason());}
 void upload(ID3D12Resource* r,const void* data,UINT pixelBytes){auto desc=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 n;d->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&n);auto b=buffer(n,D3D12_HEAP_TYPE_UPLOAD);void* p;HR(b->Map(0,nullptr,&p));for(UINT y=0;y<desc.Height;++y)memcpy((char*)p+y*fp.Footprint.RowPitch,(char*)data+y*desc.Width*pixelBytes,desc.Width*pixelBytes);b->Unmap(0,nullptr);
 run([&](auto* l){CD3DX12_TEXTURE_COPY_LOCATION src(b.Get(),fp),dst(r,0);l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);barrier(l,r,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);});}
 std::vector<unsigned char> read(ID3D12Resource* r,UINT pixelBytes){auto desc=r->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 n;d->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&n);auto b=buffer(n,D3D12_HEAP_TYPE_READBACK);
 run([&](auto* l){barrier(l,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);CD3DX12_TEXTURE_COPY_LOCATION src(r,0),dst(b.Get(),fp);l->CopyTextureRegion(&dst,0,0,0,&src,nullptr);barrier(l,r,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);});
 void* p;HR(b->Map(0,nullptr,&p));std::vector<unsigned char> result(desc.Width*desc.Height*pixelBytes);for(UINT y=0;y<desc.Height;++y)memcpy(result.data()+y*desc.Width*pixelBytes,(char*)p+y*fp.Footprint.RowPitch,desc.Width*pixelBytes);b->Unmap(0,nullptr);return result;}
 void clean(){ComPtr<ID3D12InfoQueue> info;HR(d.As(&info));for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n=0;info->GetMessage(i,nullptr,&n);std::vector<char>b(n);auto* m=(D3D12_MESSAGE*)b.data();HR(info->GetMessage(i,m,&n));if(m->Severity<=D3D12_MESSAGE_SEVERITY_WARNING){std::cerr<<m->pDescription<<'\n';std::exit(1);}}std::cout<<"PASS: WARP debug layer clean\n";}
};
