// CPU-only regression against production helpers. No provider, GPU or game.
// Contract under test: a retained motion texture's samples and subrect do not
// change when only model processing resolution changes. The NR reader divides
// MVecScale by that same subrect, so normalized history displacement must stay
// unchanged. This is NOT a private-model or DD2 visual-acceptance test.
#include <optional>
#include <cmath>
#include <cstdio>
#include <array>
#include <shaders/dlssnr/DlssNr_Common.h>

namespace {
unsigned checks=0, failures=0;
void Check(bool ok,const char* label) {
    ++checks;
    if(!ok) {++failures;std::printf("FAIL %s\n",label);}
}
bool Near(double a,double b) {return std::isfinite(a)&&std::abs(a-b)<1e-7;}
}

int main() {
    // Non-square, signed conventions, independent high-resolution motion,
    // nonzero origins and per-pass sizes are intentional controls.
    const std::array<std::array<unsigned,2>,2> guideSizes={{{2560,1440},{3840,2160}}};
    const std::array<std::array<unsigned,2>,4> workSizes={{{3840,2160},{2560,1440},{960,544},{1704,952}}};
    for(const auto& guide:guideSizes)for(bool pixelUnits:{false,true}) {
        DlssNrFrameInfo native{};
        native.NativeScalarFacts.emplace();
        native.RenderSubrectWidth=2560;native.RenderSubrectHeight=1440;
        native.DepthSubrectWidth=2560;native.DepthSubrectHeight=1440;
        native.MotionSubrectWidth=guide[0];native.MotionSubrectHeight=guide[1];
        native.DepthSubrectX=3;native.DepthSubrectY=5;
        native.MotionSubrectX=7;native.MotionSubrectY=11;
        native.NativeFeatureHandle=17;native.NativeFeatureGeneration=23;
        native.MvScaleX=pixelUnits?-1.f:static_cast<float>(guide[0]);
        native.MvScaleY=pixelUnits?.5f:-static_cast<float>(guide[1]);
        native.NativeScalarFacts->mvX=native.MvScaleX;
        native.NativeScalarFacts->mvY=native.MvScaleY;
        native.JitterX=.25f;native.JitterY=-.125f;
        const double sampleX=pixelUnits?4.0:.01;
        const double sampleY=pixelUnits?-2.0:.02;
        const double expectedX=sampleX*native.MvScaleX/guide[0];
        const double expectedY=sampleY*native.MvScaleY/guide[1];
        const auto present=DlssNrProjectPresentGuides(native,3840,2160,true);
        Check(present.Reset&&present.PresentNativeGuides,"Present identity/reset retained");
        Check(present.MotionSubrectWidth==guide[0]&&present.MotionSubrectHeight==guide[1]&&
            present.MotionSubrectX==7&&present.MotionSubrectY==11&&
            present.DepthSubrectX==3&&present.DepthSubrectY==5,"guide extents/origins retained");
        for(const auto& work:workSizes) {
            const auto factor=DlssNrModelMotionFactor(false,3840,2160,work[0],work[1]);
            const auto actual=DlssNrModelTemporal(
                {{present.MvScaleX,present.MvScaleY},{present.JitterX,present.JitterY}},
                present.PresentNativeGuides,factor,3840,2160,work[0],work[1]);
            const double dx=sampleX*actual.values.motionScale.x/guide[0];
            const double dy=sampleY*actual.values.motionScale.y/guide[1];
            std::printf("SAMPLE guide=%ux%u work=%ux%u pixels=%d UV=(%.9g,%.9g) expected=(%.9g,%.9g)\n",
                guide[0],guide[1],work[0],work[1],pixelUnits?1:0,dx,dy,expectedX,expectedY);
            Check(Near(dx,expectedX)&&Near(dy,expectedY),"retained native guide displacement invariant");
            // Jitter submission policy is independent; no jitter change in this repair.
            const auto expectedJitter=DlssNrModelJitter(present,3840,2160,work[0],work[1]);
            Check(actual.values.jitter.x==expectedJitter.x&&actual.values.jitter.y==expectedJitter.y,
                  "existing jitter projection retained");
        }
        // Native Pre and Native Post are outside this Present-only correction.
        for(bool pre:{false,true}) {
            const unsigned cw=pre?2560u:3840u,ch=pre?1440u:2160u;
            const auto preserve=DlssNrPreserveNativePostMotionScale(native,pre,false,cw,ch,2560,1440);
            const auto motion=DlssNrModelMotionFactor(preserve,cw,ch,2560,1440);
            const auto temporal=DlssNrModelTemporal({{native.MvScaleX,native.MvScaleY},
                {native.JitterX,native.JitterY}},false,motion,cw,ch,2560,1440);
            Check(temporal.values.motionScale.x==native.MvScaleX&&
                  temporal.values.motionScale.y==native.MvScaleY&&
                  temporal.values.jitter.x==native.JitterX&&temporal.values.jitter.y==native.JitterY,
                  "Native Pre/Post control unchanged");
        }
    }
    // Prepared/image-only consumers retain their supplied conversion policy.
    const Neurotic::Context::Temporal::Values input{{-1920,1080},{.125f,-.25f}};
    const auto prepared=DlssNrModelTemporal(input,false,{.5f,.75f},3840,2160,1920,1620);
    Check(prepared.values.motionScale.x==-960&&prepared.values.motionScale.y==810&&
          prepared.values.jitter.x==.125f&&prepared.values.jitter.y==-.25f,
          "non-native-Present policy unchanged");
    std::printf("CHECKS=%u FAILURES=%u\n",checks,failures);
    return failures?1:0;
}
