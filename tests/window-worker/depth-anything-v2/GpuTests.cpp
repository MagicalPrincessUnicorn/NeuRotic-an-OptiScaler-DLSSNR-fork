#include <iostream>
#if __has_include("../../../apps/NeuRoticWindowWorker/guidance/dav2/Dav2InteropDx12.h")
#include "../../../apps/NeuRoticWindowWorker/guidance/dav2/Dav2InteropDx12.h"
#include "PrepareInput.h"
#include "MaterializeDepth.h"
#include "EstimateRange.h"
#include <cmath>
#include <limits>
using namespace nrw;using namespace nrw::depth;
static void Check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);}
int main(){try{
 ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
 DxCheck(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"WARP factory");DxCheck(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"WARP adapter");DxCheck(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"WARP D12 device");
 for(unsigned i=0;;++i){ComPtr<IDXGIAdapter1> a;if(factory->EnumAdapters1(i,&a)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 d{};a->GetDesc1(&d);std::wcout<<L"Detected adapter: "<<d.Description<<L" vendor="<<d.VendorId<<L"\n";}
 ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};DxCheck(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"test queue");
 ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
 DxCheck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"test allocator");DxCheck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"test list");DxCheck(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"test fence");
 uint64_t serial=0;auto submit=[&]{DxCheck(list->Close(),"test close");ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);DxCheck(queue->Signal(fence.Get(),++serial),"test signal");auto start=GetTickCount64();while(fence->GetCompletedValue()<serial){Check(GetTickCount64()-start<5000,"test completion timeout");Sleep(1);}Check(fence->GetCompletedValue()!=UINT64_MAX,"test device removed");};
 auto reset=[&]{DxCheck(allocator->Reset(),"test allocator reset");DxCheck(list->Reset(allocator.Get(),nullptr),"test list reset");};
 D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
 D3D12_RESOURCE_DESC td{};td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;td.Width=4;td.Height=2;td.DepthOrArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;td.SampleDesc.Count=1;
 ComPtr<ID3D12Resource> color;DxCheck(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&color)),"test source");
 D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};uint64_t uploadBytes=0;device->GetCopyableFootprints(&td,0,1,0,&layout,nullptr,nullptr,&uploadBytes);
 auto upload=Buffer(device.Get(),uploadBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* mapped=nullptr;DxCheck(upload->Map(0,nullptr,&mapped),"map source upload");
 for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x){auto p=static_cast<uint8_t*>(mapped)+layout.Offset+y*layout.Footprint.RowPitch+x*4;p[0]=(x==0||x==3)?255:0;p[1]=0;p[2]=(x==1||x==2)?255:0;p[3]=255;}upload->Unmap(0,nullptr);
 D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=upload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=layout;D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=color.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);Transition(list.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 constexpr unsigned pixels=14*14;auto tensor=Buffer(device.Get(),pixels*12,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);auto readback=Buffer(device.Get(),pixels*12,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 ComputeKernel prepare;prepare.Create(device.Get(),Dav2PrepareInput,sizeof(Dav2PrepareInput));prepare.TextureSrv(device.Get(),0,color.Get(),td.Format);prepare.BufferUav(device.Get(),0,tensor.Get(),pixels*3);
 auto box=MakeLetterbox(2,1,14,14);std::array<uint32_t,12> constants{2,1,14,14,box.contentWidth,box.contentHeight,box.left,box.top,1,0,0,0};prepare.Dispatch(list.Get(),constants,2,2);Transition(list.Get(),tensor.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyResource(readback.Get(),tensor.Get());submit();
 D3D12_RANGE range{0,pixels*12};DxCheck(readback->Map(0,&range,&mapped),"read prepared tensor");auto values=static_cast<const float*>(mapped);const unsigned p=(box.top+2)*14+4;
 Check(std::abs(values[p]-(1-.485f)/.229f)<1e-4f,"GPU red plane and crop origin");Check(std::abs(values[pixels+p]+.456f/.224f)<1e-4f,"GPU green plane");Check(std::abs(values[pixels*2+p]+.406f/.225f)<1e-4f,"GPU blue plane");Check(values[0]==0&&values[pixels]==0&&values[pixels*2]==0,"mean padding is normalized zero");readback->Unmap(0,nullptr);
 // Materialization numeric validity at an invalid contributing sample.
 auto rawUpload=Buffer(device.Get(),pixels*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);DxCheck(rawUpload->Map(0,nullptr,&mapped),"raw oracle upload");auto rawValues=static_cast<float*>(mapped);for(unsigned i=0;i<pixels;++i)rawValues[i]=float(i%14);rawValues[0]=std::numeric_limits<float>::quiet_NaN();rawUpload->Unmap(0,nullptr);
 auto raw=Buffer(device.Get(),pixels*4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);td.Width=td.Height=14;td.Format=DXGI_FORMAT_R32_FLOAT;td.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
 ComPtr<ID3D12Resource> rawImage,validity;DxCheck(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&rawImage)),"raw image");DxCheck(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&validity)),"validity image");
 device->GetCopyableFootprints(&td,0,1,0,&layout,nullptr,nullptr,&uploadBytes);auto imageReadback=Buffer(device.Get(),uploadBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 reset();list->CopyResource(raw.Get(),rawUpload.Get());Transition(list.Get(),raw.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);ComputeKernel materialize;materialize.Create(device.Get(),Dav2MaterializeDepth,sizeof(Dav2MaterializeDepth));materialize.BufferSrv(device.Get(),0,raw.Get(),pixels);materialize.TextureUav(device.Get(),0,rawImage.Get());materialize.TextureUav(device.Get(),1,validity.Get());materialize.Dispatch(list.Get(),{14,14,14,14,0,0,14,14,0,0,0,0},2,2);Transition(list.Get(),validity.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
 from={};from.pResource=validity.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to={};to.pResource=imageReadback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=layout;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);submit();DxCheck(imageReadback->Map(0,nullptr,&mapped),"validity readback");values=static_cast<float*>(mapped);Check(values[0]==0&&values[1]==1,"invalid contributing sample has no fabricated confidence");imageReadback->Unmap(0,nullptr);
 // Bounded scene statistics: abrupt scene changes must reset, similar frames must smooth.
 auto historyUpload=Buffer(device.Get(),257*16,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
 auto history=Buffer(device.Get(),257*16,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
 auto next=Buffer(device.Get(),257*16,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
 auto metadata=Buffer(device.Get(),16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
 ComputeKernel estimate;estimate.Create(device.Get(),Dav2EstimateRange,sizeof(Dav2EstimateRange));estimate.BufferSrv(device.Get(),0,raw.Get(),pixels);estimate.BufferSrv(device.Get(),1,history.Get(),257,true);estimate.TextureSrv(device.Get(),2,color.Get(),DXGI_FORMAT_B8G8R8A8_UNORM);estimate.BufferUav(device.Get(),0,next.Get(),257,true);
 for(unsigned pass=0;pass<2;++pass){
  DxCheck(historyUpload->Map(0,nullptr,&mapped),"scene history upload");auto h=static_cast<float*>(mapped);std::fill(h,h+257*4,0.f);h[0]=100;h[1]=200;h[2]=1;
  for(unsigned i=0;i<256;++i){auto s=h+4*(i+1);unsigned x=std::min(3u,unsigned((float(i%16)+.5f)*4/16));if(pass){s[0]=(x==1||x==2)?1.f:0.f;s[2]=(x==0||x==3)?1.f:0.f;}s[3]=1;}
  historyUpload->Unmap(0,nullptr);reset();if(pass)Transition(list.Get(),history.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(history.Get(),historyUpload.Get());Transition(list.Get(),history.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  estimate.Dispatch(list.Get(),{14,14,14,0,0,0,4,2,std::bit_cast<uint32_t>(.01f),std::bit_cast<uint32_t>(.25f),0,0},1);Transition(list.Get(),next.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyBufferRegion(metadata.Get(),0,next.Get(),0,16);Transition(list.Get(),next.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);submit();D3D12_RANGE r{0,16};DxCheck(metadata->Map(0,&r,&mapped),"scene metadata readback");auto v=static_cast<float*>(mapped);
  Check(v[2]==1, "scene range remains numerically valid");Check(pass?(v[0]>90&&v[1]>180):(v[0]<1&&v[1]<14),pass?"similar scene smooths range":"abrupt scene resets range");metadata->Unmap(0,nullptr);
 }
 std::cout<<"PASS WARP GPU RGB/crop/padding, numeric materialization and abrupt/similar scene oracle\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL GPU tests: "<<e.what()<<'\n';return 1;}}
#else
int main(){std::cerr<<"FAIL GPU Depth Anything preprocessing/materialization owner missing\n";return 1;}
#endif
