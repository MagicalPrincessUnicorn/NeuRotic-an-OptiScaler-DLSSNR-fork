#include "nr/semantic/character/CharacterRuntime.h"
#include <iostream>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
int main(){try{
    using Choice=Detail::CaptureSourceChoice;
    Detail::CaptureSourcePolicy policy;
    const std::uint64_t start=1'000'000'000;
    const auto check=[](bool value){if(!value)throw std::runtime_error("capture source policy regression");};
    // Optional early attempts have no weight until a valid CPU readback exists.
    check(policy.Select(start,false,0)==Choice::None);
    check(policy.Select(start,true,0)==Choice::Present);
    // Switching is an explicit barrier: discard this candidate and ask for a
    // fresh capture carrying the new epoch instead of relabeling its origin.
    check(policy.Select(start+20'000'000,true,start+10'000'000)==Choice::SwitchToEarly);
    check(policy.Select(start+30'000'000,true,0)==Choice::None);
    check(policy.Select(start+40'000'000,true,start+35'000'000)==Choice::Early);
    check(policy.Select(start+185'000'000,true,0)==Choice::None);
    check(policy.Select(start+185'000'001,true,0)==Choice::SwitchToPresent);
    check(policy.Select(start+190'000'000,true,0)==Choice::Present);
    check(policy.Select(start+200'000'000,false,start+195'000'000)==Choice::SwitchToEarly);
    check(policy.Select(start+210'000'000,false,start+205'000'000)==Choice::Early);
    // Starting directly on early data needs no artificial source transition.
    Detail::CaptureSourcePolicy direct;
    check(direct.Select(start,false,start)==Choice::Early);
    // A second completed NGX feature must also get an epoch barrier even when
    // the source kind stays early. A rejected attempt never enters Select.
    check(direct.Select(start+20'000'000,false,start+15'000'000,7)==Choice::SwitchToEarly);
    check(direct.Select(start+40'000'000,false,start+35'000'000,7)==Choice::Early);
    check(!Detail::EarlyFeatureAllowed(start+150'000'000,start,7,8));
    check(Detail::EarlyFeatureAllowed(start+150'000'001,start,7,8));
    check(Detail::EarlyFeatureAllowed(start+10'000'000,start,7,7));
    check(Detail::EarlyFeatureAllowed(start,0,0,8));
    Detail::CaptureSourcePolicy ambiguous;
    check(ambiguous.Select(start,false,start,7)==Choice::Early);
    const auto quietUntil=start+170'000'000;
    // Seeing a competing viewport revokes early health immediately. A pending
    // old early readback cannot postpone fallback or become usable after quiet.
    check(ambiguous.Select(start+30'000'000,true,start+25'000'000,7,quietUntil)==Choice::SwitchToPresent);
    check(ambiguous.Select(start+50'000'000,true,start+45'000'000,7,quietUntil)==Choice::Present);
    check(ambiguous.Select(quietUntil+1,true,start+100'000'000,7,quietUntil)==Choice::Present);
    check(ambiguous.Select(quietUntil+2,true,quietUntil+1,7,quietUntil)==Choice::SwitchToEarly);
    check(!Detail::EarlyFeatureCompetes(start+160'000'000,start,7,8));
    check(!Detail::EarlyFeatureCompetes(start+160'000'000,start,7,7));
    check(!Detail::EarlyFeatureCompetes(quietUntil+1,start,7,8));
    // A disappears at start while B continues every 20ms. B may not perpetuate
    // ambiguity by itself; the last overlap's deadline expires at 290ms.
    std::uint64_t deadline=0,recoveredAt=0;
    for(std::uint64_t elapsed=20'000'000;elapsed<=400'000'000;elapsed+=20'000'000){
        const auto now=start+elapsed;
        if(Detail::EarlyFeatureCompetes(now,start,7,8))deadline=now+DisplayTtlNs;
        else if(now>=deadline){recoveredAt=elapsed;break;}
    }
    check(recoveredAt==300'000'000);
    // A still observed during suppression remains a real competitor even
    // though no early recording is admitted during the ambiguity interval.
    check(Detail::EarlyFeatureCompetes(start+400'000'000,start+390'000'000,7,8));
    std::cout<<"PASS completed early capture preference, bounded health fallback, source-switch epoch barriers\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
