#pragma once
#include "D3D12Connection.h"
#include <d3d11_4.h>
namespace DlssNr::Connections {
struct D3D11ObservedFrame {
    Descriptor description{};
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> color,depth,outputTarget,motion,distrust;
    bool deriveMotion=true;
    std::shared_ptr<void> lease;
};
class D3D11Connection {
public:
    explicit D3D11Connection(ID3D12Device*);
    ~D3D11Connection();
    bool Capture(const D3D11ObservedFrame&,PendingFrame&,std::string&);
    bool Finish(PendingFrame&,nrpg::CpuMainlineClient::GpuOutput&,CopybackReceipt&,std::string&);
    bool Cancel(PendingFrame&,std::string&);
    bool RequiresRestart()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
