#include "../OptiScaler/dlssnr/PreFg.h"
#include <d3d12sdklayers.h>
#include <cassert>
#include <cstdio>

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
    RememberQueue(chain.Get(), queue.Get());
    ComPtr<ID3D12CommandQueue> remembered;
    UINT size = sizeof(ID3D12CommandQueue*);
    Check(chain->GetPrivateData(creationQueueKey, &size, remembered.GetAddressOf()));
    assert(remembered.Get() == queue.Get());
    assert(Register(chain.Get(), remembered.Get()) && State().swapchains == 1);
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
    alias.Reset(); chain.Reset();
    assert(State().swapchains == 0); // no swapchain/owner reference cycle
    DestroyWindow(window); UnregisterClassW(windowClass.lpszClassName, instance);
    std::puts("Pre-FG DXGI ownership: creation queue, aliases, duplicate registration, downstream bypass, resize, release PASS");
}
