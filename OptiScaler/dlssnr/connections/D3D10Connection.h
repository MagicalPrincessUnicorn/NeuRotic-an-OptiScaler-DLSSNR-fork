#pragma once
#include "D3D11Connection.h"
#include <d3d10.h>
namespace DlssNr::Connections {
struct D3D10ObservedFrame {
    Descriptor description{};
    ComPtr<ID3D10Device> device;
    ComPtr<ID3D10Texture2D> color,depth,outputTarget,motion,distrust;
    bool deriveMotion=true;
    std::shared_ptr<void> lease;
};
class D3D10Connection {
public:
    explicit D3D10Connection(ID3D12Device*);
    ~D3D10Connection();
    bool Capture(const D3D10ObservedFrame&,PendingFrame&,std::string&);
    bool Finish(PendingFrame&,nrpg::CpuMainlineClient::GpuOutput&,CopybackReceipt&,std::string&);
    bool Cancel(PendingFrame&,std::string&);
    bool RequiresRestart()const;
private: struct Impl;std::unique_ptr<Impl> impl_;
};
}
