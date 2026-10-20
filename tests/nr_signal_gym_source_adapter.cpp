// Test-only framed value adapter. Production helpers remain the sole algorithms.
#include "../external/nlohmann/json.hpp"
#include "../OptiScaler/dlssnr/NativeTemporalInputs.h"
#include "../OptiScaler/dlssnr/DlssNr_PresentCompatibility.h"
#include "../OptiScaler/dlssnr/DlssNr_PresentHistory.h"
#include "../OptiScaler/dlssnr/NrReadiness.h"
#include <iostream>
#include <set>
#include <stdexcept>
#include <fcntl.h>
#include <io.h>
#include "signal_gym_descriptor.h"
using J = nlohmann::json;
static J strictParse(const std::string& bytes) {
    if(bytes.size()>=3 && static_cast<unsigned char>(bytes[0])==0xef && static_cast<unsigned char>(bytes[1])==0xbb && static_cast<unsigned char>(bytes[2])==0xbf)throw std::runtime_error("BOM");
    bool quoted=false,escaped=false;
    for(size_t i=0;i<bytes.size();++i){char c=bytes[i];if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}else{if(c=='"')quoted=true;else if(c=='-'&&i+1<bytes.size()&&bytes[i+1]=='0'&&(i+2==bytes.size()||bytes[i+2]<'0'||bytes[i+2]>'9'))throw std::runtime_error("negative zero");}}
    std::vector<std::set<std::string>> objects;
    auto callback=[&](int depth,J::parse_event_t event,J& parsed){
        if(depth>32)throw std::runtime_error("depth");
        if(event==J::parse_event_t::object_start)objects.emplace_back();
        if(event==J::parse_event_t::object_end)objects.pop_back();
        if(event==J::parse_event_t::key){std::string key=parsed;for(unsigned char c:key)if(c>127)throw std::runtime_error("key");if(!objects.back().insert(key).second)throw std::runtime_error("duplicate");}
        if(event==J::parse_event_t::value && (parsed.is_number_float() || (parsed.is_number_integer() && (parsed>9007199254740991LL || parsed< -9007199254740991LL))))throw std::runtime_error("numeric profile");
        return true;
    };
    return J::parse(bytes,callback);
}
static void keys(const J& v, std::initializer_list<const char*> expected) {
    if (!v.is_object() || v.size()!=expected.size()) throw std::runtime_error("GYM.INPUT_TYPE_MISMATCH");
    for (auto k:expected) if (!v.contains(k)) throw std::runtime_error("GYM.INPUT_TYPE_MISMATCH");
}
static J known(const J& f) {
    if (!f.is_object() || !f.contains("state") || !f["state"].is_string()) throw std::runtime_error("GYM.INPUT_TYPE_MISMATCH");
    if(f["state"]!="known") throw std::runtime_error("GYM.INPUT_NOT_KNOWN");
    keys(f,{"state","value"}); return f["value"];
}
static uint32_t u32value(const J& v) {
    if (!v.is_number_integer() || v.is_boolean() || v < 0 || v > UINT32_MAX) throw std::runtime_error("GYM.INPUT_TYPE_MISMATCH");
    return v.get<uint32_t>();
}
static uint32_t u32(const J& f) { return u32value(known(f)); }
static uint64_t u64(const J& f) {
    J v=known(f); if(!v.is_string()) throw std::runtime_error("GYM.INPUT_TYPE_MISMATCH");
    std::string s=v; if(s.empty() || s.size()>20 || (s.size()>1&&s[0]=='0')) throw std::runtime_error("GYM.INPUT_TYPE_MISMATCH");
    uint64_t n=0; for(char c:s){ if(c<'0'||c>'9'||n>(UINT64_MAX-(c-'0'))/10) throw std::runtime_error("GYM.INPUT_TYPE_MISMATCH"); n=n*10+c-'0'; } return n;
}
static bool boolean(const J& f){ J v=known(f); if(!v.is_boolean()) throw std::runtime_error("GYM.INPUT_TYPE_MISMATCH");return v.get<bool>(); }
static std::string string(const J& f){ J v=known(f); if(!v.is_string()||v.get_ref<const std::string&>().size()>1024) throw std::runtime_error("GYM.INPUT_TYPE_MISMATCH");return v.get<std::string>(); }
static std::optional<unsigned> flag(const J& f){ if(f.is_object()&&f.value("state","")=="absent"){keys(f,{"state","reason_id"});return std::nullopt;}return u32(f); }
static J history(const DlssNr::PresentHistory::Continuity& h){ return {{"resetPending",h.ResetForNextEvaluation()},{"completedFrames",std::to_string(h.CompletedFrames())},{"resetReason",h.ResetReason()},{"lastInvalidationReason",h.LastInvalidationReason()}}; }
static J invoke(const J& p, DlssNr::PresentHistory::Continuity& h, const J& desc){
    keys(p,{"case_id","record_id","event_id","operation_id","subject_ref","inputs"});
    std::string op=p.at("operation_id"); const J& a=p.at("inputs");
    int ci=op.rfind("native.",0)==0?0:op=="present.admit"?1:op=="readiness.evaluate"?2:3;
    J milestones=J::object();for(auto k:{"requested","accepted","recorded","submitted","produced","delivered","completed","sealed","retired"})milestones[k]="not_applicable";
    J result={{"status","ok"},{"domain",ci==0?"qualification":"decision"},{"scope","source_policy_only"},{"contract",desc["contract_refs"][ci]},{"output",nullptr},{"reason_ids",J::array()},{"consumed_pointers",J::array()},{"milestones",milestones}};
    try {
        for(auto it=a.begin();it!=a.end();++it)result["consumed_pointers"].push_back(it.key());
        J out;
        using namespace DlssNr;
        if(op=="native.fits"){
            keys(a,{"rect","width","height"});J r=known(a["rect"]);keys(r,{"x","y","width","height"});
            NativeTemporalInputs::Rect rect{u32value(r["x"]),u32value(r["y"]),u32value(r["width"]),u32value(r["height"])};
            auto w=u64(a["width"]);auto ht=u32(a["height"]);out={{"fits",NativeTemporalInputs::Fits(rect,w,ht)}};
        }else if(op=="native.resolve_flags"){
            keys(a,{"creation","evaluation"});auto c=flag(a["creation"]),e=flag(a["evaluation"]);out={{"flags",NativeTemporalInputs::ResolveFlags(c,e)}};
        }else if(op=="native.guide_rect"){
            keys(a,{"x","y","renderWidth","renderHeight","outputWidth","outputHeight","lowResolution"});
            auto r=NativeTemporalInputs::GuideRect(u32(a["x"]),u32(a["y"]),u32(a["renderWidth"]),u32(a["renderHeight"]),u32(a["outputWidth"]),u32(a["outputHeight"]),boolean(a["lowResolution"]));out={{"x",r.x},{"y",r.y},{"width",r.width},{"height",r.height}};
        }else if(op=="native.authoritative"){
            keys(a,{"superResolution","dlssBackend","dx12","adapter"});out={{"authoritative",NativeTemporalInputs::Authoritative(boolean(a["superResolution"]),boolean(a["dlssBackend"]),boolean(a["dx12"]),boolean(a["adapter"]))}};
        }else if(op=="present.admit"){
            keys(a,{"capabilities"});const J& c=a["capabilities"];
            keys(c,{"api","directQueue","singleSample","flipModel","sdr","texture2D","nonZeroExtent","sameDevice","rgba8","rgb10","rgba8ShaderLoad","rgba8TypedStore","targetShaderLoad","targetTypedStore","sharedResources","synchronization"});
            PresentCompatibility::Capabilities cap;auto api=string(c["api"]);if(api!="D3D11"&&api!="D3D12")throw std::runtime_error("GYM.INPUT_TYPE_MISMATCH");cap.api=api=="D3D11"?PresentCompatibility::Api::D3D11:PresentCompatibility::Api::D3D12;
#define CAP(n) cap.n=boolean(c[#n]);
            CAP(directQueue) CAP(singleSample) CAP(flipModel) CAP(sdr) CAP(texture2D) CAP(nonZeroExtent) CAP(sameDevice) CAP(rgba8) CAP(rgb10) CAP(rgba8ShaderLoad) CAP(rgba8TypedStore) CAP(targetShaderLoad) CAP(targetTypedStore) CAP(sharedResources) CAP(synchronization)
#undef CAP
            auto r=PresentCompatibility::Admit(cap);const char* path=r.path==PresentCompatibility::PixelPath::None?"None":r.path==PresentCompatibility::PixelPath::Rgba8Direct?"Rgba8Direct":"Rgb10Conversion";out={{"supported",r.supported},{"path",path},{"reason",r.reason}};
        }else if(op=="readiness.evaluate"){
            keys(a,{"input"});const J& c=a["input"];keys(c,{"enabled","sessionOpen","failed","featureLoaded","evaluated","resetPending","requestedGeneration","evaluatedGeneration","preSrRequested","lastEvaluationWasPreSr","scratchPrimed","awaitingEvaluation"}); ReadinessInput in;
#define BOOL(n) in.n=boolean(c[#n]);
            BOOL(enabled) BOOL(sessionOpen) BOOL(failed) BOOL(featureLoaded) BOOL(evaluated) BOOL(resetPending) BOOL(preSrRequested) BOOL(lastEvaluationWasPreSr) BOOL(scratchPrimed) BOOL(awaitingEvaluation)
#undef BOOL
            in.requestedGeneration=u64(c["requestedGeneration"]);in.evaluatedGeneration=u64(c["evaluatedGeneration"]);auto r=GetReadiness(in);out={{"running",r.running},{"preSrDisplayReady",r.preSrDisplayReady},{"transitionPending",r.transitionPending},{"outputQuarantined",r.outputQuarantined}};
        }else if(op=="history.read"||op=="history.complete"){
            keys(a,{});if(op=="history.complete")h.CompleteOutputPresent();out=history(h);
        }else if(op=="history.invalidate"){
            keys(a,{"reason"});auto reason=string(a["reason"]);h.Invalidate(reason);out=history(h);
        }else throw std::runtime_error("GYM.PROTOCOL_ERROR");
        result["output"]=out;
    }catch(const std::exception& e){result["status"]="refused";std::string reason=e.what();result["reason_ids"]={reason=="GYM.INPUT_NOT_KNOWN"?reason:"GYM.INPUT_TYPE_MISMATCH"};}
    return result;
}
int main(){
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    J desc=J::parse(SIGNAL_GYM_DESCRIPTOR);DlssNr::PresentHistory::Continuity hist;int state=0;std::string caseId;uint64_t next=1;
    while(true){uint32_t length=0;std::cin.read(reinterpret_cast<char*>(&length),4);if(std::cin.gcount()==0)return state==2?0:1;if(std::cin.gcount()!=4||length==0||length>4194304)return 2;
        std::string bytes(length,'\0');if(!std::cin.read(bytes.data(),length))return 2;J request,response;std::string id="0";
        try {
            request=strictParse(bytes);keys(request,{"interface_version","request_id","method","payload"});
            if(request["interface_version"]!="2.0.0"||request["request_id"]!=std::to_string(next++))throw std::runtime_error("GYM.PROTOCOL_ERROR");id=request["request_id"];std::string method=request["method"];J out;auto p=request["payload"];
            if(method=="describe"&&state==0&&p.is_null())out=desc;
            else if(method=="begin_case"&&state==0){keys(p,{"case_id","environment","contract_bindings"});if(p["contract_bindings"]!=desc["contract_refs"])throw std::runtime_error("GYM.CONTRACT_MISMATCH");caseId=p["case_id"];state=1;out={{"case_id",caseId},{"state","begun"}};}
            else if(method=="invoke"&&state==1&&p.at("case_id")==caseId){if(std::find(desc["operations"].begin(),desc["operations"].end(),p["operation_id"])==desc["operations"].end())throw std::runtime_error("GYM.PROTOCOL_ERROR");out=invoke(p,hist,desc);}
            else if(method=="end_case"&&state==1&&p.at("case_id")==caseId){keys(p,{"case_id","reason"});if(p["reason"]!="completed"&&p["reason"]!="cancelled"&&p["reason"]!="error")throw std::runtime_error("GYM.PROTOCOL_ERROR");state=2;hist=DlssNr::PresentHistory::Continuity{};out={{"status","ok"},{"resource_scope","no_resources"},{"gpu_state","no_gpu_work"},{"state_destroyed",{{"state","known"},{"value",true}}},{"reason_ids",J::array()}};}
            else throw std::runtime_error("GYM.PROTOCOL_ERROR");
            response={{"interface_version","2.0.0"},{"request_id",id},{"status","ok"},{"payload",out}};
        }catch(const std::exception&){response={{"interface_version","2.0.0"},{"request_id",id},{"status","error"},{"payload",{{"reason_id","GYM.PROTOCOL_ERROR"},{"detail","Invalid message or case transition"}}}};}
        bytes=response.dump();length=static_cast<uint32_t>(bytes.size());std::cout.write(reinterpret_cast<char*>(&length),4);std::cout.write(bytes.data(),length);std::cout.flush();if(!std::cout)return 3;
    }
}
