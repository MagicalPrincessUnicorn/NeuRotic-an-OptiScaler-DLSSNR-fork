// Compile with the extractor's output directory on the include path.
// No GPU/device/provider is constructed. Production try/restore flow executes.
#include <nr/d3d12/NativeRecordingState.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Neurotic::D3D12;
namespace P { static std::string Symbol(const char* value) { return value; } }
struct Result { std::string reason; };
struct Callback { bool current=true; bool Current() const { return current; } };
struct Host
{
    bool throwPrepare=false, throwRun=false;
    std::vector<NativeRecordingPhase> phases;
    void PrepareRuntime(Callback&, int)
    {
        phases.push_back(NativeRecordingPhaseScope::Current());
        if(throwPrepare) throw std::runtime_error("prepare");
    }
    Result RunNativeBefore(Callback&, int, int)
    {
        phases.push_back(NativeRecordingPhaseScope::Current());
        if(throwRun) throw std::runtime_error("run");
        return {};
    }
};
struct Restoration
{
    unsigned calls=0;
    NativeRecordingPhase phase=NativeRecordingPhase::Reset;
    bool Restore() { ++calls; phase=NativeRecordingPhaseScope::Current(); return true; }
};
static Result Execute(Host* host, Callback& callback, Restoration& restore)
{
    Result result;
    const int settings=0, parameters=0;
#include "native_phase_envelope.inc"
    if(!restored) throw std::runtime_error("restoration failed");
    return result;
}
int main()
{
    unsigned checks=0, failures=0;
    const auto test=[&](const char* name,bool throwPrepare,bool throwRun,bool current,NativeRecordingPhase outer) {
        ++checks;
        try {
            Host host;host.throwPrepare=throwPrepare;host.throwRun=throwRun;
            Callback callback;callback.current=current;Restoration restore;
            NativeRecordingPhaseScope enclosing(outer);
            const auto result=Execute(&host,callback,restore);
            const std::vector<NativeRecordingPhase> expected(throwPrepare||!current?1:2,NativeRecordingPhase::NeuralRendering);
            if(host.phases!=expected || restore.calls!=1 || restore.phase!=outer || NativeRecordingPhaseScope::Current()!=outer)
                throw std::runtime_error("native phase must cover preparation/evaluation and end before restore");
            const auto expectedReason=throwPrepare||throwRun?"Native.InvocationOwnerFailure":!current?"Native.ScopeUnavailable":"";
            if(result.reason!=expectedReason) throw std::runtime_error("failure semantics changed");
            std::cout<<"PASS "<<name<<'\n';
        } catch(const std::exception& error) {++failures;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    test("native production envelope normal",false,false,true,NativeRecordingPhase::Caller);
    test("native production envelope prepare exception",true,false,true,NativeRecordingPhase::Caller);
    test("native production envelope evaluation exception",false,true,true,NativeRecordingPhase::Caller);
    test("native production envelope reentry restores upstream phase",false,false,true,NativeRecordingPhase::UpstreamEvaluation);
    test("native production envelope unavailable callback",false,false,false,NativeRecordingPhase::Caller);
    std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
