#define P1_STAGE_PROBE_TEST
#include "../../../tools/nr/lab/P1StageProbe.cpp"

int main() {
    using lab::json;
    unsigned checks=0;
    const auto expect=[&](bool value){++checks;if(!value)throw std::runtime_error("WORKER_TEST_FAILED");};
    const auto refused=[&](const json& value){try{stage_probe::ValidateRequest(value);return false;}catch(const std::exception&){return true;}};
    json request={{"schema_version",1},{"record_kind","P1_STAGE_PROBE_REQUEST"},{"mode","create-only"},{"backend","warp"},{"nonce","{a26da8b3-3c95-4b54-88f1-b13fc1ac871a}"},{"package_sha256",std::string(64,'a')},{"runtime_config_sha256",std::string(64,'b')},{"allow_provider",false}};
    stage_probe::ValidateRequest(request);expect(true);
    for(const auto& key:{"schema_version","record_kind","mode","backend","nonce","package_sha256","runtime_config_sha256","allow_provider"}){auto m=request;m.erase(key);expect(refused(m));}
    auto m=request;m["unexpected"]=true;expect(refused(m));
    m=request;m["allow_provider"]=0;expect(refused(m));
    m=request;m["allow_provider"]=true;expect(refused(m));
    m=request;m["schema_version"]=true;expect(refused(m));
    m=request;m["nonce"]="not-a-guid";expect(refused(m));
    m=request;m["package_sha256"]=std::string(64,'A');expect(refused(m));
    m=request;m["mode"]="evaluate-only";expect(refused(m));
    m=request;m["backend"]="provider";expect(refused(m));
    m["allow_provider"]=true;stage_probe::ValidateRequest(m);expect(true);
    expect(stage_probe::CompletionVerified(1,1,WAIT_OBJECT_0,S_OK));
    expect(stage_probe::CompletionVerified(3,3,WAIT_OBJECT_0,S_OK));
    expect(!stage_probe::CompletionVerified(1,UINT64_MAX,WAIT_OBJECT_0,S_OK));
    expect(!stage_probe::CompletionVerified(2,1,WAIT_OBJECT_0,S_OK));
    expect(!stage_probe::CompletionVerified(2,3,WAIT_OBJECT_0,S_OK));
    expect(!stage_probe::CompletionVerified(1,1,WAIT_TIMEOUT,S_OK));
    expect(!stage_probe::CompletionVerified(1,1,WAIT_FAILED,S_OK));
    expect(!stage_probe::CompletionVerified(1,1,WAIT_OBJECT_0,DXGI_ERROR_DEVICE_REMOVED));
    const json selected={{"provider",{{"selection_status","SELECTED_UNVERIFIED"}}},{"ngx_core",{{"selection_status","HASH_VERIFIED"}}}};
    stage_probe::RequireSelectedModules(selected);expect(true);
    for(const auto* role:{"provider","ngx_core"}){
        for(const auto& status:json::array({"UNBOUND","unknown",false,nullptr})){
            auto config=selected;config[role]["selection_status"]=status;bool rejected=false;
            try{stage_probe::RequireSelectedModules(config);}catch(const std::exception&){rejected=true;}expect(rejected);
        }
        auto config=selected;config[role].erase("selection_status");bool rejected=false;
        try{stage_probe::RequireSelectedModules(config);}catch(const std::exception&){rejected=true;}expect(rejected);
    }
    std::cout<<"stage worker CPU checks: "<<checks<<" passed\n";
}
