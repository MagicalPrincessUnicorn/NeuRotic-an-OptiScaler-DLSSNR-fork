#pragma once
#include "NativeGuideCapture.h"
#include "PreparedTextureCapture.h"
#include <memory>
#ifdef NRPG_CPU_MAINLINE_TESTING
#include "../../addons/prepared-guides/src/CpuMainlineClient.h"
#ifdef _WIN64
#include "NativeFsr3D3D12.h"
#endif
#endif

namespace DlssNr::NativeGuides {
// Serialized owner. CPU input is copied; shared input uses completed GPU buffers. Uncertain GPU work
// permanently quarantines this owner rather than allocating replacement slots.
class Processor {
public:
    Processor();
    ~Processor();
    Processor(const Processor&)=delete;
    Processor& operator=(const Processor&)=delete;
    Outcome Process(const Capture&,std::vector<std::uint8_t>&,std::string&);
    ID3D12Device* SharedDevice(std::uint64_t,std::string&);
    Outcome ProcessShared(const Capture&,const SharedCapture&,std::string&);
    Outcome ProcessTextures(const TextureCapture&,nrpg::CpuMainlineClient::GpuOutput&,std::string&);
    // Serialized caller also proves all external output consumers completed.
    // This queries this processor's own work without waiting or releasing it.
    bool CanYieldOutput(std::string& reason) const;
    // Input handoff retains allocations. Caller also proves external consumers.
    bool CanSwitchInput(std::string& reason) const;
#ifdef NRPG_CPU_MAINLINE_TESTING
    explicit Processor(nrpg::CpuMainlineClient::TestRecorder, ID3D12Device* testDevice=nullptr,NativeFg::FsrApi framegenApi={});
#endif
private:
    Outcome ProcessImpl(const Capture&,std::vector<std::uint8_t>&,std::string&,const SharedCapture*);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
