#include "nr/diagnostics/candidate/Observer.h"
#include <windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
using namespace DlssNr::CandidateObserver;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const auto destination=std::filesystem::absolute(argv[1]);
    assert(!std::filesystem::exists(destination));std::filesystem::create_directories(destination);
    SetEnvironmentVariableA("NEUROTIC_CANDIDATE_OBSERVER","1");
    SetEnvironmentVariableA("NEUROTIC_FRAME_TRACE_SESSION","bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    SetEnvironmentVariableW(L"NEUROTIC_DIAGNOSTIC_DIRECTORY",destination.c_str());
    StartCold(false);for(unsigned i=0;i<1000 && !CaptureTestGateReady();i++)Sleep(1);
    assert(CaptureTestGateReady() && Enabled());
    ObserveCoverage(0x22);
    Event e{};e.kind=Kind::Committed;e.source=1;e.deviceBits=20;e.descriptorKnown=true;
    e.descriptor.dimension=1;e.descriptor.width=256;e.descriptor.height=1;e.descriptor.depth=1;
    e.descriptor.mips=1;e.descriptor.sampleCount=1;e.descriptor.layout=1;
    for(uintptr_t r=1;r<=7;r++) {
        e.resourceBits=r;
        assert(TryEmit(e)==(r<=4?EmitResult::Recorded:EmitResult::BudgetRefused));
    }
    RequestClose(Reason::Requested);
    ReleaseCaptureTestGate();
    const auto trace=destination/L"candidate-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"/L"trace.jsonl";
    bool footer=false;
    for(unsigned i=0;i<1000 && !footer;i++) {
        std::ifstream input(trace);std::string line;
        while(std::getline(input,line))if(line.find("\"type\":\"footer\"")!=std::string::npos)footer=true;
        Sleep(2);
    }
    Snapshot snapshot{};assert(footer && TryRead(snapshot)==ReadResult::Closed && snapshot.count==4);
    assert(!snapshot.coverage.complete && snapshot.coverage.dropped==3);
    const auto index=static_cast<uint32_t>(BudgetCause::WindowQuotaSlots);
    assert(snapshot.coverage.budgetRefused[index]==3 && snapshot.coverage.budgetRefusalExact[index]);
    for(uint32_t i=0;i<snapshot.count;i++)assert(!snapshot.candidates[i].mayAdmit);
    std::cout<<"PASS: actual native worker exports three precise quota refusals and remains incomplete after close\n";
}
