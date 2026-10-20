#include "../tools/nr/lab/alternate_frame/Experiments.h"
#include <iostream>
#include <cstdlib>
using namespace Neurotic::Lab::AlternateFrame;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
struct Host {
    bool fresh=true;unsigned calls=0,resets=0,sources=0;bool begun=false,retired=false;
    bool FreshHistory(){return fresh;}
    bool BeginIndependent(Variant,float,const Authorization&){begun=true;fresh=false;return true;}
    bool Source(unsigned,FrameInstruction i){++sources;calls+=i.model;resets+=i.reset;return true;}
    bool FinishAndRetire(){retired=true;return true;}
};
int main(){
    Authorization a;Host blocked;CHECK(!RunCase(a,Variant::Dense,12,1,blocked));CHECK(!blocked.begun);
    const std::string_view hash="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    a={true,hash,hash,hash,hash};
    for(const auto& c:Cases){Host host;CHECK(RunCase(a,c.variant,12,c.variant==Variant::MatchedBudgetDense?.5f:1.f,host));
        CHECK(host.sources==12&&host.retired);CHECK(host.calls==(c.variant==Variant::Sustained?7u:
            c.variant==Variant::OmitOrdinary||c.variant==Variant::OmitReset?11u:12u));
        CHECK(host.resets==(c.variant==Variant::OmitReset?2u:1u));CHECK(!RunCase(a,c.variant,12,1,host));}
    CHECK(Classify(false,false,false,false,false)==Evidence::NotRun);
    CHECK(Classify(true,true,false,true,true)==Evidence::Incomplete);
    CHECK(Classify(true,false,false,false,true)==Evidence::Incomplete);
    CHECK(Classify(true,false,true,true,true)==Evidence::Failed);
    CHECK(Classify(true,false,false,true,true)==Evidence::Complete);
    std::cout<<"AFNR harness PASS "<<checks<<" checks; no module load, provider or game launch\n";
}
