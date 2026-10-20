#ifdef CHARACTER_PACKET_BASELINE
#include "CharacterInspectorCore.hpp"
#else
#include "nr/semantic/character/CharacterObservation.h"
#endif
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
static int checks=0;
static void Check(bool ok,const char* why) { ++checks; if(!ok) throw std::runtime_error(why); }
int main() {
 try {
    const Epoch epoch{1,2,3,4,5,6,7,8};
    const FrameKey frame{epoch,10,1'000'000'000};
    Snapshot snapshot{}; snapshot.origin=Origin::LiveModel; snapshot.key=frame;
    snapshot.count=1; snapshot.items[0].id=1; snapshot.items[0].body={.1,.1,.5,.8};
    snapshot.items[0].score=.8; snapshot.items[0].hasPose=true;
    snapshot.items[0].pose[0].position.x=std::numeric_limits<double>::quiet_NaN();
    DisplayContext display{epoch,12,1'100'000'000,true,false,false};
    // Removing production landmark validation admits poisoned worker geometry.
    Check(Admit(snapshot,display)==Admission::Malformed,"NaN pose rejected before drawing");
    snapshot.items[0].pose[0].position.x=0;
    snapshot.items[0].pose[0].visibility=2;
    Check(Admit(snapshot,display)==Admission::Malformed,"invalid landmark score rejected");
    snapshot.items[0].pose[0].visibility=0;
    Check(Admit(snapshot,display)==Admission::Ready,"finite unobserved landmarks retained");
    auto fields=std::array<std::uint64_t Epoch::*,8>{&Epoch::sessionA,&Epoch::sessionB,
        &Epoch::device,&Epoch::swapchain,&Epoch::descriptor,&Epoch::scene,&Epoch::provider,&Epoch::config};
    for(auto field:fields) { display.epoch=epoch; ++(display.epoch.*field);
        Check(Admit(snapshot,display)==Admission::WrongEpoch,"each generation revokes geometry"); }
    display.epoch=epoch; display.nowNs=frame.captureNs+DisplayTtlNs;
    Check(Admit(snapshot,display)==Admission::Ready,"TTL inclusive boundary");
    ++display.nowNs;
    Check(Admit(snapshot,display,DisplayTtlNs*10)==Admission::Stale,"caller cannot extend hard display TTL");
    display.nowNs=frame.captureNs-1;
    Check(Admit(snapshot,display)==Admission::Future,"clock rollback refuses geometry");
    display.nowNs=frame.captureNs;
    snapshot.count=2; snapshot.items[1]=snapshot.items[0];
    Check(Admit(snapshot,display)==Admission::Malformed,"duplicate IDs rejected");
    snapshot.count=0;
    for(auto origin:{Origin::Fixture,Origin::OfflineImage,Origin::ManualSnapshot}) {
        snapshot.origin=origin;
        Check(Admit(snapshot,display)==Admission::NotLive,"nonlive provenance refused even for empty output");
    }
    Detection detection{}; detection.body={.1,.1,.5,.8}; detection.score=.8;
    SampleTracker tracker; auto first=tracker.Update(frame,&detection,1);
    Check(first && first->origin==Origin::Fixture,"unbound tracker remains a fixture");
    const auto previousId=first->items[0].id;
    Detection crossing[2]{detection,detection}; crossing[1].body.x0+=.001; crossing[1].body.x1+=.001;
    auto next=frame; ++next.sequence; next.captureNs+=100'000'000;
    auto ambiguous=tracker.Update(next,crossing,2);
    Check(ambiguous && ambiguous->items[0].id!=previousId && ambiguous->items[1].id!=previousId,
        "two competing detections cannot claim one previous identity");
    SampleTracker classes; auto before=classes.Update(frame,&detection,1);
    ++detection.classId; auto after=classes.Update(next,&detection,1);
    Check(after && after->items[0].id!=before->items[0].id,"class changes cannot reuse identity");
    Check(!classes.Update(next,&detection,1),"duplicate source rejected");
    ++next.sequence; next.captureNs=frame.captureNs-1;
    Check(!classes.Update(next,&detection,1),"rollback source rejected");
    next.captureNs=frame.captureNs+200'000'000;
    auto empty=classes.Update(next,nullptr,0);
    Check(empty && empty->count==0,"no-match clears display rather than replaying lost tracks");
    ++next.sequence; ++next.captureNs; detection.hasPose=true;
    detection.pose[1].presence=std::numeric_limits<double>::infinity();
    Check(!classes.Update(next,&detection,1),"tracker refuses malformed pose without advancing source");
    detection.pose[1].presence=0;
    Check(bool(classes.Update(next,&detection,1)),"valid retry after malformed input");
    std::cout << "PASS " << checks << " additional observation contract checks\n";
    return 0;
 } catch(const std::exception& e) { std::cerr << "FAIL after " << checks << ": " << e.what() << '\n'; return 1; }
}
