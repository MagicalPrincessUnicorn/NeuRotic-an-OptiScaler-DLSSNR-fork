#include "../OptiScaler/dlssnr/VulkanPresentColor.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
    Check(SelectVkPresentColorRecipe(VK_FORMAT_B8G8R8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR).supported,"explicit SDR recipe");
    Check(SelectVkPresentColorRecipe(VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_COLOR_SPACE_HDR10_ST2084_EXT).supported,"PQ2020 exact packed format/colorspace");
    Check(!SelectVkPresentColorRecipe(VK_FORMAT_R16G16B16A16_SFLOAT,VK_COLOR_SPACE_HDR10_ST2084_EXT).supported,"same format cannot guess PQ");
    Check(SelectVkPresentColorRecipe(VK_FORMAT_R16G16B16A16_SFLOAT,VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT).supported,"explicit signed scRGB recipe");
    Check(!SelectVkPresentColorRecipe(VK_FORMAT_R16G16B16A16_SFLOAT,VK_COLOR_SPACE_HDR10_HLG_EXT).supported,"unknown HLG refused");
    Check(!SelectVkPresentColorRecipe(VK_FORMAT_B8G8R8A8_UNORM,VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT).supported,"changed colorspace requalifies the recipe");
    Check(!SelectVkPresentColorRecipe(VK_FORMAT_A2B10G10R10_UNORM_PACK32).supported,"one-argument calls remain explicitly SDR only");
    const auto pq=SelectVkPresentColorRecipe(VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_COLOR_SPACE_HDR10_ST2084_EXT);
    const auto linear=SelectVkPresentColorRecipe(VK_FORMAT_R16G16B16A16_SFLOAT,VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
    Check(pq.hdr&&linear.hdr&&pq.signedOriginal&&linear.signedOriginal&&pq.whitePoint==2.5f&&linear.whitePoint==2.5f,
        "HDR uses the committed 200-nit/2.5 scRGB model contract and signed-original policy");
    Check(pq.revision!=linear.revision,"PQ and scRGB conversions have distinct generation identity");
    std::puts("explicit SDR/PQ/scRGB representation pairs and unknown/mismatched refusal verified");
}
