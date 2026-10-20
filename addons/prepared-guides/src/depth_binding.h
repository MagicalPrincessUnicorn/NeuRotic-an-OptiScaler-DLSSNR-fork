#pragma once
#include <reshade_api.hpp>

namespace nrpg
{
// Keep lookup scoped to the consuming effect: its binding is the one sampled
// by the prepared-guide shader, including ReShade's selected DEPTH resource.
template<class Runtime>
reshade::api::resource PreparedDepth(Runtime* runtime, const char* effect)
{
    namespace A = reshade::api;
    // ReShade.fxh declares DepthBufferTex : DEPTH. DepthBuffer is its sampler,
    // which find_texture_variable cannot return. The API accepts the declared
    // texture name as well as the compiler's unique name.
    auto variable = runtime->find_texture_variable(effect, "DepthBufferTex");
    A::resource_view srv{}, srgb{};
    if (variable.handle) runtime->get_texture_binding(variable, &srv, &srgb);
    if (!srv.handle) return {};
    auto depth = runtime->get_device()->get_resource_from_view(srv);
    if (!depth.handle) return {};
    auto desc = runtime->get_device()->get_resource_desc(depth);
    if (desc.type != A::resource_type::texture_2d || desc.texture.width < 32 ||
        desc.texture.height < 32 || desc.texture.samples != 1) return {};
    return depth;
}
}
