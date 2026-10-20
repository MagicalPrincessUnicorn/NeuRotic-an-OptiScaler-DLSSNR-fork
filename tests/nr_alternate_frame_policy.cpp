#ifndef NR_AFNR_TEST
#define NR_AFNR_TEST
#endif
#include <dlssnr/NrAlternateFramePolicy.h>
#include "NrConfigFixture.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace DlssNr::AlternateFrame;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
SourceRef Source(unsigned n) {return SourceRef::Fixture(n);}
DecisionInputs Input(unsigned n) {
    DecisionInputs i;i.enabled=true;i.supported=true;i.source=Source(n);if(n>1)i.predecessor=Source(n-1);
    i.timeUs=10000*n;i.clockKnown=true;i.scope=1;i.anchorReady=true;return i;
}
Decision Begin(PolicyState& s,const DecisionInputs& i) {
    s=Reduce(s,{EventKind::Admit,i});return Decide(s,i);
}
void Finish(PolicyState& s,const DecisionInputs& i,Decision d) {
    PolicyEvent e{d.kind==DecisionKind::Carry?EventKind::CarryCommitted:EventKind::CleanFull,i};
    e.association=s.association;e.anchorAvailable=d.capture;s=Reduce(s,e);
}
int main() {
    TestConfig cfg;auto a=NrConfigSnapshot<TestConfig>(cfg);
    CHECK(!a.DlssNrAlternateFrame.value_or_default());
    cfg.DlssNrAlternateFrame=true;auto b=NrConfigSnapshot<TestConfig>(cfg);
    CHECK(!a.SameConfiguration(b));CHECK(a.SameModelConfiguration(b));
    cfg.DlssNrIntensity=0.5f;CHECK(!b.SameModelConfiguration(NrConfigSnapshot<TestConfig>(cfg)));
    PolicyState s;std::string sequence;unsigned captures=0;
    for(unsigned n=1;n<=12;++n){auto i=Input(n);auto d=Begin(s,i);sequence+=d.kind==DecisionKind::Carry?'C':'F';captures+=d.capture;Finish(s,i,d);}
    CHECK(sequence=="FFFCFCFCFCFC");CHECK(captures==5);CHECK(s.fullCalls==7);CHECK(s.omissions==5);
    CHECK(s.lastModelSource==Source(11));CHECK(s.lastSource==Source(12));
    auto duplicate=Input(12);CHECK(Begin(s,duplicate).kind==DecisionKind::NoSourceDecision);
    const auto fulls=s.fullCalls;auto generated=Input(13);generated.eventClass=SourceEventClass::Generated;
    CHECK(Begin(s,generated).kind==DecisionKind::NoSourceDecision);CHECK(s.fullCalls==fulls);
    generated.invalidate=true;Begin(s,generated);CHECK(s.state==StateKind::Recovery);CHECK(s.required==8);
    for(unsigned n=13;n<=20;++n){auto i=Input(n);auto d=Begin(s,i);CHECK(d.kind==DecisionKind::Full);CHECK(d.capture==(n==20));Finish(s,i,d);}
    CHECK(s.state==StateKind::Eligible);
    auto age=Input(21);age.timeUs=250000;CHECK(Begin(s,age).kind==DecisionKind::Carry);
    PolicyState boundary=s;auto older=age;older.timeUs=250001;
    CHECK(Decide(boundary,older).kind==DecisionKind::Full);
    PolicyEvent late{EventKind::ZeroCoverage,age};late.association=s.association-1;late.observation=1;
    CHECK(Reduce(s,late).state==s.state);
    auto gap=Input(22);gap.source.reset();Begin(s,gap);CHECK(s.state==StateKind::Recovery);
    auto off=gap;off.enabled=false;Begin(s,off);CHECK(s.state==StateKind::Disabled);
    auto on=Input(30);on.predecessor.reset();Begin(s,on);CHECK(s.state==StateKind::Bootstrap);
    for(unsigned n=30;n<=32;++n){auto i=Input(n);if(n==30)i.predecessor.reset();auto d=Begin(s,i);Finish(s,i,d);}
    auto noAnchor=Input(33);noAnchor.anchorReady=false;auto d=Begin(s,noAnchor);
    CHECK(d.kind==DecisionKind::Full);CHECK(s.state!=StateKind::Recovery);Finish(s,noAnchor,d);
    auto contradicted=Input(33);contradicted.contradictoryDuplicate=true;Begin(s,contradicted);CHECK(s.state==StateKind::Recovery);
    std::cout<<"AFNR policy PASS "<<checks<<" checks\n";
}
