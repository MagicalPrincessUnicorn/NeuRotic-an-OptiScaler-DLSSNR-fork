#include "depth_binding.h"
#include <cstdio>
#include <string_view>

namespace A = reshade::api;
// API fixture for the pinned ReShade.fxh declaration:
// texture DepthBufferTex : DEPTH; sampler DepthBuffer { Texture = DepthBufferTex; };
// runtime_api.cpp matches texture.name OR texture.unique_name, never sampler names.
struct Runtime
{
    bool loaded=true, bound=true, validResource=true;
    A::resource_desc desc{1920,1080,1,1,A::format::r32_float,1,A::memory_heap::default_,A::resource_usage::shader_resource};
    A::effect_texture_variable find_texture_variable(const char* effect, const char* name) const
    {
        return {loaded && std::string_view(effect)=="NeuRotic_PreparedGuides.fx" &&
            std::string_view(name)=="DepthBufferTex" ? 7ull : 0ull};
    }
    void get_texture_binding(A::effect_texture_variable v,A::resource_view* srv,A::resource_view* srgb) const
    { *srv={v.handle==7 && bound ? 42ull : 0ull}; *srgb={}; }
    Runtime* get_device(){return this;}
    A::resource get_resource_from_view(A::resource_view view) const
    {return {view.handle==42 && validResource ? 99ull : 0ull};}
    A::resource_desc get_resource_desc(A::resource) const{return desc;}
};
int main()
{
    Runtime runtime;
    auto depth=[&](){return nrpg::PreparedDepth(&runtime,"NeuRotic_PreparedGuides.fx").handle;};
    if(depth()!=99){std::puts("FAIL: bound ReShade DEPTH texture rejected despite valid scene resource");return 1;}
    runtime.bound=false;if(depth())return 2;runtime.bound=true;
    runtime.loaded=false;if(depth())return 3;runtime.loaded=true;
    runtime.validResource=false;if(depth())return 4;runtime.validResource=true;
    runtime.desc.texture.width=1;if(depth())return 5;runtime.desc.texture.width=1920;
    runtime.desc.texture.height=1;if(depth())return 6;runtime.desc.texture.height=1080;
    runtime.desc.texture.samples=4;if(depth())return 7;runtime.desc.texture.samples=1;
    runtime.desc.type=A::resource_type::buffer;if(depth())return 8;runtime.desc.type=A::resource_type::texture_2d;
    if(nrpg::PreparedDepth(&runtime,"Other.fx").handle)return 9;
    if(depth()!=99)return 10;
    std::puts("PASS: consuming-effect depth binding, unavailable/unbound/invalid/small/MSAA rejection, recovery");
}
