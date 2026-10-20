#include "../contracts/Test002Support.h"
#include "RoutingFixture.h"
#include <nr/orchestration/RoutingSelector.h>
#include <fstream>
#if __has_include(<nr/orchestration/RoutingSerialization.h>)
#include <nr/orchestration/RoutingSerialization.h>
#define NR_ORCH_REPLAY 1
#endif
using namespace NrTest002;using namespace NrRoutingTests;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("routing-replay",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_ORCH_REPLAY
    Fixture f;const auto bytes=O::EncodeInput(f.input,f.store);
    r.Check("T22.serialize-input",bytes.error==C::Error::None&&!bytes.text.empty(),true,"EncodeInput");
    const auto replay=O::Replay(bytes.text);
    r.Check("T22.replay-exact-plan",replay.error==C::Error::None&&Chose(replay.decision,1),true,"Replay");
    const auto again=O::Replay(bytes.text);
    r.Check("T22.replay-bytes",!replay.canonicalOutput.empty()&&replay.canonicalOutput==again.canonicalOutput,true,"Replay");
    std::swap(*f.input.offers.Get(0),*f.input.offers.Get(1));const auto reordered=O::EncodeInput(f.input,f.store);
    r.Check("T22.canonical-input-permutation",bytes.text==reordered.text,true,"EncodeInput");
    r.Check("T22.digest-deterministic",replay.inputDigest==again.inputDigest&&replay.outputDigest==again.outputDigest,true,"Replay");
    const auto save=[](const std::string& path,const std::string& body){std::ofstream file(path,std::ios::binary|std::ios::out);file<<body;return bool(file);};
    const auto prefix=std::string(argv[0])+"."+(argc==2?argv[1]:"manual");
    r.Check("T22.replay-evidence-saved",save(prefix+".input.json",bytes.text)&&save(prefix+".output.json",replay.canonicalOutput),true,"Replay");
    auto left=std::make_unique<Fixture>(),right=std::make_unique<Fixture>();
    left->Publish();std::reverse(right->factsA.begin(),right->factsA.end());right->Publish();
    const auto canonicalLeft=O::EncodeInput(left->input,left->store),canonicalRight=O::EncodeInput(right->input,right->store);
    r.Check("T22.fact-set-canonicalization",canonicalLeft.error==C::Error::None&&canonicalLeft.text==canonicalRight.text,true,"EncodeInput");
    auto changed=f.input;changed.schemaVersion=99;
    r.Check("T22.encode-future-refusal",O::EncodeInput(changed,f.store).error!=C::Error::None,true,"EncodeInput");
    r.Check("T22.malformed-replay-refusal",O::Replay("{}").error!=C::Error::None,true,"Replay");
    const auto missing=f.input.offers.Get(0);f.store.Withdraw(*missing);
    r.Check("T22.missing-metadata-refusal",O::EncodeInput(f.input,f.store).error!=C::Error::None,true,"EncodeInput");
#else
    r.Check("T22.serialize-input",false,true,"EncodeInput");
#endif
    return r.Finish();});
}
