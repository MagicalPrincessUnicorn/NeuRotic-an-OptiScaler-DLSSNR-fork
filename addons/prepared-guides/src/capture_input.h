#pragma once
#include <reshade_api.hpp>
namespace nrpg {
inline bool TypedCaptureShape(reshade::api::resource_desc& image,const reshade::api::resource_view_desc& view)
{
    namespace A=reshade::api;
    if(image.type!=A::resource_type::texture_2d || image.texture.levels!=1 ||
       image.texture.depth_or_layers!=1 || image.texture.samples!=1 ||
       (view.type!=A::resource_view_type::texture_2d && view.type!=A::resource_view_type::texture_2d_array) ||
       view.texture.first_level || view.texture.first_layer ||
       (view.texture.levels!=1 && view.texture.levels!=UINT32_MAX) ||
       (view.texture.layers!=1 && view.texture.layers!=UINT32_MAX))return false;
    if(view.format==A::format::unknown || A::format_is_typeless(view.format))return false;
    if(image.texture.format!=view.format &&
       (!A::format_is_typeless(image.texture.format) ||
        A::format_to_typeless(image.texture.format)!=A::format_to_typeless(view.format)))return false;
    image.texture.format=view.format;
    return true;
}
}
