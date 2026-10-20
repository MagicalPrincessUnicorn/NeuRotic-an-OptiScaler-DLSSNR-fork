#include <iostream>
#include <limits>
#if __has_include("../../../apps/NeuRoticWindowWorker/guidance/DepthPolicy.h")
#include "../../../apps/NeuRoticWindowWorker/guidance/DepthPolicy.h"
using namespace nrw::depth;
static unsigned checks;
static void Check(bool value,const char* name){++checks;if(!value)throw name;}
int main(){try{
    Check(PublicationFresh(1000,1249,1000),"recent retained result may publish");
    Check(!PublicationFresh(1000,1251,1000),"expired same-epoch completion refuses");
    Check(!PublicationFresh(1000,999,1000)&&!PublicationFresh(0,1000,1000),"unknown or future capture age refuses");
    Settings settings;Check(settings.provider==Provider::Off&&!settings.preview&&settings.hz==30,"legacy settings keep depth off");
    Check(ValidSettings(settings),"off settings valid");settings.provider=Provider::Dav2;settings.preview=true;
    Check(ValidSettings(settings)&&!NeedsNrModel(settings),"depth preview starts without private NR model");
    settings.preview=false;Check(NeedsNrModel(settings),"ordinary depth plus NR retains private model requirement");
    settings.hz=31;Check(!ValidSettings(settings),"unsupported cadence refuses");settings.hz=60;Check(ValidSettings(settings),"explicit 60 valid");
    FrameKey key{1,2,3,4,5};Receipt receipt{key,Encoding::EstimatedRelativeRaw,true,true};
    Check(Validate(receipt,key,true)==Admission::Accepted,"matching raw result");
    auto wanted=key;wanted.geometryEpoch++;Check(Validate(receipt,wanted,true)==Admission::WrongFrame,"same-size crop rejects stale result");
    wanted=key;wanted.configEpoch++;Check(Validate(receipt,wanted,true)==Admission::WrongFrame,"settings epoch rejects stale result");
    wanted=key;wanted.streamEpoch++;Check(Validate(receipt,wanted,true)==Admission::WrongFrame,"same HWND reconnect rejects stale result");
    Check(Validate(receipt,key,false)==Admission::ConsumerUnsupported,"unsupported estimated consumer refuses");
    receipt.completed=false;Check(Validate(receipt,key,true)==Admission::NotComplete,"submission is not completion");
    auto box=MakeLetterbox(1920,1080,518,294);Check(box.contentWidth==518&&box.contentHeight==291&&box.top==1,"recorded letterbox mapping");
    Check(!NormalizeRelative(1,1,1).valid,"flat range is not a valid guide");
    Check(!NormalizeRelative(-1,0,1).valid,"negative raw relative value refuses");
    Check(!NormalizeRelative(1,-std::numeric_limits<float>::max(),std::numeric_limits<float>::max()).valid,"overflowing float range refuses");
    CompletionEvidence proof{};Check(CanRetire(proof),"never submitted can retire");
    proof.inference=Stage::Submitted;Check(!CanRetire(proof),"pending inference cannot retire");
    proof.inference=Stage::Complete;proof.consumer=Stage::Submitted;Check(!CanRetire(proof),"pending consumer cannot retire");
    proof.consumer=Stage::Complete;Check(CanRetire(proof),"all actual stages retired");
    proof.inference=Stage::Untrackable;Check(!CanRetire(proof),"unknown completion quarantines");
    JobQueue queue;Check(queue.Offer(key),"first pending admitted");Check(queue.Begin()==key,"first job active");
    auto next=key;next.captureSequence++;Check(queue.Offer(next),"one next pending");
    auto newest=next;newest.captureSequence++;Check(queue.Offer(newest)&&queue.Dropped()==1,"newest replaces pending only");
    Check(!queue.Begin()&&queue.Active()==key,"active cannot be replaced");
    Check(!queue.Finish(next)&&queue.Active()==key,"wrong completion cannot free active job");
    Check(queue.Finish(key)&&queue.Begin()==newest,"active completes before newest starts");
    auto future=newest;++future.captureSequence;queue.Offer(future);queue.DiscardPending();Check(queue.Active()==newest&&!queue.Pending(),"epoch discard releases only unsubmitted pending metadata");
    queue.Invalidate();Check(queue.Active()==newest&&!queue.Pending(),"invalidation retains active retirement owner");
    Check(queue.Finish(newest),"invalidated active can still retire");
    Check(!queue.Offer(key),"invalidated queue admission closed");
    FreshCounter fresh;Check(fresh.Record(key)&&!fresh.Record(key)&&fresh.Count()==1,"repeat map is not fresh");
    Check(fresh.Record(next)&&!fresh.Record(key)&&fresh.Count()==2,"older map cannot increase fresh count");
    std::cout<<"PASS "<<checks<<" depth contract checks\n";return 0;
}catch(const char* error){std::cerr<<"FAIL "<<error<<'\n';return 1;}}
#else
int main(){std::cerr<<"FAIL Depth Anything settings and owned-job policy are not implemented\n";return 1;}
#endif
