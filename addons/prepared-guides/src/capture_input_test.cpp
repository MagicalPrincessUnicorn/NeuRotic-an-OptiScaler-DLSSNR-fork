#include "capture_input.h"
#include <cstdio>
namespace A=reshade::api;
int main(){
    A::resource_desc image{3840,2160,1,1,A::format::b8g8r8a8_typeless,1,A::memory_heap::default_,A::resource_usage::render_target};
    A::resource_view_desc view{A::format::b8g8r8a8_unorm,0,1,0,1};
    if(!nrpg::TypedCaptureShape(image,view)||image.texture.format!=A::format::b8g8r8a8_unorm){std::puts("FAIL: observed BGRA8 view of compatible typeless capture rejected");return 1;}
    image.texture.format=A::format::r8g8b8a8_typeless;if(nrpg::TypedCaptureShape(image,view))return 2;
    image.texture.format=A::format::b8g8r8a8_typeless;view.format=A::format::unknown;if(nrpg::TypedCaptureShape(image,view))return 3;
    view.format=A::format::b8g8r8a8_typeless;if(nrpg::TypedCaptureShape(image,view))return 4;
    view.format=A::format::b8g8r8a8_unorm;view.texture.first_level=1;if(nrpg::TypedCaptureShape(image,view))return 5;
    view.texture.first_level=0;image.texture.samples=4;if(nrpg::TypedCaptureShape(image,view))return 6;
    image.texture.samples=1;image.texture.format=A::format::r32_float;view.format=A::format::r32_float;if(!nrpg::TypedCaptureShape(image,view))return 7;
    image.texture.format=A::format::r32_uint;if(nrpg::TypedCaptureShape(image,view))return 8;
    std::puts("PASS: typed-view capture admission, incompatible/untyped/subresource/MSAA rejection");
}
