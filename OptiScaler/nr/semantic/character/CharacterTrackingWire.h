#pragma once
#include "CharacterWire.h"
#include <deque>
namespace Neurotic::Semantic::Character {
struct TrackingStats {double detectorMs=0,poseMs=0,trackingMs=0;unsigned candidates=0,returned=0,updates=0,poseUpdates=0,poseFailures=0;};
class SubmittedFrames {
    struct Entry {FrameKey key;std::string hash;};std::deque<Entry> entries_;
public:
    void Add(const CpuFrame& f,const std::string& hash){
        if(!entries_.empty()&&entries_.back().key.epoch!=f.key.epoch)entries_.clear();
        entries_.push_back({f.key,hash});while(entries_.size()>32)entries_.pop_front();
    }
    std::uint64_t Confirm(const Json& source,const FrameKey& current,std::uint64_t limit) const {
        if(!ExactFields(source,{"sequence","capture_ns","image_sha256"}))return 0;
        for(const auto& e:entries_)if(e.key.epoch==current.epoch&&e.key.sequence<=current.sequence&&
            e.key.captureNs<=current.captureNs&&current.captureNs-e.key.captureNs<=limit&&
            ExactUnsigned(source.at("sequence"),e.key.sequence)&&ExactUnsigned(source.at("capture_ns"),e.key.captureNs)&&
            source.at("image_sha256")==e.hash)return e.key.captureNs;
        return 0;
    }
};
inline Json MakeTrackingRequest(const CpuFrame& f,const std::string& nonce,unsigned holdMs){
    auto r=MakeRequest(f,nonce);r["schema"]=3;r["hold_ms"]=std::min(holdMs,2000u);return r;
}
inline bool DecodeTrackingReply(const Json& r,const CpuFrame& f,const std::string& nonce,unsigned pid,const std::string& hash,
    const SubmittedFrames& history,Snapshot& result,TrackingStats& stats) noexcept {
    try{
        if(!ExactFields(r,{"schema","nonce","pid","models","sequence","epoch","capture_ns","width","height","image_sha256","status","persons","tracking"})||
            !ExactUnsigned(r.at("schema"),3))return false;
        const auto& people=r.at("persons");if(!people.is_array()||people.size()>f.maximumPersons||people.size()>MaxInstances)return false;
        // Reuse exact geometry/identity validation. Local envelope counts are
        // validation inputs only; actual model metrics stay separate below.
        Json geometry=r;geometry["schema"]=2;geometry.erase("tracking");geometry["persons"]=Json::array();unsigned poses=0;
        for(const auto& p:people){
            if(!ExactFields(p,{"box","score","geometry","class_id","track_id","source","pose_source","quality","velocity","pose"}))return false;
            geometry["persons"].push_back({{"box",p.at("box")},{"score",p.at("score")},{"geometry",p.at("geometry")},{"class_id",p.at("class_id")},{"pose",p.at("pose")}});
            if(!p.at("pose").empty())++poses;
        }
        geometry["stats"]={{"candidates",people.size()},{"returned",people.size()},{"pose_attempts",poses},{"usable_poses",poses},{"detector_ms",0},{"pose_ms",0}};
        std::array<Detection,16> decoded{};std::size_t count=0;
        if(!DecodeReply(geometry,f,nonce,pid,hash,decoded,count))return false;
        const auto number=[](const Json& v){if(!v.is_number())throw std::runtime_error("non-numeric tracking value");const auto n=v.get<double>();if(!std::isfinite(n))throw std::runtime_error("non-finite tracking value");return n;};
        const auto integer=[](const Json& v,std::uint64_t maximum){
            if(!v.is_number_integer()||v.is_boolean()||(!v.is_number_unsigned()&&v.get<std::int64_t>()<0))throw std::runtime_error("invalid tracking integer");
            auto n=v.get<std::uint64_t>();if(n>maximum)throw std::runtime_error("tracking integer bound");return n;
        };
        Snapshot out{};out.key=f.key;out.origin=Origin::LiveModel;out.count=count;
        for(std::size_t i=0;i<count;++i){
            const auto& p=people[i];const auto& d=decoded[i];auto& item=out.items[i];
            item.id=integer(p.at("track_id"),UINT64_MAX);if(!item.id)return false;
            item.body=d.body;item.score=d.score;item.classId=d.classId;item.pose=d.pose;item.hasPose=d.hasPose;item.geometry=d.geometry;
            item.confirmedNs=history.Confirm(p.at("source"),f.key,350'000'000);if(!item.confirmedNs)return false;
            if(item.hasPose){item.poseNs=history.Confirm(p.at("pose_source"),f.key,250'000'000);if(!item.poseNs)return false;}
            else if(!p.at("pose_source").is_null())return false;
            const auto quality=number(p.at("quality"));if(quality<.65||quality>1)return false;
            const auto& v=p.at("velocity");if(!v.is_array()||v.size()!=2)return false;
            item.velocity={number(v[0]),number(v[1])};item.motionTracked=true;
        }
        if(!ValidSnapshot(out))return false;
        const auto& s=r.at("tracking");
        if(!ExactFields(s,{"detector_candidates","detector_returned","detector_ms","pose_ms","tracking_ms","detection_updates","pose_updates","pose_failures"}))return false;
        TrackingStats measured{};
        measured.candidates=static_cast<unsigned>(integer(s.at("detector_candidates"),5000));measured.returned=static_cast<unsigned>(integer(s.at("detector_returned"),16));
        measured.updates=static_cast<unsigned>(integer(s.at("detection_updates"),UINT32_MAX));measured.poseUpdates=static_cast<unsigned>(integer(s.at("pose_updates"),UINT32_MAX));measured.poseFailures=static_cast<unsigned>(integer(s.at("pose_failures"),UINT32_MAX));
        measured.detectorMs=number(s.at("detector_ms"));measured.poseMs=number(s.at("pose_ms"));measured.trackingMs=number(s.at("tracking_ms"));
        if(measured.returned>measured.candidates||measured.returned>f.maximumPersons)return false;
        // Finite diagnostic durations do not authorize geometry. A completed
        // long model stall must not fault otherwise healthy tracking replies.
        for(auto ms:{measured.detectorMs,measured.poseMs,measured.trackingMs})if(ms<0)return false;
        result=out;stats=measured;return true;
    }catch(...){return false;}
}
}
