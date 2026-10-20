#pragma once
#include <dxgiformat.h>

// Shared depth family meaning; transport callers still own state, copy and lifetime.
namespace DlssNr::GuideFormats
{
struct DepthFormat { DXGI_FORMAT carrier, view; unsigned bytes; };
inline DepthFormat DepthType(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT:
        return {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, 4};
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return {DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, 4};
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return {DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, 8};
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM:
        return {DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UNORM, 2};
    case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_FLOAT: return {format, format, 2};
    case DXGI_FORMAT_R32_FLOAT: return {format, format, 4};
    default: return {DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, 0};
    }
}
}
