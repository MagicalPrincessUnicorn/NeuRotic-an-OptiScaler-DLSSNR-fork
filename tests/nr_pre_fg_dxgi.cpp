#include "../OptiScaler/dlssnr/PreFg.h"
#include <d3d12sdklayers.h>
#include <cassert>
#include <cstdio>
#include "nr_streamline_proxy_fixture.h"
#include "../OptiScaler/dlssnr/PresentColor.h"

using Microsoft::WRL::ComPtr;
static void Check(HRESULT result) { assert(SUCCEEDED(result)); }
static void TestHdrProxyObservation(IDXGISwapChain1* chain, IDXGISwapChain1* other)
{
    using namespace DlssNr;
    HdrObservation::Registry observations;
    ComPtr<IdentityProxy> proxy, nested;
    proxy.Attach(new IdentityProxy(chain));
    nested.Attach(new IdentityProxy(proxy.Get()));
    ComPtr<IDXGISwapChain3> alias;
    Check(chain->QueryInterface(IID_PPV_ARGS(&alias)));
    const auto first = observations.Register(proxy.Get(), DXGI_FORMAT_R10G10B10A2_UNORM);
    observations.RecordColorSpace(proxy.Get(), DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, S_OK);
    const auto fromNative = observations.Read(chain);
    if (!fromNative.registered)
    {
        std::fputs("FAIL: native Present cannot read HDR observation registered through Streamline proxy\n", stderr);
        std::exit(1);
    }
    assert(PresentColor::Select(fromNative, DXGI_FORMAT_R10G10B10A2_UNORM).profile == PresentColor::Profile::Hdr10Pq2020);
    assert(observations.Read(alias.Get()).identityGeneration == fromNative.identityGeneration);
    assert(observations.Read(nested.Get()).identityGeneration == fromNative.identityGeneration);
    // The game stack also has NeuRotic's own wrapper underneath Streamline.
    // Its identity-only base contract must be traversed without changing render interfaces.
    constexpr GUID neuroticBase = {0x63f165e1, 0x1ff0, 0x46ef, {0xb7,0x96,0x90,0x3f,0x0c,0x82,0x98,0x71}};
    ComPtr<IdentityProxy> owned, outer;
    owned.Attach(new IdentityProxy(chain, IdentityProxy::Normal, neuroticBase));
    outer.Attach(new IdentityProxy(owned.Get()));
    if (!observations.Read(outer.Get()).registered)
    {
        std::fputs("FAIL: Streamline over NeuRotic wrapper cannot join native HDR observation\n", stderr);
        std::exit(1);
    }
    // The ordinary resolver must not start unwrapping NeuRotic for execution.
    assert(!NativeIdentity::Resolve<IDXGISwapChain>(owned.Get()).object);
    // A second real swapchain on the same device must not borrow HDR from this one.
    assert(!observations.Read(other).registered);
    observations.Register(other, DXGI_FORMAT_R10G10B10A2_UNORM);
    assert(PresentColor::Select(observations.Read(other), DXGI_FORMAT_R10G10B10A2_UNORM).profile == PresentColor::Profile::Sdr709);
    observations.Unregister(other);
    assert(observations.Read(chain).registered);
    observations.Register(outer.Get(), DXGI_FORMAT_R10G10B10A2_UNORM);
    observations.Unregister(proxy.Get()); // another registered wrapper still owns the observation
    const auto refs = proxy->References();
    for (int i = 0; i < 1000; ++i) assert(observations.Read(nested.Get()).registered);
    assert(proxy->References() == refs);
    observations.RecordColorSpace(nested.Get(), DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, E_FAIL);
    assert(observations.Read(chain).colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
    observations.BeginResize(outer.Get());
    assert(!PresentColor::Select(observations.Read(chain), DXGI_FORMAT_R10G10B10A2_UNORM).Supported());
    observations.CompleteResize(alias.Get(), E_INVALIDARG, DXGI_FORMAT_UNKNOWN);
    assert(PresentColor::Select(observations.Read(chain), DXGI_FORMAT_R10G10B10A2_UNORM).Supported());
    DXGI_HDR_METADATA_HDR10 metadata {};
    metadata.MaxContentLightLevel = 1000;
    observations.RecordMetadata(outer.Get(), DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata, S_OK);
    assert(observations.Read(chain).metadataSize == sizeof(metadata));
    for (auto baseId : {NativeIdentity::streamlineBase, neuroticBase})
    for (auto mode : {IdentityProxy::NullBase, IdentityProxy::Self, IdentityProxy::Error, IdentityProxy::Unsupported})
    {
        ComPtr<IdentityProxy> malformed;
        malformed.Attach(new IdentityProxy(chain, mode, baseId));
        assert(!observations.Register(malformed.Get(), DXGI_FORMAT_R10G10B10A2_UNORM).registered);
        assert(!observations.Read(malformed.Get()).registered);
        observations.Unregister(malformed.Get());
        assert(malformed->References() == 1);
    }
    observations.Unregister(nested.Get());
    assert(!observations.Read(chain).registered);
    const auto fresh = observations.Register(chain, DXGI_FORMAT_R10G10B10A2_UNORM);
    assert(fresh.identityGeneration != first.identityGeneration && !fresh.colorSpaceObserved);
    assert(observations.Read(proxy.Get()).colorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    observations.Unregister(alias.Get());
    std::puts("HDR observation: proxy/native join, alias/nested identity, failed color/resize, metadata, balanced owners and malformed wrapper refusal PASS");
}
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
    desc.Width = 160; desc.Height = 90; desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> chain;
    Check(factory->CreateSwapChainForHwnd(queue.Get(), window, &desc, nullptr, nullptr, &chain));
    HWND otherWindow = CreateWindowW(windowClass.lpszClassName, L"NR second offline fixture", WS_POPUP,
        0, 0, 160, 90, nullptr, nullptr, instance, nullptr);
    assert(otherWindow);
    ComPtr<IDXGISwapChain1> otherChain;
    Check(factory->CreateSwapChainForHwnd(queue.Get(), otherWindow, &desc, nullptr, nullptr, &otherChain));
    TestHdrProxyObservation(chain.Get(), otherChain.Get());
    otherChain.Reset();
    DestroyWindow(otherWindow);
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
    ObserveConstants(9, 0, Provider().generation); ObserveTags(9, 0, Provider().generation);
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
