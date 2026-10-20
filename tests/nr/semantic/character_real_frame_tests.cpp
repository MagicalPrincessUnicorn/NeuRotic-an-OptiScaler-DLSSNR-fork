#include "nr/semantic/character/CharacterRealFrame.h"
#include <iostream>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
static void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    Epoch epoch{1,2,3,4,5,6,7,8};RealFrameClock clock;
    Check(clock.Advance(epoch,99,1,1'000'000'000),"first application image");
    Check(clock.Advance(epoch,99,2,1'049'000'000),"49ms real frame must not be skipped by a 50ms poll gate");
    Check(clock.intervalNs==49'000'000,"cadence from application images");
    Check(!clock.Advance(epoch,99,2,1'099'000'000),"repeated token cannot renew clock or trigger analysis");
    Check(!clock.Advance(epoch,99,1,1'100'000'000),"old token cannot become a real successor");
    RealFrameClock wrap;
    Check(wrap.Advance(epoch,99,0x1'0000'0000ull,1'000'000'000)&&wrap.Advance(epoch,99,1,1'050'000'000),"32bit frame token wraps safely");
    Snapshot sample;sample.origin=Origin::LiveModel;sample.key={epoch,1,1'000'000'000};sample.count=1;
    sample.items[0].id=1;sample.items[0].score=.9;sample.items[0].body={.1,.1,.3,.8};
    sample.items[0].motionTracked=true;sample.items[0].confirmedNs=sample.key.captureNs;
    BoxHold boxes;Check(boxes.Update(sample,1'067'000'000,80,true,true),"source accepted");
    DisplayContext context{epoch,2,1'110'000'000,true,true,true,0,50'000'000};
    RealFrameDisplay published;published.Publish(boxes,context,80,16,true,true);
    auto original=published.Read(8,6,context.nowNs);Check(bool(original),"20 real FPS latch admitted");
    sample.key.sequence=2;sample.key.captureNs=1'120'000'000;sample.items[0].body.x0=.2;
    Check(boxes.Update(sample,1'130'000'000,80,true,true),"worker candidate arrives during generated outputs");
    for(unsigned i=0;i<6;++i){auto shown=published.Read(8,6,context.nowNs+i*8'333'333);
        Check(shown==original,"all generated outputs reuse immutable publication");
        Check(AdmitHeld(shown->held,shown->display,80,true)==Admission::Ready,"TTL cannot flicker within output repetition");
        Check(shown->held.geometry.items[0].body.x0==.1,"worker reply does not mutate displayed boxes mid-cadence");}
    context.nowNs=1'160'000'000;published.Publish(boxes,context,80,16,true,false);
    Check(published.Read(8,6,context.nowNs)->held.geometry.items[0].body.x0==.2,"next real boundary hands off");
    Check(!published.Read(9,6,context.nowNs)&&!published.Read(8,7,context.nowNs),"resize/disable/scene epoch revokes immediately");
    Check(!published.Read(8,6,1'660'000'001),"stalled source cannot hold indefinitely");
    context.nowNs=1'351'000'001;published.Publish(boxes,context,80,16,true,true);
    Check(!published.Read(8,6,context.nowNs),"detector confirmation still expires");
    published.Clear();Check(!published.Read(8,6,context.nowNs),"clear revokes publication");
    std::cout<<"PASS application cadence, 6x immutable handoff, real token dedup, stalls and epoch invalidation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
