#include "nr/semantic/character/CharacterBoxHold.h"
#include <iostream>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
static unsigned checks=0;
static void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
static Snapshot Sample(std::uint64_t sequence,std::uint64_t time,std::size_t count=2){
    Snapshot s;s.origin=Origin::LiveModel;s.key={{1,2,3,4,5,6,7,8},sequence,time};s.count=count;
    for(std::size_t i=0;i<count;++i){s.items[i].id=i+1;s.items[i].score=.9;s.items[i].body={.1+i*.4,.1,.3+i*.4,.9};}
    return s;
}
int main(){try{
    BoxHold hold;HeldSnapshot out;auto initial=Sample(1,1'000'000'000);
    DisplayContext display{initial.key.epoch,20,1'100'000'000,true,false,false};
    Check(hold.Update(initial,1'100'000'000,500),"accept valid two-person sample");
    display.nowNs=1'400'000'000;
    Check(hold.Read(display,500,out),"boxes persist after old 150ms cutoff");
    Check(out.geometry.count==2&&out.sources[0].captureNs==initial.key.captureNs,"hold preserves both people and original capture provenance");
    auto empty=Sample(2,1'410'000'000,0);
    Check(hold.Update(empty,1'430'000'000,500),"accept no-match sample");display.nowNs=1'440'000'000;
    Check(hold.Read(display,500,out)&&out.geometry.count==2,"no-match does not immediately erase held people");
    Check(out.receivedNs[0]==1'100'000'000,"no-match cannot renew a person's lifetime");
    auto one=Sample(3,1'450'000'000,1);Check(hold.Update(one,1'480'000'000,500),"update only one person");
    display.nowNs=1'590'000'000;
    Check(hold.Read(display,500,out)&&out.geometry.count==2,"unobserved second person retains its independent hold");
    display.nowNs=1'610'000'000;
    Check(hold.Read(display,500,out)&&out.geometry.count==1,"only expired person disappears");
    display.nowNs=1'980'000'001;Check(!hold.Read(display,500,out),"hold expires without timer renewal");
    Check(!hold.Update(Sample(4,1'000'000'000),1'600'000'001,500),"response older than 500ms refused");
    Check(!hold.Update(Sample(4,2'000'000'000),1'990'000'000,500),"future capture refused");
    auto next=Sample(5,2'000'000'000);Check(hold.Update(next,2'100'000'000,500),"new fresh sample");display.nowNs=2'200'000'000;
    Check(!hold.Read(display,0,out),"zero hold restores strict 150ms capture lifetime");
    display.fgActive=true;Check(!hold.Read(display,500,out),"FG revokes held boxes");display.fgActive=false;
    ++display.epoch.scene;Check(!hold.Read(display,500,out),"scene change revokes held boxes");--display.epoch.scene;
    display.enabled=false;Check(!hold.Read(display,500,out),"disable revokes held boxes");display.enabled=true;
    hold.Clear();Check(!hold.Read(display,500,out),"explicit invalidation clears all held boxes");
    Check(hold.Update(next,2'100'000'000,500),"restart cache");
    auto changed=Sample(6,2'200'000'000,0);++changed.key.epoch.swapchain;
    Check(hold.Update(changed,2'210'000'000,500),"new swapchain sample");display.epoch=changed.key.epoch;
    Check(!hold.Read(display,500,out),"new swapchain never inherits previous boxes");
    hold.Clear();
    for(unsigned i=0;i<5;++i){auto different=Sample(i+1,3'000'000'000+i*100'000'000,1);different.items[0].id=100+i;different.items[0].body={i*.18,.1,i*.18+.1,.9};
        Check(hold.Update(different,different.key.captureNs+1,500),"accept disjoint changing identity");
        display.epoch=different.key.epoch;display.nowNs=different.key.captureNs+2;
        Check(hold.Read(display,500,out,1)&&out.geometry.count==1&&out.geometry.items[0].id==100+i,"maximum one displays only newest observation");}
    hold.Clear();auto smart=Sample(1,4'000'000'000,1);
    Check(hold.Update(smart,4'000'000'001,80,true),"smart positive sample");
    auto miss=Sample(2,4'050'000'000,0);
    Check(hold.Update(miss,4'050'000'001,80,true),"smart miss sample");
    display.epoch=smart.key.epoch;display.nowNs=4'090'000'001;
    Check(hold.Read(display,80,out,16,true)&&out.geometry.count==1,"miss waits for replacement through bounded 100ms window");
    miss=Sample(3,4'095'000'000,0);hold.Update(miss,4'095'000'001,80,true);
    display.nowNs=4'100'000'002;
    Check(!hold.Read(display,80,out,16,true),"repeated misses do not extend handoff lifetime");
    hold.Clear();hold.Update(smart,4'000'000'001,80,true);hold.Update(Sample(2,4'050'000'000,0),4'050'000'001,80,true);
    auto replacement=Sample(3,4'099'000'000,1);replacement.items[0].id=999;
    hold.Update(replacement,4'099'000'001,80,true);display.nowNs=4'099'000'002;
    Check(hold.Read(display,80,out,16,true)&&out.geometry.count==1&&out.geometry.items[0].id==999,"same-region replacement removes old geometry immediately");
    display.nowNs=4'251'000'000;
    Check(!hold.Read(display,0,out,16,true),"zero hold never extends capture lifetime");
    hold.Clear();hold.Update(smart,4'000'000'001,80,true);display.nowNs=4'090'000'001;
    Check(!hold.Read(display,80,out,16,true),"ordinary positive lifetime is 80ms");
    Check(hold.Read(display,80,out,16,true,true),"pending replacement bridges the 80 to 100ms gap");
    display.nowNs=4'100'000'002;
    Check(!hold.Read(display,80,out,16,true,true),"pending worker cannot renew beyond 100ms");
    hold.Clear();hold.Update(smart,4'000'000'001,500,true);display.nowNs=4'150'000'001;
    Check(hold.Read(display,500,out,16,true,true),"pending inference preserves longer explicit positive holds");
    hold.Update(Sample(2,4'151'000'000,0),4'151'000'001,500,true);display.nowNs=4'151'000'002;
    Check(!hold.Read(display,500,out,16,true,true),"confirmed miss has bounded smart window even with longer hold");
    hold.Clear();auto lowRate=Sample(30,2'000'000'000,1);
    lowRate.items[0].motionTracked=true;lowRate.items[0].confirmedNs=lowRate.key.captureNs;
    Check(hold.Update(lowRate,2'067'000'000,80,true,true),"real image reply after measured 67ms latency");
    display={lowRate.key.epoch,30,2'110'000'000,true,true,true};
    display.realFrameIntervalNs=50'000'000;
    Check(hold.Read(display,80,out,16,true,true),"20FPS real-frame handoff survives readback plus one real interval");
    display.fgActive=false;
    Check(!hold.Read(display,80,out,16,true,true),"ordinary presentation retains strict motion age");
    display.fgActive=true;display.nowNs=2'351'000'000;
    Check(!hold.Read(display,80,out,16,true,true),"real cadence never renews detector confirmation indefinitely");
    std::cout<<"PASS "<<checks<<" persistent multi-person box checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
