#include "nr/semantic/character/CharacterFgActivity.h"
#include <iostream>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
static unsigned checks=0;
static void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
int main(){try{
    NativeFgWorkTracker activity;activity.Requested(true);
    Check(!activity.Read(1'000'000'000).active,"retained On request without FG evaluations does not pause inspector");
    auto a=activity.Begin(10,1);Check(activity.Read(1'000'000'000).active,"foreign FG entry pauses immediately");
    Check(activity.Read(9'000'000'000).active,"in-flight FG cannot time out");
    activity.Finish(a,true,1,9'000'000'000);
    Check(activity.Read(8'999'999'999).active,"work published after reader clock remains conservatively active");
    Check(activity.Read(9'500'000'000).active,"inclusive 500ms recency boundary");
    Check(!activity.Read(9'500'000'001).active,"idle retained resources stop pausing after recency window");
    auto first=activity.Begin(10,1),second=activity.Begin(20,2);
    activity.Finish(first,false,1,10'000'000'000);
    Check(activity.Read(10'000'000'000).active,"failed call does not erase overlapping work");
    activity.Released(20,2);Check(activity.Read(20'000'000'000).active,"release cannot pretend an in-flight call has ended");
    activity.Finish(second,true,2,20'000'000'000);
    Check(!activity.Read(20'000'000'001).active,"released instance late return cannot revive work");
    auto old=activity.Begin(10,1);activity.Reset();
    Check(activity.Read(30'000'000'000).active,"provider reset preserves in-flight guard");
    activity.Finish(old,true,1,30'000'000'000);
    Check(!activity.Read(30'000'000'001).active,"old provider return cannot republish activity");
    auto reused=activity.Begin(10,3);activity.Finish(reused,true,4,40'000'000'000);
    Check(!activity.Read(40'000'000'001).active,"handle reuse rejects late mismatched instance");
    activity.Requested(false);auto unknown=activity.Begin(30,0);
    Check(activity.Read(40'000'000'000).active,"untracked native call guarded while in flight");
    activity.Finish(unknown,true,0,40'000'000'000);
    Check(activity.Read(40'100'000'000).active,"untracked returned call retains conservative recent guard");
    std::cout<<"PASS "<<checks<<" native FG activity checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
