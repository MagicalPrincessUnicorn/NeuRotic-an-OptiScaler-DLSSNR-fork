#pragma once
#include "PreparedGpuProtocol.h"
#include "../src/CpuMainlineClient.h"
#include <filesystem>
namespace nrpg::gpu {
struct ManagedHostSpec {
    // Installation-owned paths, never decoded from peer messages.
    std::filesystem::path executable,coreModule;
    Nonce buildIdentity=BuildIdentity;
    std::uint32_t consumerMode=1,timeoutMs=5000;
};
struct PeerPolicy {
    T::Identity episode{};
    std::uint64_t adapterLuid=0,creationTime=0;
    std::uint32_t processId=0;
    Nonce nonce{};
    Microsoft::WRL::ComPtr<ID3D12Device> device;
};
struct CopybackReceipt {
    T::FrameTicket ticket;
    Microsoft::WRL::ComPtr<ID3D12Fence> completion;
    std::uint64_t value=0;
    // Supplied only by the local API copyback owner, never a decoded remote bit.
    bool restored=false;
};
class PreparedGpuClient {
public:
    PreparedGpuClient();~PreparedGpuClient();
    PreparedGpuClient(const PreparedGpuClient&)=delete;
    bool Start(const ManagedHostSpec&,const PeerPolicy&,std::string&);
    // handle is the caller's NT handle. Client duplicates it into the pinned child.
    bool Register(const GpuRegistration&,std::string&);
    bool Process(const GpuFrameRequest&,GpuFrameResult&,std::string&);
    bool Release(const CopybackReceipt&,std::string&);
    bool Drain(std::string&);
    ID3D12Fence* ProducerFence()const;
    ID3D12Fence* OutputFence()const;
    // Borrowed local alias. Host-created GL/Vulkan ingress can export an NT
    // handle from this object for its API import; caller balances that handle.
    ID3D12Resource* Resource(std::uint32_t registrationId)const;
    Nonce SessionNonce()const;
    bool RequiresRestart()const;
private:struct Impl;std::unique_ptr<Impl> p_;
};
}
