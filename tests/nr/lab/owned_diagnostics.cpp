#include "../../../tools/nr/lab/OwnedD3D12.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "../../../tools/nr/lab/OwnedDiagnosticsValidation.h"
#include <d3d12.h>
#include <wrl/client.h>
#include "../../../tools/nr/lab/OwnedDiagnosticsCapture.h"
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) throw std::runtime_error("fresh output required");
        std::filesystem::path root(argv[1]);
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("fresh output required");
        for (bool cross : {false, true}) {
            auto dir = root / (cross ? L"cross" : L"same"); std::filesystem::create_directory(dir);
            auto r = lab::RunOwnedD3D12({{"adapter", "warp"}, {"adapter_luid", nullptr}}, dir/L"owned", cross, true);
            std::ofstream(root / (cross ? L"cross.json" : L"same.json")) << r.dump(2);
            if (!r.at("success").get<bool>()) throw std::runtime_error(r.dump());
            if (!r.contains("diagnostics")) throw std::runtime_error("missing runtime resource and validation diagnostics");
            std::cout<<lab::ValidateOwnedDiagnostics(r,dir)<<'\n';
            size_t rejected=0;
            auto reject=[&](lab::json bad) { bool caught=false;try{lab::ValidateOwnedDiagnostics(bad,dir);}catch(const std::exception&){caught=true;}
                if(!caught)throw std::runtime_error("diagnostic evidence mutant accepted");++rejected; };
            auto bad=r;bad["diagnostics"]["resources"][0]["description"]["width"]=1;reject(bad);
            bad=r;bad["diagnostics"]["resources"][0]["owner_device"]="different";reject(bad);
            bad=r;bad["diagnostics"]["resources"][1]["requested_debug_name"]="";reject(bad);
            bad=r;bad["diagnostics"]["resources"][1]["set_name_hresult"]=-1;reject(bad);
            bad=r;bad["diagnostics"]["resources"][1]["caller_initial_state"]=0;reject(bad);
            bad=r;bad["diagnostics"]["barriers"][0]["resource"]=r["resources"]["decoy"];reject(bad);
            bad=r;bad["diagnostics"]["barriers"][0]["after"]=0;reject(bad);
            bad=r;bad["diagnostics"]["configuration"]["configured_before_device"]=false;reject(bad);
            bad=r;bad["diagnostics"]["state_provenance"]="GPU_OBSERVED";reject(bad);
            bad=r;bad["diagnostics"]["unexpected"]=true;reject(bad);
            bad=r;bad["diagnostics"]["validation"]["message_limit"]=100000;reject(bad);
            if(r["diagnostics"]["validation"]["available"].get<bool>()) {
                bad=r;bad["diagnostics"]["validation"]["captured"]=100000;reject(bad);
                bad=r;bad["diagnostics"]["validation"]["error_count"]=999;reject(bad);
                bad=r;bad["diagnostics"]["validation"]["complete"]=!bad["diagnostics"]["validation"]["complete"].get<bool>();reject(bad);
                // Synthetic metadata only: a known error must stay visible even
                // when other messages were lost. No GPU error is induced.
                auto withError=r;auto& v=withError["diagnostics"]["validation"];
                v["messages"].push_back({{"index",v["visible"]},{"id",0},{"severity",1},{"category",0},{"description","synthetic error"},{"description_bytes",15}});
                for(auto name:{"stored","visible","captured","error_count"})v[name]=v[name].get<uint64_t>()+1;
                v["captured_storage_bytes"]=v["captured_storage_bytes"].get<uint64_t>()+64;
                v["discarded"]=v["discarded"].get<uint64_t>()+1;v["complete"]=false;
                if(lab::ValidateOwnedDiagnostics(withError,dir)!="VALIDATION_ERRORS")throw std::runtime_error("known validation error hidden by incomplete capture");
            }
            // Real unavailable/debug-loss states must remain valid evidence of a
            // missing prerequisite; they must never be labelled a clean capture.
            std::cout<<rejected<<" diagnostic mutants rejected\n";
        }
        std::cout << "PASS: owned diagnostics same/cross WARP\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
