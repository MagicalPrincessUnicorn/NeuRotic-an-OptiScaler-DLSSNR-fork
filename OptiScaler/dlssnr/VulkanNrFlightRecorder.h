#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace DlssNr::VkFlight {
enum class Kind : uint32_t { Record, Refusal, SubmitEnter, SubmitReturn, Completion,
    ModelEnter, ModelReturn, Reset, Failure, Occurrence, Profile, Resolver, SpanEnter, SpanReturn, Wsi };
enum class CallSite : uint64_t { PrivateCreate=1, PrivateRelease, DlaaCreate, DlaaRelease,
    DeviceIdle, FenceWait, Acquire, Present, DeviceCreate, DeviceDestroy, PresentCompositionWait };
struct Event {
    uint64_t sequence=0, ticks=0, thread=0, operation=0, evaluation=0;
    uint64_t command=0, use=0, queue=0, detail=0;
    Kind kind=Kind::Record;
    int32_t result=0;
};
struct Snapshot { std::vector<Event> events;uint64_t overwritten=0,dropped=0; };
template<size_t Capacity> class Ring {
public:
    void Append(Event event) noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()){++dropped_;return;}
        if(count_==Capacity)++overwritten_;else ++count_;
        events_[next_]=event;next_=(next_+1)%Capacity;
    }
    Snapshot Read() const {
        std::lock_guard lock(mutex_);Snapshot s;
        s.overwritten=overwritten_;s.dropped=dropped_.load();s.events.reserve(count_);
        const auto first=(next_+Capacity-count_)%Capacity;
        for(size_t i=0;i<count_;++i)s.events.push_back(events_[(first+i)%Capacity]);return s;
    }
private:
    mutable std::mutex mutex_;std::array<Event,Capacity> events_{};
    size_t next_=0,count_=0;uint64_t overwritten_=0;std::atomic<uint64_t> dropped_{0};
};
template<size_t PerShard=5632,size_t Critical=4096,size_t Shards=16> class Recorder {
public:
    void Write(Kind kind,uint64_t operation=0,uint64_t evaluation=0,uint64_t command=0,
               uint64_t use=0,uint64_t queue=0,uint64_t detail=0,int32_t result=0,bool critical=false) noexcept {
        const auto thread=std::hash<std::thread::id>{}(std::this_thread::get_id());
        const auto ticks=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        Event e{++sequence_,static_cast<uint64_t>(ticks),thread,operation,evaluation,command,use,queue,detail,kind,result};
        if(critical)critical_.Append(e);else shards_[thread%Shards].Append(e);
    }
    Snapshot Read() const {
        Snapshot all=critical_.Read();
        for(const auto& shard:shards_){auto s=shard.Read();all.overwritten+=s.overwritten;all.dropped+=s.dropped;
            all.events.insert(all.events.end(),s.events.begin(),s.events.end());}
        std::sort(all.events.begin(),all.events.end(),[](const Event& a,const Event& b){return a.sequence<b.sequence;});return all;
    }
private:
    std::array<Ring<PerShard>,Shards> shards_;Ring<Critical> critical_;std::atomic<uint64_t> sequence_{0};
};
static_assert(sizeof(Recorder<>)<=8*1024*1024,"Vulkan flight recorder exceeds its fixed session budget");
inline Recorder<>& Current(){static Recorder<> recorder;return recorder;}
inline std::atomic<uint64_t> nextCall{0};
// The same invocation id pairs entry/return even when driver/provider calls nest.
// An exception is recorded as an aborted call and rethrown without translation.
template<class F> auto Call(CallSite site,uint64_t object,F&& function) -> decltype(function()) {
    const auto id=++nextCall;
    Current().Write(Kind::SpanEnter,id,0,object,0,0,static_cast<uint64_t>(site));
    try {
        auto result=function();
        Current().Write(Kind::SpanReturn,id,0,object,0,0,static_cast<uint64_t>(site),static_cast<int32_t>(result));
        return result;
    }catch(...){
        Current().Write(Kind::SpanReturn,id,0,object,0,0,static_cast<uint64_t>(site),INT32_MIN,true);
        throw;
    }
}
// Explicit diagnostic capture only. No file I/O, allocation or blocking lock in Write.
// Coverage remains partial: absent events never grant queue, frame or release rights.
inline bool Export(const std::filesystem::path& path) noexcept {
    try {
        auto snapshot=Current().Read();std::ofstream out(path,std::ios::trunc);
        out<<"# Vulkan observation trace v1; clock=steady_ns; coverage=partial; overwritten="
           <<snapshot.overwritten<<"; dropped="<<snapshot.dropped<<"\n";
        out<<"sequence,ticks,thread,kind,operation,evaluation,command,use,queue,detail,result\n";
        for(const auto& e:snapshot.events)out<<e.sequence<<','<<e.ticks<<','<<e.thread<<','<<static_cast<uint32_t>(e.kind)
            <<','<<e.operation<<','<<e.evaluation<<','<<e.command<<','<<e.use<<','<<e.queue<<','<<e.detail<<','<<e.result<<'\n';
        out.flush();return bool(out);
    }catch(...){return false;}
}
}
