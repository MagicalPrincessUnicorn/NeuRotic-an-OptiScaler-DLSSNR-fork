#include "../addons/prepared-guides/helper/PreparedHelperProtocol.h"
#include "../addons/prepared-guides/helper/PreparedHelperFrame.h"
#include <cassert>
#include <iostream>
#include <limits>
namespace H=Neurotic::Feed::Prepared::Helper;
namespace P=Neurotic::Feed::Prepared;
namespace C=Neurotic::Contracts;
int main(){
    H::Request request{};auto& d=request.guides;
    d.producer=1;d.session=2;d.stream=3;d.device=4;d.generation=5;d.capture=2;d.previousCapture=1;
    d.color={4,2,0,0,4,2};d.depth=d.color;d.motion={2,1,0,0,2,1};d.motionGridWidth=2;d.motionGridHeight=2;
    d.motionUnits=C::MotionUnits::ActiveRasterUV;d.scaleX=2;d.scaleY=3;
    request.timestampQpc=20;request.flags=H::CallerReadbackCompleted;
    request.header.payloadBytes=*H::PayloadBytes(d);assert(H::Validate(request));
    auto bad=request;bad.header.version++;assert(!H::Validate(bad));
    bad=request;bad.header.payloadBytes++;assert(!H::Validate(bad));
    bad=request;bad.flags=0;assert(!H::Validate(bad));
    auto cancel=request;cancel.flags=H::Cancel;cancel.header.payloadBytes=0;assert(H::Validate(cancel));
    cancel.guides.reserved[0]=1;assert(!H::Validate(cancel));
    bad=request;bad.guides.scaleX=NAN;assert(!H::Validate(bad));
    bad=request;bad.guides.maskCapture=1;bad.guides.flags|=P::HasDistrust;bad.guides.distrust=d.color;
    assert(!H::Validate(bad));
    H::Payload payload;payload.color.resize(32);payload.depth.assign(8,.25f);payload.motion={.25f,.5f,.25f,.5f};
    nrw::GuideResult guides;std::string reason;
    nrw::FrameStamp stamp{2,2,20,4,2,false};assert(H::Normalize(request,payload,stamp,guides,reason));
    assert(guides.rawDeviceDepth&&guides.width==4&&guides.height==2&&guides.motionXY[0]==2&&guides.motionXY[1]==3);
    std::vector<float> depth;bool inverted=false;
    assert(nrw::PrepareDepthUpload(guides,depth,inverted));assert(depth[0]==.25f&&!inverted);
    guides.rawDeviceDepthReversed=true;assert(nrw::PrepareDepthUpload(guides,depth,inverted)&&inverted);
    guides.depth[0]=1.1f;assert(!nrw::PrepareDepthUpload(guides,depth,inverted));
    guides.rawDeviceDepth=false;guides.depth={10,20};assert(nrw::PrepareDepthUpload(guides,depth,inverted));
    assert(depth[0]==0&&depth[1]==1&&inverted); // existing inverse-depth default
    payload.motion[0]=std::numeric_limits<float>::infinity();assert(!H::Normalize(request,payload,stamp,guides,reason));
    payload.motion[0]=.25f;payload.depth[0]=-1;assert(!H::Normalize(request,payload,stamp,guides,reason));
    payload.depth[0]=.25f;
    H::Sequence sequence;assert(sequence.Admit(request));assert(!sequence.Admit(request));
    request.guides.capture=3;request.guides.previousCapture=2;request.timestampQpc++;
    assert(sequence.Admit(request));
    request.guides.generation++;request.guides.capture=1;request.guides.previousCapture=0;request.guides.flags|=P::Reset;
    assert(sequence.Admit(request));
    H::Reply reply{};reply.result.identity=P::Ipc::IdentityOf(request.guides);
    assert(!H::CompletedReply(reply,request));reply.result.state=P::Ipc::Outcome::CompletedSuccess;
    reply.result.facts=P::Ipc::Recorded|P::Ipc::Submitted|P::Ipc::GpuCompleted|P::Ipc::OutputValid;
    reply.result.outputCapture=request.guides.capture;reply.result.completionValue=7;
    reply.header.payloadBytes=32;reply.width=4;reply.height=2;assert(H::CompletedReply(reply,request));
    reply.diagnostics=H::DistrustPublicDlaa;assert(!H::CompletedReply(reply,request));reply.diagnostics=0;
    reply.result.identity.device++;assert(!H::CompletedReply(reply,request));
    std::cout<<"prepared helper protocol/normalization checks passed\n";
}
