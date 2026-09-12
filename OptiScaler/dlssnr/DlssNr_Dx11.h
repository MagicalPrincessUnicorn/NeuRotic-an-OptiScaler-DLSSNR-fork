#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <nvsdk_ngx.h>
#include <memory>

namespace DlssNr::NativeDx11
{
// Registered by the native swapchain wrapper, not inferred from the last global pointer.
void RegisterSwapchain(IDXGISwapChain* swapchain);
void UnregisterSwapchain(IDXGISwapChain* swapchain);
void ResizeSwapchain(IDXGISwapChain* swapchain);

class Feature
{
    struct State;
    std::unique_ptr<State> state;
  public:
    Feature();
    ~Feature();
    void Created(NVSDK_NGX_Parameter* parameters);
    void Prepare(ID3D11DeviceContext* context, NVSDK_NGX_Parameter* parameters);
    void Complete(ID3D11DeviceContext* context, bool nativeSucceeded);
};
}
