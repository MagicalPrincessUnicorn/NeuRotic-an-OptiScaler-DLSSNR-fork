#pragma once
#include <d3d12.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <limits>
namespace DlssNr::Connections::GpuBudget {
inline std::atomic<std::uint64_t> usedBytes{0},limitBytes{1024ull*1024*1024};
inline std::uint64_t Bytes(){return usedBytes.load();}
inline std::uint64_t Limit(){return limitBytes.load();}
// Module-local ledger. Remote producer/host each clamp to 512 MiB, yielding a
// conservative combined ceiling. Imported aliases may deliberately be counted twice.
inline void ClampLimit(std::uint64_t bytes){auto old=limitBytes.load();while(bytes<old&&!limitBytes.compare_exchange_weak(old,bytes)){} }
struct Reservation {
    const std::uint64_t bytes;
    explicit Reservation(std::uint64_t n):bytes(n){}
    ~Reservation(){usedBytes.fetch_sub(bytes);}
    Reservation(const Reservation&)=delete;Reservation& operator=(const Reservation&)=delete;
};
inline std::shared_ptr<Reservation> Reserve(std::uint64_t bytes){
    if(!bytes)return {};auto used=usedBytes.load();
    for(;;){const auto limit=limitBytes.load();if(bytes>limit||used>limit-bytes)return {};
        if(usedBytes.compare_exchange_weak(used,used+bytes))break;}
    try{return std::make_shared<Reservation>(bytes);}catch(...){usedBytes.fetch_sub(bytes);return {};}
}
inline std::uint64_t TextureBytes(ID3D12Device* device,const D3D12_RESOURCE_DESC& desc){
    if(!device)return 0;const auto info=device->GetResourceAllocationInfo(0,1,&desc);
    return info.SizeInBytes&&info.SizeInBytes!=(std::numeric_limits<std::uint64_t>::max)()?info.SizeInBytes:0;
}
inline std::uint64_t ImageBytes(ID3D12Device* device,std::uint32_t w,std::uint32_t h,DXGI_FORMAT format,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=format;d.Flags=flags;
    return TextureBytes(device,d);
}
inline bool AdmitRaster(std::uint32_t w,std::uint32_t h,bool derive=false){
    return w&&h&&w<=4096&&h<=2160&&std::uint64_t(w)*h<=8388608&&(!derive||(w>=256&&h>=64));
}
}
