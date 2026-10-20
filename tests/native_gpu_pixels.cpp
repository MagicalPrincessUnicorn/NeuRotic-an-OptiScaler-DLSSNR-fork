#include <dlssnr/NativeGpuPixels.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <cmath>
#include <limits>
#include <bit>
#include "native_gpu_fixture.h"
using Microsoft::WRL::ComPtr;
int main(){ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)))||
       FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))return 77;
    DlssNr::NativeGuides::GpuPixels pixels;std::string reason;
    if(!pixels.Initialize(device.Get(),reason)){std::fprintf(stderr,"FAIL: GPU pixel conversion initialization: %s\n",reason.c_str());return 1;}
    NativeGpuFixture f;DlssNr::NativeGuides::GpuPixels converter;if(!converter.Initialize(f.device.Get(),reason))return 1;
    constexpr unsigned w=5,h=3,n=w*h;auto rawColor=f.Buffer(n*4),rawDepth=f.Buffer(n*4),output=f.Buffer(n*4);
    std::vector<uint8_t> color(n*4),depth(n*4);for(unsigned i=0;i<n;++i){color[i*4]=uint8_t(i*13);color[i*4+1]=37;color[i*4+2]=211;color[i*4+3]=255;}
    f.Upload(rawColor.Get(),color);
    for(unsigned bits:{16u,24u,32u})for(bool bgra:{false,true}){
        for(unsigned i=0;i<n;++i){if(bits==16){uint16_t z=uint16_t(i*4000);std::memcpy(depth.data()+i*2,&z,2);}else{uint32_t z=bits==24?(0xa5000000u|i*1000000):std::bit_cast<uint32_t>(float(i)/float(n));std::memcpy(depth.data()+i*4,&z,4);}}
        f.Upload(rawDepth.Get(),depth);ComPtr<ID3D12Resource> normalizedColor,normalizedDepth;
        f.Begin();NativeGpuFixture::Require(converter.Decode(f.list.Get(),rawColor.Get(),rawDepth.Get(),w,h,bits,bgra,normalizedColor,normalizedDepth,reason));f.Finish();NativeGpuFixture::Require(converter.Valid(reason));
        const auto* colorIdentity=normalizedColor.Get();const auto* depthIdentity=normalizedDepth.Get();
        f.Begin();NativeGpuFixture::Require(converter.Decode(f.list.Get(),rawColor.Get(),rawDepth.Get(),w,h,bits,bgra,normalizedColor,normalizedDepth,reason,true));f.Finish();NativeGpuFixture::Require(converter.Valid(reason));
        NativeGpuFixture::Require(normalizedColor.Get()==colorIdentity&&normalizedDepth.Get()==depthIdentity);
        f.Begin();NativeGpuFixture::Require(!converter.Decode(f.list.Get(),rawColor.Get(),rawDepth.Get(),w-1,h,bits,bgra,normalizedColor,normalizedDepth,reason,true));f.Finish();
        // Inspect real normalized depth, including odd D16 final pixel and D24 stencil padding.
        const auto desc=normalizedDepth->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT64 bytes=0;f.device->GetCopyableFootprints(&desc,0,1,0,&layout,nullptr,nullptr,&bytes);auto readback=f.Buffer(bytes,D3D12_HEAP_TYPE_READBACK);
        f.Begin();DlssNr::NativeGuides::GpuPixels::Transition(f.list.Get(),normalizedDepth.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=normalizedDepth.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=layout;
        f.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);f.Finish();void* p=nullptr;NativeGpuFixture::Require(SUCCEEDED(readback->Map(0,nullptr,&p)));
        for(unsigned i=0;i<n;++i){float actual=0;std::memcpy(&actual,static_cast<uint8_t*>(p)+(i/w)*layout.Footprint.RowPitch+(i%w)*4,4);const float expected=bits==16?float(i*4000)/65535.f:bits==24?float(i*1000000)/16777215.f:float(i)/float(n);NativeGpuFixture::Require(std::abs(actual-expected)<.000001f);}readback->Unmap(0,nullptr);
        f.Begin();DlssNr::NativeGuides::GpuPixels::Transition(f.list.Get(),normalizedColor.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        NativeGpuFixture::Require(converter.Encode(f.list.Get(),normalizedColor.Get(),output.Get(),bgra,reason));f.Finish();NativeGpuFixture::Require(f.Read(output.Get(),n*4)==color);
    }
    float invalid=std::numeric_limits<float>::quiet_NaN();std::memcpy(depth.data(),&invalid,4);f.Upload(rawDepth.Get(),depth);
    ComPtr<ID3D12Resource> c,z;f.Begin();NativeGpuFixture::Require(converter.Decode(f.list.Get(),rawColor.Get(),rawDepth.Get(),w,h,32,true,c,z,reason));f.Finish();NativeGpuFixture::Require(!converter.Valid(reason));
    f.Begin();NativeGpuFixture::Require(!converter.Decode(f.list.Get(),rawColor.Get(),rawDepth.Get(),w,h,8,true,c,z,reason));f.Finish();
    // Distinct source dimensions, row/edge patterns and both scaling directions.
    for(unsigned scale:{1u,3u})for(unsigned bits:{16u,24u,32u}){
        const unsigned dw=w*scale,dh=h*scale,ow=w*2,oh=h*2;
        auto rc=f.Buffer(ow*oh*4),rd=f.Buffer((dw*dh*(bits==16?2:4)+3)&~3u);
        std::vector<uint8_t> cc(ow*oh*4,100),zz(size_t(rd->GetDesc().Width));
        for(unsigned i=0;i<dw*dh;++i){if(bits==16){uint16_t v=uint16_t(i*97);std::memcpy(zz.data()+i*2,&v,2);}
            else {uint32_t v=bits==24?(0xab000000u|i*3001):std::bit_cast<uint32_t>(float(i)/float(dw*dh));std::memcpy(zz.data()+i*4,&v,4);}}
        f.Upload(rc.Get(),cc);f.Upload(rd.Get(),zz);
        f.Begin();NativeGpuFixture::Require(converter.Decode(f.list.Get(),rc.Get(),rd.Get(),ow,oh,bits,true,c,z,reason,false,dw,dh));f.Finish();NativeGpuFixture::Require(converter.Valid(reason));
        const auto desc=z->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT64 bytes=0;
        f.device->GetCopyableFootprints(&desc,0,1,0,&layout,nullptr,nullptr,&bytes);auto rb=f.Buffer(bytes,D3D12_HEAP_TYPE_READBACK);
        f.Begin();DlssNr::NativeGuides::GpuPixels::Transition(f.list.Get(),z.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=z.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.pResource=rb.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=layout;
        f.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);f.Finish();void* p=nullptr;NativeGpuFixture::Require(SUCCEEDED(rb->Map(0,nullptr,&p)));
        for(unsigned y=0;y<oh;++y)for(unsigned x=0;x<ow;++x){
            const unsigned i=unsigned((y+.5)*double(dh)/oh)*dw+unsigned((x+.5)*double(dw)/ow);
            const float expected=bits==16?float(i*97)/65535.f:bits==24?float(i*3001)/16777215.f:float(i)/float(dw*dh);
            float actual=0;std::memcpy(&actual,static_cast<uint8_t*>(p)+y*layout.Footprint.RowPitch+x*4,4);NativeGpuFixture::Require(std::abs(actual-expected)<.000001f);
        }rb->Unmap(0,nullptr);
        f.Begin();NativeGpuFixture::Require(!converter.Decode(f.list.Get(),rc.Get(),rd.Get(),ow,oh,bits,true,c,z,reason,false,dw,0));f.Finish();
        f.Begin();NativeGpuFixture::Require(!converter.Decode(f.list.Get(),rc.Get(),rd.Get(),ow,oh,bits,true,c,z,reason,false,dw+1,dh));f.Finish();
        if(scale==3){auto shortDepth=f.Buffer(4);f.Begin();NativeGpuFixture::Require(!converter.Decode(f.list.Get(),rc.Get(),shortDepth.Get(),ow,oh,bits,true,c,z,reason,false,dw,dh));f.Finish();
            if(bits==32){float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(zz.data()+4,&nan,4); // x=1 is skipped by the output sampling grid.
                f.Upload(rd.Get(),zz);f.Begin();NativeGpuFixture::Require(converter.Decode(f.list.Get(),rc.Get(),rd.Get(),ow,oh,bits,true,c,z,reason,false,dw,dh));f.Finish();NativeGpuFixture::Require(!converter.Valid(reason));}}
    }
    std::puts("PASS: actual WARP D16/D24/D32 normalization, mixed extents up/down, odd padding, full-source validation, bounds and RGBA/BGRA roundtrip");return 0;
}
