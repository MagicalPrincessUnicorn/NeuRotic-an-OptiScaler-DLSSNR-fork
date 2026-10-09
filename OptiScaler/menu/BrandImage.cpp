#include "SleekBrand.h"
#include <imgui/imgui_internal.h>
#include <algorithm>
#include <cstring>

namespace Neurotic::Brand
{
namespace Asset
{
#include "brand/BrandPixels.inc"
}
namespace
{
struct Image
{
    ImTextureData texture;
    bool valid=false;
    Image()
    {
        texture.Create(ImTextureFormat_RGBA32,Asset::Width,Asset::Height);
        auto* output=texture.GetPixels();
        const auto* input=Asset::Packed;
        const auto* end=input+sizeof(Asset::Packed);
        size_t remaining=static_cast<size_t>(Asset::Width)*Asset::Height;
        while(input<end && remaining)
        {
            if(end-input<2) return;
            const unsigned tag=input[0]|(static_cast<unsigned>(input[1])<<8);
            input+=2;
            const unsigned count=tag&0x7fff;
            if(!count || count>remaining) return;
            if(tag&0x8000)
            {
                if(end-input<4) return;
                for(unsigned i=0;i<count;++i) { std::memcpy(output,input,4);output+=4; }
                input+=4;
            }
            else
            {
                const size_t bytes=static_cast<size_t>(count)*4;
                if(static_cast<size_t>(end-input)<bytes) return;
                std::memcpy(output,input,bytes);output+=bytes;input+=bytes;
            }
            remaining-=count;
        }
        valid=remaining==0 && input==end;
        if(valid)
        {
            texture.UniqueID=0x4e5201;
            texture.UseColors=true;
            texture.RefCount=1;
            texture.SetStatus(ImTextureStatus_WantCreate);
        }
    }
};
void Release(ImGuiContext* context,ImGuiContextHook* hook)
{
    auto* image=static_cast<Image*>(hook->UserData);
    context->UserTextures.find_erase(&image->texture);
    IM_DELETE(image);
    hook->UserData=nullptr;
}
Image* GetImage()
{
    auto& context=*ImGui::GetCurrentContext();
    const auto owner=ImHashStr("NeuRotic.OfficialHeaderLogo");
    for(const auto& hook:context.Hooks)
        if(hook.Owner==owner && hook.Type==ImGuiContextHookType_Shutdown)
        {
            auto* image=static_cast<Image*>(hook.UserData);
            if(image->valid && image->texture.Status==ImTextureStatus_Destroyed)
                image->texture.SetStatus(ImTextureStatus_WantCreate);
            return image;
        }
    auto* image=IM_NEW(Image)();
    if(image->valid) ImGui::RegisterUserTexture(&image->texture);
    ImGuiContextHook hook;
    hook.Type=ImGuiContextHookType_Shutdown;hook.Owner=owner;
    hook.Callback=Release;hook.UserData=image;
    ImGui::AddContextHook(&context,&hook);
    return image;
}
}
void Draw(ImDrawList* draw,ImVec2 origin,float height,float available,
          bool markOnly,ImU32 wordmarkTint,ImU32 markTint)
{
    if(height<=0 || available<=0) return;
    auto* image=GetImage();
    if(!image->valid) return;
    const float right=static_cast<float>(markOnly?Asset::SplitX:Asset::X1);
    const float scale=(std::min)(height/(Asset::Y1-Asset::Y0),available/(right-Asset::X0));
    const float split=origin.x+(Asset::SplitX-Asset::X0)*scale;
    const float bottom=origin.y+(Asset::Y1-Asset::Y0)*scale;
    const auto texture=image->texture.GetTexRef();
    const float uvTop=static_cast<float>(Asset::Y0)/Asset::Height;
    const float uvBottom=static_cast<float>(Asset::Y1)/Asset::Height;
    const float uvSplit=static_cast<float>(Asset::SplitX)/Asset::Width;
    draw->AddImage(texture,origin,{split,bottom},
        {static_cast<float>(Asset::X0)/Asset::Width,uvTop},{uvSplit,uvBottom},markTint);
    if(!markOnly)
        draw->AddImage(texture,{split,origin.y},{origin.x+(Asset::X1-Asset::X0)*scale,bottom},
            {uvSplit,uvTop},{static_cast<float>(Asset::X1)/Asset::Width,uvBottom},wordmarkTint);
}
}
