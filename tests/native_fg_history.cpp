#include "../OptiScaler/dlssnr/NativeFgHistory.h"
#include <cassert>
#include <limits>
int main(){
    DlssNr::NativeFg::History h;
    auto a=h.Accept(1,1,1,64,64,false,false,16.0);assert(a.accepted&&a.reset&&!a.interpolate);
    a=h.Accept(1,1,2,64,64,false,false,17.0);assert(a.accepted&&!a.reset&&a.interpolate);
    assert(!h.Accept(1,1,2,64,64,false,false,17.0).accepted);
    a=h.Accept(1,1,4,64,64,false,false,17.0);assert(a.reset&&!a.interpolate);
    a=h.Accept(1,1,5,64,64,true,false,17.0);assert(a.reset&&!a.interpolate);
    a=h.Accept(1,1,6,64,64,true,true,17.0);assert(a.reset&&!a.interpolate);
    a=h.Accept(1,2,1,128,64,true,false,17.0);assert(a.reset&&!a.interpolate);
    assert(!h.Accept(1,2,2,128,64,true,false,std::numeric_limits<double>::quiet_NaN()).accepted);
    assert(!h.Accept(0,2,2,128,64,true,false,17.0).accepted);
    assert(!h.Accept(1,2,2,0,64,true,false,17.0).accepted);
    h.Reset();assert(h.Accept(1,2,2,128,64,true,false,17.0).reset);
}
