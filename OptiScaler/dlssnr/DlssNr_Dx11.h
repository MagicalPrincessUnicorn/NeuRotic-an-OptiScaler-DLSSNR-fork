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
bool QualifiedPresentInputs(IDXGISwapChain* swapchain);

struct CreationValues
{
    unsigned int flags = 0, width = 0, height = 0, outWidth = 0, outHeight = 0;
    int quality = 0;
    // Successful parameter observations, including observed zero. Effective
    // flags can include the separately recorded configured HDR override.
    // These facts do not grant resource admission.
    bool flagsObserved = false, widthObserved = false, heightObserved = false;
    bool outWidthObserved = false, outHeightObserved = false, qualityObserved = false;
    bool hdrOverridden = false;
    bool valid = false;
    static CreationValues Read(NVSDK_NGX_Parameter* parameters);
};

class Feature
{
    struct State;
    std::unique_ptr<State> state;
  public:
    Feature();
    ~Feature();
    void Created(const CreationValues& caller, const CreationValues& effective);
    // Admission only: transport resources keep their original fence retirement.
    void Revoke();
    UINT64 AcquireAttempt();
    void Prepare(ID3D11DeviceContext* context, NVSDK_NGX_Parameter* parameters, UINT64 attempt);
    void Restore(NVSDK_NGX_Parameter* parameters, UINT64 attempt);
    void Complete(ID3D11DeviceContext* context, bool nativeSucceeded, UINT64 attempt);
};
}
