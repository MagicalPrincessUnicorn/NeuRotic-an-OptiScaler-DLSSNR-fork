#include <shaders/dlssnr/DlssNr_Common.h>
#include <dlssnr/PresentColor.h>
#include <dlssnr/VulkanPresentColor.h>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
using ColorStamp=std::array<std::uint32_t,6>;
static ColorStamp Stamp(const DlssNrConstants& c)
{return {c.Passthrough,std::bit_cast<std::uint32_t>(c.WhitePoint),c.SignedPresent,c.UseGameExposure,
         std::bit_cast<std::uint32_t>(c.ExposurePreMul),c.ReversibleMode};}
struct ColorSetting {unsigned value;unsigned value_or_default()const{return value;}};
struct ColorConfig {ColorSetting DlssNrReversibleMode,DlssNrSecondLayerReversibleMode;};
using DlssNr::VkNrRoute;
#include "color_operation_calls.inc"
static unsigned checks=0;
static void Check(bool value,const char* name)
{++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
int main()
{
    DlssNrFrameInfo frame;frame.ColorDomainKnown=false;
    for(bool hdr:{false,true})for(bool signedOriginal:{false,true})for(unsigned live:{0u,1u})
    for(float white:{-0.0f,1.0f,2.5f,64.0f})for(float pre:{0.0f,.25f,8.0f})
    for(unsigned mode:{0u,1u,2u})for(unsigned second:{0u,1u,2u})for(unsigned pass:{0u,1u,2u})
    {
        frame.PreserveSignedOriginal=signedOriginal;
        Check(ColorDx12Old(frame,hdr,white,live,pre,mode,second,pass)==ColorDx12New(frame,hdr,white,live,pre,mode,second,pass),
              "all six D3D12 encode/resolve primary/second/later actual selected constants match committed bits");
        Check(!frame.ColorDomainKnown,"unknown Native source domain does not become a new observation or refusal");
    }
    for(const auto format:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_B8G8R8A8_SRGB,
          VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_A2R10G10B10_UNORM_PACK32,VK_FORMAT_R16G16B16A16_SFLOAT})
    for(const auto space:{VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,VK_COLOR_SPACE_HDR10_ST2084_EXT,VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,VK_COLOR_SPACE_HDR10_HLG_EXT})
    {
        const auto recipe=DlssNr::SelectVkPresentColorRecipe(format,space);const auto operation=recipe.Operation();
        Check(operation.formatSrgb==recipe.srgb&&operation.hdr==recipe.hdr&&operation.signedOriginal==recipe.signedOriginal&&
              operation.modelWhitePoint==recipe.whitePoint,"Vulkan sampling/HDR/signed/model units projected without changing selector");
        for(auto route:{DlssNr::VkNrRoute::Native,DlssNr::VkNrRoute::Present})for(bool linear:{false,true})
        for(float white:{1.0f,2.5f,64.0f})for(unsigned mode:{0u,1u,2u})
            Check(ColorVkOld(route,recipe,linear,white,mode)==ColorVkNew(route,recipe,linear,white,mode),
                  "Vulkan actual constants preserve CPU/held exposure and zero live-texture fields");
    }
    for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT})
    for(const auto space:{DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709,DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020,DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,DXGI_COLOR_SPACE_CUSTOM})
    {
        DlssNr::HdrObservation::Snapshot source;source.registered=true;source.format=format;source.colorSpace=space;
        const auto decision=DlssNr::PresentColor::Select(source,format);const auto operation=decision.Operation(format);const auto model=decision.Model();
        Check(model.hdr==decision.hdr&&model.whitePoint==decision.whitePoint&&model.preserveSignedOriginal==decision.hdr&&model.recipe==decision.recipe,
              "D3D12 actual Present model handoff remains unchanged");
        if(decision.Supported())
        {
            using Conversion=Neurotic::Context::PresentColorConversion;
            const auto oldConversion=format==DXGI_FORMAT_B8G8R8A8_UNORM?Conversion::ChannelOrder:
                decision.profile==DlssNr::PresentColor::Profile::Hdr10Pq2020?Conversion::Pq2020Linear709:Conversion::Copy;
            Check(operation.conversion==oldConversion,"actual D3D12 conversion priority and selected API profile retained");
            Check(operation.workingNitsPerUnit==(decision.hdr?80.0f:0.0f),"explicit HDR normalization units are separate from display metadata");
        }
    }
    std::printf("COLOR_OPERATION checks=%u PASS actualConstantCapture=1 GPU=0 privateProvider=0\n",checks);
}
