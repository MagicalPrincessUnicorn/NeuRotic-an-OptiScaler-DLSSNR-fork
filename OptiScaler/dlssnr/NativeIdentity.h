#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <utility>

namespace DlssNr::NativeIdentity
{
// StreamlineRetrieveBaseInterface, also used by NR GPU safety and
// Util::CheckForRealObject. QueryInterface returns an owning reference to the
// underlying interface. No global Streamline API/state or borrowed pointer needed.
inline constexpr GUID streamlineBase =
    {0xadec44e2, 0x61f0, 0x45c3, {0xad, 0x9f, 0x1b, 0x37, 0x37, 0x92, 0x84, 0xff}};

template<class T> struct Resolved
{
    Microsoft::WRL::ComPtr<T> object;
    HRESULT result = E_POINTER;
    unsigned int layers = 0;
};

template<class T> Resolved<T> Resolve(IUnknown* input)
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
        if (resolved.result == E_NOINTERFACE)
        {
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
        current = std::move(base);
    }
}

struct DeviceComparison
{
    Resolved<ID3D12Device> left, right;
    bool equal = false;
};
inline DeviceComparison CompareDevices(IUnknown* a, IUnknown* b)
{
    DeviceComparison comparison {Resolve<ID3D12Device>(a), Resolve<ID3D12Device>(b)};
    Microsoft::WRL::ComPtr<IUnknown> left, right;
    comparison.equal = comparison.left.object && comparison.right.object &&
        SUCCEEDED(comparison.left.object.As(&left)) && SUCCEEDED(comparison.right.object.As(&right)) &&
        left.Get() == right.Get();
    // Adapter/LUID equality cannot establish device ownership.
    return comparison;
}
}
