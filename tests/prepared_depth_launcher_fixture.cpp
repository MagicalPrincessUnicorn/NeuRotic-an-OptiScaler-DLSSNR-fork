// Environment/argv probe only. No DLL/provider loading or graphics calls.
#include <cstdlib>
#include <fstream>
#include <string>
#include <windows.h>
int main(int argc,char** argv)
{
    std::ofstream environment("child-environment.txt");
    for(const auto* name:{"NEUROTIC_PREPARED_DEPTH_MIP0","NEUROTIC_FRAME_TRACE_SESSION",
        "NEUROTIC_FRAME_TRACE_TRIGGER","NEUROTIC_FRAME_TRACE_PROFILE","NEUROTIC_PREPARED_DEPTH_DIAGNOSTICS"})
    {
        char* value=nullptr;size_t length=0;
        if(_dupenv_s(&value,&length,name)==0&&value)environment<<name<<'='<<value<<'\n';
        std::free(value);
    }
    for(int i=1;i<argc;++i){std::ofstream argument("child-argument-"+std::to_string(i)+".txt");argument<<argv[i];}
    if(argc>1&&std::string(argv[1])=="--emit-depth-eligibility")
    {
        char* session=nullptr;size_t length=0;_dupenv_s(&session,&length,"NEUROTIC_FRAME_TRACE_SESSION");
        LARGE_INTEGER qpc{},frequency{};QueryPerformanceCounter(&qpc);QueryPerformanceFrequency(&frequency);
        std::ofstream log("OptiScaler.log");
        if(session)log<<"NR_FRAME_TRACE v=1 session="<<session<<" pid="<<GetCurrentProcessId()
                      <<" kind=trace-ended reason=event-budget-exhausted\n";
        const auto row=[&](const char* id,DWORD pid,unsigned observation){
            log<<"NR_DEPTH_ELIGIBILITY v=1 session="<<id<<" pid="<<pid<<" qpc="<<qpc.QuadPart
               <<" frequency="<<frequency.QuadPart<<" phase=descriptor-only observation="<<observation
               <<" limit=64 budgetExhausted=false enabled=true privateList=false protocolObserver=false "
                 "generation=2 source=42 width=2560 height=1440 mips=1 format=19 dimension=3 arraySize=1 "
                 "sampleCount=1 sampleQuality=0 flags=0 x=0 y=0 activeWidth=2560 activeHeight=1440 decision=direct reason=single-mip\n";
        };
        if(session){row("00000000000000000000000000000000",GetCurrentProcessId(),1);
            row(session,GetCurrentProcessId()+1,1);row(session,GetCurrentProcessId(),1);
            row(session,GetCurrentProcessId(),1);row(session,GetCurrentProcessId(),65);}
        std::free(session);
    }
    return 0;
}
