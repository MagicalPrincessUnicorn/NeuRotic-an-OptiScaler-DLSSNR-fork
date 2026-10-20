#include "nr/diagnostics/candidate/CreateValue.h"
#include <windows.h>
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace DlssNr::CandidateObserver;
int main(int argc,char** argv) {
    if(argc<2)return 2;
    auto destination=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(destination);
    SetEnvironmentVariableA("NEUROTIC_CANDIDATE_OBSERVER","1");
    SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION","cccccccccccccccccccccccccccccccc");
    SetEnvironmentVariableW(L"NEUROTIC_DIAGNOSTIC_DIRECTORY",destination.c_str());
    const std::string mode=argc>2?argv[2]:"on";
    if(mode=="off")SetEnvironmentVariableA("NEUROTIC_CANDIDATE_OBSERVER",nullptr);
    if(mode=="bad_session")SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION","invalid");
    if(mode=="existing")std::filesystem::create_directories(destination/L"candidate-cccccccccccccccccccccccccccccccc");
    SetLastError(1234);StartCold(mode=="protected");assert(GetLastError()==1234);
    if(mode=="off" || mode=="bad_session" || mode=="protected" || mode=="existing") {
        Sleep(250);assert(!Enabled());Snapshot unavailable{};assert(TryRead(unavailable)==ReadResult::Unavailable);
        assert(!std::filesystem::exists(destination/L"candidate-cccccccccccccccccccccccccccccccc"/L"trace.jsonl"));
        std::cout<<"cold refusal without capture: "<<mode<<" passed\n";return 0;
    }
    for(int i=0;i<1000 && !Enabled();i++)Sleep(1);
    assert(Enabled());ObserveCoverage(0x22);
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=1920;desc.Height=1080;desc.DepthOrArraySize=1;desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_D32_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;desc.SampleDesc.Count=1;
    void* object=reinterpret_cast<void*>(0x12345678);
    auto poison=reinterpret_cast<void**>(1);
    // Failed output storage must never be read. No COM object is created or called.
    auto failed=CreateValue(false,true,E_FAIL,nullptr,&desc,D3D12_RESOURCE_STATE_COMMON,
                            __uuidof(ID3D12Resource),poison,nullptr,D3D12_HEAP_FLAG_NONE,nullptr,0);
    assert(failed.kind==Kind::Failed);
    assert(TryEmit(failed)==EmitResult::Recorded);
    auto success=CreateValue(false,true,S_OK,nullptr,&desc,D3D12_RESOURCE_STATE_COMMON,
                            __uuidof(ID3D12Resource),&object,nullptr,D3D12_HEAP_FLAG_NONE,nullptr,0);
    assert(success.kind==Kind::Committed && success.resourceBits==0x12345678);
    assert(TryEmit(success)==EmitResult::Recorded);
    success.resourceBits=0x12345679;assert(TryEmit(success)==EmitResult::Recorded);
    if(mode=="loss") {
        Snapshot pending{};for(int i=0;i<1000 && TryRead(pending)==ReadResult::Unavailable;i++)Sleep(1);
        {CreationScope outer;assert(outer.ShouldEmit());CreationScope nested;assert(!nested.ShouldEmit());}
        assert(TryRead(pending)==ReadResult::Snapshot && !pending.coverage.complete);
        OwnerFact unsupported{};unsupported.available=true;
        assert(TryObserveOwnerFact(unsupported)==ImportResult::UnsupportedContract);
    }
    RequestClose(Reason::Requested);
    auto path=destination/L"candidate-cccccccccccccccccccccccccccccccc"/L"trace.jsonl";
    bool footer=false;
    for(int i=0;i<1000;i++) {
        std::ifstream input(path);std::string line;
        while(std::getline(input,line)) if(line.find("\"type\":\"footer\"")!=std::string::npos)footer=true;
        if(footer)break;Sleep(2);
    }
    assert(footer);Snapshot snapshot{};
    assert(TryRead(snapshot)==ReadResult::Closed);assert(snapshot.count==2);
    for(uint32_t i=0;i<snapshot.count;i++)assert(!snapshot.candidates[i].mayAdmit);
    OwnerFact fact{};fact.available=true;assert(TryObserveOwnerFact(fact)==ImportResult::Unavailable);
    std::cout<<"synthetic metadata -> actual native capture -> closed snapshot passed\n";
}
