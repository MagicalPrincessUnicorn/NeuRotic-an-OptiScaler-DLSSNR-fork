#include "../../../tools/nr/lab/LabRegistry.h"
#include <iostream>

int checksMain(int argc,char** argv) {
    using namespace lab;
    if(argc!=3)return 2;
    const fs::path source=argv[1],root=argv[2];
    Require(!fs::exists(root),"FRESH_TEST_ROOT_REQUIRED");fs::create_directories(root/L"package/schemas");
    const auto packageRoot=root/L"package";
    int checks=0;
    auto reject=[&](auto fn){bool refused=false;try{fn();}catch(const std::exception&){refused=true;}Require(refused,"INVALID_INPUT_ACCEPTED");++checks;};
    json files=json::array();
    for(const auto name:{"NeuRoticLab.exe","LabWorker.exe","LabSupervisor.exe","W03-P1.exe","nvngx.dll_dlssnr.dll","CASE_CATALOG.json","SUITE_PRESETS.json","LOCAL_RUNTIME.template.json","SOURCE_BUILD_MANIFEST.json","implemented_cases.json"}) {
        WriteAtomic(packageRoot/name,"test-only package closure input\n");files.push_back({{"path",name},{"sha256",FileHash(packageRoot/name)}});
    }
    for(const auto name:{"case-request.schema.json","case-result.schema.json","run-plan.schema.json","run-summary.schema.json","local-runtime.schema.json","event.schema.json"}) {
        const auto path=packageRoot/L"schemas"/name;
        fs::copy_file(source/L"tools/nr/lab/design/schemas"/name,path);
        files.push_back({{"path",std::string("schemas/")+name},{"sha256",FileHash(path)}});
    }
    WriteJson(packageRoot/L"PACKAGE_MANIFEST.json",{{"schema_version",1},{"build_id","test-fixture"},{"files",files}});
    reject([&]{Package missingCompanion(packageRoot);});
    // Benign names test rejection without opening Windows device paths.
    for(const auto& name:std::vector<std::string>{"file. ","file.","file ","CON","con.txt","COM1.json","lpt9","NUL.txt","cases//receipt.json",std::string("file\0.json",10)})reject([&]{SafeRelative(name);});
    SafeRelative("cases/case-001/result.json");++checks;
    WriteAtomic(packageRoot/L"LabSupervisorControl.exe","test-only companion\n");
    files.push_back({{"path","LabSupervisorControl.exe"},{"sha256",FileHash(packageRoot/L"LabSupervisorControl.exe")}});
    fs::remove(packageRoot/L"PACKAGE_MANIFEST.json");
    WriteJson(packageRoot/L"PACKAGE_MANIFEST.json",{{"schema_version",1},{"build_id","test-fixture"},{"files",files}});
    Package package(packageRoot);++checks;
    auto config=ReadJson(source/L"tools/nr/lab/design/LOCAL_RUNTIME.template.json");
    json r={{"schema_version",1},{"record_kind","LIVE_REQUEST"},{"run_id","binding-test"},{"case_id","NLAB-CTL-001"},{"case_revision",1},{"handler_id","package_closure"},{"variant_id","default"},{"request_nonce","0123456789abcdef"},{"runtime_config_sha256",Sha256(config.dump())},{"package_sha256",package.hash},{"parameters_sha256",Sha256("{}")},{"scene_sha256",nullptr},{"oracle_sha256",FileHash(packageRoot/L"NeuRoticLab.exe")},{"runtime_config_relative_path","../../LOCAL_RUNTIME.json"},{"parameters",json::object()},{"budgets",{{"wall_seconds",180},{"raw_bytes","268435456"},{"caller_allocation_bytes","536870912"}}},{"consent_flags",config["execution_consent"]}};
    ValidateRequest(r,package);++checks;
    for(const auto& field:{"variant_id","scene_sha256","runtime_config_relative_path"}){
        auto changed=r;changed[field]=field==std::string("scene_sha256")?std::string(64,'a'):"unsupported";
        reject([&]{ValidateRequest(changed,package);});
    }
    unsigned trial=0;
    auto createCase=[&](json request,const std::string& runtimeText,bool bindHash=true){
        const auto run=root/("run-"+std::to_string(++trial));fs::create_directories(run/L"cases/fixture-trial");
        WriteAtomic(run/L"LOCAL_RUNTIME.json",runtimeText);if(bindHash)request["runtime_config_sha256"]=Sha256(runtimeText);
        const auto path=run/L"cases/fixture-trial/case-request.json";WriteJson(path,request);return path;
    };
    const auto requestPath=createCase(r,config.dump());
    {
        BoundRequest bound(requestPath,package);++checks;
        Require(bound.request.hash==Sha256(bound.request.text),"REQUEST_BYTES_HASH_MISMATCH");++checks;
        for(const auto& path:{requestPath,bound.runtime->path}){
            WinHandle writer(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr));
            Require(!writer.valid(),"LOCKED_INPUT_REMAINED_WRITABLE");++checks;
        }
    }
    auto mismatch=r;mismatch["consent_flags"]["physical_provider"]=true;
    auto bad=createCase(mismatch,config.dump());reject([&]{BoundRequest b(bad,package);});
    const auto duplicate="{\"schema_version\":1,"+config.dump().substr(1);
    bad=createCase(r,duplicate);reject([&]{BoundRequest b(bad,package);});
    auto invalidConfig=config;invalidConfig["unknown"]=true;
    bad=createCase(r,invalidConfig.dump());reject([&]{BoundRequest b(bad,package);});
    auto changedConfig=config;changedConfig["gpu"]["description_hint"]="changed without updating request hash";
    bad=createCase(r,changedConfig.dump(),false);reject([&]{BoundRequest b(bad,package);});
    auto physical=r;physical["case_id"]="NLAB-OBS-001";physical["handler_id"]="owned_readback";physical["variant_id"]="physical";physical["parameters"]={{"adapter","physical"},{"adapter_luid","00000000:00000001"}};physical["parameters_sha256"]=Sha256(physical["parameters"].dump());physical["consent_flags"]["physical_provider"]=true;
    auto selected=config;selected["execution_consent"]["physical_provider"]=true;selected["gpu"]["selection_status"]="EXPLICITLY_SELECTED";selected["gpu"]["adapter_luid"]="00000000:00000002";
    bad=createCase(physical,selected.dump());reject([&]{BoundRequest b(bad,package);});
    selected["gpu"]["adapter_luid"]="00000000:00000001";
    const auto matched=createCase(physical,selected.dump());{BoundRequest b(matched,package);++checks;}
    physical["parameters"]["adapter"]="warp";physical["parameters_sha256"]=Sha256(physical["parameters"].dump());reject([&]{ValidateRequest(physical,package);});
    std::cout<<"BOUND_REQUEST_PASS checks="<<checks<<"\n";
    return 0;
}
int main(int argc,char** argv){try{return checksMain(argc,argv);}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
