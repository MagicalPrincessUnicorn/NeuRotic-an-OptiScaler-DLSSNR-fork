#include <dlssnr/NrDiagnosticSignals.h>
#include <cassert>
#include <cstdio>
using namespace DlssNr;
int main(){
 using R=SignalReceipts::State;using D=NrPreflightState::State;using S=SignalReceipts::Signal;
 DiagnosticSignals::Readings readings{};
 auto& r=readings[size_t(S::Motion)];
 assert(!DiagnosticSignals::Current(r)&&!DiagnosticSignals::Used(readings,S::Motion));
 const R states[]={R::Pending,R::Off,R::NotApplicable,R::Detected,R::Usable,R::Consumed,R::Stale,R::Unavailable,R::Blocked};
 const D expected[]={D::Pending,D::Off,D::NotApplicable,D::Detected,D::Usable,D::Consumed,D::Stale,D::Unavailable,D::Fault};
 for(unsigned i=0;i<9;++i){r.state=states[i];assert(DiagnosticSignals::State(r)==expected[i]);}
 r.sequence=9;r.fact.present=true;r.fact.admitted=true;r.state=R::Consumed;r.fact.width=2258;r.fact.height=1270;
 r.fact.valuesKnown=true;r.fact.x=2256;r.fact.y=1264;
 assert(DiagnosticSignals::Used(readings,S::Motion));
 assert(DiagnosticSignals::Pair(r).find("2256.000000")!=std::string::npos);
 assert(DiagnosticSignals::Extent(r)=="2258 x 1270");
 r.age=2001;assert(!DiagnosticSignals::Used(readings,S::Motion));
 assert(DiagnosticSignals::Pair(r).find("2256")==std::string::npos);
 r.age=0;r.state=R::Pending;assert(!DiagnosticSignals::Used(readings,S::Motion));
 r.state=R::Off;assert(!DiagnosticSignals::Current(r));
 r.state=R::NotApplicable;assert(!DiagnosticSignals::Current(r));
 puts("PASS: all nine diagnostic states, completion-only use, exact model values, stale/off/unavailable exclusion");
}
