#include "nr/semantic/character/CharacterObservation.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
static int checks = 0;
static void check(bool ok, const char* label) { ++checks; if(!ok) throw std::runtime_error(label); }
int main() {
 try {
 check(ValidRect({.1f,.2f,.4f,.8f}), "valid rect");
 check(!ValidRect({.4f,.2f,.1f,.8f}), "reversed rect");
 check(!ValidRect({0,0,0,1}), "empty rect");
 check(!ValidRect({0,0,std::numeric_limits<float>::quiet_NaN(),1}), "nan rect");
 check(IoU({0,0,1,1},{0,0,1,1}) == 1.0f, "identical iou");
 check(IoU({0,0,.2f,.2f},{.8f,.8f,1,1}) == 0.0f, "disjoint iou");
 auto a=Affine::Letterbox(1920,1080,640,640);
 check(bool(a), "letterbox created");
 auto p=a->Apply({0,0}); check(std::abs(p.y-140)<.001, "letterbox top padding");
 auto inv=a->Inverse(); check(bool(inv), "inverse exists");
 auto q=inv->Apply(a->Apply({1832,1021}));
 check(std::abs(q.x-1832)<.001 && std::abs(q.y-1021)<.001, "affine roundtrip");
 check(!Affine::Letterbox(0,1080,640,640), "zero source");
 check(!Affine{1,1,0,1,1,0}.Inverse(), "singular affine");
 Pose pose{}; for(auto& k:pose) k={{.5f,.5f},1,1};
 pose[11].position={.4f,.2f}; pose[12].position={.6f,.2f};
 pose[23].position={.42f,.6f}; pose[24].position={.58f,.6f};
 auto band=TorsoBand(pose,.1f,.5f); check(bool(band), "torso region");
 check(std::abs(band->y0-.24f)<1e-5f && std::abs(band->y1-.4f)<1e-5f,"band geometry");
 auto old=pose[11]; pose[11].visibility=.1f;
 check(!TorsoBand(pose,.1f,.5f), "low visibility refusal"); pose[11]=old;
 auto reflected=pose; for(auto& k:reflected) k.position.x=1-k.position.x;
 check(bool(TorsoBand(reflected,.1f,.5f)), "mirrored view not orientation inference");
 check(!TorsoBand(pose,.5f,.1f), "invalid band order");
 pose[23].position=pose[11].position; pose[24].position=pose[12].position;
 check(!TorsoBand(pose,.1f,.5f), "collapsed torso refusal");
 Epoch e{1,2,3,4,5,6,7,8}; FrameKey key{e,10,1'000'000'000};
 Snapshot s{}; s.key=key; s.origin=Origin::LiveModel;
 DisplayContext dc{e,12,1'100'000'000,true,false,false};
 check(Admit(s,dc)==Admission::Ready, "fresh live admission");
 dc.enabled=false; check(Admit(s,dc)==Admission::Disabled,"disabled"); dc.enabled=true;
 dc.nowNs=1'150'000'001; check(Admit(s,dc)==Admission::Stale,"ttl expires");
 dc.nowNs=999'999'999; check(Admit(s,dc)==Admission::Future,"future timestamp");
 dc.nowNs=1'100'000'000; dc.frameSequence=9; check(Admit(s,dc)==Admission::Future,"future frame"); dc.frameSequence=12;
 dc.epoch.scene++; check(Admit(s,dc)==Admission::WrongEpoch,"cut invalidates"); dc.epoch=e;
 dc.epoch.descriptor++; check(Admit(s,dc)==Admission::WrongEpoch,"resize invalidates"); dc.epoch=e;
 dc.fgActive=true; check(Admit(s,dc)==Admission::UnqualifiedFg,"fg gate");
 dc.postFgQualified=true; check(Admit(s,dc)==Admission::Ready,"qualified fg geometry gate"); dc.fgActive=false;
 s.origin=Origin::Fixture; check(Admit(s,dc)==Admission::NotLive,"fixture barred from live"); s.origin=Origin::LiveModel;
 s.count=MaxInstances+1; check(Admit(s,dc)==Admission::Malformed,"over capacity"); s.count=0;
 s.count=1; s.items[0].body={0,0,0,0}; check(Admit(s,dc)==Admission::Malformed,"malformed item"); s.count=0;
 check(SanitizeLabel("armor\n\t%s",64)=="armor  %s","label has no format interpretation");
 check(SanitizeLabel(std::string(100,'x'),64).size()==64,"bounded label");
 SampleTracker tr; Detection det{}; det.body={.1f,.1f,.3f,.8f}; det.score=.9f;
 auto r1=tr.Update(key,&det,1); check(bool(r1)&&r1->count==1,"first track");
 auto id=r1->items[0].id; key.sequence++; key.captureNs+=100'000'000; det.body.x0+=.01f; det.body.x1+=.01f;
 auto r2=tr.Update(key,&det,1); check(r2&&r2->items[0].id==id,"id continuity");
 check(!tr.Update(key,&det,1),"duplicate sample rejected");
 key.sequence++; key.captureNs+=50'000'000; auto empty=tr.Update(key,nullptr,0);
 check(empty && empty->count==0,"explicit no match clears output");
 key.sequence++; key.captureNs+=400'000'000; auto r3=tr.Update(key,&det,1);
 check(r3&&r3->items[0].id!=id,"lost track expires");
 key.epoch.scene++; key.sequence=1; auto r4=tr.Update(key,&det,1);
 check(r4&&r4->count==1,"new epoch restarts association");
 check(!tr.Update(key,&det,MaxInstances+1),"bounded tracker input");
 SampleTracker amb; key.sequence++; key.captureNs++;
 Detection pair[2]{det,det}; pair[1].body.x0+=.004f; pair[1].body.x1+=.004f;
 auto ar1=amb.Update(key,pair,2); auto id0=ar1->items[0].id; auto id1=ar1->items[1].id;
 key.sequence++; key.captureNs+=50'000'000; auto ar2=amb.Update(key,&det,1);
 check(ar2 && ar2->items[0].id!=id0 && ar2->items[0].id!=id1,"ambiguous identity is not silently reused");
 GateTimer timer; check(!timer.Due(false,100),"disabled timer");
 check(timer.Due(true,100),"initial due"); check(!timer.Due(true,101),"cadence bounded");
 check(timer.Due(true,100'000'100),"10hz due");
 check(timer.Due(true,50),"clock reset recovers");
 check(!timer.Due(true,32'000'050,33),"33ms interval is bounded");
 check(timer.Due(true,33'000'050,33),"adjustable capture exceeds old 10Hz ceiling");
 std::cout << "PASS " << checks << " C++ core checks\n";
 return 0;
 } catch(const std::exception& ex) {std::cerr << "FAIL after " << checks << ": " << ex.what()<<'\n'; return 1;}
}
