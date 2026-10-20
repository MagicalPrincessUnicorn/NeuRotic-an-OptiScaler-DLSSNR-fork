#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#ifdef NR_OLD_ONLOAD_JSON_RETURN
#include "old_onload_json_return.inc"
#else
#include "../../../OptiScaler/hooks/StreamlineOwnedJson.h"
#include <array>
#include <limits>
#include <thread>
using OwnedJson=Neurotic::Hooks::StreamlineOwnedJson;
struct FailedAllocation
{
    static inline unsigned calls=0;
    static void* Allocate(std::size_t) noexcept {++calls;return nullptr;}
};
static bool ReturnJson(bool result,const char** output,std::string rendered)
{return OwnedJson::PublishSuccessfulForProcess(result,output,rendered);}
#endif
static unsigned checks=0;
static void Check(bool ok,const char* message)
{++checks;if(!ok)throw std::runtime_error(message);std::printf("PASS %s\n",message);}
int main()
{
    try
    {
        const char* first="provider A";const char* second="provider B";
        Check(ReturnJson(true,&first,"{\"id\":\"A\"}")&&std::strcmp(first,"{\"id\":\"A\"}")==0,
            "first successful callback publishes its rendered JSON");
        Check(ReturnJson(true,&second,"{\"id\":\"B\"}")&&std::strcmp(second,"{\"id\":\"B\"}")==0,
            "second successful callback publishes its rendered JSON");
        Check(first!=second&&std::strcmp(first,"{\"id\":\"A\"}")==0,
            "later callback cannot overwrite an earlier issued JSON pointer");
#ifndef NR_OLD_ONLOAD_JSON_RETURN
        Check(HeapValidate(GetProcessHeap(),0,const_cast<char*>(first))&&
              HeapValidate(GetProcessHeap(),0,const_cast<char*>(second)),
            "published text is allocated from the actual Windows process heap");
        std::string source(4096,'L');const char* longResult="provider long";
        Check(ReturnJson(true,&longResult,source)&&std::strcmp(longResult,source.c_str())==0,
            "long JSON payload is copied completely without a fixed length limit");
        source.assign(4096,'M');
        Check(std::strlen(longResult)==4096&&longResult[0]=='L'&&longResult[4095]=='L',
            "provider and temporary string changes cannot mutate issued text");
        const char* raw=source.c_str();
        Check(OwnedJson::SnapshotSuccessfulForProcess(true,&raw)&&raw!=source.c_str()&&
              std::strcmp(raw,source.c_str())==0&&HeapValidate(GetProcessHeap(),0,const_cast<char*>(raw)),
            "unpatched PCL output receives independent process-owned bytes");
        source.clear();source.shrink_to_fit();
        Check(std::strlen(raw)==4096&&raw[4095]=='M',"raw output survives destruction of source storage");
        const char* empty="provider empty";
        Check(ReturnJson(true,&empty,"")&&empty&&empty[0]=='\0',"successful empty text remains a valid owned string");
        const auto unreadable=reinterpret_cast<const char*>(static_cast<std::uintptr_t>(1));
        const auto unreadableOutput=reinterpret_cast<const char**>(static_cast<std::uintptr_t>(1));
        const std::string_view unreadableText(unreadable,1);
        Check(!OwnedJson::PublishSuccessfulForProcess<FailedAllocation>(false,unreadableOutput,unreadableText)&&
              !OwnedJson::SnapshotSuccessfulForProcess<FailedAllocation>(false,unreadableOutput)&&
              FailedAllocation::calls==0,"false original result never reads output or allocates");
        const char* failedOriginal=unreadable;
        Check(!OwnedJson::PublishSuccessfulForProcess<FailedAllocation>(false,&failedOriginal,unreadableText)&&
              failedOriginal==unreadable,"failed callback preserves its original output value");
        const char* nullText=nullptr;
        Check(OwnedJson::PublishSuccessfulForProcess<FailedAllocation>(true,nullptr,unreadableText)&&
              OwnedJson::SnapshotSuccessfulForProcess<FailedAllocation>(true,nullptr)&&
              OwnedJson::PublishSuccessfulForProcess<FailedAllocation>(true,&nullText,unreadableText)&&
              OwnedJson::SnapshotSuccessfulForProcess<FailedAllocation>(true,&nullText)&&
              !nullText&&FailedAllocation::calls==0,"null output slots and null successful text pass through without reads or allocation");
        const char* allocationFailure="provider allocation";
        Check(!OwnedJson::PublishSuccessfulForProcess<FailedAllocation>(true,&allocationFailure,"rendered")&&
              !allocationFailure&&FailedAllocation::calls==1,"failed allocation returns false and clears borrowed output");
        const char* impossible="provider overflow";
        const std::string_view oversized(unreadable,(std::numeric_limits<std::size_t>::max)());
        Check(!OwnedJson::PublishSuccessfulForProcess<FailedAllocation>(true,&impossible,oversized)&&
              !impossible&&FailedAllocation::calls==1,"terminator size overflow fails before allocator or source read");
        std::array<const char*,64> parallel{};std::array<std::thread,4> threads;
        for(std::size_t t=0;t<threads.size();++t)
            threads[t]=std::thread([&,t]{for(std::size_t i=t*16;i<(t+1)*16;++i)
            {parallel[i]="provider parallel";const auto value=std::to_string(i);
             if(!OwnedJson::PublishSuccessfulForProcess(true,&parallel[i],value))parallel[i]=nullptr;}});
        for(auto& thread:threads)thread.join();
        bool independent=true;
        for(std::size_t i=0;i<parallel.size();++i)
        {
            independent=independent&&parallel[i]&&std::strcmp(parallel[i],std::to_string(i).c_str())==0;
            for(std::size_t j=0;j<i;++j)independent=independent&&parallel[i]!=parallel[j];
        }
        Check(independent,"concurrent publications retain all distinct caller results after temporary destruction");
#endif
        std::printf("ONLOAD_JSON_OWNERSHIP checks=%u PASS privateProvider=0 GPU=0\n",checks);return 0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"ONLOAD_JSON_OWNERSHIP FAIL: %s\n",e.what());return 1;}
}
