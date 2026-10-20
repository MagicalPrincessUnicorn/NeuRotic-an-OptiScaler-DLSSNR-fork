#pragma once
#include <array>
#include <atomic>
#include <cstdint>
namespace DlssNr::SourceAvailability {
enum class State { NotObserved, Detected, Available, Unsupported };
using Inventory=std::array<std::array<State,4>,3>;
struct Context {
 uint64_t policy=0,nativeGeneration=0,presentGeneration=0,providerGeneration=0;
 unsigned route=0,api=0,placement=0,width=0,height=0,selectedSources=0;
 bool enabled=false,nativeLive=false,presentLive=false,ownerLive=false,nativeGuides=false;
 bool operator==(const Context&)const=default;
};
// UI-only scheduler: one bounded collection window per change, never a timer
// that periodically restarts discovery. Rapid edits coalesce into the last one.
class Refresh {
 Context context_{};bool known_=false,pending_=false,probing_=false;uint64_t requestedAt_=0;
public:
 bool Update(const Context& context,uint64_t now) noexcept {
  if(known_&&context==context_)return false;
  context_=context;known_=true;Request(now);return true;
 }
 void Request(uint64_t now) noexcept {requestedAt_=now;pending_=true;probing_=false;}
 bool StartProbe(uint64_t now)noexcept{if(!pending_||probing_||now<requestedAt_||now-requestedAt_<200)return false;probing_=true;return true;}
 bool Ready(uint64_t now)const noexcept{return pending_&&now>=requestedAt_&&now-requestedAt_>=1000;}
 bool Pending()const noexcept{return pending_;}
 void Complete()noexcept{pending_=false;}
};
// A request is serviced once by an already-running native callback. Only raw
// parameter presence is sampled: no resource query, ownership, GPU work or waits.
class PassiveProbe {
 std::atomic<uint64_t> requested_{0},completed_{0};
public:
 uint64_t Request()noexcept{return requested_.fetch_add(1,std::memory_order_relaxed)+1;}
 uint64_t Pending()const noexcept{const auto request=requested_.load(std::memory_order_relaxed);
  return request && (completed_.load(std::memory_order_acquire)>>4)!=request?request:0;}
 void Complete(uint64_t token,unsigned mask)noexcept{
  if(requested_.load(std::memory_order_relaxed)==token)completed_.store((token<<4)|8|(mask&7),std::memory_order_release);
 }
 unsigned Read(uint64_t token)const noexcept{const auto value=completed_.load(std::memory_order_acquire);
  return (value>>4)==token?unsigned(value&7):0;}
};
inline PassiveProbe& NativeProbe(){static PassiveProbe probe;return probe;}
}
