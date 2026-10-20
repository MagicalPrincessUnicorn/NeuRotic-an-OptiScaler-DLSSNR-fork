#include <dlssnr/NativeFsr3D3D12.h>
#include <dlssnr/NativeGpuPixels.h>
#include "native_gpu_fixture.h"
#include <cstdio>
#include <cmath>
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;NativeGpuFixture f;auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!module){std::printf("FAIL: provider load %lu\n",GetLastError());return 1;}
 DlssNr::NativeFg::FsrApi api;
 api.create=reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(module,"ffxCreateContext"));api.destroy=reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(module,"ffxDestroyContext"));
 api.configure=reinterpret_cast<PfnFfxConfigure>(GetProcAddress(module,"ffxConfigure"));api.dispatch=reinterpret_cast<PfnFfxDispatch>(GetProcAddress(module,"ffxDispatch"));
 auto texture=[&](DXGI_FORMAT format,D3D12_RESOURCE_STATES state,const std::array<uint8_t,4>& pixel){
  NativeGpuFixture::Ptr<ID3D12Resource> r;D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=d.Height=64;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;f.Require(SUCCEEDED(f.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r))));
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;f.device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&total);auto upload=f.Buffer(total,D3D12_HEAP_TYPE_UPLOAD);void* p=nullptr;f.Require(SUCCEEDED(upload->Map(0,nullptr,&p)));for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)std::memcpy(static_cast<uint8_t*>(p)+fp.Offset+y*fp.Footprint.RowPitch+x*4,pixel.data(),4);upload->Unmap(0,nullptr);
  f.Begin();DlssNr::NativeGuides::GpuPixels::Transition(f.list.Get(),r.Get(),state,D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=r.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=fp;
  f.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);DlssNr::NativeGuides::GpuPixels::Transition(f.list.Get(),r.Get(),D3D12_RESOURCE_STATE_COPY_DEST,state);f.Finish();return r;
 };
 const std::array<uint8_t,4> expected{20,80,160,255};float z=.25f;std::array<uint8_t,4> depthPixel{};std::memcpy(depthPixel.data(),&z,4);
 auto color=texture(DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,expected);
 auto depth=texture(DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,depthPixel);
 auto motion=texture(DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,{});
 DlssNr::NativeFg::FsrSession session(api);DlssNr::NativeFg::FsrOutput output;std::string reason;
 for(unsigned i=1;i<=4;++i){DlssNr::NativeFg::FsrFrame frame{1,1,i,64,64,false,i==1,16.667,color.Get(),depth.Get(),motion.Get(),f.fence.Get(),f.serial};
  if(!session.Process(f.device.Get(),frame,output,reason)){std::printf("FAIL: actual bundled FSR frame %u: %s\n",i,reason.c_str());return 1;}}
 f.Require(output.generated&&output.completion->GetCompletedValue()>=output.value);
 auto desc=output.color->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 total=0;f.device->GetCopyableFootprints(&desc,0,1,0,&fp,nullptr,nullptr,&total);auto read=f.Buffer(total,D3D12_HEAP_TYPE_READBACK);
 f.Begin();DlssNr::NativeGuides::GpuPixels::Transition(f.list.Get(),output.color.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
 D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=output.color.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=read.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;f.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);DlssNr::NativeGuides::GpuPixels::Transition(f.list.Get(),output.color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);f.Finish();
 void* p=nullptr;f.Require(SUCCEEDED(read->Map(0,nullptr,&p)));const auto* pixel=static_cast<uint8_t*>(p)+fp.Offset+32*fp.Footprint.RowPitch+32*4;
 std::printf("Actual FSR center RGBA: %u %u %u %u\n",pixel[0],pixel[1],pixel[2],pixel[3]);bool match=true;for(unsigned i=0;i<3;++i)match&=std::abs(int(pixel[i])-expected[i])<=4;read->Unmap(0,nullptr);
 output={};if(!session.Close(reason)||!match){std::printf("FAIL: actual FSR output/readback: %s\n",reason.c_str());return 1;}FreeLibrary(module);
 std::puts("PASS: bundled FSR provider on WARP, standalone prepare/dispatch and completed constant-scene generated pixels");return 0;
}
