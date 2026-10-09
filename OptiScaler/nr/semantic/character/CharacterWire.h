#pragma once
#include "CharacterPipeline.h"
#include <json.hpp>
#include <set>
namespace Neurotic::Semantic::Character {
using Json=nlohmann::json;
inline bool ExactUnsigned(const Json& value,std::uint64_t expected){
    if(value.is_number_unsigned())return value.get<std::uint64_t>()==expected;
    if(value.is_number_integer()){const auto n=value.get<std::int64_t>();return n>=0&&static_cast<std::uint64_t>(n)==expected;}
    return false;
}
inline bool ExactFields(const Json& value,std::initializer_list<const char*> names){
    if(!value.is_object()||value.size()!=names.size())return false;
    for(const auto* name:names)if(!value.contains(name))return false;
    return true;
}
inline Json ParseWire(const std::string& text){
    std::vector<std::set<std::string>> objects;
    return Json::parse(text,[&objects](int,Json::parse_event_t event,Json& value){
        if(event==Json::parse_event_t::object_start)objects.emplace_back();
        else if(event==Json::parse_event_t::key){if(objects.empty()||!objects.back().insert(value.get<std::string>()).second)throw std::runtime_error("duplicate wire field");}
        else if(event==Json::parse_event_t::object_end)objects.pop_back();
        return true;
    });
}
inline Json ModelHashes(){return Json::array({
    "47fd5599d6fa17608f03e0eb0ae230baa6e597d7e8a2c8199fe00abea55a701f",
    "9d89c599319a18fb7d2e28451a883476164543182bafca5f09eb2cf767ed2f3f",
    "4b82da9944b88577175ee23a459dce2e26e6e4be573def65b1055dc2d9720186"});}
struct DetectionStats {unsigned candidates=0,poseAttempts=0,usablePoses=0,returned=0;double detectorMs=0,poseMs=0;};
inline Json EpochWire(const Epoch& e){return Json::array({e.sessionA,e.sessionB,e.device,e.swapchain,e.descriptor,e.scene,e.provider,e.config});}
inline Json MakeRequest(const CpuFrame& f,const std::string& nonce){
    return {{"schema",2},{"nonce",nonce},{"sequence",f.key.sequence},{"epoch",EpochWire(f.key.epoch)},
        {"capture_ns",f.key.captureNs},{"width",f.width},{"height",f.height},{"stride",f.stride},
        {"bytes",f.pixels.size()},{"maximum_persons",f.maximumPersons},{"pose_requested",f.poseRequested},{"detect_objects",f.detectObjects}};
}
inline bool DecodeReply(const Json& r,const CpuFrame& f,const std::string& nonce,unsigned pid,
    const std::string& imageHash,std::array<Detection,16>& out,std::size_t& count,DetectionStats* stats=nullptr) noexcept {
    count=0;
    try {
        if(!ExactFields(r,{"schema","nonce","pid","models","sequence","epoch","capture_ns","width","height","image_sha256","status","persons","stats"}) ||
           !ExactUnsigned(r.at("schema"),2) || r.at("nonce")!=nonce || !ExactUnsigned(r.at("pid"),pid) ||
           r.at("models")!=ModelHashes() || !ExactUnsigned(r.at("sequence"),f.key.sequence) ||
           !ExactUnsigned(r.at("capture_ns"),f.key.captureNs) ||
           !ExactUnsigned(r.at("width"),f.width) || !ExactUnsigned(r.at("height"),f.height) || r.at("image_sha256")!=imageHash ||
           r.at("status")!="ok")return false;
        const auto expectedEpoch=EpochWire(f.key.epoch);const auto& epoch=r.at("epoch");
        if(!epoch.is_array()||epoch.size()!=8)return false;
        for(unsigned i=0;i<8;++i)if(!ExactUnsigned(epoch[i],expectedEpoch[i].get<std::uint64_t>()))return false;
        const auto& people=r.at("persons");
        if(!people.is_array() || people.size()>16 || people.size()>f.maximumPersons)return false;
        std::array<Detection,16> decoded{};
        const auto number=[](const Json& v){if(!v.is_number())throw std::runtime_error("non-numeric coordinate");const double x=v.get<double>();if(!std::isfinite(x))throw std::runtime_error("non-finite coordinate");return x;};
        const auto& s=r.at("stats");
        if(!ExactFields(s,{"candidates","pose_attempts","usable_poses","returned","detector_ms","pose_ms"}))return false;
        const auto bounded=[](const Json& v,unsigned limit){
            if(!v.is_number_integer()||v.is_boolean())throw std::runtime_error("invalid stage count");
            const auto n=v.get<std::int64_t>();if(n<0||n>limit)throw std::runtime_error("stage count out of range");return static_cast<unsigned>(n);
        };
        DetectionStats stages{bounded(s.at("candidates"),5000),bounded(s.at("pose_attempts"),16),
            bounded(s.at("usable_poses"),16),bounded(s.at("returned"),16),number(s.at("detector_ms")),number(s.at("pose_ms"))};
        if(stages.returned!=people.size()||stages.usablePoses>stages.poseAttempts||stages.poseAttempts>stages.candidates||
           stages.returned>stages.candidates||stages.detectorMs<0||stages.detectorMs>10000||stages.poseMs<0||stages.poseMs>10000||
           (!f.poseRequested&&(stages.poseAttempts||stages.usablePoses)))return false;
        unsigned poses=0;
        for(std::size_t i=0;i<people.size();++i){
            const auto& p=people[i];if(!ExactFields(p,{"box","score","pose","geometry","class_id"}))return false;
            auto& d=decoded[i];const auto& b=p.at("box");
            if(!b.is_array()||b.size()!=4)return false;
            d.body={number(b[0]),number(b[1]),number(b[2]),number(b[3])};d.score=number(p.at("score"));
            if(!ValidRect(d.body)||!ScoreValid(d.score))return false;
            d.classId=bounded(p.at("class_id"),79);
            const auto& geometry=p.at("geometry");const auto& pose=p.at("pose");if(!pose.is_array())return false;
            if(geometry=="estimated_person_region"||geometry=="detector_box"){
                if((!f.detectObjects&&d.classId)||f.detectObjects!=(geometry=="detector_box"))return false;
                d.geometry=f.detectObjects?BoxGeometry::DetectorBox:BoxGeometry::EstimatedPersonRegion;
                if(pose.empty())continue;
                if(!f.poseRequested||d.classId||pose.size()!=33)return false;
            }
            else if(geometry!="visible_landmark_envelope"||!f.poseRequested||f.detectObjects||d.classId||pose.size()!=33)return false;
            for(std::size_t k=0;k<33;++k){const auto& a=pose[k];if(!a.is_array()||a.size()!=5)return false;
                d.pose[k]={{number(a[0]),number(a[1])},number(a[2]),number(a[3]),number(a[4])};}
            d.hasPose=true;if(!ValidPose(d.pose))return false;++poses;
        }
        if(poses!=stages.usablePoses)return false;
        count=people.size();out=decoded;if(stats)*stats=stages;return true;
    }catch(...){return false;}
}
}
