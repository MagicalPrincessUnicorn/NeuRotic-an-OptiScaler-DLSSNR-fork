#pragma once
#include <nr/contracts/Evidence.h>
#include <nr/contracts/SemanticDescriptions.h>
#include <cstdint>
#include <type_traits>

namespace Neurotic::Feed::Prepared
{
namespace C=Contracts;
inline constexpr std::uint32_t Version=1;
enum Flags : std::uint32_t { Reset=1, HasDistrust=2, HudIncluded=4, ZeroJitterPolicy=8 };
// Pointer-free descriptor, shared by native, addon and helper adapters. Handles,
// resource rights and completion belong to the transport owner, never this blob.
struct Raster
{
    std::uint32_t allocationWidth=0,allocationHeight=0,x=0,y=0,width=0,height=0;
    bool operator==(const Raster&)const=default;
};
#pragma pack(push,8)
struct Descriptor
{
    std::uint32_t size=sizeof(Descriptor),version=Version;
    std::uint64_t producer=0,session=0,stream=0,device=0,generation=0;
    std::uint64_t capture=0,previousCapture=0,maskCapture=0;
    Raster color,depth,motion,distrust;
    C::SourceClass colorOrigin=C::SourceClass::HostObserved;
    C::SourceClass depthOrigin=C::SourceClass::HostObserved;
    C::SourceClass motionOrigin=C::SourceClass::Derived;
    C::SourceClass maskOrigin=C::SourceClass::Derived;
    C::GraphicsApi sourceApi=C::GraphicsApi::D3D12;
    C::ColorDomain colorDomain=C::ColorDomain::EncodedDisplay;
    C::DepthKind depthKind=C::DepthKind::Device;
    C::MotionUnits motionUnits=C::MotionUnits::Pixels;
    C::MotionDirection motionDirection=C::MotionDirection::CurrentToPrevious;
    C::RasterOrigin rasterOrigin=C::RasterOrigin::TopLeft;
    // Grid spacing and vector units are independent (FFX: grid8, pixel vectors).
    std::uint32_t motionGridWidth=1,motionGridHeight=1;
    float scaleX=1,scaleY=1;
    std::uint32_t depthReversed=0,flags=ZeroJitterPolicy;
    std::uint32_t reserved[4]{};
};
#pragma pack(pop)
static_assert(std::is_trivially_copyable_v<Descriptor> && std::is_standard_layout_v<Descriptor>);
static_assert(sizeof(Descriptor)==248);
}
