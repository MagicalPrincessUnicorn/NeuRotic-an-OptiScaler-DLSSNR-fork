#ifndef NR_AFNR_TEST
#define NR_AFNR_TEST
#endif
#include <dlssnr/NrAlternateFrameInputs.h>
#include <iostream>
#include <cstdlib>
using namespace DlssNr::AlternateFrame;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
int main(){
    MappingInput m;m.p={.5f,.5f};m.delta={.1f,-.05f};m.currentJitter={.01f,-.02f};m.anchorJitter={-.01f,.01f};
    m.motion={5,7,50,40,100,100};m.depth={1,2,25,20,100,100};m.includesJitter=true;
    auto r=MapCurrentPixel(m);CHECK(r.valid);CHECK(std::abs(r.q[0]-.62f)<1e-6f);CHECK(std::abs(r.q[1]-.42f)<1e-6f);
    CHECK(r.motionX==30&&r.motionY==27);CHECK(r.depthX==13&&r.depthY==12);
    m.includesJitter=false;CHECK(std::abs(MapCurrentPixel(m).q[0]-.6f)<1e-6f);
    m.guidesJittered=true;m.currentJitter={.6f,0};CHECK(!MapCurrentPixel(m).valid);
    NativeSourceView v;AlternateFrameConfig c;c.enabled=true;
    CHECK(BuildAlternateFrameInputs(v,nullptr,c).reason==Reason::InputIdentityUnknown);
    v.source=SourceRef::Fixture(1);
    v.identityObserved=true;v.d3d12=true;v.postSr=true;v.width=v.height=v.workWidth=v.workHeight=100;
    v.samples=1;v.rgba16f=true;v.linearDomain=1;v.motion={0,0,50,50,50,50};v.depth=v.motion;
    v.motionConvention=Provenance::SchemaDefined;v.depthEncoding=Provenance::SchemaDefined;v.originalMetadata=true;
    v.scale.domain=1;v.currentAccess=true;v.clockKnown=true;v.timeUs=1;
    CHECK(BuildAlternateFrameInputs(v,nullptr,c).accepted);
    v.rayReconstruction=true;CHECK(BuildAlternateFrameInputs(v,nullptr,c).reason==Reason::RayReconstructionDeferred);v.rayReconstruction=false;
    v.motionConvention=Provenance::Unknown;CHECK(BuildAlternateFrameInputs(v,nullptr,c).reason==Reason::MissingMotionConvention);
    v.motionConvention=Provenance::SchemaDefined;v.motion.x=1;CHECK(BuildAlternateFrameInputs(v,nullptr,c).reason==Reason::GuideBounds);
    v.motion.x=0;v.rgba16f=false;v.rgb11f=true;
    CHECK(BuildAlternateFrameInputs(v,nullptr,c).accepted);
    AnchorMetadata anchor{v};v.predecessor=v.source;v.source=SourceRef::Fixture(2);
    CHECK(BuildAlternateFrameInputs(v,&anchor,c).accepted);
    v.rgb11f=false;v.rgba16f=true;
    CHECK(BuildAlternateFrameInputs(v,&anchor,c).reason==Reason::ConfigRevoked);
    v.rgb11f=true;CHECK(BuildAlternateFrameInputs(v,nullptr,c).reason==Reason::UnsupportedSceneFormat);
    v.rgb11f=v.rgba16f=false;CHECK(BuildAlternateFrameInputs(v,nullptr,c).reason==Reason::UnsupportedSceneFormat);
    std::cout<<"AFNR inputs PASS "<<checks<<" checks\n";
}
