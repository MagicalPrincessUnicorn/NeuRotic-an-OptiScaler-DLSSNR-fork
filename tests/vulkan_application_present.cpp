#include <cstdio>
#include <cstdlib>
#if __has_include("dlssnr/VulkanNrPresentFrame.h")
#include "dlssnr/VulkanNrPresentFrame.h"
static void Check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    using namespace DlssNr;
    VkNrPresentFrames frames;
    frames.Provider(7);
    frames.Tags(7,42,1,true);
    Check(!frames.Take(7,100),"tags alone do not identify Present");
    frames.Start(7,42,100,true);
    auto frame=frames.Take(7,100);
    Check(frame&&frame->provider==7&&frame->frame==42&&frame->viewport==1,"successful public marker selects exact viewport without Backbuffer tag");
    Check(!frames.Take(7,100),"one marker scope cannot authorize two Presents");
    frames.Start(7,42,101,false);Check(!frames.Take(7,101),"failed marker refuses");
    frames.Start(7,42,101,true);Check(!frames.Take(7,102),"another thread has no marker authority");
    frames.End(7,41,101,true);Check(!frames.Take(7,101),"mismatched end revokes scope");
    frames.Tags(7,43,1,true);frames.Tags(7,43,2,true);frames.Start(7,43,100,true);
    Check(!frames.Take(7,100),"multiple viewports remain ambiguous");
    frames.Tags(7,44,1,true);frames.Tags(7,44,1,false);frames.Start(7,44,100,true);
    Check(!frames.Take(7,100),"failed tagging revokes frame metadata");
    frames.Tags(7,45,1,true);frames.Start(7,45,100,true);frames.Provider(8);
    Check(!frames.Take(7,100)&&!frames.Take(8,100),"provider replacement revokes old scopes");
    frames.Tags(7,46,1,true);frames.Start(7,46,100,true);Check(!frames.Take(8,100),"stale provider calls cannot replace current provider");
    frames.Tags(8,0,1,true);frames.Start(8,0,100,true);frame=frames.Take(8,100);
    Check(frame&&frame->frame==0,"public frame zero is valid");
    for(unsigned f=1;f<300;++f)frames.Tags(8,f,1,true);
    frames.Start(8,1,100,true);Check(!frames.Take(8,100),"evicted frame cannot revive");
    frames.Start(8,299,100,true);Check(frames.Take(8,100).has_value(),"bounded metadata remains usable for fresh frame");
    frames.Start(8,299,100,true);frames.End(8,299,100,false);Check(!frames.Take(8,100),"failed end revokes scope");
    std::puts("Application Present public-frame identity verified");return 0;
}
#else
int main(){std::fputs("FAIL: application Present has no public marker/frame association without Backbuffer tags\n",stderr);return 1;}
#endif
