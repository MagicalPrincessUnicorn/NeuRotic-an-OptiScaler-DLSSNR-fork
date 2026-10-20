#include "../../addons/prepared-guides/src/ExternalFeederAbi.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
extern "C" unsigned __cdecl NeuRotic_EvaluateExternalFeederV1(const nrpg::external::Frame*,nrpg::external::Receipt*);
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
int main(){namespace E=nrpg::external;namespace P=Neurotic::Feed::Prepared;
    E::Frame f;E::Receipt r;
    Check(!NeuRotic_EvaluateExternalFeederV1(nullptr,&r),"null frame rejected");
    r.version=2;Check(!NeuRotic_EvaluateExternalFeederV1(&f,&r),"unsupported receipt version");r={};
    Check(NeuRotic_EvaluateExternalFeederV1(&f,&r)&&r.result==E::Result::Refused,"incomplete frame never delivered");
    f.runtime=1;f.commands=2;f.outputView=3;for(auto& resource:f.resources)resource=4;
    auto& d=f.descriptor;d.producer=E::FeederProducer;d.session=d.stream=d.device=d.generation=d.capture=d.maskCapture=1;
    d.color=d.depth=d.motion=d.distrust={256,64,0,0,256,64};d.flags=P::HasDistrust|P::Reset|P::ZeroJitterPolicy;
    Check(NeuRotic_EvaluateExternalFeederV1(&f,&r)&&r.result==E::Result::Refused&&std::strstr(r.reason,"registration"),"unregistered runtime refused without pointer dereference");
    d.previousCapture=d.capture;Check(NeuRotic_EvaluateExternalFeederV1(&f,&r)&&r.result==E::Result::Refused&&std::strstr(r.reason,"CapturePair"),"replayed capture pair refused");d.previousCapture=0;
    f.producerVersion=99;Check(NeuRotic_EvaluateExternalFeederV1(&f,&r)&&r.result==E::Result::Refused,"unsupported producer refused");f.producerVersion=1;
    d.motionGridWidth=0;Check(NeuRotic_EvaluateExternalFeederV1(&f,&r)&&r.result==E::Result::Refused,"invalid motion semantics refused");d.motionGridWidth=1;
    d.sourceApi=P::C::GraphicsApi::OpenGL;d.rasterOrigin=P::C::RasterOrigin::BottomLeft;
    Check(NeuRotic_EvaluateExternalFeederV1(&f,&r)&&r.result==E::Result::Refused&&std::strstr(r.reason,"registration"),"GL physical origin admitted only to live-runtime gate");
    std::puts("PASS actual addon external export: malformed, unsupported producer, semantics, unregistered runtime, GL origin admission");
}
