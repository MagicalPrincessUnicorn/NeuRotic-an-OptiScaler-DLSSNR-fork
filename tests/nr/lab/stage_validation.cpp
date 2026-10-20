// Synthetic metadata only. No fixture module is executable; no GPU/provider is loaded.
#if __has_include("../../../tools/nr/lab/StageValidation.h")
#include "../../../tools/nr/lab/StageValidation.h"
#else
#include "../../../tools/nr/lab/LabPlan.h"
namespace lab {
inline json ComputeStageResult(const Package&,const fs::path&) { throw std::runtime_error("STAGE_VALIDATION_NOT_IMPLEMENTED"); }
inline void ValidateStageResult(const Package&,const fs::path&) { throw std::runtime_error("STAGE_VALIDATION_NOT_IMPLEMENTED"); }
}
#endif
#include <iostream>
using namespace lab;
static void Put(const fs::path& p,const std::string& text) { fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::trunc);f<<text;Require(f.good(),"FIXTURE_WRITE"); }
static void PutJson(const fs::path& p,const json& j) { Put(p,j.dump()); }
struct Fixture {
    fs::path run;json request,probe,process,capture,execution;std::vector<json> raw;
    Fixture(const Package& p,const fs::path& path,bool split=true):run(path) {
        fs::create_directories(run/L"worker");
        Put(run/L"LOCAL_RUNTIME.json",ReadText(p.root/L"LOCAL_RUNTIME.template.json"));
        request={{"schema_version",1},{"record_kind","P1_STAGE_PROBE_REQUEST"},{"mode",split?"split-evaluate":"create-only"},{"backend","warp"},{"nonce","{01234567-89ab-cdef-0123-456789abcdef}"},{"package_sha256",p.hash},{"runtime_config_sha256",FileHash(run/L"LOCAL_RUNTIME.json")},{"allow_provider",false}};
        PutJson(run/L"REQUEST.json",request);
        execution={{"schema_version",1},{"supervisor_exit",0},{"controller_error",""}};
        process={{"schema_version",1},{"process_status","exited"},{"child_exit_code",0},{"child_pid",1234},{"timed_out",false},{"child_drained",true},{"stdout_file","runner-stdout.txt"},{"stderr_file","runner-stderr.txt"},{"dump_file",nullptr},{"dialog_suppression","debugger-second-chance-and-inherited-error-mode"},{"unhandled_exception",nullptr}};
        capture={{"schema_version",1},{"owner","W03-P1-Supervisor.cpp"},{"capture_limit_bytes_per_stream",1048576},{"capture_complete",true},{"cancel_requested",false},{"cancel_forced",false},{"cancel_grace_ms",500},{"stdout",{{"bytes_seen",0},{"bytes_retained",0},{"truncated",false}}},{"stderr",{{"bytes_seen",0},{"bytes_retained",0},{"truncated",false}}}};
        Put(run/L"worker/runner-stdout.txt","");Put(run/L"worker/runner-stderr.txt","");
        probe={{"schema_version",1},{"record_kind","P1_STAGE_PROBE_OBSERVATION"},{"request_sha256",FileHash(run/L"REQUEST.json")},{"nonce",request["nonce"]},{"mode",request["mode"]},{"backend","warp"},{"pid",1234},{"status","COMPLETED"},{"error",""},{"stages",json::array()},{"runner_sha256",nullptr}};
        const char* names[]={"initialize","evaluate","readback"};
        std::string pixels;for(int i=0;i<4096;++i)pixels+=std::string("\x00\x34\x00\x38\x00\x3a\x00\x3c",8);
        if(split)Put(run/L"worker/output.rgba16f",pixels);
        for(int i=0;i<(split?3:1);++i) {
            bool rb=i==2;
            probe["stages"].push_back({{"name",names[i]},{"state","COMPLETE"},{"fence_value",std::to_string(i+1)},{"completed_fence",std::to_string(i+1)},{"wait_result",0},{"device_removed_reason",0},{"command_list_closed",true},{"submitted",true},{"fence_signaled",true},{"snapshot","runner-"+std::string(names[i])+".json"},{"snapshot_sha256",nullptr}});
            json r={{"schema_version",3},{"status","P1_STAGE_RUNNING"},{"stage",names[i]},{"error",""},{"process_id",1234},{"receipt_kind","P1_STAGE_PROBE"},{"requested_feature_id",0},{"create_count",0},{"evaluate_count",0},{"created_handle","0x0"},{"evaluated_handle","0x0"},{"parameter_path","core-capability/forwarder-dlssnr-v1"},{"float_slot",-1},
                {"loaded_modules",json::array({{{"role","patched_provider"},{"path",""},{"sha256_at_load",""}},{{"role","ngx_core"},{"path",""},{"sha256_at_load",""}},{{"role","nr_forwarder"},{"path",""},{"sha256_at_load",""}}})},
                {"module_load",{{"provider",false},{"core",false},{"forwarder",false}}},
                {"adapter",{{"name","Microsoft Basic Render Driver"},{"vendor_id",5140},{"device_id",140},{"subsystem_id",0},{"revision",0},{"luid",UINT64_C(9223372036854775810)},{"driver_version_raw",0},{"driver_version_known",false},{"driver_version_source","IDXGIAdapter.CheckInterfaceSupport(IDXGIDevice)"}}},
                {"ngx_results",{{"init",-1},{"get_parameters",-1},{"provider_init",-1},{"create_feature",-1},{"evaluate_feature",-1}}},
                {"retained_baseline",{{"queue_type","DIRECT"},{"queue_count",1},{"queue_address",UINT64_C(9223372036854775820)},{"list_address",200+i},{"allocator_address",300+i},{"feature_address",0},{"parameters_address",0},{"list_closed",true},{"submitted",true},{"fence_signaled",true},{"fence_completed",true},{"readback_observed",rb},{"output_changed_from_zero",rb},{"submitted_fence_value",i+1},{"completed_fence_value",i+1},{"output_fnv1a64",0},{"format","R16G16B16A16_FLOAT"},{"input_extent",{64,64}},{"output_extent",{64,64}}}},
                {"readback",{{"file","output.rgba16f"},{"byte_count",32768},{"sha256",rb?Sha256(pixels):""},{"feature_id",0},{"feature_handle","0x0"},{"classification",rb?"OUTPUT_CHANGED":"NOT_OBSERVED"}}},
                {"debug_layer_enabled",false},{"device_removed_reason",0},{"dred",json::object()},{"phase_markers",json::array()},{"debug_messages",json::array()}};
            raw.push_back(r);
        }
    }
    void Provider(const Package& p) {
        auto config=ReadJson(run/L"LOCAL_RUNTIME.json");config["execution_consent"]["physical_provider"]=true;
        config["provider"]["path"]=PathText(run/L"synthetic-provider.fixture");config["provider"]["selection_status"]="SELECTED_UNVERIFIED";
        config["ngx_core"]["path"]=PathText(run/L"synthetic-core.fixture");config["ngx_core"]["selection_status"]="SELECTED_UNVERIFIED";config["ngx_core"]["expected_sha256"]=std::string(64,'a');
        config["gpu"]["selection_status"]="EXPLICITLY_SELECTED";config["gpu"]["adapter_luid"]="80000000:00000002";
        PutJson(run/L"LOCAL_RUNTIME.json",config);request["backend"]="provider";request["allow_provider"]=true;request["runtime_config_sha256"]=FileHash(run/L"LOCAL_RUNTIME.json");PutJson(run/L"REQUEST.json",request);
        probe["backend"]="provider";probe["request_sha256"]=FileHash(run/L"REQUEST.json");
        for(size_t i=0;i<raw.size();++i){auto& r=raw[i];r["requested_feature_id"]=18;r["create_count"]=1;r["evaluate_count"]=i?1:0;r["created_handle"]="0x1000";r["evaluated_handle"]=i?"0x1000":"0x0";r["float_slot"]=1;
            for(auto key:{"provider","core","forwarder"})r["module_load"][key]=true;
            for(size_t k=0;k<3;++k){r["loaded_modules"][k]["path"]=k==0?config["provider"]["path"]:k==1?config["ngx_core"]["path"]:json(PathText(p.root/L"nvngx.dll_dlssnr.dll"));r["loaded_modules"][k]["sha256_at_load"]=k==0?config["provider"]["expected_sha256"]:k==1?config["ngx_core"]["expected_sha256"]:json(FileHash(p.root/L"nvngx.dll_dlssnr.dll"));}
            r["adapter"]["vendor_id"]=0x10de;r["adapter"]["name"]="NVIDIA GeForce RTX 4080";r["adapter"]["driver_version_known"]=true;r["adapter"]["driver_version_raw"]=UINT64_C(9223372036854775809);
            r["retained_baseline"]["feature_address"]=4096;r["retained_baseline"]["parameters_address"]=123;
            for(auto key:{"init","get_parameters","provider_init","create_feature"})r["ngx_results"][key]=1;r["ngx_results"]["evaluate_feature"]=i?1:-1;
            r["readback"]["feature_id"]=18;r["readback"]["feature_handle"]=i?"0x1000":"0x0";
        }
    }
    void Save() {
        PutJson(run/L"EXECUTION.json",execution);PutJson(run/L"worker/supervisor.json",process);PutJson(run/L"worker/lab-supervision.json",capture);
        for(size_t i=0;i<raw.size();++i){auto name="runner-"+raw[i]["stage"].get<std::string>()+".json";PutJson(run/L"worker"/Wide(name),raw[i]);probe["stages"][i]["snapshot_sha256"]=FileHash(run/L"worker"/Wide(name));}
        auto final=raw.back();final["status"]=probe["status"]=="COMPLETED"?"P1_STAGE_COMPLETE":"P1_STAGE_FAILED";if(probe["status"]=="COMPLETED")final["stage"]="complete";final["error"]=probe["error"];PutJson(run/L"worker/runner.json",final);probe["runner_sha256"]=FileHash(run/L"worker/runner.json");PutJson(run/L"worker/probe.json",probe);
    }
};
int wmain(int argc,wchar_t** argv) {
    try {
        Require(argc==3,"usage: stage_validation <fresh-output> <source-root>");fs::path root=fs::absolute(argv[1]),source=fs::absolute(argv[2]);Require(!fs::exists(root),"FRESH_OUTPUT_REQUIRED");auto pkg=root/L"synthetic-package";fs::create_directories(pkg);
        json manifest={{"schema_version",1},{"build_id","SYNTHETIC_NO_EXECUTION"},{"files",json::array()}};
        for(auto name:{"NeuRoticLab.exe","LabWorker.exe","LabSupervisor.exe","LabSupervisorControl.exe","W03-P1.exe","P1StageProbe.exe","P1Diagnostic.exe","nvngx.dll_dlssnr.dll","CASE_CATALOG.json","SUITE_PRESETS.json","LOCAL_RUNTIME.template.json","schemas/case-request.schema.json","schemas/case-result.schema.json","schemas/run-plan.schema.json","schemas/run-summary.schema.json","schemas/local-runtime.schema.json","schemas/event.schema.json","SOURCE_BUILD_MANIFEST.json","IMPLEMENTED_CASES.json"}) {
            auto original=source/L"tools/nr/lab/design"/Wide(name);Put(pkg/Wide(name),fs::exists(original)?ReadText(original):"synthetic nonexecutable metadata fixture");manifest["files"].push_back({{"path",name},{"sha256",FileHash(pkg/Wide(name))}});
        }
        PutJson(pkg/L"PACKAGE_MANIFEST.json",manifest);Package p(pkg);int count=0;
        auto check=[&](const char* label,const std::function<void(Fixture&)>& mutation,const char* want="EVIDENCE_INVALID",bool split=true){Fixture f(p,root/std::to_string(++count),split);mutation(f);f.Save();auto before=ReadText(f.run/L"worker/runner.json");auto result=ComputeStageResult(p,f.run);Require(result["verdict"]==want,std::string(label)+": "+result.dump());Require(ReadText(f.run/L"worker/runner.json")==before,"RAW_REWRITTEN");WriteJson(f.run/L"RESULT.json",result);ValidateStageResult(p,f.run);std::cout<<"PASS "<<label<<'\n';};
        check("synthetic WARP split",[](auto&){},"CONTROL_PASS");check("synthetic WARP create only",[](auto&){},"CONTROL_PASS",false);
        check("forged completion",[](auto& f){f.probe["stages"][0]["submitted"]=false;});
        check("MAX sentinel",[](auto& f){f.probe["stages"][0]["completed_fence"]="18446744073709551615";});
        check("failed wait cannot complete",[](auto& f){f.probe["stages"][0]["wait_result"]=258;});
        check("device removal cannot complete",[](auto& f){f.probe["stages"][0]["device_removed_reason"]=-2005270523;});
        check("stage reorder",[](auto& f){f.probe["stages"][0]["name"]="evaluate";});
        check("stage extra keys",[](auto& f){f.probe["stages"][0]["invented"]=true;});
        check("boolean fence value",[](auto& f){f.probe["stages"][0]["wait_result"]=false;});
        check("stale nonce",[](auto& f){f.probe["nonce"]="stale";});
        check("raw PID mismatch",[](auto& f){f.raw[1]["process_id"]=1235;});
        check("count mismatch",[](auto& f){f.raw[0]["create_count"]=1;});
        check("handle mismatch",[](auto& f){f.raw[1]["evaluated_handle"]="0x1";});
        check("raw fence mismatch",[](auto& f){f.raw[1]["retained_baseline"]["completed_fence_value"]=1;});
        check("allocator reuse",[](auto& f){f.raw[1]["retained_baseline"]["allocator_address"]=300;});
        check("module loaded in WARP",[](auto& f){f.raw[0]["module_load"]["core"]=true;});
        check("WARP NGX result contradicts no calls",[](auto& f){f.raw[0]["ngx_results"]["init"]=1;});
        check("capture number as boolean",[](auto& f){f.capture["stdout"]["bytes_seen"]=false;});
        check("process PID wrong type",[](auto& f){f.process["child_pid"]=true;});
        check("process drain false",[](auto& f){f.process["child_drained"]=false;});
        check("wrong WARP pixels",[](auto& f){Put(f.run/L"worker/output.rgba16f",std::string(32768,'x'));f.raw.back()["readback"]["sha256"]=FileHash(f.run/L"worker/output.rgba16f");});
        check("controller infrastructure error",[](auto& f){f.execution["controller_error"]="supervisor launch failed";f.execution["supervisor_exit"]=nullptr;},"INFRA_ERROR");
        check("synthetic provider initialization",[&](auto& f){f.Provider(p);},"INITIALIZATION_COMPLETED",false);
        check("synthetic provider evaluation",[&](auto& f){f.Provider(p);},"EVALUATION_READBACK_OBSERVED");
        check("synthetic provider unchanged output",[&](auto& f){f.Provider(p);Put(f.run/L"worker/output.rgba16f",std::string(32768,0));f.raw.back()["readback"]["sha256"]=FileHash(f.run/L"worker/output.rgba16f");f.raw.back()["readback"]["classification"]="OUTPUT_UNCHANGED";f.raw.back()["retained_baseline"]["output_changed_from_zero"]=false;},"INCONCLUSIVE");
        check("provider identity mismatch",[&](auto& f){f.Provider(p);f.raw[0]["loaded_modules"][0]["sha256_at_load"]=std::string(64,'b');});
        check("provider LUID mismatch",[&](auto& f){f.Provider(p);f.raw[0]["adapter"]["luid"]=1;});
        check("failed stage before later stage",[](auto& f){f.probe["stages"][0]["state"]="FAILED";f.raw[0]["status"]="P1_STAGE_FAILED";});
        check("valid failed initialization wait",[](auto& f){f.probe["status"]="FAILED";f.probe["error"]="wait failed";f.process["child_exit_code"]=1;auto& s=f.probe["stages"][0];s["state"]="FAILED";s["wait_result"]=258;s["completed_fence"]="0";f.raw[0]["status"]="P1_STAGE_FAILED";f.raw[0]["error"]="wait failed";f.raw[0]["retained_baseline"]["fence_completed"]=false;f.raw[0]["retained_baseline"]["completed_fence_value"]=0;},"INCONCLUSIVE",false);
        check("readback CPU failure after fence completion",[](auto& f){f.probe["status"]="FAILED";f.probe["error"]="ReadbackMap failed";f.process["child_exit_code"]=1;f.probe["stages"][2]["state"]="FAILED";auto& r=f.raw.back();r["status"]="P1_STAGE_FAILED";r["error"]="ReadbackMap failed";r["readback"]["classification"]="NOT_OBSERVED";r["readback"]["sha256"]="";r["retained_baseline"]["readback_observed"]=false;r["retained_baseline"]["output_changed_from_zero"]=false;fs::remove(f.run/L"worker/output.rgba16f");},"INCONCLUSIVE");
        for(bool cancel:{false,true}) {Fixture f(p,root/std::to_string(++count));f.Save();f.process["process_status"]=cancel?"cancelled":"timeout";f.process["timed_out"]=!cancel;f.process["child_exit_code"]=cancel?130:124;f.capture["cancel_requested"]=cancel;f.capture["cancel_forced"]=cancel;f.execution["supervisor_exit"]=cancel?130:124;PutJson(f.run/L"worker/supervisor.json",f.process);PutJson(f.run/L"worker/lab-supervision.json",f.capture);PutJson(f.run/L"EXECUTION.json",f.execution);fs::remove(f.run/L"worker/probe.json");fs::remove(f.run/L"worker/runner.json");auto result=ComputeStageResult(p,f.run);Require(result["verdict"]==(cancel?"ABORTED":"INCONCLUSIVE"),"INTERRUPTED_MISSING_RECEIPT_INVALID");WriteJson(f.run/L"RESULT.json",result);ValidateStageResult(p,f.run);}
        for(bool native:{false,true}) {Fixture f(p,root/std::to_string(++count));f.Save();f.process["process_status"]=native?"native_exception":"timeout";f.process["timed_out"]=!native;f.process["child_exit_code"]=native?UINT64_C(0xe0425041):124;f.capture["cancel_requested"]=true;f.capture["cancel_forced"]=true;f.execution["supervisor_exit"]=native?3:124;if(native)f.process["unhandled_exception"]={{"code",UINT64_C(0xe0425041)},{"address","0x1234"},{"module_path",nullptr},{"module_rva",nullptr}};PutJson(f.run/L"worker/supervisor.json",f.process);PutJson(f.run/L"worker/lab-supervision.json",f.capture);PutJson(f.run/L"EXECUTION.json",f.execution);fs::remove(f.run/L"worker/probe.json");fs::remove(f.run/L"worker/runner.json");auto result=ComputeStageResult(p,f.run);Require(result["verdict"]=="INCONCLUSIVE",std::string("CANCELLATION_RACE_INVALID:")+(native?"native_exception":"timeout")+result.dump());WriteJson(f.run/L"RESULT.json",result);ValidateStageResult(p,f.run);}
        {Fixture f(p,root/std::to_string(++count));f.Save();Put(f.run/L"worker/runner.json","altered");Require(ComputeStageResult(p,f.run)["verdict"]=="EVIDENCE_INVALID","FINAL_HASH_IGNORED");}
        {Fixture f(p,root/std::to_string(++count));f.Save();fs::remove(f.run/L"worker/runner-initialize.json");Require(ComputeStageResult(p,f.run)["verdict"]=="EVIDENCE_INVALID","SNAPSHOT_LOSS_IGNORED");}
        for(auto change:{"worker/probe.json","worker/runner-initialize.json","worker/runner-stdout.txt"}) {Fixture f(p,root/std::to_string(++count));f.Save();WriteJson(f.run/L"RESULT.json",ComputeStageResult(p,f.run));Put(f.run/Wide(change),"changed artifact");bool rejected=false;try{ValidateStageResult(p,f.run);}catch(...){rejected=true;}Require(rejected,"CHANGED_ARTIFACT_ACCEPTED");}
        {Fixture f(p,root/std::to_string(++count));f.Save();WriteJson(f.run/L"RESULT.json",ComputeStageResult(p,f.run));fs::remove(f.run/L"worker/output.rgba16f");bool rejected=false;try{ValidateStageResult(p,f.run);}catch(...){rejected=true;}Require(rejected,"LOST_ARTIFACT_ACCEPTED");}
        std::cout<<"STAGE_VALIDATION_PASS checks="<<count<<" synthetic metadata only\n";return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
