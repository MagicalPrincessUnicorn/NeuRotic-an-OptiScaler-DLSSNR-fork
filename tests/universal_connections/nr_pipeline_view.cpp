#include <dlssnr/NrPipelineView.h>
#include <dlssnr/NrPreflightSignals.h>
#include <cassert>
#include <cstdio>
namespace V=DlssNr::PipelineView;
static int Index(const V::Model& m,V::Stage s){for(unsigned i=0;i<m.count;++i)if(m.nodes[i].stage==s)return int(i);return -1;}
int main(){
 V::Facts activity;activity.renderActive=true;activity.presentSeen=true;
 auto a=V::Build(activity);
 assert(a.nodes[Index(a,V::Stage::Render)].state==V::State::Used);
 assert(a.nodes[Index(a,V::Stage::Present)].state==V::State::Used);
 assert(a.nodes[Index(a,V::Stage::Delivery)].state==V::State::Unknown);
 assert(a.nrState==V::State::Off); // Game activity must not imply NR consumption.
 activity.renderActive=false;activity.presentSeen=false;a=V::Build(activity);
 assert(a.nodes[Index(a,V::Stage::Render)].state==V::State::Unknown);
 assert(a.nodes[Index(a,V::Stage::Present)].state==V::State::Unknown);
 NrPreflightSignals::FreshObservation progress;
 assert(!progress.Update(1,100,1000,true)); // A saved total is not current activity.
 assert(progress.Update(1,101,1016,true));
 assert(!progress.Update(1,101,3100,true));
 assert(!progress.Update(2,500,3116,true)); // A new owner needs new progress.
 assert(progress.Update(2,501,3132,true));
 assert(!progress.Update(2,501,3148,false));
 V::Facts f;f.enabled=true;f.requested=V::Placement::BeforeSr;f.srAvailable=true;
 auto m=V::Build(f);assert(Index(m,V::Stage::Nr)<Index(m,V::Stage::Sr));assert(m.nrState==V::State::Requested);
 f.active=true;f.effective=V::Placement::AfterSr;f.srUsed=true;f.rrIntegrated=true;
 m=V::Build(f);assert(Index(m,V::Stage::Nr)>Index(m,V::Stage::Sr));assert(m.rrIntegrated&&m.nrState==V::State::Used);
 assert(m.nodes[Index(m,V::Stage::Delivery)].state==V::State::Used);
 f.degraded=true;m=V::Build(f);assert(m.nrState==V::State::Fallback&&m.nodes[Index(m,V::Stage::Delivery)].state==V::State::Used);f.degraded=false;
 f.deliveryObserved=true;m=V::Build(f);assert(m.nodes[Index(m,V::Stage::Delivery)].state==V::State::Used);f.deliveryObserved=false;
 f.effective=V::Placement::AfterPresent;f.fgRequested=true;f.fgUsed=true;f.fgOrder=V::FgOrder::AfterPresent;
 m=V::Build(f);assert(Index(m,V::Stage::Present)<Index(m,V::Stage::Nr));assert(Index(m,V::Stage::Nr)<Index(m,V::Stage::Fg));assert(Index(m,V::Stage::Fg)<Index(m,V::Stage::Delivery));
 f.fgOrder=V::FgOrder::Unknown;m=V::Build(f);assert(!m.nodes[Index(m,V::Stage::Fg)].ordered);assert(m.nodes[Index(m,V::Stage::Fg)].state==V::State::Detected);
 f.enabled=false;m=V::Build(f);assert(m.nrState==V::State::Off);assert(m.nodes[Index(m,V::Stage::Inputs)].state!=V::State::Used);
 f.enabled=true;f.active=false;f.blocked=true;m=V::Build(f);assert(m.nrState==V::State::Blocked);
 f.deliveryObserved=true;m=V::Build(f);assert(m.nodes[Index(m,V::Stage::Delivery)].state!=V::State::Used);f.deliveryObserved=false;
 f.blocked=false;f.requested=V::Placement::Unknown;m=V::Build(f);assert(!m.nodes[Index(m,V::Stage::Nr)].ordered);
 V::SignalEvidence e;e.preferred=4;e.selected=3;e.used=2;e.selectedFresh=true;e.usedFresh=true;e.enabled=true;e.outputActive=true;
 auto s=V::Project(e);assert(s.selected==3&&s.used==2&&s.preferred==4&&s.state==V::State::Fallback);
 e.usedFresh=false;s=V::Project(e);assert(!s.used&&s.selected==3&&s.state==V::State::Detected);
 e.enabled=false;s=V::Project(e);assert(!s.used&&s.state==V::State::Off);
 e.preferred=99;s=V::Project(e);assert(s.preferred==99);
 e.enabled=true;e.preferred=5;e.usedFresh=true;s=V::Project(e);assert(s.used==2&&s.selected==3&&s.state==V::State::Fallback);
 std::puts("PASS requested/effective pipeline, RR, Present/FG order, unknown placement and separate selected/consumed signals");
}
