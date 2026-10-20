// Public authored x64 code only. This witnesses replay unwind arithmetic and
// real return/exception paths, not a hook engine, debug stop, VEH or private ABI.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>

#ifndef _M_X64
#error This finite witness describes Windows AMD64 version-1 unwind records.
#endif
namespace
{
unsigned checks=0;
void Check(bool ok,const char* text)
{
    ++checks;if(!ok)throw std::runtime_error(text);
    std::printf("PASS %s\n",text);
}
enum class Prefix {Neutral,PushRbx,Sub40};
using Entry=unsigned(__fastcall*)(unsigned);
constexpr unsigned Result=0x31415926;
void ReturnMarker() {}

// Keep SEH in a separate function with no objects requiring C++ unwinding.
bool CatchIllegal(Entry entry,unsigned argument) noexcept
{
    __try {entry(argument);return false;}
    __except(GetExceptionCode()==EXCEPTION_ILLEGAL_INSTRUCTION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
    {return true;}
}

struct Arena
{
    unsigned char* bytes=nullptr;
    std::array<RUNTIME_FUNCTION,3> table{};
    bool registered=false;
    DWORD copied=0,landing=0,destination=0,q1=0,q2=0,negativeQ=0;
    static constexpr DWORD Provider=0,Replay=128,Negative=256,Starter=384;
    Arena(Prefix kind,bool endbr,bool fault)
    {
    try
    {
        bytes=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        if(!bytes)throw std::runtime_error("owned code allocation failed");
        std::memset(bytes,0xcc,4096);
        landing=endbr?4u:0u;copied=kind==Prefix::Sub40?4u:1u;
        const auto prefix=[&](DWORD start){
            if(endbr){const unsigned char e[]={0xf3,0x0f,0x1e,0xfa};std::memcpy(bytes+start,e,sizeof(e));}
            const DWORD at=start+landing;
            if(kind==Prefix::Sub40){const unsigned char s[]={0x48,0x83,0xec,0x28};std::memcpy(bytes+at,s,sizeof(s));}
            else bytes[at]=kind==Prefix::PushRbx?0x53:0x90;
        };
        prefix(Provider);prefix(Replay);prefix(Negative);
        destination=Provider+landing+copied;
        // The provider continuation is a neutral BODY boundary, not its epilogue.
        bytes[destination]=fault?0x0f:0x90;bytes[destination+1]=fault?0x0b:0x90;
        DWORD tail=destination+2;bytes[tail++]=0xb8;
        const unsigned result=Result;std::memcpy(bytes+tail,&result,sizeof(result));tail+=4;
        if(kind==Prefix::PushRbx)bytes[tail++]=0x5b;
        if(kind==Prefix::Sub40){const unsigned char a[]={0x48,0x83,0xc4,0x28};std::memcpy(bytes+tail,a,sizeof(a));tail+=4;}
        bytes[tail++]=0xc3;
        q1=Replay+landing+copied;q2=q1+6;negativeQ=Negative+landing+copied;
        const auto displacement=[&](DWORD at,DWORD next){
            const auto delta=std::int64_t(destination)-std::int64_t(next);
            Check(delta>=std::numeric_limits<std::int32_t>::min()&&delta<=std::numeric_limits<std::int32_t>::max(),"each direct displacement fits independently");
            const auto value=static_cast<std::int32_t>(delta);std::memcpy(bytes+at,&value,sizeof(value));
        };
        bytes[q1]=0x0f;bytes[q1+1]=0x84;displacement(q1+2,q1+6);
        bytes[q2]=0x0f;bytes[q2+1]=0x85;displacement(q2+2,q2+6);
        bytes[negativeQ]=0xe9;displacement(negativeQ+1,negativeQ+5);
        // Establish incoming ZF through actual TEST before entering the replay.
        // PUSH/NOP preserve it; SUB generates its own flags from the real RSP.
        bytes[Starter]=0x85;bytes[Starter+1]=0xc9;bytes[Starter+2]=0xe9;
        const auto enter=static_cast<std::int32_t>(std::int64_t(Replay)-std::int64_t(Starter+7));
        std::memcpy(bytes+Starter+3,&enter,sizeof(enter));
        for(unsigned i=0;i<table.size();++i)
        {
            const DWORD unwind=512+i*16;
            std::memset(bytes+unwind,0,16);
            bytes[unwind]=1; // version1, no handler/chaining
            bytes[unwind+1]=static_cast<unsigned char>(landing+copied);
            bytes[unwind+2]=kind==Prefix::Neutral?0:1;
            bytes[unwind+3]=0; // no frame register
            if(kind!=Prefix::Neutral)
            {
                bytes[unwind+4]=static_cast<unsigned char>(landing+copied);
                // UWOP_PUSH_NONVOL RBX(3), or UWOP_ALLOC_SMALL opinfo4 = 40 bytes.
                bytes[unwind+5]=kind==Prefix::PushRbx?0x30:0x42;
                bytes[unwind+6]=bytes[unwind+7]=0; // padded code array
            }
            table[i]={i==0?Provider:i==1?Replay:Negative,i==0?tail:i==1?q2+6:negativeQ+5,unwind};
        }
        DWORD old=0;
        Check(VirtualProtect(bytes,4096,PAGE_EXECUTE_READ,&old)!=0,"owned code sealed executable/read-only");
        Check(FlushInstructionCache(GetCurrentProcess(),bytes,4096)!=0,"authored code cache flushed");
        Check(RtlAddFunctionTable(table.data(),static_cast<DWORD>(table.size()),reinterpret_cast<DWORD64>(bytes))!=0,"actual sorted runtime function table registered");
        registered=true;
    }
    catch(...)
    {
        if(bytes)VirtualFree(bytes,0,MEM_RELEASE);
        throw;
    }
    }
    ~Arena()
    {
        if(registered&&!RtlDeleteFunctionTable(table.data()))std::terminate();
        if(bytes&&!VirtualFree(bytes,0,MEM_RELEASE))std::terminate();
    }
    Arena(const Arena&)=delete;Arena& operator=(const Arena&)=delete;
    Entry Function()const{return reinterpret_cast<Entry>(bytes+Starter);}
};

CONTEXT Original(DWORD64 rsp)
{
    CONTEXT c{};c.ContextFlags=CONTEXT_FULL;c.Rsp=rsp;
    c.Rbx=0x11223344;c.Rbp=0x22334455;c.Rsi=0x33445566;c.Rdi=0x44556677;
    c.R12=0x55667788;c.R13=0x66778899;c.R14=0x778899aa;c.R15=0x8899aabb;
    c.Xmm6={6,60};c.Xmm7={7,70};c.Xmm8={8,80};c.Xmm9={9,90};c.Xmm10={10,100};
    c.Xmm11={11,110};c.Xmm12={12,120};c.Xmm13={13,130};c.Xmm14={14,140};c.Xmm15={15,150};
    return c;
}
bool SameNonvolatiles(const CONTEXT& a,const CONTEXT& b)
{
    return a.Rbx==b.Rbx&&a.Rbp==b.Rbp&&a.Rsi==b.Rsi&&a.Rdi==b.Rdi&&
        a.R12==b.R12&&a.R13==b.R13&&a.R14==b.R14&&a.R15==b.R15&&
        std::memcmp(&a.Xmm6,&b.Xmm6,10*sizeof(M128A))==0;
}
void Unwind(Arena& arena,Prefix kind,DWORD offset,bool executed,bool negative)
{
    alignas(16) std::array<DWORD64,128> stack{};
    auto* s=&stack[65];const auto rsp=reinterpret_cast<DWORD64>(s);
    const auto original=Original(rsp);const auto returned=reinterpret_cast<DWORD64>(&ReturnMarker);
    s[0]=returned;s[-1]=original.Rbx;
    auto observed=original;observed.Rip=reinterpret_cast<DWORD64>(arena.bytes+offset);
    if(executed&&kind==Prefix::PushRbx)observed.Rsp-=8;
    if(executed&&kind==Prefix::Sub40)observed.Rsp-=40;
    // Explicit synthetic boundary contexts, not sampled live exception contexts.
    // ZF does not participate in unwind; actual flags/branches are exercised below.
    observed.EFlags=0x202;
    DWORD64 base=0;
    auto* entry=RtlLookupFunctionEntry(observed.Rip,&base,nullptr);
    const unsigned expected=offset<128?0:offset<256?1:2;
    Check(entry&&base==reinterpret_cast<DWORD64>(arena.bytes)&&
        entry->BeginAddress==arena.table[expected].BeginAddress&&entry->EndAddress==arena.table[expected].EndAddress&&
        entry->UnwindData==arena.table[expected].UnwindData,"lookup selects actual replay or provider metadata");
    void* handler=nullptr;DWORD64 frame=0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,observed.Rip,entry,&observed,&handler,&frame,nullptr);
    const bool exact=observed.Rip==returned&&observed.Rsp==rsp+8&&SameNonvolatiles(observed,original);
    std::printf("UNWIND prefix=%u landing=%lu offset=%lu executed=%u negative=%u callerExact=%u rip=%llx rsp=%llx\n",
        static_cast<unsigned>(kind),arena.landing,offset,unsigned(executed),unsigned(negative),unsigned(exact),
        static_cast<unsigned long long>(observed.Rip),static_cast<unsigned long long>(observed.Rsp));
    if(!negative)Check(exact,"paired Jcc boundary recovers exact caller RIP/RSP and every nonvolatile");
}
void Witness(Prefix kind,bool landing)
{
    Arena arena(kind,landing,false);
    std::printf("CODE prefix=%u landing=%u base=%llx destination=%lu q1=%lu q2=%lu\n",static_cast<unsigned>(kind),unsigned(landing),
        static_cast<unsigned long long>(reinterpret_cast<DWORD64>(arena.bytes)),arena.destination,arena.q1,arena.q2);
    for(unsigned i=0;i<arena.table.size();++i)
    {
        const auto& f=arena.table[i];std::printf("TABLE begin=%lu end=%lu unwind=%lu bytes=",f.BeginAddress,f.EndAddress,f.UnwindData);
        for(DWORD n=f.BeginAddress;n<f.EndAddress;++n)std::printf("%02x",arena.bytes[n]);
        std::printf(" metadata=");for(DWORD n=f.UnwindData;n<f.UnwindData+8;++n)std::printf("%02x",arena.bytes[n]);std::puts("");
    }
    Unwind(arena,kind,Arena::Replay,false,false);
    if(landing)Unwind(arena,kind,Arena::Replay+4,false,false);
    Unwind(arena,kind,arena.q1,true,false);Unwind(arena,kind,arena.q2,true,false);
    Unwind(arena,kind,arena.destination,true,false);
    Unwind(arena,kind,arena.negativeQ,true,true); // observation, no demanded failure pattern
    for(unsigned argument:{0u,1u})Check(arena.Function()(argument)==Result,"real typed call transfers through pair and provider tail returns");
    Arena fault(kind,landing,true);
    for(unsigned argument:{0u,1u})Check(CatchIllegal(fault.Function(),argument),"real provider-body illegal instruction unwinds into actual caller SEH");
}
}
int main()
{
    try
    {
        for(auto kind:{Prefix::Neutral,Prefix::PushRbx,Prefix::Sub40})
            for(bool landing:{false,true})Witness(kind,landing);
        std::printf("PAIRED_JCC_UNWIND checks=%u PASS publicAuthored=1 syntheticBoundaryContexts=1 realReturnAndSEH=1 debugAttach=0 hookEngine=0 privateProvider=0 GPU=0\n",checks);
        return 0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"PAIRED_JCC_UNWIND FAIL: %s\n",e.what());return 1;}
}
