#include <dlssnr/NrAlternateFrameReference.h>
#include <array>
#include <limits>
#include <iostream>
#include <cstdlib>
using namespace DlssNr::AlternateFrame;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
int main() {
    std::array<ResidualSample,4> residual{{{{1,-1,.5f},true},{{2,-2,1},true},{{3,-3,1.5f},true},{{4,-4,2},true}}};
    std::array<float,4> depths{.5f,.5f,.5f,.5f};
    ReferenceInput i;i.base={10,20,30,.25f};i.qx=i.qy=.5f;i.width=i.height=2;
    i.residual=residual;i.depths=depths;i.currentDepth=.5f;
    auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget);CHECK(r.pixel.r==12.5f);CHECK(r.pixel.g==17.5f);CHECK(r.pixel.b==31.25f);CHECK(r.pixel.a==.25f);
    residual[3].valid=false;CHECK(!ReconstructPixel(i).writeCurrentTarget);
    i.qx=i.qy=.25f;CHECK(ReconstructPixel(i).writeCurrentTarget);
    i.qx=0;CHECK(!ReconstructPixel(i).writeCurrentTarget);
    CHECK(!CaptureResidual({-60000,0,0,1},{60000,0,0,1}).valid);
    CHECK(CaptureResidual({1,2,3,1},{1,2,3,0}).valid);
    CHECK(RoundStoredHalf(1.00048828125f)==1.0f);
    CHECK(RoundStoredHalf(1.00146484375f)==1.001953125f);
    CHECK(RoundStoredHalf(-0.0f)==0.0f);
    i.qx=.25f;i.base.r=65504;CHECK(!ReconstructPixel(i).writeCurrentTarget);
    i.base.r=std::numeric_limits<float>::quiet_NaN();CHECK(ReconstructPixel(i).reason==Reason::OriginalInputNonfinite);
    i.base={10,20,30,.25f};depths[0]=.51f;CHECK(ReconstructPixel(i).reason==Reason::DepthMismatch);
    i.depthTest=DepthTest::LinearView;depths[0]=100;i.currentDepth=100.5f;CHECK(ReconstructPixel(i).writeCurrentTarget);
    StorageScaleFacts a{1,2.0f},b{1,8.0f};CHECK(ResolveStorageScale(a,b).ratio==4.0f);
    b.scale=8.01f;CHECK(!ResolveStorageScale(a,b).eligible);
    b.scale.reset();CHECK(ResolveStorageScale(a,b).reason==Reason::ExposureScaleTransition);
    a.scale.reset();CHECK(ResolveStorageScale(a,b).eligible);CHECK(ResolveStorageScale(a,b).assumed);
    b.domain=2;CHECK(!ResolveStorageScale(a,b).eligible);
    std::cout<<"AFNR reference PASS "<<checks<<" checks\n";
}
