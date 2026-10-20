#include "dlssnr/NativeIdentity.h"

#include <cassert>
#include <atomic>
#include <iostream>
#include <vector>
#include <dxgi1_4.h>
#include "nr_reshade_device_fixture.h"

using Microsoft::WRL::ComPtr;

// Minimal typed IUnknown wrapper. Device QI always returns a real WARP device;
// the test never fabricates an ID3D12Device vtable or invokes a proprietary provider.
class Probe final : public IUnknown
{
  public:
    enum class Mode { StreamlineAlias, OpaqueTerminal, NullBase, FailedBase,
        FailedBaseWithValue, NoInterfaceWithValue, SelfCycle, NullDevice,
        FailedDeviceWithValue, NullIdentity, FailedIdentityWithValue };

    Probe(ID3D12Device* device, Mode behavior) : native(device), mode(behavior) {}
    void SetBase(IUnknown* next) { base = next; }
    ULONG References() const { return refs.load(); }
    unsigned Releases() const { return releaseCalls.load(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == DlssNr::NativeIdentity::streamlineBase)
        {
            if (mode == Mode::NullBase) return S_OK;
            if (mode == Mode::FailedBase) return E_FAIL;
            if (mode == Mode::OpaqueTerminal || mode == Mode::NullDevice ||
                mode == Mode::FailedDeviceWithValue || mode == Mode::NullIdentity ||
                mode == Mode::FailedIdentityWithValue) return E_NOINTERFACE;
            IUnknown* value = mode == Mode::SelfCycle ? static_cast<IUnknown*>(this) : base.Get();
            if (value) { value->AddRef(); *out = value; }
            if (mode == Mode::FailedBaseWithValue) return E_FAIL;
            if (mode == Mode::NoInterfaceWithValue) return E_NOINTERFACE;
            return S_OK;
        }
        if (id == __uuidof(IUnknown))
        {
            if (mode == Mode::NullIdentity) return S_OK;
            *out = static_cast<IUnknown*>(this);
            AddRef();
            return mode == Mode::FailedIdentityWithValue ? E_FAIL : S_OK;
        }
        if (id == __uuidof(ID3D12Device))
        {
            if (mode == Mode::NullDevice) return S_OK;
            if (!native) return E_NOINTERFACE;
            *out = native.Get();
            native->AddRef();
            return mode == Mode::FailedDeviceWithValue ? E_FAIL : S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ++releaseCalls;
        const ULONG left = --refs;
        if (!left) delete this;
        return left;
    }

  private:
    std::atomic<ULONG> refs {1};
    std::atomic<unsigned> releaseCalls {0};
    ComPtr<ID3D12Device> native;
    ComPtr<IUnknown> base;
    Mode mode;
};

static ComPtr<ID3D12Device> WarpDevice()
{
    ComPtr<IDXGIFactory4> factory;
    assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    ComPtr<IDXGIAdapter> warp;
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
    ComPtr<ID3D12Device> device;
    assert(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
    return device;
}

static ComPtr<Probe> Make(ID3D12Device* device, Probe::Mode mode)
{
    ComPtr<Probe> result;
    result.Attach(new Probe(device, mode));
    return result;
}

int main()
{
    using namespace DlssNr::NativeIdentity;
    const auto native = WarpDevice();
    ComPtr<ReShadeDeviceProxy> reshade;
    reshade.Attach(new ReShadeDeviceProxy(native.Get()));
    const auto reshadeComparison = CompareDevices(reshade.Get(), native.Get());
    assert(SUCCEEDED(reshadeComparison.left.result) && SUCCEEDED(reshadeComparison.right.result));
    assert(reshadeComparison.equal); // old code returned success, zero layers, unequal identities
    assert(reshadeComparison.left.reshadeLayers == 1 && reshadeComparison.right.reshadeLayers == 0);
    assert(CompareDevices(native.Get(), reshade.Get()).equal);
    const auto refsBefore = reshade->References();
    for (int i = 0; i < 1000; ++i) assert(CompareDevices(reshade.Get(), native.Get()).equal);
    assert(reshade->References() == refsBefore);
    // Rendering must retain the ReShade device even when owner comparison unwraps it.
    // Present uses this interface for private lists/resources and provider setup.
    const auto executionDevice = Resolve<ID3D12Device>(reshade.Get());
    if (executionDevice.object.Get() != static_cast<ID3D12Device*>(reshade.Get()))
    {
        std::cerr << "FAIL: owner reconciliation replaced the rendering device interface\n";
        return 1;
    }
    {
        // Exercise actual creation/submission through the retained interface.
        // A canonical-base substitution silently skips these proxy callbacks.
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Fence> fence;
        D3D12_COMMAND_QUEUE_DESC desc {};
        assert(SUCCEEDED(executionDevice.object->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))));
        assert(SUCCEEDED(executionDevice.object->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
        assert(SUCCEEDED(executionDevice.object->CreateCommandList(0,
            D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));
        assert(SUCCEEDED(executionDevice.object->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
        assert(reshade->commandListsCreated == 1 && reshade->fencesCreated == 1);
        assert(SUCCEEDED(list->Close()));
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        assert(SUCCEEDED(queue->Signal(fence.Get(), 1)));
        HANDLE done = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        assert(done && SUCCEEDED(fence->SetEventOnCompletion(1, done)));
        assert(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0);
        CloseHandle(done);
        assert(fence->GetCompletedValue() == 1);
    }
    // Non-device resolution must keep the add-on callback identity intact.
    assert(Resolve<IUnknown>(reshade.Get()).object.Get() == static_cast<IUnknown*>(reshade.Get()));
    for (auto mode : {ReShadeDeviceProxy::NullBase, ReShadeDeviceProxy::Self, ReShadeDeviceProxy::Error,
                     ReShadeDeviceProxy::Unsupported, ReShadeDeviceProxy::MalformedNoInterface,
                     ReShadeDeviceProxy::FailedBaseWithValue})
    {
        ComPtr<ReShadeDeviceProxy> invalid;
        invalid.Attach(new ReShadeDeviceProxy(native.Get(), mode));
        assert(!CompareDevices(invalid.Get(), native.Get()).equal);
        assert(invalid->References() == 1);
    }
    std::vector<ComPtr<ReShadeDeviceProxy>> reshadeChain;
    for (int i = 0; i < 5; ++i)
    {
        ComPtr<ReShadeDeviceProxy> layer;
        layer.Attach(new ReShadeDeviceProxy(i ? reshadeChain.back().Get() : native.Get()));
        reshadeChain.push_back(layer);
    }
    assert(CompareDevices(reshadeChain[3].Get(), native.Get()).equal);
    assert(!CompareDevices(reshadeChain[4].Get(), native.Get()).equal);
    auto mixedAlias = Make(native.Get(), Probe::Mode::StreamlineAlias);
    mixedAlias->SetBase(reshade.Get());
    assert(CompareDevices(mixedAlias.Get(), native.Get()).equal);
    assert(Resolve<ID3D12Device>(mixedAlias.Get()).object.Get() == reshade.Get());
    assert(ResolveDeviceIdentity(mixedAlias.Get()).reshadeLayers == 1);
    static_assert(!std::is_convertible_v<decltype(reshadeComparison.left.object.Get()),
                  ID3D12Device*>,
                  "An owner identity must not be assignable to an execution device");
    ComPtr<ID3D12Device> other;
    // D3D12 may return one cached COM device for repeated calls on the WARP
    // adapter. A separate hardware adapter supplies the distinct-device case.
    assert(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&other))));

    auto terminal = Make(native.Get(), Probe::Mode::OpaqueTerminal);
    auto alias = Make(native.Get(), Probe::Mode::StreamlineAlias);
    alias->SetBase(terminal.Get());
    assert(CompareDevices(alias.Get(), native.Get()).equal);
    assert(CompareDevices(native.Get(), alias.Get()).equal);
    assert(CompareDevices(alias.Get(), terminal.Get()).equal);
    if (native.Get() != other.Get())
        assert(!CompareDevices(native.Get(), other.Get()).equal);

    auto malformed = Make(native.Get(), Probe::Mode::NoInterfaceWithValue);
    malformed->SetBase(terminal.Get());
    const ULONG before = terminal->References();
    const unsigned released = terminal->Releases();
    for (int i = 0; i < 32; ++i)
    {
        const auto result = Resolve<ID3D12Device>(malformed.Get());
        assert(FAILED(result.result));
        assert(!result.object);
        assert(!CompareDevices(malformed.Get(), native.Get()).equal);
    }
    assert(terminal->References() == before);
    assert(terminal->Releases() == released + 64);

    auto failedBase = Make(native.Get(), Probe::Mode::FailedBaseWithValue);
    failedBase->SetBase(terminal.Get());
    const ULONG failedBefore = terminal->References();
    for (int i = 0; i < 16; ++i)
        assert(!CompareDevices(failedBase.Get(), native.Get()).equal);
    assert(terminal->References() == failedBefore);

    auto badDevice = Make(native.Get(), Probe::Mode::FailedDeviceWithValue);
    assert(!CompareDevices(badDevice.Get(), native.Get()).equal);
    assert(!CompareDevices(native.Get(), badDevice.Get()).equal);
    auto nullDevice = Make(native.Get(), Probe::Mode::NullDevice);
    assert(!CompareDevices(nullDevice.Get(), native.Get()).equal);
    auto nullBase = Make(native.Get(), Probe::Mode::NullBase);
    assert(!CompareDevices(nullBase.Get(), native.Get()).equal);

    auto nullIdentity = Make(native.Get(), Probe::Mode::NullIdentity);
    assert(!Resolve<IUnknown>(nullIdentity.Get()).object);
    auto failedIdentity = Make(native.Get(), Probe::Mode::FailedIdentityWithValue);
    assert(FAILED(Resolve<IUnknown>(failedIdentity.Get()).result));

    auto self = Make(native.Get(), Probe::Mode::SelfCycle);
    assert(!CompareDevices(self.Get(), native.Get()).equal);
    std::vector<ComPtr<Probe>> layers;
    for (int i = 0; i < 5; ++i) layers.push_back(Make(native.Get(), Probe::Mode::StreamlineAlias));
    layers[4]->SetBase(terminal.Get());
    for (int i = 3; i >= 0; --i) layers[i]->SetBase(layers[i + 1].Get());
    assert(CompareDevices(layers[1].Get(), native.Get()).equal); // four wrapper layers
    assert(!CompareDevices(layers[0].Get(), native.Get()).equal); // fifth refused
    for (auto& layer : layers) layer->SetBase(nullptr);
    std::cout << "PASS: bounded native identity, malformed QI, aliases and reference balance\n";
}
