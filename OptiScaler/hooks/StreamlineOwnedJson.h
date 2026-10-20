#pragma once
#include <atomic>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace Neurotic::Hooks
{
// Own every issued result independently: later calls, reentry and provider image
// retirement must not overwrite an earlier caller's returned text. The owning
// context must outlive ALL issued pointers. Destruction requires real external
// quiescence; a transient zero active-call count is not that proof.
class StreamlineOwnedJson
{
    struct Text
    {
        const std::string bytes;
        Text* next=nullptr;
        explicit Text(std::string value):bytes(std::move(value)){}
    };
    std::atomic<Text*> head{nullptr};
  public:
    StreamlineOwnedJson()=default;
    StreamlineOwnedJson(const StreamlineOwnedJson&)=delete;
    StreamlineOwnedJson& operator=(const StreamlineOwnedJson&)=delete;
    ~StreamlineOwnedJson()
    {
        // Caller has ended all uses of issued pointers before destroying this
        // concrete owner. Provider unload alone never destroys this storage.
        auto* item=head.load(std::memory_order_relaxed);
        while(item){auto* next=item->next;delete item;item=next;}
    }
    const char* Publish(std::string value)
    {
        // Allocation/construction precedes publication, with no internal lock
        // spanning an allocator or any provider callback. Failure publishes none.
        auto* item=new Text(std::move(value));
        auto* prior=head.load(std::memory_order_acquire);
        do{item->next=prior;}while(!head.compare_exchange_weak(prior,item,
            std::memory_order_release,std::memory_order_acquire));
        return item->bytes.c_str();
    }
#ifdef _WIN32
  private:
    struct ProcessHeapAllocation
    {
        static void* Allocate(std::size_t bytes) noexcept
        {return HeapAlloc(GetProcessHeap(),0,bytes);}
    };
    template<class Allocation>
    static const char* CopyForProcess(std::string_view value) noexcept
    {
        if(value.size()==(std::numeric_limits<std::size_t>::max)())return nullptr;
        auto* copy=static_cast<char*>(Allocation::Allocate(value.size()+1));
        if(!copy)return nullptr;
        if(!value.empty())std::memcpy(copy,value.data(),value.size());
        copy[value.size()]='\0';
        return copy;
    }
  public:
    // Nullable result lets a caller honor its actual existing failure protocol.
    // A null result grants no ownership and must never be called an owned copy.
    template<class Allocation=ProcessHeapAllocation>
    static const char* TryCopyForProcess(std::string_view value) noexcept
    {return CopyForProcess<Allocation>(value);}
    // OnLoad has no notification ending a caller's use of a returned pointer.
    // Each successful publication therefore owns process-heap bytes until
    // process exit, independent of this DLL, its CRT and any provider image.
    // There is deliberately no DLL destructor or provider pin for these bytes.
    // Allocation is a policy only for deterministic failure-path verification;
    // production callers always use the actual Windows process heap above.
    template<class Allocation=ProcessHeapAllocation>
    static bool PublishSuccessfulForProcess(bool result,const char** output,
                                            std::string_view rendered) noexcept
    {
        if(!result||!output||!*output)return result;
        const auto* copy=CopyForProcess<Allocation>(rendered);
        if(!copy){*output=nullptr;return false;}
        *output=copy;
        return result;
    }
    template<class Allocation=ProcessHeapAllocation>
    static bool SnapshotSuccessfulForProcess(bool result,const char** output) noexcept
    {
        if(!result||!output||!*output)return result;
        return PublishSuccessfulForProcess<Allocation>(result,output,std::string_view(*output));
    }
#endif
};
}
