#include "nr/semantic/character/CharacterPipeline.h"
#include <iostream>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
static unsigned checks=0;
static void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
int main(){try{
    CaptureSlotState s;
    Check(!s.Complete(),"unsubmitted cannot complete");
    Check(s.Record(),"free slot records");
    Check(s.Cancel(),"unsubmitted cancellation frees");
    Check(s.Record()&&s.Submit(),"record then submit");
    Check(!s.Cancel(),"submitted cancellation cannot free");
    Check(!s.Record(),"pending GPU cannot recycle");
    Check(s.Complete()&&s.Release(),"completion plus owned CPU release recycles");
    FrameKey key{{1,2,3,4,5,6,7,8},10,100};
    OutstandingRequest ledger;
    Check(ledger.Begin(key,960,540),"request binds exact key");
    Check(!ledger.Begin(key,960,540),"one outstanding request only");
    auto wrong=key;wrong.epoch.scene++;
    Check(!ledger.Accept(wrong,960,540),"scene mismatch rejected");
    Check(!ledger.Accept(key,961,540),"wrong transform extent rejected");
    Check(ledger.Accept(key,960,540),"exact current request accepted");
    Check(!ledger.Accept(key,960,540),"duplicate reply rejected");
    Check(ledger.Begin(key,960,540),"new request can begin");ledger.Cancel();
    Check(!ledger.Accept(key,960,540),"disable revokes outstanding request");
    // Same one-second interval at 60 application frames/s, multiplied 1x-6x.
    // More physical outputs must not multiply a 50 ms capture budget.
    for(unsigned ratio=1;ratio<=6;++ratio){
        GateTimer timer;unsigned admitted=0;
        for(unsigned output=0;output<60*ratio;++output)
            if(timer.Due(true,1'000'000'000ull+static_cast<std::uint64_t>(output)*1'000'000'000ull/(60*ratio),50))++admitted;
        Check(admitted>=19&&admitted<=20,"MFG ratio preserves wall-clock capture budget");
    }
    std::cout<<"PASS "<<checks<<" pipeline ownership checks\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
