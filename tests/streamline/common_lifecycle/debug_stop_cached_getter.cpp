// Disposable authored process only. Normal activation witness; no production
// hook, private provider/game, crash-recovery or mitigation qualification.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifndef _M_X64
#error This public continuation witness uses the Windows AMD64 context.
#endif

namespace
{
struct alignas(64) Shared
{
    volatile LONG64 calls=0,original=0,wrapped=0,bad=0,heapHeld=0,releaseHeap=0;
    ULONG64 getter=0;
    volatile LONG64 vehEntries=0,vehSiteEntries=0,lastExceptionAddress=0,lastContextRip=0,
        lastExceptionCode=0,rootEntries=0,rootOriginalBad=0,lastSiteRip=0,firstSiteRip=0;
};
static_assert(sizeof(Shared)==128);
using Getter=unsigned(__cdecl*)();
unsigned char* targetCode=nullptr;
Getter targetOriginal=nullptr;
Shared* targetObservations=nullptr;
unsigned __cdecl RootGetter()
{
    if(targetObservations)InterlockedIncrement64(&targetObservations->rootEntries);
    const auto result=targetOriginal();
    if(result!=7&&targetObservations)InterlockedIncrement64(&targetObservations->rootOriginalBad);
    return result==7?8:0;
}
LONG CALLBACK TargetVeh(EXCEPTION_POINTERS* e)
{
    if(targetObservations&&e&&e->ExceptionRecord&&e->ContextRecord)
    {
        auto* observations=targetObservations;
        InterlockedIncrement64(&observations->vehEntries);
        InterlockedExchange64(&observations->lastExceptionAddress,static_cast<LONG64>(reinterpret_cast<ULONG64>(e->ExceptionRecord->ExceptionAddress)));
        InterlockedExchange64(&observations->lastContextRip,static_cast<LONG64>(e->ContextRecord->Rip));
        InterlockedExchange64(&observations->lastExceptionCode,static_cast<LONG64>(e->ExceptionRecord->ExceptionCode));
        if(e->ExceptionRecord->ExceptionCode==EXCEPTION_BREAKPOINT&&e->ExceptionRecord->ExceptionAddress==targetCode)
        {
            InterlockedIncrement64(&observations->vehSiteEntries);
            InterlockedExchange64(&observations->lastSiteRip,static_cast<LONG64>(e->ContextRecord->Rip));
            InterlockedCompareExchange64(&observations->firstSiteRip,static_cast<LONG64>(e->ContextRecord->Rip),0);
        }
    }
    if(e&&e->ExceptionRecord&&e->ContextRecord&&
       e->ExceptionRecord->ExceptionCode==EXCEPTION_BREAKPOINT&&
       e->ExceptionRecord->ExceptionAddress==targetCode&&
       e->ContextRecord->Rip==reinterpret_cast<DWORD64>(targetCode))
    {
        // Already-published typed continuation. No allocation, wait or lock.
        e->ContextRecord->Rip=reinterpret_cast<DWORD64>(&RootGetter);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
LONG64 Read(volatile LONG64& value){return InterlockedCompareExchange64(&value,0,0);}
unsigned checks=0;
void Check(bool ok,const char* text)
{++checks;if(!ok){const auto error=GetLastError();throw std::runtime_error(std::string(text)+" ambientLastError="+std::to_string(error));}std::printf("PASS %s\n",text);std::fflush(stdout);}

void HandlerPredicates()
{
    unsigned char ownedSite=0xcc;
    targetCode=&ownedSite;
    for(unsigned scenario=0;scenario<5;++scenario)
    {
        EXCEPTION_RECORD record{};CONTEXT context{};
        record.ExceptionCode=scenario==0?EXCEPTION_ACCESS_VIOLATION:EXCEPTION_BREAKPOINT;
        record.ExceptionAddress=scenario==1?static_cast<void*>(&record):static_cast<void*>(targetCode);
        context.Rip=reinterpret_cast<DWORD64>(targetCode)+(scenario==2?1:scenario==3?2:0);
        const auto originalRip=context.Rip;
        EXCEPTION_POINTERS event{&record,&context};
        const auto result=TargetVeh(&event);
        if(scenario==4)
            Check(result==EXCEPTION_CONTINUE_EXECUTION&&context.Rip==reinterpret_cast<DWORD64>(&RootGetter),
                "authored exact owned breakpoint context selects typed continuation without executing it");
        else
            Check(result==EXCEPTION_CONTINUE_SEARCH&&context.Rip==originalRip,
                "wrong exception, foreign site and nonmatching saved PC remain untouched");
    }
    targetCode=nullptr;
}

int Target(const wchar_t* stem)
{
    const auto name=std::wstring(stem);
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,(name+L"-map").c_str());
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,(name+L"-ready").c_str());
    HANDLE stop=OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,(name+L"-stop").c_str());
    if(!mapping||!ready||!stop)return 2;
    auto* shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
    if(!shared)return 2;
    targetObservations=shared;
    targetCode=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!targetCode)return 2;
    std::memset(targetCode,0xcc,4096);
    const unsigned char original[]={0x90,0xb8,7,0,0,0,0xc3};
    std::memcpy(targetCode,original,sizeof(original));
    // Stack-neutral original replay; both direct conditional branches land at
    // the unchanged provider continuation. One owned allocation keeps rel32 near.
    constexpr unsigned replay=64;
    targetCode[replay]=0x90;
    for(unsigned branch=0;branch<2;++branch)
    {
        const unsigned at=replay+1+branch*6;
        targetCode[at]=0x0f;targetCode[at+1]=branch?0x85:0x84;
        const auto delta=static_cast<std::int32_t>(1-static_cast<int>(at+6));
        std::memcpy(targetCode+at+2,&delta,sizeof(delta));
    }
    targetOriginal=reinterpret_cast<Getter>(targetCode+replay);
    DWORD old=0;
    if(!VirtualProtect(targetCode,4096,PAGE_EXECUTE_READ,&old)||
       !FlushInstructionCache(GetCurrentProcess(),targetCode,4096))return 2;
    void* handler=AddVectoredExceptionHandler(1,&TargetVeh);
    if(!handler)return 2;
    const auto cached=reinterpret_cast<Getter>(targetCode); // cache BEFORE attach/patch
    std::vector<std::thread> workers;
    for(unsigned i=0;i<4;++i)workers.emplace_back([&]{
        while(WaitForSingleObject(stop,0)==WAIT_TIMEOUT)
        {
            const auto result=cached();
            if(result==7)InterlockedIncrement64(&shared->original);
            else if(result==8)InterlockedIncrement64(&shared->wrapped);
            else InterlockedIncrement64(&shared->bad);
            InterlockedIncrement64(&shared->calls);
        }
    });
    std::thread heapHolder([&]{
        const auto heap=GetProcessHeap();
        const bool locked=HeapLock(heap)!=0;
        InterlockedExchange64(&shared->heapHeld,locked?1:-1);
        if(locked)
        {
            while(!Read(shared->releaseHeap)&&WaitForSingleObject(stop,0)==WAIT_TIMEOUT)Sleep(1);
            HeapUnlock(heap);InterlockedExchange64(&shared->heapHeld,0);
        }
    });
    const auto heapDeadline=GetTickCount64()+5000;
    while(!Read(shared->heapHeld)&&GetTickCount64()<heapDeadline)Sleep(1);
    shared->getter=reinterpret_cast<ULONG64>(targetCode);
    const bool announced=SetEvent(ready)!=0;
    const bool stopped=announced&&WaitForSingleObject(stop,30000)==WAIT_OBJECT_0;
    // Every started caller is joined even if ready publication or timeout fails.
    if(!stopped)SetEvent(stop);
    for(auto& worker:workers)worker.join();
    heapHolder.join();
    const bool valid=stopped&&Read(shared->bad)==0;
    RemoveVectoredExceptionHandler(handler);VirtualFree(targetCode,0,MEM_RELEASE);
    targetCode=nullptr;targetOriginal=nullptr;targetObservations=nullptr;
    UnmapViewOfFile(shared);CloseHandle(stop);CloseHandle(ready);CloseHandle(mapping);
    return valid?0:3;
}

struct OwnedTarget
{
    PROCESS_INFORMATION process{};
    HANDLE mapping=nullptr,ready=nullptr,stop=nullptr;
    Shared* shared=nullptr;
    bool attached=false,held=false;
    DEBUG_EVENT pending{};
    struct DebugThread {DWORD id;HANDLE handle;};
    std::vector<DebugThread> debugThreads;
    HANDLE debugProcess=nullptr;
    ~OwnedTarget()
    {
        if(held)ContinueDebugEvent(pending.dwProcessId,pending.dwThreadId,DBG_CONTINUE);
        if(attached)DebugActiveProcessStop(process.dwProcessId);
        for(auto& thread:debugThreads)if(thread.handle)CloseHandle(thread.handle);
        if(debugProcess)CloseHandle(debugProcess);
        if(stop)SetEvent(stop);
        const bool natural=process.hProcess&&WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0;
        if(process.hProcess&&!natural)
        {
            // Only this fixture's exact freshly created disposable process.
            TerminateProcess(process.hProcess,ERROR_CANCELLED);WaitForSingleObject(process.hProcess,5000);
        }
        if(process.hProcess)
        {
            DWORD exit=STILL_ACTIVE;const bool queried=GetExitCodeProcess(process.hProcess,&exit)!=0;
            std::printf("CLEANUP_TARGET naturalExit=%u forced=%u exitKnown=%u exit=0x%08lx\n",unsigned(natural),unsigned(!natural),unsigned(queried),exit);
            std::fflush(stdout);
        }
        if(shared)UnmapViewOfFile(shared);
        for(HANDLE h:{process.hThread,process.hProcess,mapping,ready,stop})if(h)CloseHandle(h);
    }
};
void ObserveEventHandles(const DEBUG_EVENT& event,OwnedTarget& own)
{
    if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT)
    {
        if(event.u.CreateProcessInfo.hFile)CloseHandle(event.u.CreateProcessInfo.hFile);
        if(event.u.CreateProcessInfo.hThread&&event.u.CreateProcessInfo.hThread!=own.process.hThread)
            own.debugThreads.push_back({event.dwThreadId,event.u.CreateProcessInfo.hThread});
        if(event.u.CreateProcessInfo.hProcess&&event.u.CreateProcessInfo.hProcess!=own.process.hProcess)
            own.debugProcess=event.u.CreateProcessInfo.hProcess;
    }
    else if(event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT&&event.u.CreateThread.hThread)
        own.debugThreads.push_back({event.dwThreadId,event.u.CreateThread.hThread});
    else if(event.dwDebugEventCode==EXIT_THREAD_DEBUG_EVENT)
    {
        for(auto& thread:own.debugThreads)if(thread.id==event.dwThreadId)thread.handle=nullptr; // OS closes on Continue
    }
    else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT)
    {
        own.debugProcess=nullptr;
        for(auto& thread:own.debugThreads)thread.handle=nullptr;
    }
    else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT&&event.u.LoadDll.hFile)CloseHandle(event.u.LoadDll.hFile);
}
void Census(DWORD pid)
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    Check(snapshot!=INVALID_HANDLE_VALUE,"held-event thread census created outside target");
    THREADENTRY32 item{};item.dwSize=sizeof(item);unsigned count=0;
    bool valid=true;
    if(Thread32First(snapshot,&item))do
    {
        if(item.th32OwnerProcessID!=pid)continue;
        ++count;
        HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,item.th32ThreadID);
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
        const bool read=thread&&GetThreadContext(thread,&context)&&context.Rip!=0;
        std::printf("THREAD heldInitialEvent=1 tid=%lu context=%u rip=%llx\n",item.th32ThreadID,unsigned(read),
            static_cast<unsigned long long>(context.Rip));
        if(thread)CloseHandle(thread);
        valid=valid&&read;
    }while(Thread32Next(snapshot,&item));
    CloseHandle(snapshot);
    Check(count>=6&&valid,"actual target main, four callers and heap owner have stopped contexts without SuspendThread");
}
int Parent()
{
    HandlerPredicates();
    OwnedTarget own;
    const auto stem=L"Local\\NRCommonDebugStop-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    own.mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(sizeof(Shared)),(stem+L"-map").c_str());
    Check(own.mapping&&GetLastError()!=ERROR_ALREADY_EXISTS,"unique task-owned shared mapping created");
    own.shared=static_cast<Shared*>(MapViewOfFile(own.mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
    Check(own.shared!=nullptr,"shared counters mapped");std::memset(own.shared,0,sizeof(Shared));
    own.ready=CreateEventW(nullptr,TRUE,FALSE,(stem+L"-ready").c_str());
    Check(own.ready&&GetLastError()!=ERROR_ALREADY_EXISTS,"unique task-owned ready event created");
    own.stop=CreateEventW(nullptr,TRUE,FALSE,(stem+L"-stop").c_str());
    Check(own.stop&&GetLastError()!=ERROR_ALREADY_EXISTS,"unique task-owned stop event created");
    std::array<wchar_t,32768> executable{};
    const auto length=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    Check(length&&length<executable.size(),"actual own executable path obtained");
    std::wstring command=L"\""+std::wstring(executable.data())+L"\" --target "+stem;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    Check(CreateProcessW(executable.data(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&own.process)!=0,
        "fresh disposable authored target launched normally, no debugger at startup");
    Check(WaitForSingleObject(own.ready,10000)==WAIT_OBJECT_0,"target published handler replay and cached callers before activation");
    const auto deadline=GetTickCount64()+5000;
    while(Read(own.shared->original)<1000&&GetTickCount64()<deadline)Sleep(1);
    Check(Read(own.shared->original)>=1000&&Read(own.shared->wrapped)==0&&Read(own.shared->bad)==0,
        "cached getter really executes original before late attach");
    Check(DebugActiveProcess(own.process.dwProcessId)!=0,"debug connection established only to this owned target");own.attached=true;
    // A disposable target permits this test's short default-kill acquisition
    // window. A real-target no-kill bootstrap policy remains separate work.
    Check(DebugSetProcessKillOnExit(FALSE)!=0,"normal fixture connection configured non-killing after acquisition");
    Check(WaitForDebugEvent(&own.pending,10000)!=0,"actual initial debug event returned");own.held=true;
    Check(own.pending.dwProcessId==own.process.dwProcessId&&own.pending.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT,
        "first matching CREATE_PROCESS event held, no assumption from attach return alone");
    const auto before=Read(own.shared->calls);Sleep(30);
    Check(Read(own.shared->calls)==before,"active target counters stable while initial event held");
    Check(Read(own.shared->heapHeld)==1,"actual target process heap remains owned by a stopped target thread");
    Census(own.process.dwProcessId);
    std::vector<std::uint64_t> externalAllocation(4096,7);
    Check(externalAllocation.back()==7,"external owner allocates independently while target heap owner is stopped");
    const auto getter=reinterpret_cast<void*>(own.shared->getter);
    std::array<unsigned char,7> prior{};SIZE_T read=0;
    Check(ReadProcessMemory(own.process.hProcess,getter,prior.data(),prior.size(),&read)&&read==prior.size()&&
        prior==std::array<unsigned char,7>{0x90,0xb8,7,0,0,0,0xc3},"actual authored entry and unchanged continuation verified");
    DWORD old=0;
    Check(VirtualProtectEx(own.process.hProcess,getter,1,PAGE_EXECUTE_READWRITE,&old)!=0,"owned entry page writable under real all-target stop");
    const unsigned char breakpoint=0xcc;SIZE_T written=0;
    Check(WriteProcessMemory(own.process.hProcess,getter,&breakpoint,1,&written)&&written==1,"exactly one known instruction byte patched");
    Check(FlushInstructionCache(own.process.hProcess,getter,1)!=0,"normal activation flush completed before continuation");
    DWORD ignored=0;
    Check(VirtualProtectEx(own.process.hProcess,getter,1,old,&ignored)!=0,"entry's original page protection restored");
    Check(Read(own.shared->calls)==before,"target still stopped through external writes and flush");
    // This authored held-lock control may finish once execution resumes. It is
    // not a target-side patch coordinator or a production rendezvous promise.
    InterlockedExchange64(&own.shared->releaseHeap,1);
    ObserveEventHandles(own.pending,own);
    Check(ContinueDebugEvent(own.pending.dwProcessId,own.pending.dwThreadId,DBG_CONTINUE)!=0,"initial held event explicitly continued");own.held=false;
    bool initialBreakpoint=false;unsigned ownBreakpoints=0;
    const auto debugDeadline=GetTickCount64()+10000;
    while(!initialBreakpoint&&GetTickCount64()<debugDeadline)
    {
        DEBUG_EVENT event{};
        if(!WaitForDebugEvent(&event,100))continue;
        own.pending=event;own.held=true;
        Check(event.dwProcessId==own.process.dwProcessId,"debug event belongs to owned target");
        DWORD disposition=DBG_CONTINUE;
        if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT)
        {
            const auto& exception=event.u.Exception.ExceptionRecord;
            if(exception.ExceptionCode==EXCEPTION_BREAKPOINT&&exception.ExceptionAddress==getter)
            {
                Check(event.u.Exception.dwFirstChance!=0,"getter breakpoint reaches first-chance routing");
                HANDLE thread=OpenThread(THREAD_GET_CONTEXT,FALSE,event.dwThreadId);
                CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL;
                const bool known=thread&&GetThreadContext(thread,&context);
                const auto error=known?0:GetLastError();
                std::printf("OBS_DEBUGGER_GETTER tid=%lu firstChance=%lu contextKnown=%u contextError=%lu rip=%llx rsp=%llx exceptionAddress=%llx\n",
                    event.dwThreadId,event.u.Exception.dwFirstChance,unsigned(known),error,static_cast<unsigned long long>(context.Rip),
                    static_cast<unsigned long long>(context.Rsp),static_cast<unsigned long long>(reinterpret_cast<ULONG64>(exception.ExceptionAddress)));
                if(thread)CloseHandle(thread);
                ++ownBreakpoints;disposition=DBG_EXCEPTION_NOT_HANDLED;
            }
            else if(exception.ExceptionCode==EXCEPTION_BREAKPOINT)initialBreakpoint=true;
            else disposition=DBG_EXCEPTION_NOT_HANDLED;
        }
        ObserveEventHandles(event,own);
        Check(event.dwDebugEventCode!=EXIT_PROCESS_DEBUG_EVENT,"target remains live through activation events");
        Check(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,disposition)!=0,"owned event continued with its selected exception disposition");own.held=false;
    }
    Check(initialBreakpoint,"normal attach initial breakpoint drained before detach");
    std::printf("OBS_BEFORE_DETACH calls=%lld old=%lld wrapped=%lld bad=%lld veh=%lld siteVeh=%lld root=%lld rootOriginalBad=%lld siteRip=%llx firstSiteRip=%llx debuggerGetterEvents=%u\n",
        Read(own.shared->calls),Read(own.shared->original),Read(own.shared->wrapped),Read(own.shared->bad),Read(own.shared->vehEntries),Read(own.shared->vehSiteEntries),
        Read(own.shared->rootEntries),Read(own.shared->rootOriginalBad),static_cast<unsigned long long>(Read(own.shared->lastSiteRip)),
        static_cast<unsigned long long>(Read(own.shared->firstSiteRip)),ownBreakpoints);std::fflush(stdout);
    Check(DebugActiveProcessStop(own.process.dwProcessId)!=0,"normal debugger detached after initial event sequence");own.attached=false;
    const auto wrappedDeadline=GetTickCount64()+5000;
    const auto wrappedBefore=Read(own.shared->wrapped);
    while(Read(own.shared->wrapped)-wrappedBefore<1000&&GetTickCount64()<wrappedDeadline)Sleep(1);
    DWORD observedExit=STILL_ACTIVE;const bool exitKnown=GetExitCodeProcess(own.process.hProcess,&observedExit)!=0;
    std::array<unsigned char,7> after{};SIZE_T afterRead=0;
    const bool codeRead=ReadProcessMemory(own.process.hProcess,getter,after.data(),after.size(),&afterRead)!=0;
    std::printf("OBS_AFTER_DETACH calls=%lld old=%lld wrapped=%lld wrappedDelta=%lld bad=%lld heapHeld=%lld veh=%lld siteVeh=%lld root=%lld rootOriginalBad=%lld lastCode=%llx lastAddress=%llx lastRip=%llx siteRip=%llx firstSiteRip=%llx getter=%llx exitKnown=%u exit=0x%08lx codeRead=%u codeBytes=%llu firstByte=%02x\n",
        Read(own.shared->calls),Read(own.shared->original),Read(own.shared->wrapped),Read(own.shared->wrapped)-wrappedBefore,Read(own.shared->bad),Read(own.shared->heapHeld),
        Read(own.shared->vehEntries),Read(own.shared->vehSiteEntries),Read(own.shared->rootEntries),Read(own.shared->rootOriginalBad),
        static_cast<unsigned long long>(Read(own.shared->lastExceptionCode)),static_cast<unsigned long long>(Read(own.shared->lastExceptionAddress)),
        static_cast<unsigned long long>(Read(own.shared->lastContextRip)),static_cast<unsigned long long>(Read(own.shared->lastSiteRip)),
        static_cast<unsigned long long>(Read(own.shared->firstSiteRip)),static_cast<unsigned long long>(own.shared->getter),unsigned(exitKnown),observedExit,
        unsigned(codeRead),static_cast<unsigned long long>(afterRead),static_cast<unsigned>(after[0]));std::fflush(stdout);
    Check(Read(own.shared->wrapped)-wrappedBefore>=1000&&Read(own.shared->bad)==0,
        "same cached getters execute typed wrapper and original replay after detach");
    Check(Read(own.shared->vehSiteEntries)>0&&Read(own.shared->rootEntries)>0&&
        Read(own.shared->rootOriginalBad)==0&&static_cast<ULONG64>(Read(own.shared->firstSiteRip))==own.shared->getter,
        "actual owned-site handler and typed wrapper execute with correct original result and observed saved PC");
    Check(SetEvent(own.stop)!=0&&WaitForSingleObject(own.process.hProcess,10000)==WAIT_OBJECT_0,"owned target joins callers before retiring handler and code");
    DWORD exit=1;Check(GetExitCodeProcess(own.process.hProcess,&exit)&&exit==0,"owned target exits cleanly");
    std::printf("DEBUG_STOP_CACHED_GETTER checks=%u PASS old=%lld wrapped=%lld bad=%lld debuggerGetterEvents=%u privateProvider=0 game=0 GPU=0 productEngine=0 helperCrashProof=0\n",
        checks,Read(own.shared->original),Read(own.shared->wrapped),Read(own.shared->bad),ownBreakpoints);
    return 0;
}
}
int wmain(int argc,wchar_t** argv)
{
    if(argc==3&&!std::wcscmp(argv[1],L"--target"))return Target(argv[2]);
    try{return Parent();}
    catch(const std::exception& e){std::fprintf(stderr,"DEBUG_STOP_CACHED_GETTER FAIL: %s\n",e.what());return 1;}
}
