#pragma once
#include <d3d12.h>

namespace DlssNr::Connections::D3D12DepthPlane {
// The connection's existing full-raster contract accepts one mip and one array
// slice. Depth is plane0; a stencil plane, when present, is a different
// subresource. This constant is not a substitute for arbitrary view indexing.
inline constexpr UINT Subresource=0;
inline DXGI_FORMAT View(DXGI_FORMAT format) noexcept {
    switch(format) {
    case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24G8_TYPELESS:return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:case DXGI_FORMAT_R32G8X24_TYPELESS:return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
inline DXGI_FORMAT Raw(DXGI_FORMAT format) noexcept {
    switch(View(format)) {
    case DXGI_FORMAT_R32_FLOAT:return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_R16_UNORM:return DXGI_FORMAT_R16_TYPELESS;
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:return DXGI_FORMAT_R24G8_TYPELESS;
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:return DXGI_FORMAT_R32G8X24_TYPELESS;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
inline bool ValidDsv(const D3D12_RESOURCE_DESC& resource,const D3D12_DEPTH_STENCIL_VIEW_DESC* view) noexcept {
    if(resource.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||resource.MipLevels!=1||
       resource.DepthOrArraySize!=1||resource.SampleDesc.Count!=1||
       !(resource.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))return false;
    const auto format=view&&view->Format!=DXGI_FORMAT_UNKNOWN?view->Format:resource.Format;
    if(format!=DXGI_FORMAT_D16_UNORM&&format!=DXGI_FORMAT_D32_FLOAT&&
       format!=DXGI_FORMAT_D24_UNORM_S8_UINT&&format!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT)return false;
    if(resource.Format!=format&&resource.Format!=Raw(format))return false;
    if(!view)return true;
    if(view->ViewDimension==D3D12_DSV_DIMENSION_TEXTURE2D)return view->Texture2D.MipSlice==0;
    return view->ViewDimension==D3D12_DSV_DIMENSION_TEXTURE2DARRAY&&
        view->Texture2DArray.MipSlice==0&&view->Texture2DArray.FirstArraySlice==0&&view->Texture2DArray.ArraySize==1;
}
inline bool AffectsDepth(UINT subresource) noexcept {
    return subresource==Subresource||subresource==D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
}
inline void ObserveTransition(UINT subresource,D3D12_RESOURCE_BARRIER_FLAGS flags,
    D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after,D3D12_RESOURCE_STATES& state,bool& known) noexcept {
    if(!AffectsDepth(subresource))return;
    const bool matches=!known||state==before;
    state=after;known=flags==D3D12_RESOURCE_BARRIER_FLAG_NONE&&matches;
}
template<class List>
inline void Transition(List* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    if(before==after)return;
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,Subresource,before,after};list->ResourceBarrier(1,&barrier);
}
template<class List>
inline void Copy(List* list,ID3D12Resource* destination,ID3D12Resource* source,D3D12_RESOURCE_STATES before) {
    Transition(list,source,before,D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION to{},from{};to.pResource=destination;from.pResource=source;
    to.Type=from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.SubresourceIndex=from.SubresourceIndex=Subresource;
    // Full depth-plane rectangle is mandatory for a depth/stencil source.
    // No stencil copy, cropping, conversion or shader access to the source.
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    Transition(list,source,D3D12_RESOURCE_STATE_COPY_SOURCE,before);
}
}
