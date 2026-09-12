#include "../OptiScaler/dlssnr/PreFg.h"
#include <d3d12sdklayers.h>
#include <cassert>
#include <cstdio>
#include "nr_streamline_proxy_fixture.h"

using Microsoft::WRL::ComPtr;
static void Check(HRESULT result) { assert(SUCCEEDED(result)); }
int main()
{
    using namespace DlssNr::PreFg;
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC qd {};
    Check(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)));
    ComPtr<IdentityProxy> deviceProxy;
    deviceProxy.Attach(new IdentityProxy(device.Get()));
    ComPtr<QueueProxy> queueProxy;
    queueProxy.Attach(new QueueProxy(queue.Get(), deviceProxy.Get()));
    // Reproduce the failed identity comparison: proxy IUnknown != native IUnknown.
    ComPtr<IUnknown> wrappedIdentity, nativeIdentity;
    Check(queueProxy->GetDevice(IID_PPV_ARGS(&wrappedIdentity)));
    Check(device.As(&nativeIdentity));
    assert(wrappedIdentity != nativeIdentity);
    assert(DlssNr::NativeIdentity::CompareDevices(wrappedIdentity.Get(), device.Get()).equal);
    assert(DlssNr::NativeIdentity::CompareDevices(device.Get(), wrappedIdentity.Get()).equal);
    assert(!DlssNr::NativeIdentity::CompareDevices(nullptr, device.Get()).equal);
    // A non-device object and unknown proxy cannot be accepted even if they wrap the same GPU.
    assert(!DlssNr::NativeIdentity::CompareDevices(queue.Get(), device.Get()).equal);
    for (auto mode : {IdentityProxy::NullBase, IdentityProxy::Self, IdentityProxy::Error, IdentityProxy::Unsupported})
    {
        ComPtr<IdentityProxy> malformed;
        malformed.Attach(new IdentityProxy(device.Get(), mode));
        assert(!DlssNr::NativeIdentity::CompareDevices(malformed.Get(), device.Get()).equal);
        assert(malformed->References() == 1);
    }
    ComPtr<IdentityProxy> nested;
    nested.Attach(new IdentityProxy(deviceProxy.Get()));
    assert(DlssNr::NativeIdentity::CompareDevices(nested.Get(), device.Get()).equal);
    const auto refs = deviceProxy->References();
    for (int i = 0; i < 1000; ++i)
        assert(DlssNr::NativeIdentity::CompareDevices(deviceProxy.Get(), device.Get()).equal);
    assert(deviceProxy->References() == refs);
    // Distinct real device: hardware versus WARP if a hardware adapter is available.
    ComPtr<IDXGIAdapter1> hardware;
    bool differentDeviceChecked = false;
    for (UINT i = 0; factory->EnumAdapters1(i, &hardware) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 hd {}; Check(hardware->GetDesc1(&hd));
        ComPtr<ID3D12Device> other;
        if (!(hd.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            SUCCEEDED(D3D12CreateDevice(hardware.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&other))))
        {
            assert(!DlssNr::NativeIdentity::CompareDevices(deviceProxy.Get(), other.Get()).equal);
            differentDeviceChecked = true; break;
        }
        hardware.Reset();
    }
    assert(differentDeviceChecked);
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"NRPreFgOwnershipFixture";
    assert(RegisterClassW(&windowClass));
    HWND window = CreateWindowW(windowClass.lpszClassName, L"NR offline fixture", WS_POPUP,
        0, 0, 160, 90, nullptr, nullptr, instance, nullptr);
    assert(window); // deliberately hidden: no game, overlay or user-visible window
    DXGI_SWAP_CHAIN_DESC1 desc {};
    desc.Width = 160; desc.Height = 90; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> chain;
    Check(factory->CreateSwapChainForHwnd(queue.Get(), window, &desc, nullptr, nullptr, &chain));
    assert(!BypassLate(chain.Get()));
    RememberQueue(chain.Get(), queueProxy.Get());
    ComPtr<ID3D12CommandQueue> remembered;
    UINT size = sizeof(ID3D12CommandQueue*);
    Check(chain->GetPrivateData(creationQueueKey, &size, remembered.GetAddressOf()));
    assert(remembered.Get() == queue.Get());
    assert(Register(chain.Get(), queueProxy.Get()) && State().swapchains == 1);
    assert(GetOwner(chain.Get())->queue.Get() == queue.Get());
    ComPtr<ID3D12Device> ownedDevice;
    Check(GetOwner(chain.Get())->queue->GetDevice(IID_PPV_ARGS(&ownedDevice)));
    assert(DlssNr::NativeIdentity::CompareDevices(ownedDevice.Get(), device.Get()).equal);
    assert(Register(chain.Get(), queue.Get()) && State().swapchains == 1);
    ComPtr<IDXGISwapChain3> alias;
    Check(chain.As(&alias));
    assert(GetOwner(alias.Get()).Get() == GetOwner(chain.Get()).Get());
    ObserveConstants(9, 0); ObserveTags(9, 0);
    assert(Claim().valid);
    const auto real = State().realCalls.load();
    assert(BypassLate(chain.Get()) && BypassLate(alias.Get()));
    assert(State().realCalls == real && State().bypassed == 2);
    // Neither the owner nor remembered queue retains a backbuffer and blocks resize.
    Check(chain->ResizeBuffers(2, 320, 180, desc.Format, 0));
    assert(GetOwner(chain.Get())->queue.Get() == queue.Get());
    // Releasing application proxies leaves the retained underlying queue usable.
    queueProxy.Reset(); nested.Reset(); wrappedIdentity.Reset(); deviceProxy.Reset();
    Check(GetOwner(chain.Get())->queue->GetDevice(IID_PPV_ARGS(ownedDevice.ReleaseAndGetAddressOf())));
    alias.Reset(); chain.Reset();
    assert(State().swapchains == 0); // no swapchain/owner reference cycle
    DestroyWindow(window); UnregisterClassW(windowClass.lpszClassName, instance);
    std::puts("Pre-FG DXGI ownership: Streamline proxy/base identity, exact native queue, malformed wrappers, distinct devices, refcounts, resize, release PASS");
}
