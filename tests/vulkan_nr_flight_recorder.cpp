#include "../OptiScaler/dlssnr/VulkanNrFlightRecorder.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
using namespace DlssNr::VkFlight;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
int main(){
 Ring<4> ring;
 for(unsigned i=0;i<6;++i)ring.Append(Event{.sequence=i+1,.operation=i});
 auto s=ring.Read();Check(s.events.size()==4&&s.overwritten==2,"bounded ring reports overwritten evidence");
 Check(s.events.front().sequence==3&&s.events.back().sequence==6,"latest ordered evidence survives rollover");
 Recorder<4,4,2> recorder;
 recorder.Write(Kind::Failure,1,0,0,0,0,0,0,true);
 for(unsigned i=0;i<100;++i)recorder.Write(Kind::SubmitEnter,i);
 auto captured=recorder.Read();
 Check(captured.overwritten>0,"loss cannot look like complete causal graph");
 bool failure=false;for(auto e:captured.events)failure|=e.kind==Kind::Failure;
 Check(failure,"render traffic cannot overwrite reserved failure history");
 std::vector<std::thread> writers;
 for(unsigned i=0;i<4;++i)writers.emplace_back([&]{for(unsigned j=0;j<1000;++j)recorder.Write(Kind::Record,j);});
 for(auto& t:writers)t.join();
 captured=recorder.Read();Check(captured.events.size()<=12,"concurrent append remains bounded");
 Check(captured.events.size()+captured.overwritten+captured.dropped==4101,"all observations or losses accounted for");
 Check(Call(CallSite::PrivateCreate,11,[]{return Call(CallSite::FenceWait,12,[]{return 7;});})==7,
       "nested traced calls preserve the original result");
 bool thrown=false;try{Call(CallSite::PrivateRelease,13,[]()->int{throw 42;});}catch(int value){thrown=value==42;}
 Check(thrown,"tracing preserves exception identity");
 auto spans=Current().Read();Check(spans.events.size()==6,"three calls produce three paired spans");
 Check(spans.events[0].operation!=spans.events[1].operation &&
       spans.events[0].operation==spans.events[3].operation &&
       spans.events[1].operation==spans.events[2].operation,
       "nested calls have globally distinct pairing ids");
 Check(spans.events[4].operation==spans.events[5].operation && spans.events[5].result==INT32_MIN,
       "exceptional call records an aborted span");
 std::puts("PASS bounded trace, reserved lifecycle, rollover and concurrent accounting");
}
