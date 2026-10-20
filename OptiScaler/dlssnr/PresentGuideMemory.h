#pragma once
#include "PresentGuideBudget.h"
#include "PresentGuideAllocation.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <map>
#include <tuple>

namespace DlssNr::PresentGuideMemory
{
inline UINT NormalizeMask(UINT mask) noexcept { return mask ? mask : 1u; }
struct Sample
{
    PresentGuideBudget::Observation observation;
    std::shared_ptr<PresentGuideAllocation::Ledger> ledger;
    LUID adapter {};
    UINT creationMask = 1, visibleMask = 1, node = 0;
    HRESULT queryResult = S_FALSE;
    bool queried = false;
    bool Matches(LUID other, UINT mask) const noexcept
    {
        return adapter.HighPart == other.HighPart && adapter.LowPart == other.LowPart &&
               creationMask == NormalizeMask(mask);
    }
};
// Owned by Bridge and called under its existing capture mutex. Histories are
// per actual adapter/node/LOCAL domain; switching devices cannot borrow a budget.
class Cache
{
    struct Entry
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
        PresentGuideBudget::Observation observation;
        std::shared_ptr<PresentGuideAllocation::Ledger> ledger = std::make_shared<PresentGuideAllocation::Ledger>();
    };
    using Key = std::tuple<LONG, DWORD, UINT>;
    std::map<Key, Entry> entries;
  public:
    Sample Read(ID3D12Device* device, const D3D12_HEAP_PROPERTIES& heap, bool query) noexcept
    {
        Sample result;
        if (!device) { result.queryResult = E_POINTER; return result; }
        result.adapter = device->GetAdapterLuid();
        // Zero is documented as one for these heap masks. This follows the
        // actual private-copy heap, rather than guessing a development GPU node.
        result.creationMask = NormalizeMask(heap.CreationNodeMask);
        result.visibleMask = NormalizeMask(heap.VisibleNodeMask);
        if ((result.creationMask & (result.creationMask - 1)) != 0 ||
            (result.visibleMask & result.creationMask) == 0)
        { result.queryResult = E_INVALIDARG; return result; }
        UINT bit = result.creationMask;
        while ((bit & 1u) == 0) { ++result.node; bit >>= 1; }
        if (result.node >= device->GetNodeCount())
        { result.queryResult = E_INVALIDARG; return result; }
        try
        {
            const Key key {result.adapter.HighPart, result.adapter.LowPart, result.node};
            auto found = entries.find(key);
            if (found == entries.end())
            {
                if (!query) return result;
                found = entries.try_emplace(key).first;
            }
            auto& entry = found->second;
            result.ledger = entry.ledger;
            result.observation = PresentGuideBudget::ObserveResult(entry.observation, false, 0, 0);
            if (!query)
            {
                return result;
            }
            result.queried = true;
            HRESULT hr = S_OK;
            if (!entry.adapter)
            {
                Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
                hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
                if (SUCCEEDED(hr) && !factory) hr = E_NOINTERFACE;
                if (SUCCEEDED(hr))
                    hr = factory->EnumAdapterByLuid(result.adapter, IID_PPV_ARGS(&entry.adapter));
            }
            DXGI_QUERY_VIDEO_MEMORY_INFO memory {};
            // DEFAULT private textures use LOCAL on both UMA and discrete
            // adapters. DedicatedVideoMemory is not an admission or identity test.
            if (SUCCEEDED(hr) && !entry.adapter) hr = E_NOINTERFACE;
            if (SUCCEEDED(hr))
                hr = entry.adapter->QueryVideoMemoryInfo(result.node, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory);
            entry.observation = PresentGuideBudget::ObserveResult(entry.observation, SUCCEEDED(hr),
                                                                  memory.Budget, memory.CurrentUsage);
            result.queryResult = hr;
            result.observation = entry.observation;
        }
        catch (...)
        {
            // Cache bookkeeping failure is missing observation, not incompatibility.
            // Existing safe reuse and the prior bounded fallback remain available.
            result.queryResult = E_OUTOFMEMORY;
        }
        return result;
    }
};
}
