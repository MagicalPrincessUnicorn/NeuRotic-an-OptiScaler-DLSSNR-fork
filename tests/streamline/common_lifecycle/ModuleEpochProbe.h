#pragma once
// Fixture-only feasibility prototype. Product registry implementation must wait
// for the actual public lifetime/ordering evidence and independent review.
#include <windows.h>
#include <winternl.h>
#include <atomic>
#include <cstdint>
#include <exception>
#include <new>
#include <utility>

namespace CommonModuleProbe
{
struct NoticeImage { ULONG flags; const UNICODE_STRING* fullName; const UNICODE_STRING* baseName; void* base; ULONG size; };
union Notice { NoticeImage loaded; NoticeImage unloaded; };
struct Epoch
{
    HMODULE image = nullptr;
    std::uint64_t serial = 0;
    std::atomic<bool> retired{false};
    Epoch* next = nullptr; // immutable before head publication
};
static_assert(std::atomic<Epoch*>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

class ImageLease
{
    HMODULE reference = nullptr;
  public:
    ImageLease()=default;
    ImageLease(const ImageLease&)=delete;
    ImageLease& operator=(const ImageLease&)=delete;
    ImageLease(ImageLease&& other)noexcept:reference(std::exchange(other.reference,nullptr)){}
    ImageLease& operator=(ImageLease&& other)noexcept
    {
        if(this!=&other){Reset();reference=std::exchange(other.reference,nullptr);}return *this;
    }
    ~ImageLease(){Reset();}
    void Reset(){if(auto value=std::exchange(reference,nullptr))FreeLibrary(value);}
    explicit operator bool()const{return reference!=nullptr;}
    struct AcquireObservation
    {
        bool initiallyLive=false,ownerAcquired=false,ownerMatches=false,postRetirementKnown=false,postRetired=false;
    };
    static ImageLease Acquire(const Epoch* epoch,const void* target,
                              void(*interleaveForTest)(void*)=nullptr,void* context=nullptr,
                              AcquireObservation* observation=nullptr)
    {
        ImageLease lease;
        if(observation)*observation={};
        if(!epoch||!target||epoch->retired.load(std::memory_order_acquire))return lease;
        if(observation)observation->initiallyLive=true;
        // Fixture-only deterministic scheduler: it changes real host ownership
        // between the actual precheck and actual loader reference acquisition.
        // It never changes or bypasses the subsequent admission predicates.
        if(interleaveForTest)interleaveForTest(context);
        HMODULE owner=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                             reinterpret_cast<LPCWSTR>(target),&owner))return lease;
        // A reference acquired at a reused address does NOT authenticate the old
        // epoch. Unload notifications permanently revoked that old record.
        const bool matches=owner==epoch->image;
        const bool retired=matches&&epoch->retired.load(std::memory_order_acquire);
        if(observation)*observation={true,true,matches,matches,retired};
        if(!matches||retired)
        {FreeLibrary(owner);return lease;}
        lease.reference=owner;return lease;
    }
};

class Registry
{
    using Notify=void(CALLBACK*)(ULONG,const Notice*,void*);
    using Register=LONG(NTAPI*)(ULONG,Notify,void*,void**);
    using Unregister=LONG(NTAPI*)(void*);
    std::atomic<Epoch*> head{nullptr};
    std::atomic<std::uint64_t> serial{0};
    Unregister unregister=nullptr;
    void* cookie=nullptr;
    static void CALLBACK Notification(ULONG reason,const Notice* data,void* context)
    {
        // Microsoft's notification contract forbids calls into ANY other module.
        // Only lock-free atomics/plain in-module reads; no logs, allocation,
        // loader APIs, Detours, mutex, wait or foreign callback.
        if(reason!=2||!data||!context)return;
        auto* self=static_cast<Registry*>(context);
        for(auto* item=self->head.load(std::memory_order_acquire);item;item=item->next)
            if(item->image==data->unloaded.base)item->retired.store(true,std::memory_order_release);
    }
  public:
    Registry()=default;
    Registry(const Registry&)=delete;
    Registry& operator=(const Registry&)=delete;
    ~Registry()
    {
        // The test host joins all fixture loader/callback threads before Stop.
        // This prototype does not claim arbitrary concurrent registry teardown.
        if(!Stop())std::terminate(); // Never free callback context while still registered.
        auto* item=head.load();
        while(item){auto* next=item->next;delete item;item=next;}
    }
    bool Start(ULONG flags=0)
    {
        if(cookie)return flags==0;
        const auto ntdll=GetModuleHandleW(L"ntdll.dll");
        const auto reg=reinterpret_cast<Register>(GetProcAddress(ntdll,"LdrRegisterDllNotification"));
        unregister=reinterpret_cast<Unregister>(GetProcAddress(ntdll,"LdrUnregisterDllNotification"));
        if(!reg||!unregister)return false;
        void* candidate=nullptr;
        if(reg(flags,&Notification,this,&candidate)!=0||!candidate)return false;
        cookie=candidate;return true;
    }
    bool Stop()
    {
        if(!cookie)return true;
        if(!unregister||unregister(cookie)!=0)return false;
        cookie=nullptr;return true;
    }
    Epoch* Observe(const void* target,void(*beforePublishForTest)(void*)=nullptr,void* context=nullptr)
    {
        if(!cookie||!target)return nullptr;
        HMODULE owner=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                             reinterpret_cast<LPCWSTR>(target),&owner))return nullptr;
        // Bootstrap is protected by a real temporary reference; registration
        // precedes publication, so no load-event backfill/filename guess is used.
        for(auto* item=head.load(std::memory_order_acquire);item;item=item->next)
            if(item->image==owner&&!item->retired.load(std::memory_order_acquire))
            {FreeLibrary(owner);return item;}
        auto* item=new(std::nothrow)Epoch;
        if(!item){FreeLibrary(owner);return nullptr;}
        item->image=owner;item->serial=serial.fetch_add(1)+1;
        if(beforePublishForTest)beforePublishForTest(context);
        auto* prior=head.load(std::memory_order_acquire);
        for(;;)
        {
            // Another pinned factory may have won after our initial lookup.
            // Recheck the ACTUAL competing head before each publication attempt;
            // an unissued candidate can be destroyed, a published epoch cannot.
            for(auto* existing=prior;existing;existing=existing->next)
                if(existing->image==owner&&!existing->retired.load(std::memory_order_acquire))
                {delete item;FreeLibrary(owner);return existing;}
            item->next=prior;
            if(head.compare_exchange_weak(prior,item,std::memory_order_release,std::memory_order_acquire))break;
        }
        FreeLibrary(owner);
        return item;
    }
};
}
