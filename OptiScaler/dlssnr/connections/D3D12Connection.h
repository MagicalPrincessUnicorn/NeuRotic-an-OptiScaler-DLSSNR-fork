#pragma once
#include <dlssnr/PreparedTextureCapture.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include "MixedInputSelection.h"

namespace DlssNr::Connections {
using Microsoft::WRL::ComPtr;
using Descriptor = Neurotic::Feed::Prepared::Descriptor;
struct PendingFrame {
    NativeGuides::TextureCapture textures{};
    // Opaque, owner-authenticated ticket. Copying a ticket does not permit replay.
    std::shared_ptr<void> ownerLease;
};
struct CopybackReceipt { std::uint64_t capture=0,generation=0; bool completed=false; };
struct D3D12ObservedFrame {
    Descriptor description{};
    ComPtr<ID3D12Resource> color,depth,outputTarget,motion,distrust;
    D3D12_RESOURCE_STATES colorState=D3D12_RESOURCE_STATE_COMMON;
    // State of mip0/array0/depth plane0 only. The existing Shape contract
    // requires one mip/array/sample; unused stencil state need not be known.
    D3D12_RESOURCE_STATES depthState=D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES outputState=D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_STATES motionState=D3D12_RESOURCE_STATE_COMMON,distrustState=D3D12_RESOURCE_STATE_COMMON;
    bool deriveMotion=true,invertMotionY=false;
    bool statesKnown=false;
    ComPtr<ID3D12CommandQueue> sourceQueue;
    ComPtr<ID3D12Fence> producer;
    std::uint64_t producerValue=0;
    // The observation owner must exclude subsequent writes/recreation until Finish/Cancel.
    std::shared_ptr<void> lease;
};
// In-process offers only. The acquisition owner establishes info.frame and
// excludes all writes while frame.lease is held. No wire metadata grants rights.
struct D3D12SignalOffer { Mixed::Offer info{};D3D12ObservedFrame frame{}; };
// Serialized owner. Two immutable canonical pairs; one capture in flight.
// Failure after submission quarantines the owner and preserves referenced objects.
class D3D12Connection {
public:
    explicit D3D12Connection(ID3D12Device*);
    ~D3D12Connection();
    D3D12Connection(const D3D12Connection&)=delete;
    D3D12Connection& operator=(const D3D12Connection&)=delete;
    bool Capture(const D3D12ObservedFrame&,PendingFrame&,std::string&);
    bool CaptureMixed(const D3D12ObservedFrame& anchor,const Mixed::Anchor&,
        std::span<const D3D12SignalOffer>,const Mixed::Policy&,PendingFrame&,
        Mixed::Selection&,std::string&);
    bool Finish(PendingFrame&,nrpg::CpuMainlineClient::GpuOutput&,CopybackReceipt&,std::string&);
    bool Cancel(PendingFrame&,std::string&);
    bool RequiresRestart() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
