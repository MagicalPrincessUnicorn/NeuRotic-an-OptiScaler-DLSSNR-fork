#include "ui/ThemeTransition.h"
#include "ui/UpdateStatus.h"
#include <iostream>
#include <cmath>
int RunFourthPassTests(){int failures=0;auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)failures++;};
 bool endpoints=true;for(int i=0;i<=100;i++){float x=i/100.f;endpoints&=nh::WaterSurface(x,0,0)>1&&nh::WaterSurface(x,1,1)<0;}check(endpoints,"NH-WATER: complete old/new endpoints at every column");
 check(nh::WaterSurface(0,.5f,0)<nh::WaterSurface(.5f,.5f,0)&&nh::WaterSurface(1,.5f,0)<nh::WaterSurface(.5f,.5f,0),"NH-WATER: both edges swell upward");
 check(nh::WaterSurface(.4f,.5f,0)!=nh::WaterSurface(.4f,.5f,.4f),"NH-WATER: animated waves");
 check(nh::EvaluateRelease("alpha-0.9.6").state==nh::UpdateState::Current,"NH-UPDATE: accepted release current");
 check(nh::EvaluateRelease("v0.10.0").state==nh::UpdateState::Available,"NH-UPDATE: numeric newer release");
 for(auto tag:{"invalid","0.9.6-script","999999999.1.0","0.9","0.9.-1"})check(nh::EvaluateRelease(tag).state==nh::UpdateState::Unavailable,"NH-UPDATE: malformed release never claims current");
 return failures;
}
int RunUpdateOnlineTest(){nh::StartUpdateCheck();auto until=GetTickCount64()+20000;auto status=nh::GetUpdateStatus();while(status.state==nh::UpdateState::Checking&&GetTickCount64()<until){Sleep(20);status=nh::GetUpdateStatus();}nh::StopUpdateCheck();bool ok=status.state==nh::UpdateState::Current||status.state==nh::UpdateState::Available;std::cout<<(ok?"PASS ":"FAIL ")<<"NH-UPDATE-HTTP: "<<status.detail<<" "<<status.tag<<'\n';return ok?0:1;}
