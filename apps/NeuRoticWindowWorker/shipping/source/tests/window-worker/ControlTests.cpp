#include "../../apps/NeuRoticWindowWorker/control/ControlCore.h"
#include "../../apps/NeuRoticWindowWorker/control/FrameTimings.h"
#include "../../apps/NeuRoticWindowWorker/control/DiagnosticTrace.h"
#include "../../apps/NeuRoticWindowWorker/control/PerformanceRecord.h"
#include "../../apps/NeuRoticWindowWorker/capture/ComparisonBands.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace nrw {
int RunControlTests() {
    int assertions=0;
    auto check=[&](bool ok,const char* name) { ++assertions; if(!ok) throw std::runtime_error(name); };
    auto rejects=[&](const std::string& s) { try { ParseCommand(s); return false; } catch(...) { return true; } };
    try {
        check(rejects(R"({"command":"start","revision":1,"depthProvider":"dav2","depthProfile":"reference","depthHz":30,"depthPreview":true})"),"unready depth preview is unavailable in shipping worker");
        check(rejects(R"({"command":"start","revision":1,"depthProvider":"dav2"})"),"unready depth inference is unavailable even without preview");
        check(!rejects(R"({"command":"start","revision":1,"depthProvider":"off","depthPreview":false})"),"legacy explicit depth-off settings retain image-only NR");
        check(rejects(R"({"command":"start","revision":1,"depthProvider":"cpu"})"),"unsupported depth provider refuses");
        check(rejects(R"({"command":"start","revision":1,"depthProvider":"dav2","depthProfile":"dynamic"})"),"unsupported depth profile refuses");
        check(rejects(R"({"command":"start","revision":1,"depthProvider":"dav2","depthHz":31})"),"unsupported depth cadence refuses");
        check(rejects(R"({"command":"start","revision":1,"depthPreview":true})"),"depth preview requires explicit selected estimator");
        check(rejects(R"({"command":"start","revision":1,"depthProvider":"dav2","guides":"experimental"})"),"unqualified NR adapter cannot be selected implicitly");
        check(!rejects(R"({"command":"set-processing","revision":4,"nrScalePercent":67,"modelStyle":1,"transferStrength":0.4,"colourStrength":0.7})"),"live processing command is admitted with a complete bounded configuration");
        const auto live=ParseCommand(R"({"command":"set-processing","revision":4,"nrScalePercent":67,"modelStyle":1,"transferStrength":0.4,"colourStrength":0.7})");
        for(const auto* key:{"nrScalePercent","modelStyle","transferStrength","colourStrength"}){auto bad=live;bad.erase(key);check(rejects(bad.dump()),"partial processing configuration refused");}
        for(const auto* key:{"window","workWidth","workHeight","modelPath","guides"}){auto bad=live;bad[key]=1;check(rejects(bad.dump()),"live processing cannot retarget or replace provider/source contracts");}
        for(int value:{24,101}){auto bad=live;bad["nrScalePercent"]=value;check(rejects(bad.dump()),"live scale bounded");}
        for(int direction=0;direction<8;++direction)check(!rejects(Json{{"command","set-comparison"},{"revision",2},{"split",.5},{"comparisonDirection",direction}}.dump()),"all eight comparison directions admitted");
        check(rejects(R"({"command":"set-comparison","revision":2,"split":0.5,"comparisonDirection":8})"),"unsupported comparison direction refused");
        check(!rejects(R"({"command":"snapshot","revision":1})"),"snapshot command has a revision");
        check(!rejects(R"({"command":"measure-frame","requestId":1})"),"one-frame measurement has its own identity without a render revision");
        for(const auto& bad:std::initializer_list<Json>{Json{{"command","measure-frame"}},Json{{"command","measure-frame"},{"requestId",0}},Json{{"command","measure-frame"},{"requestId",-1}},Json{{"command","measure-frame"},{"requestId",false}},Json{{"command","measure-frame"},{"requestId",1.5}},Json{{"command","measure-frame"},{"requestId","1"}},Json{{"command","measure-frame"},{"requestId",1},{"revision",9}},Json{{"command","measure-frame"},{"requestId",1},{"transferStrength",0.0}}})
            check(rejects(bad.dump()),"one-frame measurement refuses malformed identity or rendering adjustments");
        check(rejects(R"({"command":"snapshot"})"),"snapshot requires a revision");
        check(!rejects(R"({"command":"set-comparison","revision":2,"split":0.5,"stripes":6})"),"stripes accepted");
        check(rejects(R"({"command":"set-comparison","revision":2,"split":0.5,"stripes":33})"),"stripes bounded");
        check(rejects(R"({"command":"start","revision":3,"modelStyle":3})"),"unsupported model style refused");
        auto bands=ComparisonBands(13,.5f,6);
        check(bands.size()==3&&bands[0].first==0&&bands[0].second==2&&bands[2].second==10,"odd width stripes use exact proportional boundaries");
        check(ComparisonBands(13,0,0).empty()&&ComparisonBands(13,1,0)[0].second==13,"comparison endpoints");
        for(auto [left,right]:ComparisonBands(2,.5f,32))check(left<right&&right<=2,"narrow source never copies empty or outside bands");
        FrameTimings timings;
        check(timings.Count()==0 && timings.Fps()==0,"empty timing window has no invented frame rate");
        for(unsigned n=0;n<140;++n)timings.Add({double(n)*20,3,2,8,1,14,18,true});
        check(timings.Count()==120 && timings.Fps()==50,"bounded timing window derives completed-frame cadence");
        check(timings.P95Age()==18,"capture age percentile excludes unrelated stage clock");
        timings.Add({-1,0,0,0,0,0,0,true});check(timings.Count()==120&&timings.Fps()==50,"invalid clock cannot contaminate timing window");
        timings.Clear();check(timings.Count()==0 && timings.P95Age()==0,"new session clears old timing evidence");
        // PERF-01: incomplete/device-lost timestamps stay unknown, clocks never mix.
        check(!TimestampSpan(0,40000,1000000,7,8),"incomplete timing is unavailable without waiting");
        check(!TimestampSpan(0,40000,1000000,UINT64_MAX,8),"device removal is not completion");
        check(!TimestampSpan(10,9,1000,8,8) && !TimestampSpan(0,1,0,8,8),"invalid spans remain unknown");
        check(TimestampSpan(0,40000,1000000,8,8)==40 && TimestampSpan(0,40000,2000000,8,8)==20,"each queue uses its own frequency");
        timings.Add({10,1,1,1,1,4,4,true});timings.Add({30,1,1,1,1,4,4,true});
        timings.Pause();check(timings.Count()==2&&timings.Fps()==50,"pause retains last active evidence");
        timings.Add({10000,1,1,1,1,4,4,true});check(timings.Count()==1&&timings.Fps()==0,"resume excludes idle gap from active cadence");
        TraceQueue trace;
        for(int i=0;i<64;++i)check(trace.Push(std::to_string(i)),"bounded trace admits capacity");
        for(int i=0;i<1000;++i)check(!trace.Push("stalled"),"stalled sink never blocks producer");
        check(trace.dropped==1000,"saturated trace counts exact lost metadata");
        for(int i=0;i<64;++i){std::string line;check(trace.Pop(line)&&line==std::to_string(i),"trace retains FIFO identity");}
        check(!trace.Push(std::string(8193,'x')),"oversized trace record cannot grow queue budget");
        CapturedFrame timingFrame;timingFrame.stamp={1,8,1234,1920,1080,false};
        NrResult measured;measured.workWidth=1920;measured.workHeight=1080;
        measured.submitted=true;measured.measurements.evaluateCallMs=1;
        auto record=PerformanceRecord(timingFrame,measured,2,"",3,0,{},{},false);
        check(record["cpu"]["evaluate_call_ms"]==1 && record["gpu"]["nr_ms"].is_null() && !record["nr_gpu_completed"].get<bool>(),"CPU return never fabricates GPU duration/completion");
        measured.measurements.gpuCompleted=true;measured.measurements.gpu[2]=40;measured.completed=true;
        record=PerformanceRecord(timingFrame,measured,2,"",3,0,1,45,true);
        check(record["cpu"]["evaluate_call_ms"]==1 && record["gpu"]["nr_ms"]==40,"CPU evaluate and GPU NR stay separate");
        measured.measurements.guidePreparation="gpu-clear";measured.measurements.gpu[0]=0.2;
        record=PerformanceRecord(timingFrame,measured,2,"",3,0,1,45,true);
        check(record["host"]["guide_upload_bytes"]==0 && record["host"]["gpu_guide_upload_ms"].is_null() && record["host"]["gpu_guide_prepare_ms"]==0.2,"GPU clear reports zero upload bytes and separate preparation timing");
        record["synthetic"]=true;std::cout<<"PERFORMANCE_RECORD "<<record.dump()<<"\n";
        check(record["media_pts_100ns"].is_null()&&record["observed_display_latency_ms"].is_null()&&record["content_id"].is_null(),"capture clock is neither media PTS nor unique content nor scanout");
        SelectionState selection; std::string reason;
        check(!selection.Countdown(1,5,1000,false,reason), "model prerequisite blocks selection");
        check(reason=="Please provide your DLSS NR file.", "model prerequisite CTA");
        check(selection.Countdown(1,5,1000,true,reason),"countdown starts with valid model");
        check(selection.Pending() && selection.Remaining(1001)==4999,"countdown remaining");
        check(!selection.Due(5999),"no early foreground sampling");
        check(selection.Due(6000) && !selection.Due(6001),"one expiry samples once");
        check(!selection.Request(1,reason),"stale revision rejected");
        check(selection.Countdown(2,5,7000,true,reason),"next revision accepted");
        selection.Cancel(); check(!selection.Due(50000),"canceled countdown cannot engage");
        check(!selection.Countdown(3,0,0,true,reason),"zero delay invalid");
        check(!selection.Countdown(3,31,0,true,reason),"unbounded delay invalid");
        check(rejects("[]"),"object required");
        check(rejects("{\"command\":\"start\",\"revision\":-1}"),"negative revision rejected");
        check(rejects("{\"command\":\"start\",\"revision\":1.5}"),"fractional revision rejected");
        check(rejects("{\"command\":\"start\",\"revision\":0}"),"zero revision rejected");
        check(rejects("{\"command\":\"erase-game\",\"revision\":1}"),"unknown command rejected");
        check(rejects(std::string(MaxCommandBytes+1,' ')),"oversize rejected before parse");
        check(ParseCommand("{\"command\":\"status\"}")["command"]=="status","read-only status accepted");
        check(rejects("{\"command\":\"start\",\"revision\":1,\"outputMode\":\"injected\"}"),"unsupported output mode rejected");
        check(Utf8(Wide("NeuRotic \xe6\xb5\x8b\xe8\xaf\x95"))=="NeuRotic \xe6\xb5\x8b\xe8\xaf\x95","Unicode path roundtrip");
        WindowIdentity w; w.hwnd=1234;w.pid=7;w.processCreation=123456789012345ull;w.width=1920;w.height=1080;w.title=L"fixture";
        auto parsed=ParseWindow(WindowJson(w));
        check(parsed.processCreation==w.processCreation && parsed.hwnd==w.hwnd,"full window identity roundtrip");
        auto fixture=std::filesystem::absolute(L"builds/window-worker/control-fixture");
        std::filesystem::create_directories(fixture);
        auto file=fixture/L"nvngx_dlssnr.dll";
        {std::ofstream f(file,std::ios::binary);f<<"abc";}
        auto digest=HashFile(file.wstring(),reason);
        check(digest=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","real streaming SHA256");
        auto invalid=VerifyModel(file.wstring());
        check(!invalid.valid && invalid.bytes==3 && !invalid.reason.empty(),"named impostor rejected");
        auto imported=ImportModel(file.wstring(),(fixture/L"models").wstring());
        check(!imported.valid && !std::filesystem::exists(fixture/L"models"),"invalid import leaves destination untouched");
        check(std::filesystem::file_size(file)==3,"import preserves original");
        check(!VerifyModel((fixture/L"missing.dll").wstring()).valid,"missing model rejected");
        std::filesystem::remove(file); std::filesystem::remove(fixture);
        std::cout<<"PASS control "<<assertions<<" assertions\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL control after "<<assertions<<" assertions: "<<e.what()<<"\n";return 1; }
}
}
#ifdef NRW_CONTROL_TEST_MAIN
int main(){return nrw::RunControlTests();}
#endif
