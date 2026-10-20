#include "dlssnr/NrJitterDisplay.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
 namespace J=DlssNr::JitterDisplay;namespace R=DlssNr::SignalReceipts;
 J::ReceiptSnapshot display;R::Reading r;r.sequence=1;r.state=R::State::Consumed;
 r.fact.present=r.fact.valuesKnown=true;r.fact.x=.125f;r.fact.y=-.25f;
 assert(display.Update(r,true,1000)->x==.125f);
 for(unsigned i=1;i<500;++i){r.sequence++;r.fact.x=float(i);assert(display.Update(r,true,1000+i)->x==.125f);}
 assert(display.Update(r,true,1500)->x==499.f);
 r.fact.x=42;r.state=R::State::Pending;assert(display.Update(r,true,1501)->x==499.f);
 r.epoch.route++;assert(display.Update(r,true,1502)->x==42.f);
 assert(!display.Update(r,false,1503));
 assert(display.Update(r,true,1504)->x==42.f);
 r.state=R::State::Blocked;assert(!display.Update(r,true,1505));
 r.state=R::State::Consumed;r.age=2001;assert(!display.Update(r,true,1506));
 r.age=0;r.fact.x=std::numeric_limits<float>::infinity();assert(!display.Update(r,true,1507));
 r.fact.x=3;assert(display.Update(r,true,1508)->x==3.f);
 r.fact.x=4;r.sequence=1;assert(display.Update(r,true,1509)->x==4.f);
 r.fact.x=5;assert(display.Update(r,true,100)->x==5.f);
 std::puts("PASS jitter values hold for 500 ms; route, off, blocked, stale, invalid and reset states update immediately");
}
