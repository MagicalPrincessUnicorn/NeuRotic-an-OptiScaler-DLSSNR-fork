#include <storage/AppDataMaintenance.h>
#include <fstream>
#include <iostream>
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("new owned fixture root required");
 const std::filesystem::path root=argv[1];if(std::filesystem::exists(root))throw std::runtime_error("fixture already exists");
 const auto vault=root/L"Runtime/NrDllProfiles/opaque-profile";std::filesystem::create_directories(vault);
 {std::ofstream f(vault/L"nvngx_dlssnr.dll");f<<"inert imported user bytes";}
 {std::ofstream f(root/L"library.json");f<<"{}";}
 const auto plan=nh::PlanAppDataClear(root,nh::AppDataClearScope::AppData);
 if(!plan.Ready())throw std::runtime_error("maintenance fixture plan must be ready");
 for(const auto& entry:plan.entries)if(entry.relative.native().starts_with(L"Runtime"))throw std::runtime_error("App Data maintenance must preserve imported NR DLL vault");
 const auto result=nh::ExecuteAppDataClear(plan);
 if(!result.complete||!std::filesystem::exists(vault/L"nvngx_dlssnr.dll")||std::filesystem::exists(root/L"library.json"))throw std::runtime_error("maintenance must clear App documents and preserve actual vault bytes");
 std::cout<<"PASS fixture-root maintenance preserves persistent Runtime vault while clearing App documents; no provider load\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
