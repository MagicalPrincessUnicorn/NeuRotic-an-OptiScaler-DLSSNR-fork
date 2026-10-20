#include "../../../tools/nr/lab/LabTiming.h"
#include <iostream>
int wmain(int argc,wchar_t** argv) {
    using namespace lab;
    try {
        Require(argc==2,"run directory required");fs::path dir=fs::path(argv[1])/L"cases/NLAB-OBS-005-1";
        auto request=ReadJson(dir/L"case-request.json"),raw=ReadJson(dir/L"worker-receipt.json")["observations"];
        ValidateTiming(request,raw,dir,"light");unsigned count=0;
        auto reject=[&](json bad,const std::string& profile="light"){bool rejected=false;try{ValidateTiming(request,bad,dir,profile);}catch(const std::exception&){rejected=true;}Require(rejected,"timing mutation accepted");++count;};
        auto bad=raw;bad["timing"]["frequency_hz"]="0";reject(bad);
        bad=raw;bad["timing"]["frequency_hresult"]="80004005";reject(bad);
        bad=raw;bad["timing"]["query_hresult"]="80004005";reject(bad);
        bad=raw;bad["timing"]["frequency_hz"]=true;reject(bad);
        bad=raw;bad["timing"]["cpu_frequency_hz"]="0";reject(bad);
        bad=raw;bad["timing"]["cpu_end"]="0";reject(bad);
        bad=raw;bad["timing"]["queue"]="unrelated-queue";reject(bad);
        bad=raw;bad["timing"]["gbv_enabled"]=true;reject(bad);
        bad=raw;bad["timing"]["dred_enabled"]=true;reject(bad);
        bad=raw;bad["timing"]["available"]=1;reject(bad);
        bad=raw;bad["timing"]["calibration"]["hresult"]="80004005";reject(bad);
        bad=raw;bad["completion"]["observed"]=false;reject(bad);
        reject(raw,"dred");
        for(const auto& v:{Interval(1,2,0,true),Interval(2,1,100,true),Interval(1,2,100,false),Interval(1,UINT64_MAX,100,true)})Require(v["available"]==false&&v["seconds"].is_null(),"invalid timing was made available");
        auto proper=Interval(10,20,100,true);Require(proper["seconds"]==0.1,"units incorrect");
        std::cout<<"PASS "<<count<<" timing mutations and 5 interval boundary checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
