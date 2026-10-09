#pragma once
#include "../../WorkerContracts.h"
#include <optional>
#include <filesystem>
namespace nrw::depth {
struct DepthFrame {
 CapturedFrame color;
 Receipt receipt;
 ComPtr<ID3D11Texture2D> raw,validity,preview;
 float low=0,high=0;bool rangeValid=false;
 std::string reason;
};
class Dav2Host {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 Dav2Host();~Dav2Host();Dav2Host(const Dav2Host&)=delete;
 bool Initialize(ID3D11Device*,ID3D11DeviceContext*,const Settings&,const std::filesystem::path&,std::string&);
 void Offer(const CapturedFrame&,uint64_t sourceId);
 void ObserveEpochs(const FrameKey&);
 std::optional<DepthFrame> Poll(std::string&);
 void Stop();bool RequiresRestart()const;bool Ready()const;
 bool Busy()const;
 uint64_t Fresh()const;uint64_t Dropped()const;
};
}
