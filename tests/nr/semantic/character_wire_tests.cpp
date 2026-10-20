#include "nr/semantic/character/CharacterWire.h"
#include <iostream>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
int main(){try{
    CpuFrame f{{{1,2,3,4,5,6,7,8},10,100},4,4,16,4,{}};
    const std::string nonce(32,'a');
    auto r=MakeRequest(f,nonce);
    r.erase("stride");r.erase("bytes");r.erase("maximum_persons");r.erase("pose_requested");r.erase("detect_objects");
    r["pid"]=42;r["models"]=ModelHashes();r["status"]="ok";r["persons"]=Json::array();
    r["stats"]={{"candidates",0},{"pose_attempts",0},{"usable_poses",0},{"returned",0},{"detector_ms",0},{"pose_ms",0}};
    r["image_sha256"]=std::string(64,'b');
    std::array<Detection,16> out;std::size_t n=99;
    if(!DecodeReply(r,f,nonce,42,std::string(64,'b'),out,n)||n)throw std::runtime_error("valid no-match");
    unsigned count=1;
    for(const char* field:{"nonce","pid","models","sequence","epoch","capture_ns","width","image_sha256"}){
        auto bad=r;bad[field]=nullptr;
        if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error(field);
        ++count;
    }
    auto bad=r;bad["persons"]=Json::array();
    for(int i=0;i<17;++i)bad["persons"].push_back(Json::object());
    if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("oversized persons");
    ++count;
    for(const char* field:{"schema","pid","sequence","capture_ns","width","height"}){
        for(const auto& alias:{Json(true),Json(-1),Json(r[field].get<double>())}){
            bad=r;bad[field]=alias;
            if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("integer type alias");
            ++count;
        }
    }
    f.key.sequence=UINT64_MAX;r["sequence"]=f.key.sequence;
    bad=r;bad["sequence"]=-1;
    if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("signed identity alias");
    ++count;
    if(!DecodeReply(ParseWire(r.dump()),f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("exact uint64 transport");
    ++count;
    bad=r;bad["unexpected"]=1;
    if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("unknown field");
    ++count;
    for(const auto* text:{"{\"pid\":1,\"pid\":2}","{\"people\":[{\"x\":1,\"x\":2}]}"}){
        bool rejected=false;try{ParseWire(text);}catch(...){rejected=true;}
        if(!rejected)throw std::runtime_error("duplicate field");++count;
    }
    f.key.sequence=9007199254740993ULL;r["sequence"]=f.key.sequence;
    bad=r;bad["sequence"]=9007199254740992.0;
    if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("rounded identity alias");
    ++count;
    f.detectObjects=true;r["persons"]=Json::array();r["stats"]["candidates"]=16;r["stats"]["returned"]=16;
    for(unsigned i=0;i<16;++i)r["persons"].push_back({{"box",{.1,.1,.3,.8}},{"score",.9},{"pose",Json::array()},{"geometry","detector_box"},{"class_id",i}});
    if(DecodeReply(r,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("sixteen detections must respect a four-person request cap");++count;
    f.maximumPersons=16;
    if(!DecodeReply(r,f,nonce,42,std::string(64,'b'),out,n)||n!=16||out[15].classId!=15||out[0].hasPose)throw std::runtime_error("sixteen classed regions without invented pose");++count;
    for(const char* field:{"class_id","geometry","pose"}){
        bad=r;bad["persons"][0][field]=nullptr;
        if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("invalid object semantics");++count;
    }
    for(const auto& alias:{Json(80),Json(-1),Json(true),Json(1.0)}){
        bad=r;bad["persons"][0]["class_id"]=alias;
        if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("invalid object class");++count;
    }
    bad=r;bad["stats"]["returned"]=15;
    if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("incorrect returned-stage count");++count;
    bad=r;bad["stats"]["pose_attempts"]=true;
    if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("stage count bool alias");++count;
    bad=r;bad["stats"]["detector_ms"]=-1;
    if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("negative stage duration");++count;
    f.detectObjects=false;
    if(DecodeReply(r,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("object mode cannot masquerade as people-only source");++count;
    r["persons"]=Json::array({{{"box",{.1,.1,.3,.8}},{"score",.9},{"pose",Json::array()},{"geometry","estimated_person_region"},{"class_id",0}}});
    r["stats"]["returned"]=1;r["stats"]["candidates"]=1;
    if(!DecodeReply(r,f,nonce,42,std::string(64,'b'),out,n)||n!=1||out[0].geometry!=BoxGeometry::EstimatedPersonRegion)throw std::runtime_error("estimated region keeps explicit provenance");++count;
    f.poseRequested=true;f.detectObjects=true;r["persons"][0]["geometry"]="detector_box";
    r["persons"][0]["pose"]=Json::array();for(unsigned i=0;i<33;++i)r["persons"][0]["pose"].push_back({.2,.3,.9,.9,0});
    r["stats"]["pose_attempts"]=1;r["stats"]["usable_poses"]=1;
    if(!DecodeReply(r,f,nonce,42,std::string(64,'b'),out,n)||!out[0].hasPose||out[0].geometry!=BoxGeometry::DetectorBox)throw std::runtime_error("object-mode person retains detector geometry and real optional pose");++count;
    bad=r;bad["persons"][0]["class_id"]=5;
    if(DecodeReply(bad,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("object pose is limited to person class");++count;
    f.poseRequested=false;
    if(DecodeReply(r,f,nonce,42,std::string(64,'b'),out,n))throw std::runtime_error("unrequested object pose refused");++count;
    std::cout<<"PASS "<<count<<" exact wire-binding checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
