#include "../../../tools/nr/lab/LabControls.h"
#include <iostream>

int wmain(int argc,wchar_t** argv) {
    try {
        lab::Require(argc>=2,"fresh output required");
        lab::fs::path root=argv[1];lab::Require(!lab::fs::exists(root),"fresh output required");lab::fs::create_directories(root);
        for(const std::string id:{"NLAB-CTL-002","NLAB-CTL-006","NLAB-CTL-008","NLAB-CTL-010"}) {
            auto result=lab::RunCpuControl(id,{},root/lab::Wide(id));
            lab::Require(result.at("success").get<bool>(),result.dump());
            lab::Require(result.at("checks").size()>=3,"controls lack actual checks");
            lab::WriteJson(root/(lab::Wide(id)+L".json"),result);
        }
        for(const std::string id:{"NLAB-CTL-007","NLAB-CTL-009","NLAB-EVD-001"})
            lab::Require(!lab::RunCpuControl(id,{},root/lab::Wide(id)).at("success").get<bool>(),"unavailable adapter passed");
        if(argc==3) for(const std::string id:{"NLAB-CTL-001","NLAB-CTL-003"}) {
            auto result=lab::RunCpuControl(id,argv[2],root/lab::Wide(id));
            lab::WriteJson(root/(lab::Wide(id)+L".json"),result);
            lab::Require(result.at("success").get<bool>(),result.dump());
        }
        std::cout<<"PASS: runtime identity, strict observations, atomic capacity, private export policy";
        if(argc==3)std::cout<<", package closure and worker tamper";
        std::cout<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
