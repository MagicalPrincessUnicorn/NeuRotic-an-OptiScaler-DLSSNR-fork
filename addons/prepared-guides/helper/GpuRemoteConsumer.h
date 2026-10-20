#pragma once
#include "PreparedGpuClient.h"
#include <dlssnr/PreparedTextureCapture.h>
namespace nrpg::gpu {
// Serialized adapter-owned consumer. Never loads a model in the producer process.
class GpuRemoteConsumer {
public:
    explicit GpuRemoteConsumer(ManagedHostSpec);
    ~GpuRemoteConsumer();
    bool Process(const DlssNr::NativeGuides::TextureCapture&,CpuMainlineClient::GpuOutput&,std::string&);
    bool ReturnAfterCopyback(bool actualCompleted,std::uint64_t capture,std::uint64_t generation,std::string&);
    bool RequiresRestart()const;
private:struct Impl;std::unique_ptr<Impl> p_;
};
}
