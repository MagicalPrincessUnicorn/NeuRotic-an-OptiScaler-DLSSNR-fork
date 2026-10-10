#pragma once
#include <d3d12.h>
#include <mutex>
#include <new>
#include <unordered_map>

namespace DlssNr::NativeTextureContract
{
struct FormatUse
{
    UINT support1 = D3D12_FORMAT_SUPPORT1_TEXTURE2D;
    UINT support2 = 0;
};
struct FormatObservation
{
    HRESULT result = E_FAIL;
    UINT support1 = 0, support2 = 0;
    bool Supports(FormatUse use) const
    {
        return SUCCEEDED(result) && (support1 & use.support1) == use.support1 &&
            (support2 & use.support2) == use.support2;
    }
};
// One cache per renderer with its immutable device. Failure is not evidence of
// permanent lack of support, so only successful queries survive to another call.
class FormatCache
{
    std::mutex mutex_;
    std::unordered_map<DXGI_FORMAT, FormatObservation> formats_;
  public:
    template<class Query> FormatObservation Observe(DXGI_FORMAT format, Query&& query)
    {
        std::lock_guard lock(mutex_);
        if (const auto found = formats_.find(format); found != formats_.end()) return found->second;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support {};
        support.Format = format;
        const auto result = query(support);
        const FormatObservation observation {result, static_cast<UINT>(support.Support1),
                                              static_cast<UINT>(support.Support2)};
        if (SUCCEEDED(result))
            try { formats_.emplace(format, observation); }
            catch (const std::bad_alloc&) { /* Caching is optional; the observation remains valid. */ }
        return observation;
    }
};
inline const char* ViewRefusal(const D3D12_RESOURCE_DESC& desc, bool unordered)
{
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize != 1 ||
        desc.SampleDesc.Count != 1 || !desc.Width || !desc.Height)
        return "NR composition requires a nonempty single-slice, single-sample Texture2D view";
    if (unordered && !(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
        return "NR composition output does not allow an unordered-access view";
    if (!unordered && (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))
        return "NR composition input denies a shader-resource view";
    return nullptr;
}
// These modes/slots mirror precompile/dlssnr.hlsl. A bound but unused view does
// not demand a read operation. Sample uses that shader's LINEAR sampler.
inline FormatUse ComposeSrvUse(unsigned mode, unsigned slot, bool gameExposure,
                               bool passthrough, unsigned compareMode, unsigned width, unsigned height)
{
    FormatUse use;
    const bool resolve = mode != 0 && mode != 2 && mode != 3 && mode != 4 && mode != 5 && mode != 6;
    // The one-pixel meter returns after gMotion.Load at (0,0); every
    // other thread is outside the dispatch dimensions. Its source is unread.
    if (slot == 0 && !(mode == 3 && width == 1 && height == 1))
        use.support1 |= mode == 0 || mode == 2 || mode == 3 || mode == 4 ?
            D3D12_FORMAT_SUPPORT1_SHADER_LOAD : D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
    else if (slot == 1 && resolve)
        use.support1 |= D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
    else if (slot == 2 && (mode == 6 || resolve))
        use.support1 |= mode == 6 || compareMode == 1 ? D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE :
                                                       D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    else if (slot == 3 && mode == 3)
        use.support1 |= D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    else if (slot == 4 && gameExposure && ((mode == 0 && !passthrough) || mode == 6 || resolve))
        use.support1 |= D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    return use;
}
inline FormatUse ComposeUavUse(unsigned mode, unsigned slot)
{
    FormatUse use;
    use.support1 |= D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW;
    if (slot == 0 || mode == 0) use.support2 = D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    // gTarget and gKeep are never read as typed UAVs by this shader.
    return use;
}
}
