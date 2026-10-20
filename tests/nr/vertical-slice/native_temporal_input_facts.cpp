#include <dlssnr/NativeTemporalInputFacts.h>
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace DlssNr::NativeTemporalInputFacts;
static unsigned checks=0;
static void Check(bool condition,const char* name){++checks;if(!condition){std::printf("FAIL %s\n",name);std::exit(1);}}
int main(){
    const auto missing=ResolveFlags({},{});
    Check(!missing.Known()&&missing.value==0&&missing.source==FlagSource::Unknown,"absent flags are not known zero");
    const auto creationZero=ResolveFlags(0u,7u);
    Check(creationZero.Known()&&creationZero.value==0&&creationZero.source==FlagSource::Creation,"authentic creation zero wins");
    const auto evaluationZero=ResolveFlags({},0u);
    Check(evaluationZero.Known()&&evaluationZero.value==0&&evaluationZero.source==FlagSource::Evaluation,"authentic evaluation zero stays known");
    Check(ResolveFlags(3u,7u).value==3,"creation convention remains authoritative");
    const auto empty=Resolve({},{},{},{});
    Check(empty.Finite()&&!empty.JitterComplete()&&empty.ObservedMask()==0,"absence is valid unobserved input");
    Check(empty.MvX()==1&&empty.MvY()==1&&empty.JitterX()==0&&empty.JitterY()==0,"existing defaults preserved");
    const auto zero=Resolve(0.f,0.f,0.f,0.f);
    Check(zero.Finite()&&zero.JitterComplete()&&zero.ObservedMask()==15,"all authentic zeros remain observed");
    const auto partial=Resolve(-1.f,{}, {},0.5f);
    Check(partial.Finite()&&partial.ObservedMask()==9&&partial.MvX()==-1&&partial.MvY()==1,"independent motion facts retained");
    Check(!partial.JitterComplete()&&partial.jitterY==0.5f&&partial.JitterY()==0,"fallback does not erase authentic partial jitter");
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    Check(!Resolve({}, {},{},nan).Finite(),"invalid observed partial jitter cannot hide behind fallback");
    Check(!Finite(1,1,nan,0)&&!Finite(1,std::numeric_limits<float>::infinity(),0,0),"resolved frame values are checked too");
    std::printf("PASS %u temporal provenance checks; host-only\n",checks);
}
