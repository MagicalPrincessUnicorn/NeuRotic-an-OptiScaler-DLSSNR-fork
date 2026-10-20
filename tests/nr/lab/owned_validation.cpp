#include "../../../tools/nr/lab/LabOwnedValidation.h"
#include <iostream>

int wmain(int argc,wchar_t** argv) {
    try {
        lab::Require(argc==3,"usage: owned_validation fixtures fresh-output");
        const lab::fs::path fixtures=argv[1],root=argv[2];
        lab::Require(!lab::fs::exists(root),"fresh output required");lab::fs::create_directories(root);
        unsigned rejected=0;
        for(bool cross:{false,true}) {
            auto name=cross?L"cross":L"same";auto directory=root/name;
            lab::fs::create_directory(directory);lab::fs::create_directory(directory/L"owned");
            for(auto raw:{L"output.bin",L"decoy.bin",L"preimage.bin"})lab::fs::copy_file(fixtures/name/raw,directory/L"owned"/raw);
            auto observations=lab::ReadJson(fixtures/(std::wstring(name)+L".json"));
            lab::json request={{"case_id",cross?"NLAB-OBS-002":"NLAB-OBS-001"},{"parameters",{{"adapter","warp"},{"adapter_luid",nullptr}}}};
            lab::ValidateOwnedEvidence(request,observations,directory);
            auto reject=[&](const char* label,const lab::json& mutated) {
                bool caught=false;try{lab::ValidateOwnedEvidence(request,mutated,directory);}catch(const std::exception&){caught=true;}
                lab::Require(caught,std::string("accepted mutant: ")+label);++rejected;
            };
            auto bad=observations;bad["journal"].erase(bad["journal"].begin()+(cross?2:1));reject("missing journal event",bad);
            bad=observations;bad["journal"][1]["fence"]=observations["fences"][1]["identity"];reject("wrong signal fence",bad);
            bad=observations;bad["journal"][0]["operation"]="Signal";reject("signal replaced submission",bad);
            bad=observations;bad["journal"][0]["sequence"]=true;reject("boolean event sequence",bad);
            bad=observations;bad["geometry"]["subresource"]=1;reject("wrong subresource",bad);
            bad=observations;bad["geometry"]["width"]=32;reject("wrong region",bad);
            bad=observations;bad["geometry"]["height"]=true;reject("boolean geometry",bad);
            bad=observations;bad["adapter"]["kind"]="physical";reject("wrong adapter kind",bad);
            bad=observations;bad["adapter"]["software"]=false;reject("physical adapter in warp",bad);
            bad=observations;bad["device"]["luid"]="00000000:12345678";reject("wrong device adapter",bad);
            bad=observations;bad["fences"][0]["completed_value"]="18446744073709551615";reject("device removal fence sentinel",bad);
            bad=observations;bad["fences"][0]["completed_value"]=true;reject("boolean completion",bad);
            bad=observations;bad["fences"][0]["completed_value"]="unknown";reject("unknown completion",bad);
            bad=observations;bad["completion"]["observed"]=false;reject("completion not observed",bad);
            bad=observations;bad["completion"]["retained_until_process_exit"]=true;reject("retained uncertain completion",bad);
            bad=observations;bad["resources"]["decoy"]=observations["resources"]["target"];reject("aliased decoy",bad);
            bad=observations;bad["readbacks"][0]["source_resource"]=observations["resources"]["decoy"];reject("wrong-target raw attribution",bad);
            bad=observations;bad["readbacks"][0]["file"]="decoy.bin";reject("wrong raw filename",bad);
            bad=observations;bad["readbacks"][1]["offset_bytes"]=0;reject("readback region overlap",bad);
            bad=observations;bad["unknown"]=1;reject("unknown observation field",bad);
            bad=observations;bad["queues"][0]["unknown"]=1;reject("unknown queue field",bad);
            bad=observations;bad["memory"]["planned_allocation_bytes"]=0;reject("false allocation evidence",bad);
            bad=observations;bad["oracle"]["success"]=false;reject("contradictory oracle",bad);
            for(auto it=observations.begin();it!=observations.end();++it){bad=observations;bad.erase(it.key());reject("missing mandatory field",bad);}
            if(cross){bad=observations;bad["queues"][1]["identity"]=bad["queues"][0]["identity"];reject("collapsed queues",bad);}
            for(auto raw:{L"output.bin",L"decoy.bin",L"preimage.bin"}) {
                auto path=directory/L"owned"/raw;lab::fs::remove(path);reject("missing raw artifact",observations);
                lab::fs::copy_file(fixtures/name/raw,path);
                std::fstream f(path,std::ios::in|std::ios::out|std::ios::binary);f.put('\0');f.close();
                if(raw==std::wstring(L"output.bin")){std::fstream g(path,std::ios::in|std::ios::out|std::ios::binary);g.seekp(1);g.put('\xff');g.close();}
                reject("corrupt raw artifact",observations);
                lab::fs::copy_file(fixtures/name/raw,path,lab::fs::copy_options::overwrite_existing);
            }
            lab::ValidateOwnedEvidence(request,observations,directory);
            auto physicalRequest=request;physicalRequest["parameters"]["adapter"]="physical";physicalRequest["parameters"]["adapter_luid"]="00000000:12345678";
            bool mismatch=false;try{lab::ValidateOwnedEvidence(physicalRequest,observations,directory);}catch(const std::exception&){mismatch=true;}
            lab::Require(mismatch,"physical LUID request accepted WARP evidence");++rejected;
        }
        std::cout<<"PASS: same/cross WARP fixtures independently validate; "<<rejected<<" evidence mutants rejected; no GPU executed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
