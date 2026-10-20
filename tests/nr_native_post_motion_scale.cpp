// Public CPU regression: real production motion factors, no provider/model/GPU.
#include <optional>
#include <cstdio>
#include <cmath>
#include "shaders/dlssnr/DlssNr_Common.h"
#include "native_post_motion_calls.inc"
int failures=0, checks=0;
void near(const char* name,float actual,float expected) {
 ++checks;
 if (!std::isfinite(actual)||!std::isfinite(expected)||std::abs(actual-expected)>0.001f) { ++failures; std::printf("FAIL %s actual=%.7g expected=%.7g\n",name,actual,expected); }
}
using Factor = DlssNrMotionScale(*)(const DlssNrFrameInfo&,bool,bool,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
void test(const char* name, DlssNrFrameInfo f, bool pre, bool image, unsigned cw,unsigned ch,
 unsigned primaryW,unsigned primaryH,unsigned passW,unsigned passH,float expectedX,float expectedY) {
 Factor funcs[]={mvToWorkActual,mvToLayer2WorkActual,mvToPassWorkActual};
 for(int pass=0;pass<3;++pass) {
  // Main has the primary raster; later passes have independently rounded rasters.
  const auto s=funcs[pass](f,pre,image,cw,ch,primaryW,primaryH,passW,passH,passW,passH);
  char label[160];std::snprintf(label,sizeof(label),"%s.pass%d.x",name,pass+1);
  near(label,f.MvScaleX*s.x,expectedX);
  std::snprintf(label,sizeof(label),"%s.pass%d.y",name,pass+1);
  near(label,f.MvScaleY*s.y,expectedY);
 }
}
int main() {
 DlssNrFrameInfo native{}; native.NativeScalarFacts.emplace();native.MvScaleX=2560;native.MvScaleY=1440;native.NativeScalarFacts->mvX=2560.0f;native.NativeScalarFacts->mvY=1440.0f;
 native.RenderSubrectWidth=2560;native.RenderSubrectHeight=1440;
 native.MotionSubrectWidth=2560;native.MotionSubrectHeight=1440;
 test("reduced-native-post",native,false,false,3840,2160,2560,1440,2560,1440,2560,1440);
 test("stable-native-full",native,false,false,3840,2160,3840,2160,3840,2160,2560,1440);
 test("stable-native-pre",native,true,false,2560,1440,2560,1440,2560,1440,2560,1440);
 test("pre-sr-reduced-control",native,true,false,3840,2160,1920,1080,1920,1080,1280,720);
 auto projectedNative=native;projectedNative.PresentNativeGuides=true;
 test("present-marker-native-domain-control",projectedNative,false,false,3840,2160,1920,1080,1920,1080,1280,720);
 test("one-axis-native-reduction",native,false,false,3840,2160,3840,1080,3840,1080,2560,1440);
 auto unknown=native;unknown.NativeScalarFacts.reset();
 test("unknown-provenance",unknown,false,false,3840,2160,1920,1080,1920,1080,1280,720);
 auto missing=native;missing.NativeScalarFacts->mvX.reset();missing.NativeScalarFacts->mvY.reset();
 test("missing-motion-observation",missing,false,false,3840,2160,1920,1080,1920,1080,1280,720);
 auto partial=native;partial.NativeScalarFacts->mvY.reset();
 test("partial-motion-observation",partial,false,false,3840,2160,1920,1080,1920,1080,1280,720);
 auto prepared=native;prepared.PreparedSource.emplace();
 test("prepared-preserved",prepared,false,false,3840,2160,1920,1080,1920,1080,1280,720);
 auto present=native;present.PresentNativeGuides=true;
 test("present-preserved",present,false,true,3840,2160,1920,1080,1920,1080,1280,720);
 test("image-domain-preserved",native,false,true,3840,2160,1920,1080,1920,1080,1280,720);
 test("supersample-preserved",native,false,false,3840,2160,7680,4320,7680,4320,5120,2880);
 test("mixed-axis-preserved",native,false,false,3840,2160,1920,4320,1920,4320,1280,2880);
 auto signedScale=native;signedScale.MvScaleX=-1;signedScale.MvScaleY=0.5f;signedScale.NativeScalarFacts->mvX=-1.0f;signedScale.NativeScalarFacts->mvY=0.5f;
 test("signed-normalized",signedScale,false,false,3840,2160,2560,1440,2560,1440,-1,0.5f);
 native.MotionSubrectWidth=3840;native.MotionSubrectHeight=2160;
 test("high-resolution-motion",native,false,false,3840,2160,2560,1440,2560,1440,2560,1440);
 auto rounded=mvToLayer2WorkActual(native,false,false,3841,2161,2560,1440,1704,952,1704,952);
 near("rounded-second-x",native.MvScaleX*rounded.x,2560);near("rounded-second-y",native.MvScaleY*rounded.y,1440);
 auto extra=mvToPassWorkActual(native,false,false,3841,2161,2560,1440,1704,952,1280,712);
 near("rounded-extra-x",native.MvScaleX*extra.x,2560);near("rounded-extra-y",native.MvScaleY*extra.y,1440);
 auto fullSecond=mvToLayer2WorkActual(native,false,false,3840,2160,3840,2160,1920,1080,1920,1080);
 near("full-primary-reduced-second-x",native.MvScaleX*fullSecond.x,1280);
 near("full-primary-reduced-second-y",native.MvScaleY*fullSecond.y,720);
 auto upSecond=mvToLayer2WorkActual(native,false,false,3840,2160,2560,1440,7680,4320,7680,4320);
 near("reduced-primary-supersample-second-x",native.MvScaleX*upSecond.x,5120);
 near("reduced-primary-supersample-second-y",native.MvScaleY*upSecond.y,2880);
 auto zero=mvToWorkActual(native,false,false,0,0,0,0,0,0,0,0);
 near("zero-legacy-x",zero.x,1);near("zero-legacy-y",zero.y,1);
 // Existing jitter policy must remain independent of the motion correction.
 native.JitterX=.25f;native.JitterY=-.125f;
 auto j=DlssNrModelJitter(native,3840,2160,2560,1440);near("native-jitter-x",j.x,.25f);near("native-jitter-y",j.y,-.125f);
 auto projected=DlssNrProjectPresentGuides(native,3840,2160,false);
 j=DlssNrModelJitter(projected,3840,2160,2560,1440);near("present-jitter-x",j.x,.25f);near("present-jitter-y",j.y,-.125f);
 std::printf("CHECKS=%d FAILURES=%d\n",checks,failures);return failures?1:0;
}
