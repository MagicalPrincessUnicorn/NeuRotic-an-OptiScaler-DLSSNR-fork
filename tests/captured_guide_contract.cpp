#include "dlssnr/CapturedGuideContract.h"
#include <cstdio>
#include <cstdlib>
using DlssNr::CapturedGuideContract;
void check(bool v){if(!v)std::abort();}
int main(){
 CapturedGuideContract c{1,2,3,4,5,6,7,9,8,256,64,true};
 check(c.Matches(1,2,3,4,5,6,256,64));
 auto x=c;x.completed=false;check(!x.Matches(1,2,3,4,5,6,256,64));
 x=c;x.capture=0;check(!x.Matches(1,2,3,4,5,6,256,64));
 x=c;x.previous=x.capture;check(!x.Matches(1,2,3,4,5,6,256,64));
 x=c;x.previous=0;check(x.Matches(1,2,3,4,5,6,256,64));
 check(!c.Matches(42,2,3,4,5,6,256,64));
 check(!c.Matches(1,42,3,4,5,6,256,64));
 check(!c.Matches(1,2,42,4,5,6,256,64));
 check(!c.Matches(1,2,3,42,5,6,256,64));
 check(!c.Matches(1,2,3,4,42,6,256,64));
 check(!c.Matches(1,2,3,4,5,42,256,64));
 check(!c.Matches(1,2,3,4,5,6,128,64));
 x=c;x.stream=0;check(!x.Matches(1,2,3,4,5,6,256,64));
 std::puts("PASS captured guides require completed exact frame/device/acquisition and ordered capture pair");
}
