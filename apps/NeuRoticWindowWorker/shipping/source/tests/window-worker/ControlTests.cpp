#include "../../apps/NeuRoticWindowWorker/control/ControlCore.h"
#include "../../apps/NeuRoticWindowWorker/control/FrameTimings.h"
#include "../../apps/NeuRoticWindowWorker/control/DiagnosticTrace.h"
#include "../../apps/NeuRoticWindowWorker/control/PerformanceRecord.h"
#include "../../apps/NeuRoticWindowWorker/capture/ComparisonBands.h"
#include <fstream>
#include <iostream>
#include <set>
#include <vector>
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
        check(rejects(R"({"command":"status","modelUpdateContext":{}})"),"explicit update context forbidden on startup/status");
        check(rejects(R"({"command":"import-model","revision":1,"path":"chosen.dll","modelUpdateContext":{"persistedModelPath":7,"managedModelsRoot":"C:/Models"}})"),"configured update requires actual path strings");
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
        check(invalid.valid && invalid.bytes==3 && invalid.sha256==digest,"matching filename recognized regardless of content");
        auto imported=ImportModel(file.wstring(),(fixture/L"models").wstring());
        check(imported.valid && HashFile(imported.path,reason)==digest,"matching filename imports with exact bytes");
        check(std::filesystem::file_size(file)==3,"import preserves original");
        check(!VerifyModel((fixture/L"missing.dll").wstring()).valid,"missing model rejected");
        auto renamed=fixture/L"user-selected-\u6d4b\u8bd5.DLL";
        {std::ofstream f(renamed,std::ios::binary);f<<"abcd";}
        const auto selectedDigest=HashFile(renamed.wstring(),reason);
        check(VerifyModel(renamed.wstring(),true).valid && !VerifyModel(renamed.wstring()).valid,"any Unicode DLL source projects to canonical runtime lookup");
        ExplicitModelUpdate managed{imported.path,(fixture/L"models").wstring()};
        check(!ImportModel(renamed.wstring(),(fixture/L"models").wstring()).valid,"editable journal alone never authorizes managed replacement");
        auto updated=ImportModel(renamed.wstring(),(fixture/L"models").wstring(),managed);
        check(updated.valid && updated.sha256==selectedDigest,"explicit saved managed destination updates in place");
        auto same=ImportModel(renamed.wstring(),(fixture/L"models").wstring());
        check(same.valid && same.fileId==updated.fileId,"identical actual bytes reuse without adoption");
        check(HashFile(renamed.wstring(),reason)==selectedDigest,"selection never changes source bytes");
        std::vector<std::filesystem::path> updateRoots;
        auto targetFor=[&](const wchar_t* name) {
            auto root=fixture/name;std::filesystem::create_directories(root);updateRoots.push_back(root);
            {std::ofstream f(root/L"nvngx_dlssnr.dll",std::ios::binary);f<<"old";}
            return ExplicitModelUpdate{(root/L"nvngx_dlssnr.dll").wstring(),root.wstring()};
        };
        auto plain=targetFor(L"legacy-models");const auto oldDigest=HashFile(plain.persistedModelPath,reason);
        check(!ImportModel(renamed.wstring(),plain.managedModelsRoot).valid,"startup/default import never adopts receiptless target");
        auto wrong=plain;wrong.persistedModelPath=(fixture/L"elsewhere/nvngx_dlssnr.dll").wstring();
        check(!ImportModel(renamed.wstring(),plain.managedModelsRoot,wrong).valid,"unrelated saved path cannot authorize adoption");
        wrong=plain;wrong.managedModelsRoot=(fixture/L"models").wstring();
        check(!ImportModel(renamed.wstring(),plain.managedModelsRoot,wrong).valid,"configured managed root must equal resolved transaction root");
        HANDLE consumer=CreateFileW(plain.persistedModelPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(consumer!=INVALID_HANDLE_VALUE,"open actual consumer read lease");
        auto busy=ImportModel(renamed.wstring(),plain.managedModelsRoot,plain);CloseHandle(consumer);
        check(!busy.valid && busy.reason.find("Close NR sessions")!=std::string::npos && HashFile(plain.persistedModelPath,reason)==oldDigest,"exclusive update refuses active consumers without changing bytes");
        auto adopted=ImportModel(renamed.wstring(),plain.managedModelsRoot,plain);
        check(adopted.valid && adopted.sha256==selectedDigest,"explicit configured legacy destination updates and records only after commit");
        check(std::filesystem::exists(std::filesystem::path(plain.managedModelsRoot)/L".neurotic-model-owner.json"),"successful explicit action recorded");
        // Fully matching same-root forged metadata is still no overwrite authority.
        auto forged=targetFor(L"forged-models");auto forgedRoot=std::filesystem::path(forged.managedModelsRoot);
        auto foreign=VerifyModel(forged.persistedModelPath);BY_HANDLE_FILE_INFORMATION directoryInfo{},fileInfo{};
        HANDLE dir=CreateFileW(forgedRoot.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        HANDLE foreignFile=CreateFileW(forged.persistedModelPath.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(dir!=INVALID_HANDLE_VALUE && foreignFile!=INVALID_HANDLE_VALUE && GetFileInformationByHandle(dir,&directoryInfo) && GetFileInformationByHandle(foreignFile,&fileInfo),"identify exact forged receipt fixture");CloseHandle(dir);CloseHandle(foreignFile);
        Json forgedReceipt={{"owner","NeuRoticWindowWorker/ManagedModel/1"},
            {"directory",{{"volume",directoryInfo.dwVolumeSerialNumber},{"file",(uint64_t(directoryInfo.nFileIndexHigh)<<32)|directoryInfo.nFileIndexLow}}},
            {"files",Json::array({{{"volume",fileInfo.dwVolumeSerialNumber},{"file",foreign.fileId},{"bytes",foreign.bytes},{"sha256",foreign.sha256}}})}};
        for(int shape=0;shape<4;++shape) {
            auto receipt=forgedReceipt;
            if(shape==1)receipt["directory"]["file"]=0;
            if(shape==2)receipt["files"][0]["file"]="not an integer";
            const auto text=shape==3?std::string("{"):receipt.dump();
            {std::ofstream f(forgedRoot/L".neurotic-model-owner.json",std::ios::binary);f<<text;}
            auto denied=ImportModel(renamed.wstring(),forged.managedModelsRoot);
            check(!denied.valid && HashFile(forged.persistedModelPath,reason)==oldDigest,"forged/copied/malformed/numerically invalid receipts never authorize foreign overwrite");
        }
#ifdef NRW_IMPORT_TEST_HOOKS
        static std::filesystem::path faultTarget,faultRoot,faultSource;static int faultMode=0;
        static bool oldWriteDenied=false,newWriteDenied=false,sourceWriteDenied=false,concurrentImportDenied=false;
        auto installFault=[&](int mode,const ExplicitModelUpdate& context) {
            faultMode=mode;faultTarget=context.persistedModelPath;faultRoot=context.managedModelsRoot;faultSource=renamed;
            oldWriteDenied=newWriteDenied=sourceWriteDenied=concurrentImportDenied=false;
            detail::SetImportCheckpointHook(+[](detail::ImportCheckpoint point){
                auto deniedWrite=[](const std::filesystem::path& p) {
                    HANDLE file=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                    bool denied=file==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION;
                    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);return denied;
                };
                if(point==detail::ImportCheckpoint::Staged) {
                    sourceWriteDenied=deniedWrite(faultSource);
                    HANDLE second=CreateFileW((faultRoot/L".neurotic-model-import.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
                    concurrentImportDenied=second==INVALID_HANDLE_VALUE && GetLastError()==ERROR_SHARING_VIOLATION;if(second!=INVALID_HANDLE_VALUE)CloseHandle(second);
                }
                if(point==detail::ImportCheckpoint::BeforeTargetAcquire && faultMode==1){std::ofstream f(faultTarget,std::ios::binary);f<<"foreign";}
                if(point==detail::ImportCheckpoint::BeforeOldMove)oldWriteDenied=deniedWrite(faultTarget);
                if(point==detail::ImportCheckpoint::BeforeNewPublish && faultMode==3){std::ofstream f(faultTarget,std::ios::binary);f<<"foreign";}
                if(point==detail::ImportCheckpoint::ModelPublished && (faultMode==4||faultMode==6||faultMode==7))throw std::runtime_error("injected model publication failure");
                if(point==detail::ImportCheckpoint::BeforeRollbackRemoveNew)newWriteDenied=deniedWrite(faultTarget);
                if(point==detail::ImportCheckpoint::BeforeRestoreOld && faultMode==4){std::ofstream f(faultTarget,std::ios::binary);f<<"foreign";}
                if(point==detail::ImportCheckpoint::BeforeRestoreOld && faultMode==6)throw std::runtime_error("injected old restore failure");
                if(point==detail::ImportCheckpoint::BeforeRetirePrevious && faultMode==5)throw std::runtime_error("injected previous retirement failure");
                if(point==detail::ImportCheckpoint::OwnershipPublished && faultMode==8)throw std::runtime_error("injected action record failure");
            });
        };
        const auto foreignSource=fixture/L"foreign-reference.dll";{std::ofstream f(foreignSource,std::ios::binary);f<<"foreign";}
        const auto foreignDigest=HashFile(foreignSource.wstring(),reason);
        auto changedBeforeAcquire=targetFor(L"changed-before-acquire");installFault(1,changedBeforeAcquire);
        auto changed=ImportModel(renamed.wstring(),changedBeforeAcquire.managedModelsRoot,changedBeforeAcquire);detail::SetImportCheckpointHook(nullptr);
        check(!changed.valid && HashFile(changedBeforeAcquire.persistedModelPath,reason)==foreignDigest,"reopen revalidation preserves bytes changed in acquisition window");
        check(!std::filesystem::exists(std::filesystem::path(changedBeforeAcquire.managedModelsRoot)/L".neurotic-model-recovery.json"),"failed exclusive revalidation creates no recovery transaction");
        auto leases=targetFor(L"exclusive-leases");installFault(2,leases);
        auto leased=ImportModel(renamed.wstring(),leases.managedModelsRoot,leases);detail::SetImportCheckpointHook(nullptr);
        check(leased.valid && oldWriteDenied && sourceWriteDenied && concurrentImportDenied,"old object/source/cooperating import leases remain exclusive through exact move");
        auto listNames=[](const std::filesystem::path& root) {std::set<std::wstring> names;for(const auto& entry:std::filesystem::directory_iterator(root))names.insert(entry.path().filename().wstring());return names;};
        for(int mode:{3,4,5,6}) {
            auto context=targetFor((L"recovery-case-"+std::to_wstring(mode)).c_str());installFault(mode,context);
            auto outcome=ImportModel(renamed.wstring(),context.managedModelsRoot,context);detail::SetImportCheckpointHook(nullptr);
            check(outcome.valid==(mode==5),"publication/rollback fail, retirement warning keeps committed selection");
            auto root=std::filesystem::path(context.managedModelsRoot);std::ifstream recoveryFile(root/L".neurotic-model-recovery.json");auto recoveryJson=Json::parse(recoveryFile);
            auto retained=root/Wide(recoveryJson["previous"]["rollbackLeaf"].get<std::string>());
            check(HashFile(retained.wstring(),reason)==oldDigest,"exact old bytes remain at tracked recovery leaf");
            if(mode==3||mode==4)check(HashFile(context.persistedModelPath,reason)==foreignDigest,"foreign canonical survives publish and restore collisions");
            if(mode==4)check(newWriteDenied,"new object remains write-protected during rollback withdrawal");
            check(!VerifyModel(context.persistedModelPath).valid,"unresolved recovery cannot silently start NR");
            const auto before=listNames(root);
            const auto recordDigest=HashFile((root/L".neurotic-model-recovery.json").wstring(),reason);
            auto retry=ImportModel(renamed.wstring(),context.managedModelsRoot,context);
            check(!retry.valid && before==listNames(root) && recordDigest==HashFile((root/L".neurotic-model-recovery.json").wstring(),reason),"unresolved recovery blocks retry before new files or journal replacement");
        }
        for(int mode:{7,8}) {
            auto context=targetFor((L"restored-case-"+std::to_wstring(mode)).c_str());const auto before=VerifyModel(context.persistedModelPath);
            installFault(mode,context);auto restored=ImportModel(renamed.wstring(),context.managedModelsRoot,context);detail::SetImportCheckpointHook(nullptr);
            const auto after=VerifyModel(context.persistedModelPath);
            check(!restored.valid && after.valid && after.fileId==before.fileId && after.sha256==oldDigest,"rollback restores original exact object and bytes");
            auto root=std::filesystem::path(context.managedModelsRoot);
            check(!std::filesystem::exists(root/L".neurotic-model-owner.json") && !std::filesystem::exists(root/L".neurotic-model-recovery.json"),"failed first adoption leaves no claimed action or recovery record after full restoration");
            for(const auto& entry:std::filesystem::directory_iterator(root))check(entry.path().extension()!=L".tmp","completed rollback leaves no staged copies");
        }
        std::filesystem::remove(foreignSource);
#endif
        auto text=fixture/L"not-a-dll.txt";{std::ofstream f(text);f<<"abc";}
        check(!VerifyModel(text.wstring(),true).valid,"non-DLL selected extension refused");std::filesystem::remove(text);std::filesystem::remove(renamed);
        // Each root is a direct task-created directory; do not recurse or follow links.
        for(const auto& root:updateRoots) {
            for(const auto& entry:std::filesystem::directory_iterator(root)){check(entry.is_regular_file()&&!entry.is_symlink(),"owned direct fixture cleanup stays within ordinary files");std::filesystem::remove(entry.path());}
            std::filesystem::remove(root);
        }
        std::filesystem::remove(fixture/L"models/.neurotic-model-owner.json");

        std::filesystem::remove(fixture/L"models/nvngx_dlssnr.dll"); std::filesystem::remove(fixture/L"models/README.txt"); std::filesystem::remove(fixture/L"models");
        std::filesystem::remove(file); std::filesystem::remove(fixture);
        std::cout<<"PASS control "<<assertions<<" assertions\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL control after "<<assertions<<" assertions: "<<e.what()<<"\n";return 1; }
}
}
#ifdef NRW_CONTROL_TEST_MAIN
int main(){return nrw::RunControlTests();}
#endif
