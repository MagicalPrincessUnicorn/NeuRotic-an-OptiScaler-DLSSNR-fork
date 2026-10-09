#pragma once
#include "../WorkerContracts.h"
#include <optional>
#include <utility>
namespace nrw {
// One immutable completed pair. COM references alone do not lease ring storage.
// CaptureHost keeps its separate in-flight owners until its own fence retires.
class ComparisonRefresh {
public:
 struct Pair {CapturedFrame source;NrResult enhanced;uint64_t target=0,output=0,processing=0;};
 enum class State {Idle,Pending,Complete,WaitingFrame};
private:
 std::optional<Pair> pair;
 bool pending=false;uint64_t deadline=0,revision=0;unsigned attempts=0;State state=State::Idle;
 static bool SameSource(const FrameStamp& a,const FrameStamp& b){
  return a.session==b.session&&a.sequence==b.sequence&&a.timestampQpc==b.timestampQpc&&
   a.width==b.width&&a.height==b.height&&a.streamEpoch==b.streamEpoch&&a.geometryEpoch==b.geometryEpoch;
 }
public:
 void Clear(){pair.reset();pending=false;attempts=0;deadline=revision=0;state=State::Idle;}
 void DropPair(){pair.reset();}
 void Request(uint64_t requested,uint64_t now){revision=requested;deadline=now+250;attempts=0;pending=true;state=State::Pending;}
 bool Validate(const FrameStamp& current,uint64_t target,uint64_t output,uint64_t processing,bool paused){
  const bool valid=pair&&!paused&&pair->target==target&&pair->output==output&&pair->processing==processing&&SameSource(pair->source.stamp,current);
  if(!valid){pair.reset();if(pending){pending=false;state=State::WaitingFrame;}}
  return valid;
 }
 bool Store(const CapturedFrame& source,const NrResult& enhanced,uint64_t target,uint64_t output,uint64_t processing){
  if(!source.texture||!source.ownership||!enhanced.texture||!enhanced.ownership||!enhanced.completed||
     !SameSource(source.stamp,enhanced.stamp)||source.stamp.configEpoch!=processing||enhanced.stamp.configEpoch!=processing){pair.reset();return false;}
  pair=Pair{source,enhanced,target,output,processing};return true;
 }
 std::optional<CapturedFrame> TakeSource(){if(!pair)return std::nullopt;auto source=pair->source;pair.reset();return source;}
 void AcceptedFresh(){if(pending||state==State::WaitingFrame){pending=false;state=State::Complete;}}
 template<class Present> bool Try(uint64_t now,Present&& present){
  if(!pending||!pair)return false;
  if(now>deadline||attempts>=3){pending=false;state=State::WaitingFrame;return false;}
  ++attempts;const bool accepted=present(*pair);
  if(accepted){pending=false;state=State::Complete;}
  else if(attempts>=3){pending=false;state=State::WaitingFrame;}
  return accepted;
 }
 const char* Status()const{switch(state){case State::Pending:return "pending";case State::Complete:return "complete";case State::WaitingFrame:return "awaiting-frame";default:return "idle";}}
 unsigned Attempts()const{return attempts;}
 uint64_t Revision()const{return revision;}
 bool Retained()const{return pair.has_value();}
};
}
