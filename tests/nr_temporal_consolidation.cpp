// Differential control against exact committed pre-extraction function bodies,
// with the intentional retained-native Present motion correction checked separately.
// The preparation receipt pins the anchor and generated baseline, not rewritten math.
#include <shaders/dlssnr/DlssNr_Common.h>
#include <dlssnr/VulkanNrFrameContract.h>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "temporal_baseline.inc"
#include "temporal_live_calls.inc"

static unsigned checks=0;
static void Check(bool value,const char* name)
{++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
static bool Same(float a,float b)
{return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);}
static void Pair(DlssNrMotionScale a,DlssNrMotionScale b,const char* name)
{Check(Same(a.x,b.x)&&Same(a.y,b.y),name);}
int main()
{
    const std::array<std::array<unsigned,2>,8> rasters={{{0,0},{0,1080},{1920,0},{1,1},
        {2258,1270},{3841,2161},{3840,2160},{7680,4320}}};
    for(const auto& source:rasters)for(const auto& work:rasters)
    {
        Pair(DlssNrWorkingMotionScale(source[0],source[1],work[0],work[1]),
             Baseline_DlssNrWorkingMotionScale(source[0],source[1],work[0],work[1]),"per-axis legacy ratio bits");
        for(unsigned mode=0;mode<16;++mode)
        {
            DlssNrFrameInfo f;f.MvScaleX=(mode&1)?-1.25f:2258.0f;f.MvScaleY=(mode&1)?-0.0f:-1270.0f;
            f.JitterX=.25f;f.JitterY=-.375f;
            f.RenderSubrectWidth=2258;f.RenderSubrectHeight=1270;
            f.MotionSubrectWidth=(mode&2)?3840:2258;f.MotionSubrectHeight=(mode&2)?2160:1270;
            f.DepthSubrectX=3;f.DepthSubrectY=7;f.MotionSubrectX=5;f.MotionSubrectY=11;
            f.NativeScalarFacts.emplace();f.NativeScalarFacts->mvX=f.MvScaleX;f.NativeScalarFacts->mvY=f.MvScaleY;
            if(mode&4)f.NativeScalarFacts->mvY.reset();
            if(mode&8)f.PreparedSource.emplace();
            for(bool present:{false,true})for(bool pre:{false,true})
            {
                f.PresentNativeGuides=present;
                const bool keep=DlssNrPreserveNativePostMotionScale(f,pre,present,source[0],source[1],work[0],work[1]);
                Check(keep==Baseline_DlssNrPreserveNativePostMotionScale(f,pre,present,source[0],source[1],work[0],work[1]),
                      "observed reduced Post versus full Pre prepared missing facts");
                Pair(DlssNrModelMotionFactor(keep,source[0],source[1],work[0],work[1]),
                     Baseline_DlssNrModelMotionFactor(keep,source[0],source[1],work[0],work[1]),"consumer motion factor bits");
                Pair(DlssNrModelJitter(f,source[0],source[1],work[0],work[1]),
                     Baseline_DlssNrModelJitter(f,source[0],source[1],work[0],work[1]),"Native raw versus Present work jitter bits");
                for(unsigned branch=0;branch<TemporalCallsNew.size();++branch)
                {
                    const auto before=TemporalCallsOld[branch](f,keep,source[0],source[1],work[0],work[1]);
                    const auto after=TemporalCallsNew[branch](f,keep,source[0],source[1],work[0],work[1]);
                    Check(Same(present?f.MvScaleX:before[0],after[0])&&
                          Same(present?f.MvScaleY:before[1],after[1])&&
                          Same(before[2],after[2])&&Same(before[3],after[3]),
                          "nine provider calls retain Present native motion; other scalars match committed source");
                }
            }
            const auto projected=DlssNrProjectPresentGuides(f,work[0],work[1],true);
            const auto old=Baseline_DlssNrProjectPresentGuides(f,work[0],work[1],true);
            Check(Same(projected.MvScaleX,f.MvScaleX)&&Same(projected.MvScaleY,f.MvScaleY)&&
                  Same(projected.JitterX,old.JitterX)&&Same(projected.JitterY,old.JitterY),"native motion retained; Present jitter projection unchanged");
            Check(projected.DepthSubrectX==f.DepthSubrectX&&projected.DepthSubrectY==f.DepthSubrectY&&
                  projected.MotionSubrectX==f.MotionSubrectX&&projected.MotionSubrectY==f.MotionSubrectY&&
                  f.JitterX==.25f&&f.JitterY==-.375f,"origins and original inputs remain unchanged");
            DlssNr::VkNrTemporalMetadata vk;vk.color={3,7,2258,1270};vk.depth={5,11,2258,1270};vk.motion={13,17,3840,2160};
            vk.motionScaleX=f.MvScaleX;vk.motionScaleY=f.MvScaleY;vk.jitterX=f.JitterX;vk.jitterY=f.JitterY;
            vk.featureGeneration=9;vk.frameToken=27;vk.providerFrameKnown=true;vk.providerGeneration=8;vk.viewport=4;
            const auto v=DlssNr::ScaleVkNrTemporal(vk,{source[0],source[1]},{work[0],work[1]});
            const auto previous=DlssNr::Baseline_ScaleVkNrTemporal(vk,{source[0],source[1]},{work[0],work[1]});
            Check(Same(v.motionScaleX,previous.motionScaleX)&&Same(v.motionScaleY,previous.motionScaleY)&&
                  Same(v.jitterX,previous.jitterX)&&Same(v.jitterY,previous.jitterY),"Vulkan joint scaling and invalid NaN bits");
            Check(v.color==vk.color&&v.depth==vk.depth&&v.motion==vk.motion&&v.featureGeneration==9&&
                  v.frameToken==27&&v.providerFrameKnown&&v.providerGeneration==8&&v.viewport==4,
                  "Vulkan guide origins and genuine frame identity unchanged");
        }
    }
    std::printf("TEMPORAL_CONSOLIDATION checks=%u PASS retainedPresentMotion=1 otherScalarsBitwisePreExtraction=1 GPU=0 privateProvider=0\n",checks);
}
