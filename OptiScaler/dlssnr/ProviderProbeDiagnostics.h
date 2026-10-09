#pragma once
#include <json.hpp>
#include <cstdint>
#include <string>

namespace DlssNr::ProviderProbeDiagnostics {
using ReadAdmission = unsigned long long (*)(char*, unsigned long long);
// Read-only, bounded diagnostics. Never substitute diagnostic availability for
// the provider admission decision or call an NGX/provider entry point here.
inline std::string Detail(ReadAdmission read) noexcept {
    try {
        if(!read)return "admission_diagnostic=unavailable";
        const auto size=read(nullptr,0);
        if(size<2 || size>65536)return "admission_diagnostic=invalid_size";
        std::string bytes(static_cast<size_t>(size),'\0');
        if(read(bytes.data(),size)!=size || bytes.back()!='\0')return "admission_diagnostic=changed";
        const auto value=nlohmann::json::parse(bytes.begin(),bytes.end()-1,nullptr,false);
        if(!value.is_object())return "admission_diagnostic=invalid_json";
        const auto token=[&](const char* key){
            if(!value.contains(key)||!value[key].is_string())return std::string("unknown");
            auto text=value[key].get<std::string>();
            // Identity errors may append local paths after the stable code.
            text.resize(text.find(':')==std::string::npos?text.size():text.find(':'));
            if(text.size()>160)return std::string("invalid_code");
            for(auto c:text)if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))return std::string("invalid_code");
            return text.empty()?std::string("none"):text;
        };
        const auto error=value.contains("win32_error")&&value["win32_error"].is_number_unsigned()
            ?value["win32_error"].dump():"unknown";
        // Canonical identity failures carry their own Win32 error after a
        // private path. The outer admission stage may legitimately report 0.
        std::string identityError="unknown";
        if(value.contains("identity_error")&&value["identity_error"].is_string()){
            const auto& raw=value["identity_error"].get_ref<const std::string&>();
            const auto suffix=raw.rfind(":win32=");
            if(suffix!=std::string::npos){
                const auto digits=raw.substr(suffix+7);
                uint64_t number=0;bool valid=!digits.empty()&&digits.size()<=10;
                for(auto c:digits){
                    if(c<'0'||c>'9'){valid=false;break;}
                    number=number*10+static_cast<unsigned>(c-'0');
                    if(number>UINT32_MAX){valid=false;break;}
                }
                if(valid)identityError=std::to_string(number);
            }
        }
        return "stage="+token("stage")+" identity_error="+token("identity_error")+
            " stage_win32_error="+error+" identity_win32_error="+identityError;
    }catch(...){return "admission_diagnostic=unreadable";}
}
class LogGate {
    uint64_t generation_=0;
    unsigned emitted_=0;
    int mask_=-1;
    std::string detail_;
    uint64_t sampledGeneration_=0,lastSample_=0;
    int sampledMask_=-1;
    bool sampled_=false;
public:
    bool Sample(uint64_t generation,int mask,uint64_t now) noexcept {
        if(sampled_ && generation==sampledGeneration_ && mask==sampledMask_ &&
           now>=lastSample_ && now-lastSample_<1000)return false;
        sampled_=true;sampledGeneration_=generation;sampledMask_=mask;lastSample_=now;return true;
    }
    bool Changed(uint64_t generation,int mask,const std::string& detail){
        if(generation!=generation_){generation_=generation;emitted_=0;detail_.clear();mask_=-1;}
        if((mask==mask_&&detail==detail_)||emitted_>=8)return false;
        mask_=mask;detail_=detail;++emitted_;return true;
    }
};
}
