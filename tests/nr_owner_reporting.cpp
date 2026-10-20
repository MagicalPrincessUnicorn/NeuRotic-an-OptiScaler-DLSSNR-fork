#include "dlssnr/NrOwnerReporting.h"
#include <cassert>
#include <cstdio>
using namespace DlssNr;
int main(){
 using S=NrPreflightState::State;
 PreparedGuides::SnapshotV2 p;p.available=p.fresh=true;
 p.status.session=1;p.status.capture=7;p.status.generation=2;p.status.outputValid=1;
 p.status.sourceApi=0x20000;p.status.selectedSource=Connections::Source::BuiltIn;
 Connections::SignalStatusSnapshot signals;signals.usedFresh=true;
 signals.used.capture=7;signals.used.generation=2;signals.used.consumed=1;
 signals.used.color=signals.used.depth=signals.used.motion=2;
 auto r=OwnerReporting::Prepared(p,signals,true);
 assert(r.color==S::Consumed&&r.depth==S::Consumed&&r.motion==S::Consumed);
 assert(r.evaluation==S::Consumed&&r.delivery==S::Consumed);
 assert(!r.sourceChoices); // observed use is independent from preference support
 signals.used.capture=6;
 r=OwnerReporting::Prepared(p,signals,true);
 assert(r.color==S::NotReported&&r.evaluation==S::Consumed);
 p.status.outputValid=0;p.status.modelPreparing=1;
 r=OwnerReporting::Prepared(p,signals,true);
 assert(r.evaluation==S::Pending&&r.color!=S::Consumed);
 p.status.modelPreparing=0;
 assert(OwnerReporting::Prepared(p,signals,true).evaluation==S::NotReported);
 p.status.restartRequired=1;
 assert(OwnerReporting::Prepared(p,signals,true).delivery==S::Fault);
 p.fresh=false;
 assert(OwnerReporting::Prepared(p,signals,true).delivery==S::NotReported);
 assert(OwnerReporting::Prepared(p,signals,false).delivery==S::NotReported);
 puts("PASS owner reporting: same capture, unknown vs pending, stale, inactive, fault, fixed source capability");
}
