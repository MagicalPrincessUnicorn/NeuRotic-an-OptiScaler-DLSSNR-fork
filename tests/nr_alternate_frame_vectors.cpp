// Generated fixture expectations from AFNR blueprint; no product-generated oracle.
#include <dlssnr/NrAlternateFrameInputs.h>
#include <iostream>
#include <limits>
#include <cstdlib>
using namespace DlssNr::AlternateFrame;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
int main(){
{ // R001_center_2x2
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.5f;i.qy=0.5f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==true);
CHECK(std::abs(r.pixel.r-12.5f)<1e-5f);
CHECK(std::abs(r.pixel.g-17.5f)<1e-5f);
CHECK(std::abs(r.pixel.b-31.25f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::None);
}
{ // R002_exact_top_left
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.25f;i.qy=0.25f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==true);
CHECK(std::abs(r.pixel.r-11.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-19.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.5f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::None);
}
{ // R003_quarter_offset
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.375f;i.qy=0.375f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==true);
CHECK(std::abs(r.pixel.r-11.75f)<1e-5f);
CHECK(std::abs(r.pixel.g-18.25f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.875f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::None);
}
{ // R004_right_bottom_center
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.75f;i.qy=0.75f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==true);
CHECK(std::abs(r.pixel.r-14.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-16.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-32.0f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::None);
}
{ // R005_positive_outside_left
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.0f;i.qy=0.25f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==false);
CHECK(std::abs(r.pixel.r-10.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-20.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.0f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::AnchorBounds);
}
{ // R006_positive_outside_right
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=1.0f;i.qy=0.75f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==false);
CHECK(std::abs(r.pixel.r-10.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-20.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.0f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::AnchorBounds);
}
{ // R007_zero_weight_invalid_ignored
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},false},{{3.0f,-3.0f,1.5f},false},{{4.0f,-4.0f,2.0f},false}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.25f;i.qy=0.25f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==true);
CHECK(std::abs(r.pixel.r-11.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-19.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.5f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::None);
}
{ // R008_positive_weight_invalid_rejects
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},false}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.5f;i.qy=0.5f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==false);
CHECK(std::abs(r.pixel.r-10.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-20.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.0f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::AnchorInvalid);
}
{ // R009_known_scale_two
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.5f;i.qy=0.5f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=2.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==true);
CHECK(std::abs(r.pixel.r-15.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-15.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-32.5f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::None);
}
{ // R010_known_scale_quarter
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.5f;i.qy=0.5f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=0.25f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==true);
CHECK(std::abs(r.pixel.r-10.625f)<1e-5f);
CHECK(std::abs(r.pixel.g-19.375f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.3125f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::None);
}
{ // R011_negative_result_allowed
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.25f;i.qy=0.25f;
i.base={0.0f,0.25f,0.0f,0.75f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==true);
CHECK(std::abs(r.pixel.r-1.0f)<1e-5f);
CHECK(std::abs(r.pixel.g- -0.75f)<1e-5f);
CHECK(std::abs(r.pixel.b-0.5f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.75f)<1e-5f);
CHECK(r.reason==Reason::None);
}
{ // R012_depth_disagreement
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.51f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.5f;i.qy=0.5f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==false);
CHECK(std::abs(r.pixel.r-10.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-20.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.0f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::DepthMismatch);
}
{ // R013_zero_weight_depth_ignored
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.8f,0.8f,0.8f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.25f;i.qy=0.25f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==true);
CHECK(std::abs(r.pixel.r-11.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-19.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.5f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::None);
}
{ // R014_overflow_keeps_current
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.5f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.75f;i.qy=0.75f;
i.base={65504.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==false);
CHECK(std::abs(r.pixel.r-65504.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-20.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.0f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::OutputRange);
}
{ // R015_depth_inside_threshold
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.501953125f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.25f;i.qy=0.25f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==true);
CHECK(std::abs(r.pixel.r-11.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-19.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.5f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::None);
}
{ // R016_depth_outside_threshold
std::array<ResidualSample,4> residual{{{{1.0f,-1.0f,0.5f},true},{{2.0f,-2.0f,1.0f},true},{{3.0f,-3.0f,1.5f},true},{{4.0f,-4.0f,2.0f},true}}};
std::array<float,4> depths{0.502197265625f,0.5f,0.5f,0.5f};
ReferenceInput i;i.residual=residual;i.depths=depths;
i.width=2;i.height=2;i.qx=0.25f;i.qy=0.25f;
i.base={10.0f,20.0f,30.0f,0.25f};i.currentDepth=0.5f;i.ratio=1.0f;
auto r=ReconstructPixel(i);CHECK(r.writeCurrentTarget==false);
CHECK(std::abs(r.pixel.r-10.0f)<1e-5f);
CHECK(std::abs(r.pixel.g-20.0f)<1e-5f);
CHECK(std::abs(r.pixel.b-30.0f)<1e-5f);
CHECK(std::abs(r.pixel.a-0.25f)<1e-5f);
CHECK(r.reason==Reason::DepthMismatch);
}
{ // R017_signed_capture
auto r=CaptureResidual({4.0f,2.0f,0.5f,0.25f},{1.0f,5.0f,0.25f,0.75f});CHECK(r.valid==true);
CHECK(r.rgb[0]==-3.0f);
CHECK(r.rgb[1]==3.0f);
CHECK(r.rgb[2]==-0.25f);
}
{ // R018_capture_overflow
auto r=CaptureResidual({-60000.0f,0.0f,0.0f,0.25f},{60000.0f,0.0f,0.0f,0.75f});CHECK(r.valid==false);
CHECK(r.rgb[0]==0.0f);
CHECK(r.rgb[1]==0.0f);
CHECK(r.rgb[2]==0.0f);
}
{ // R019_zero_valid_edit
auto r=CaptureResidual({1.0f,2.0f,3.0f,0.25f},{1.0f,2.0f,3.0f,0.75f});CHECK(r.valid==true);
CHECK(r.rgb[0]==0.0f);
CHECK(r.rgb[1]==0.0f);
CHECK(r.rgb[2]==0.0f);
}
{ // R020_half_even_down
CHECK(RoundStoredHalf(1.00048828125f)==1.0f);
}
{ // R021_half_even_up
CHECK(RoundStoredHalf(1.00146484375f)==1.001953125f);
}
{ // R022_half_max
CHECK(RoundStoredHalf(65504.0f)==65504.0f);
}
{ // R023_half_negative
CHECK(RoundStoredHalf(-2.5f)==-2.5f);
}
{ // R024_nonfinite_original
std::array<ResidualSample,1> residual{{{{1,1,1},true}}};std::array<float,1> depths{.5f};ReferenceInput i;i.base={1,2,3,.25f};i.width=i.height=1;i.qx=i.qy=.5f;i.currentDepth=.5f;i.depths=depths;i.residual=residual;
i.base={std::numeric_limits<float>::quiet_NaN(),20.0f,30.0f,0.25f};
auto r=ReconstructPixel(i);CHECK(!r.writeCurrentTarget);CHECK(r.reason==Reason::OriginalInputNonfinite);
}
{ // R025_nonfinite_positive_tap
std::array<ResidualSample,1> residual{{{{1,1,1},true}}};std::array<float,1> depths{.5f};ReferenceInput i;i.base={1,2,3,.25f};i.width=i.height=1;i.qx=i.qy=.5f;i.currentDepth=.5f;i.depths=depths;i.residual=residual;
residual[0].rgb={std::numeric_limits<float>::infinity(),0.0f,0.0f};
auto r=ReconstructPixel(i);CHECK(!r.writeCurrentTarget);CHECK(r.reason==Reason::ResidualNonfinite);
}
{ // R026_linear_depth_boundary
ReferenceInput i;i.depthTest=DepthTest::LinearView;i.currentDepth=99.0f;CHECK(AcceptDepth(100.0f,i)==true);
}
{ // R027_linear_depth_reject
ReferenceInput i;i.depthTest=DepthTest::LinearView;i.currentDepth=98.5f;CHECK(AcceptDepth(100.0f,i)==false);
}
{ // R028_jitter_included
MappingInput i;i.motion=i.depth={0,0,10,10,10,10};i.p={0.5f,0.5f};i.delta={0.1f,-0.05f};i.currentJitter={0.01f,-0.02f};i.anchorJitter={-0.01f,0.01f};i.includesJitter=true;auto r=MapCurrentPixel(i);CHECK(r.valid);
CHECK(std::abs(r.q[0]-0.62f)<1e-6f);
CHECK(std::abs(r.q[1]-0.42f)<1e-6f);
}
{ // R029_jitter_not_included
MappingInput i;i.motion=i.depth={0,0,10,10,10,10};i.p={0.5f,0.5f};i.delta={0.1f,-0.05f};i.currentJitter={0.01f,-0.02f};i.anchorJitter={-0.01f,0.01f};i.includesJitter=false;auto r=MapCurrentPixel(i);CHECK(r.valid);
CHECK(std::abs(r.q[0]-0.6f)<1e-6f);
CHECK(std::abs(r.q[1]-0.45f)<1e-6f);
}
{ // R030_known_scale_max
StorageScaleFacts a,b;a.domain=b.domain=1;
a.scale=1.0f;
b.scale=4.0f;
auto r=ResolveStorageScale(a,b);CHECK(r.eligible==true);
CHECK(r.ratio==4.0f);
}
{ // R031_known_scale_exceeds
StorageScaleFacts a,b;a.domain=b.domain=1;
a.scale=1.0f;
b.scale=4.0001f;
auto r=ResolveStorageScale(a,b);CHECK(r.eligible==false);
}
{ // R032_both_scale_absent
StorageScaleFacts a,b;a.domain=b.domain=1;
auto r=ResolveStorageScale(a,b);CHECK(r.eligible==true);
CHECK(r.ratio==1.0f);
}
{ // R033_scale_presence_transition
StorageScaleFacts a,b;a.domain=b.domain=1;
b.scale=1.0f;
auto r=ResolveStorageScale(a,b);CHECK(r.eligible==false);
}
std::cout<<"AFNR packet vectors PASS 33 cases; "<<checks<<" checks\n";
}
