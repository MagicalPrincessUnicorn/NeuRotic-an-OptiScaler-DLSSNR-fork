// GPL-3.0. One actual device binding, selected before WGC/provider creation.
#pragma once
#include "ProcessingAdapterPolicy.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
namespace nrw::adapters {
struct Binding {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Identity id;std::uint64_t session=0;
};
struct Report {
    std::string ordering="not-enumerated",failurePhase;
    std::int32_t enumerationCode=0;
    Identity selected;
    std::vector<Attempt> attempts;
};
struct CaptureIdentity {Identity capture,output;};
HRESULT ReadIdentity(ID3D11Device*,Identity&) noexcept;
std::string IdentityText(Identity);
bool Select(std::uint64_t session,unsigned width,unsigned height,bool inference,
            Binding&,Report&,std::string& reason);
}
