#pragma once
#include <algorithm>
#include <cstdint>

namespace Neurotic::Context::GuideGeometry
{
struct Extent32 {std::uint32_t width=0,height=0;};
struct Extent64 {std::uint64_t width=0,height=0;};
struct Origin32 {std::uint32_t x=0,y=0;};
struct Rect32 {std::uint32_t x=0,y=0,width=0,height=0;};
struct Pair {Rect32 depth,motion;};

constexpr Rect32 ForResolution(Origin32 origin,Extent32 render,Extent32 output,bool lowResolution)
{
    const auto extent=lowResolution?render:output;
    return {origin.x,origin.y,extent.width,extent.height};
}

// Sampling geometry only. Allocation, resource identity, feature flags and
// frame/lifetime ownership remain with the API adapter that observed them.
constexpr Pair Project(Origin32 depthOrigin,Origin32 motionOrigin,
                       Extent32 render,Extent32 output,bool lowResolutionMotion)
{
    return {ForResolution(depthOrigin,render,output,true),
            ForResolution(motionOrigin,render,output,lowResolutionMotion)};
}

constexpr bool Fits(Rect32 region,Extent64 allocation)
{
    return region.width&&region.height&&region.x<=allocation.width&&region.y<=allocation.height&&
        region.width<=allocation.width-region.x&&region.height<=allocation.height-region.y;
}

// Existing D3D compose policy: only a complete request replaces the allocation
// extent, bounded on each axis by the FULL allocation. Origins are preserved;
// Fits must still reject an origin plus extent that exceeds that allocation.
// Strict Vulkan bindings do not use this policy.
constexpr Rect32 ClampCompleteRequest(Origin32 origin,Extent32 requested,Extent32 allocation)
{
    const auto extent=requested.width&&requested.height?
        Extent32{(std::min)(requested.width,allocation.width),(std::min)(requested.height,allocation.height)}:
        allocation;
    return {origin.x,origin.y,extent.width,extent.height};
}
}
