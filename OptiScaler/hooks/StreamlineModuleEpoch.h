#pragma once
#include <Windows.h>
#include <winternl.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace Neurotic::Hooks
{
// The successfully bootstrapped Root image is already pinned by PinRuntime.
// Its notification code/context and published epochs live until process exit.
// Provider images are NEVER pinned by this registry.
class StreamlineModuleEpochs
{
  public:
    struct Epoch
    {
        HMODULE image=nullptr;
        std::uint64_t serial=0;
        std::atomic<bool> retired{false};
        Epoch* next=nullptr; // Immutable after head publication.
    };
    class Lease
    {
        HMODULE reference=nullptr;
      public:
        Lease()=default;
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other) noexcept:reference(std::exchange(other.reference,nullptr)){}
        ~Lease(){if(reference)FreeLibrary(reference);}
        explicit operator bool()const noexcept{return reference!=nullptr;}
        static Lease Acquire(const Epoch* epoch,const void* actualCode) noexcept
        {
            Lease lease;
            if(!epoch||!actualCode||epoch->retired.load(std::memory_order_acquire))return lease;
            HMODULE owner=nullptr;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(actualCode),&owner))return lease;
            // A successful reference at a reused address cannot revive an epoch.
            if(owner!=epoch->image||epoch->retired.load(std::memory_order_acquire))
            {FreeLibrary(owner);return lease;}
            lease.reference=owner;return lease;
        }
    };
  private:
    struct ImageNotice
    {ULONG flags;const UNICODE_STRING* fullName;const UNICODE_STRING* baseName;void* base;ULONG size;};
    union Notice {ImageNotice loaded;ImageNotice unloaded;};
    using Notify=void(CALLBACK*)(ULONG,const Notice*,void*);
    using Register=LONG(NTAPI*)(ULONG,Notify,void*,void**);
    std::atomic<Epoch*> head{nullptr};
    std::atomic<std::uint64_t> serial{0};
    // 0 unstarted, 1 starting, 2 ready, 3 unavailable. Never wait on a starter
    // that might itself need the loader; unavailable wrapping preserves upstream.
    std::atomic<unsigned> startup{0};
    void* cookie=nullptr;
    static_assert(std::atomic<Epoch*>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static void CALLBACK Notification(ULONG reason,const Notice* data,void* context) noexcept
    {
        // No allocation, lazy initialization, logging, foreign API, lock or wait.
        // All reachable records/context remain allocated through loader teardown.
        if(reason!=2||!data||!context)return;
        auto* self=static_cast<StreamlineModuleEpochs*>(context);
        for(auto* item=self->head.load(std::memory_order_acquire);item;item=item->next)
            if(item->image==data->unloaded.base)item->retired.store(true,std::memory_order_release);
    }
    bool Start() noexcept
    {
        auto state=startup.load(std::memory_order_acquire);
        if(state==2)return true;
        if(state!=0||!startup.compare_exchange_strong(state,1,std::memory_order_acq_rel))return false;
        const auto ntdll=GetModuleHandleW(L"ntdll.dll");
        const auto reg=ntdll?reinterpret_cast<Register>(GetProcAddress(ntdll,"LdrRegisterDllNotification")):nullptr;
        void* registered=nullptr;
        const bool ready=reg&&reg(0,&Notification,this,&registered)>=0&&registered;
        cookie=registered;
        startup.store(ready?2u:3u,std::memory_order_release);
        return ready;
    }
  private:
    template<class,std::size_t,class> friend class StreamlineOnLoadBindings;
    StreamlineModuleEpochs()=default;
    ~StreamlineModuleEpochs()=default;
    StreamlineModuleEpochs(const StreamlineModuleEpochs&)=delete;
    StreamlineModuleEpochs& operator=(const StreamlineModuleEpochs&)=delete;
    // No Stop/destructor frees published storage. The production owner and this
    // context are intentionally non-destructed process-owned objects.
  public:
    bool Prepare() noexcept {return Start();}
    Epoch* Observe(const void* actualCode) noexcept
    {
        if(!actualCode||!Start())return nullptr;
        HMODULE owner=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(actualCode),&owner))return nullptr;
        auto* prior=head.load(std::memory_order_acquire);
        for(auto* item=prior;item;item=item->next)
            if(item->image==owner&&!item->retired.load(std::memory_order_acquire))
            {FreeLibrary(owner);return item;}
        auto* memory=HeapAlloc(GetProcessHeap(),0,sizeof(Epoch));
        if(!memory){FreeLibrary(owner);return nullptr;}
        auto* candidate=new(memory)Epoch;
        candidate->image=owner;candidate->serial=serial.fetch_add(1,std::memory_order_relaxed)+1;
        for(;;)
        {
            for(auto* item=prior;item;item=item->next)
                if(item->image==owner&&!item->retired.load(std::memory_order_acquire))
                {candidate->~Epoch();HeapFree(GetProcessHeap(),0,candidate);FreeLibrary(owner);return item;}
            candidate->next=prior;
            if(head.compare_exchange_weak(prior,candidate,std::memory_order_release,std::memory_order_acquire))break;
        }
        // The real bootstrap reference spans registration and publication.
        // Its final release can synchronously retire this now-published epoch.
        FreeLibrary(owner);return candidate;
    }
};
}
