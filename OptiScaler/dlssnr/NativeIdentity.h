#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <utility>
#include <type_traits>

namespace DlssNr::NativeIdentity
{
// StreamlineRetrieveBaseInterface, also used by NR GPU safety and
// Util::CheckForRealObject. QueryInterface returns an owning reference to the
// underlying interface. No global Streamline API/state or borrowed pointer needed.
inline constexpr GUID streamlineBase =
    {0xadec44e2, 0x61f0, 0x45c3, {0xad, 0x9f, 0x1b, 0x37, 0x37, 0x92, 0x84, 0xff}};

// NeuRotic's swapchain wrapper exposes an owning IUnknown for observation
// identity only. This private contract does not advertise a rendering unwrap.
inline constexpr GUID neuroticSwapchainIdentityBase =
    {0x63f165e1, 0x1ff0, 0x46ef, {0xb7, 0x96, 0x90, 0x3f, 0x0c, 0x82, 0x98, 0x71}};

// ReShade 6.8 IID_UnwrappedObject (source/com_utils.hpp and
// source/d3d12/d3d12_device.cpp): successful QI AddRefs the original device.
// Only device identity uses this interface. Keep lists/queues/resources on
// their existing callback path so this does not bypass ReShade or its add-ons.
inline constexpr GUID reshadeDeviceBase =
    {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};

template<class T> struct Resolved
{
    Microsoft::WRL::ComPtr<T> object;
    HRESULT result = E_POINTER;
    unsigned int layers = 0;
    unsigned int reshadeLayers = 0;
};

namespace detail
{
template<class T, bool identityOnly> Resolved<T> Resolve(IUnknown* input)
{
    Resolved<T> resolved;
    if (!input) return resolved;
    Microsoft::WRL::ComPtr<IUnknown> current = input;
    // Bounded traversal also refuses self/cyclic and excessive wrapper chains.
    for (;;)
    {
        Microsoft::WRL::ComPtr<IUnknown> base;
        resolved.result = current->QueryInterface(streamlineBase,
                                                  reinterpret_cast<void**>(base.GetAddressOf()));
        bool reshadeLayer = false;
        if constexpr (identityOnly && std::is_same_v<T, IDXGISwapChain>)
        {
            if (resolved.result == E_NOINTERFACE)
            {
                if (base) { resolved.result = E_UNEXPECTED; return resolved; }
                resolved.result = current->QueryInterface(neuroticSwapchainIdentityBase,
                    reinterpret_cast<void**>(base.GetAddressOf()));
            }
        }
        if constexpr (identityOnly && std::is_same_v<T, ID3D12Device>)
        {
            if (resolved.result == E_NOINTERFACE)
            {
                if (base) { resolved.result = E_UNEXPECTED; return resolved; }
                resolved.result = current->QueryInterface(reshadeDeviceBase,
                    reinterpret_cast<void**>(base.GetAddressOf()));
                reshadeLayer = SUCCEEDED(resolved.result);
            }
        }
        if (resolved.result == E_NOINTERFACE)
        {
            // A malformed optional-QI result is not a valid terminal. ComPtr
            // still releases the illegally returned owning reference.
            if (base) { resolved.result = E_UNEXPECTED; return resolved; }
            resolved.result = current.As(&resolved.object);
            return resolved;
        }
        if (FAILED(resolved.result)) return resolved;
        if (!base || resolved.layers == 4)
        {
            resolved.result = E_UNEXPECTED;
            return resolved;
        }
        ++resolved.layers;
        if (reshadeLayer) ++resolved.reshadeLayers;
        current = std::move(base);
    }
}

} // namespace detail

// Preserve the rendering interface. ReShade unwrap is only an ownership proof;
// using its base device for execution bypasses proxy creation/callback behavior.
template<class T> Resolved<T> Resolve(IUnknown* input)
{
    return detail::Resolve<T, false>(input);
}

// Expose only IUnknown for comparison, never a substitute rendering device.
inline Resolved<IUnknown> ResolveDeviceIdentity(IUnknown* input)
{
    const auto device = detail::Resolve<ID3D12Device, true>(input);
    Resolved<IUnknown> identity;
    identity.result = device.result;
    identity.layers = device.layers;
    identity.reshadeLayers = device.reshadeLayers;
    if (SUCCEEDED(identity.result) && device.object)
        identity.result = device.object.As(&identity.object);
    return identity;
}

inline Resolved<IUnknown> ResolveSwapchainIdentity(IUnknown* input)
{
    const auto chain = detail::Resolve<IDXGISwapChain, true>(input);
    Resolved<IUnknown> identity;
    identity.result = chain.result;
    identity.layers = chain.layers;
    if (SUCCEEDED(identity.result) && chain.object)
        identity.result = chain.object.As(&identity.object);
    return identity;
}

struct DeviceComparison
{
    Resolved<IUnknown> left, right;
    bool equal = false;
};
inline DeviceComparison CompareDevices(IUnknown* a, IUnknown* b)
{
    DeviceComparison comparison {ResolveDeviceIdentity(a), ResolveDeviceIdentity(b)};
    comparison.equal = SUCCEEDED(comparison.left.result) && SUCCEEDED(comparison.right.result) &&
        comparison.left.object && comparison.right.object &&
        comparison.left.object.Get() == comparison.right.object.Get();
    // Adapter/LUID equality cannot establish device ownership.
    return comparison;
}
}
