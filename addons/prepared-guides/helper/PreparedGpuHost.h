#pragma once
#include "PreparedGpuClient.h"
#include <functional>
namespace nrpg::gpu {
// All callbacks are process-local. Module boundary remains the prepared C exports.
struct Consumer {
    std::function<bool(const P::Descriptor&,const CpuMainlineClient::GpuInputs&,
        bool deriveMotion,CpuMainlineClient::GpuOutput&,std::string&)> process;
    std::function<bool()> requiresRestart;
    std::uint32_t capabilities=0;
};
class PreparedGpuHost {
public:
    PreparedGpuHost();~PreparedGpuHost();
    // pipe is a connected duplex named pipe; policy binds the actual server PID.
    bool Run(HANDLE pipe,const PeerPolicy&,const ManagedHostSpec&,Consumer,std::string&);
    bool RequiresRestart()const;
private:struct Impl;std::unique_ptr<Impl> p_;
};
// Called by explicit --gpu-bootstrap dispatch only. The callback is assembled
// after the trusted bootstrap/module check on the host's selected device.
using ConsumerFactory=std::function<Consumer(const PeerPolicy&,const ManagedHostSpec&,std::string&)>;
int RunGpuHostBootstrap(HANDLE bootstrap,ConsumerFactory);
}
