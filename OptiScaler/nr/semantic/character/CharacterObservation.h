// SPDX-License-Identifier: MIT
// Original reference code prepared for NeuRotic, 2026-10-02.
// No graphics resources, game hooks, model loads, or global runtime state.
// Adopted from the 2026-10-02 Character Inspector packet. Changes: bounded TTL,
// pose validation/relative Z, and bidirectional association ambiguity rejection.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace Neurotic::Semantic::Character {
constexpr std::size_t MaxInstances = 16;
constexpr std::uint64_t DisplayTtlNs = 150'000'000;
constexpr std::uint64_t AssociationTtlNs = 300'000'000;
struct Point { double x = 0, y = 0; };
struct Rect { double x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
inline bool Finite(Point p) noexcept { return std::isfinite(p.x) && std::isfinite(p.y); }
inline bool ValidRect(Rect r) noexcept {
    return Finite({r.x0,r.y0}) && Finite({r.x1,r.y1}) && r.x0 >= 0 && r.y0 >= 0 &&
           r.x1 <= 1 && r.y1 <= 1 && r.x1 > r.x0 && r.y1 > r.y0;
}
inline double IoU(Rect a, Rect b) noexcept {
    if (!ValidRect(a) || !ValidRect(b)) return 0;
    const double area = std::max(0.0,std::min(a.x1,b.x1)-std::max(a.x0,b.x0)) *
                        std::max(0.0,std::min(a.y1,b.y1)-std::max(a.y0,b.y0));
    return area / ((a.x1-a.x0)*(a.y1-a.y0)+(b.x1-b.x0)*(b.y1-b.y0)-area);
}
struct Affine {
    double a=1,b=0,c=0,d=0,e=1,f=0;
    Point Apply(Point p) const noexcept { return {a*p.x+b*p.y+c,d*p.x+e*p.y+f}; }
    std::optional<Affine> Inverse() const noexcept {
        for (double v : {a,b,c,d,e,f}) if (!std::isfinite(v)) return {};
        const double det = a*e-b*d;
        if (!std::isfinite(det) || std::abs(det) < 1e-12) return {};
        Affine out{e/det,-b/det,(b*f-e*c)/det,-d/det,a/det,(d*c-a*f)/det};
        for (double v : {out.a,out.b,out.c,out.d,out.e,out.f}) if (!std::isfinite(v)) return {};
        return out;
    }
    // Ideal continuous transform. A pixel resizer must record its actual rounded sizes/padding.
    static std::optional<Affine> Letterbox(double sw,double sh,double dw,double dh) noexcept {
        for (double v : {sw,sh,dw,dh}) if (!std::isfinite(v) || v <= 0 || v > 32768) return {};
        const double s=std::min(dw/sw,dh/sh);
        return Affine{s,0,(dw-sw*s)/2,0,s,(dh-sh*s)/2};
    }
};
struct Landmark { Point position; double visibility=0, presence=0; double zRelative=0; };
using Pose = std::array<Landmark,33>;
inline bool ScoreValid(double s) noexcept {return std::isfinite(s)&&s>=0&&s<=1;}
inline bool ValidPose(const Pose& pose) noexcept {
    for(const auto& k:pose)
        if(!Finite(k.position)||!std::isfinite(k.zRelative)||!ScoreValid(k.visibility)||!ScoreValid(k.presence)) return false;
    return true; // Finite out-of-frame landmarks remain explicit, not clamped.
}
inline bool Usable(const Landmark& k,double threshold) noexcept {
    return Finite(k.position) && k.position.x>=0 && k.position.x<=1 && k.position.y>=0 &&
        k.position.y<=1 && std::isfinite(k.visibility) && std::isfinite(k.presence) &&
        k.visibility>=threshold && k.visibility<=1 && k.presence>=threshold && k.presence<=1;
}
// An approximate geometric torso band, NOT an anatomical segmentation or orientation estimator.
inline std::optional<Rect> TorsoBand(const Pose& p,double t0,double t1,double threshold=.5) noexcept {
    if (!std::isfinite(t0)||!std::isfinite(t1)||!std::isfinite(threshold)||
        threshold<0||threshold>1||t0<0||t1>1||t0>=t1) return {};
    for (auto i : {11,12,23,24}) if (!Usable(p[i],threshold)) return {};
    const auto ls=p[11].position, rs=p[12].position, lh=p[23].position, rh=p[24].position;
    const auto distance=[](Point a,Point b){return std::hypot(a.x-b.x,a.y-b.y);};
    if (distance(ls,rs)<1e-4 || distance(lh,rh)<1e-4 ||
        distance({(ls.x+rs.x)/2,(ls.y+rs.y)/2},{(lh.x+rh.x)/2,(lh.y+rh.y)/2})<1e-4) return {};
    const auto lerp=[](Point a,Point b,double t){return Point{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};};
    std::array<Point,4> q{lerp(ls,lh,t0),lerp(rs,rh,t0),lerp(rs,rh,t1),lerp(ls,lh,t1)};
    double area=0;
    Rect r{1,1,0,0};
    for (std::size_t i=0;i<q.size();++i) {
        const auto j=(i+1)%q.size(); area+=q[i].x*q[j].y-q[j].x*q[i].y;
        r.x0=std::min(r.x0,q[i].x);r.y0=std::min(r.y0,q[i].y);
        r.x1=std::max(r.x1,q[i].x);r.y1=std::max(r.y1,q[i].y);
    }
    if (std::abs(area)<1e-7 || !ValidRect(r)) return {};
    return r;
}
struct Epoch {
    std::uint64_t sessionA=0,sessionB=0,device=0,swapchain=0,descriptor=0,scene=0,provider=0,config=0;
    bool operator==(const Epoch& v) const noexcept {
        return sessionA==v.sessionA&&sessionB==v.sessionB&&device==v.device&&swapchain==v.swapchain&&
               descriptor==v.descriptor&&scene==v.scene&&provider==v.provider&&config==v.config;
    }
    bool operator!=(const Epoch& v) const noexcept {return !(*this==v);}
};
struct FrameKey { Epoch epoch; std::uint64_t sequence=0,captureNs=0; };
enum class Origin { LiveModel, Fixture, OfflineImage, ManualSnapshot };
enum class Facing { Unknown }; // Deliberately no unqualified front/back heuristic.
enum class BoxGeometry { VisibleLandmarks, EstimatedPersonRegion, DetectorBox };
struct Detection { Rect body; double score=0; std::uint32_t classId=0; Pose pose{}; bool hasPose=false; BoxGeometry geometry=BoxGeometry::VisibleLandmarks; };
struct TrackItem {
    std::uint64_t id=0; Rect body; double score=0; std::uint32_t classId=0;
    Pose pose{}; bool hasPose=false; Facing facing=Facing::Unknown; BoxGeometry geometry=BoxGeometry::VisibleLandmarks;
    // Actual detector/pose capture times, independent of the tracked image.
    bool motionTracked=false;std::uint64_t confirmedNs=0,poseNs=0;Point velocity{};
};
struct Snapshot {
    FrameKey key; Origin origin=Origin::Fixture; std::size_t count=0;
    std::array<TrackItem,MaxInstances> items{};
};
struct DisplayContext {
    Epoch epoch; std::uint64_t frameSequence=0,nowNs=0;
    bool enabled=false,fgActive=false,postFgQualified=false;
    unsigned colorPolicy=0; // 0 SDR UNORM, 1 PQ, 2 scRGB, 4 SDR SRGB attachment.
    // Set only at a verified application-image boundary, never from output FPS.
    std::uint64_t realFrameIntervalNs=0;
};
enum class Admission { Ready,Disabled,Stale,Future,WrongEpoch,UnqualifiedFg,NotLive,Malformed };
inline bool ValidSnapshot(const Snapshot& s) noexcept {
    if(s.count>MaxInstances) return false;
    for(std::size_t i=0;i<s.count;++i) {
        const auto& item=s.items[i];
        if(!item.id||!ValidRect(item.body)||!ScoreValid(item.score)||item.facing!=Facing::Unknown||
           (item.hasPose&&!ValidPose(item.pose))) return false;
        if(item.motionTracked&&(!item.confirmedNs||item.confirmedNs>s.key.captureNs||!Finite(item.velocity)||
            std::abs(item.velocity.x)>2||std::abs(item.velocity.y)>2||
            (item.hasPose&&(!item.poseNs||item.poseNs>s.key.captureNs))))return false;
        for(std::size_t j=0;j<i;++j) if(item.id==s.items[j].id) return false;
    }
    return true;
}
inline Admission Admit(const Snapshot& s,const DisplayContext& d,std::uint64_t maxAgeNs=DisplayTtlNs) noexcept {
    if (!d.enabled) return Admission::Disabled;
    if (s.origin!=Origin::LiveModel) return Admission::NotLive;
    if (s.key.epoch!=d.epoch) return Admission::WrongEpoch;
    if (!s.key.sequence || !ValidSnapshot(s)) return Admission::Malformed;
    if (s.key.sequence>d.frameSequence || s.key.captureNs>d.nowNs) return Admission::Future;
    if (d.nowNs-s.key.captureNs>std::min(maxAgeNs,DisplayTtlNs)) return Admission::Stale;
    for(std::size_t i=0;i<s.count;++i)if(s.items[i].motionTracked&&
        (d.nowNs-s.key.captureNs>100'000'000||d.nowNs-s.items[i].confirmedNs>350'000'000))return Admission::Stale;
    if (d.fgActive&&!d.postFgQualified) return Admission::UnqualifiedFg;
    return Admission::Ready;
}
inline std::string SanitizeLabel(std::string_view s,std::size_t cap=64) {
    // ASCII reference labels only; production localization owns validated UTF-8, not byte truncation.
    std::string out; out.reserve(std::min(s.size(),cap));
    for (unsigned char c : s) {
        if (out.size()==cap) break;
        out.push_back(c>=32 && c<127 ? static_cast<char>(c) : ' ');
    }
    return out;
}
class GateTimer {
    bool armed_=false; std::uint64_t last_=0;
public:
    bool Due(bool enabled,std::uint64_t nowNs,unsigned intervalMs=100) noexcept {
        if (!enabled) {armed_=false;return false;}
        const auto interval=static_cast<std::uint64_t>(std::clamp(intervalMs,20u,200u))*1'000'000;
        if (!armed_||nowNs<last_||nowNs-last_>=interval) {armed_=true;last_=nowNs;return true;}
        return false;
    }
};
// Single-owner reference association. Call ONLY for completed detector samples, never per Present.
// No motion prediction, optical flow, GPU tracking, persistent game IDs or performance guarantees.
class SampleTracker {
    struct Entry { bool used=false;TrackItem item;std::uint64_t observedNs=0; };
    std::array<Entry,MaxInstances> entries_{};
    FrameKey last_{}; bool initialized_=false; std::uint64_t nextId_=0;
public:
    std::optional<Snapshot> Update(FrameKey key,const Detection* input,std::size_t count,
                                    Origin origin=Origin::Fixture) noexcept {
        if (!key.sequence||count>MaxInstances||(count&&!input)) return {};
        for (std::size_t i=0;i<count;++i)
            if(!ValidRect(input[i].body)||!ScoreValid(input[i].score)||(input[i].hasPose&&!ValidPose(input[i].pose))) return {};
        if (initialized_&&key.epoch==last_.epoch&&
            (key.sequence<=last_.sequence||key.captureNs<=last_.captureNs)) return {};
        if (count>std::numeric_limits<std::uint64_t>::max()-nextId_) return {};
        if (!initialized_||key.epoch!=last_.epoch) entries_={};
        for (auto& e:entries_) if(e.used&&(key.captureNs<e.observedNs||key.captureNs-e.observedNs>AssociationTtlNs)) e.used=false;
        Snapshot out{};out.key=key;out.origin=origin;out.count=count;
        std::array<bool,MaxInstances> used{};
        std::array<std::size_t,MaxInstances> order{};
        for(std::size_t i=0;i<count;++i) order[i]=i;
        std::sort(order.begin(),order.begin()+count,[&](auto a,auto b){
            return input[a].score!=input[b].score ? input[a].score>input[b].score : a<b;
        });
        auto previous=entries_; // A new detection cannot match another detection created in this sample.
        for (std::size_t k=0;k<count;++k) {
            const auto i=order[k];const auto& d=input[i];
            double best=0,second=0;std::size_t match=MaxInstances;
            for (std::size_t j=0;j<MaxInstances;++j) {
                if (!previous[j].used||used[j]||previous[j].item.classId!=d.classId) continue;
                const double score=IoU(d.body,previous[j].item.body);
                if(score>best) {second=best;best=score;match=j;} else second=std::max(second,score);
            }
            // On ambiguity deliberately issue a new ID. Do not silently transfer a selected character.
            if(best<.3 || (second>0 && best-second<.05)) match=MaxInstances;
            if(match<MaxInstances) {
                // A confident detector score does not prove identity. Reject a near-tie
                // from another current detection, even if score sorting visits it later.
                for(std::size_t other=0;other<count;++other) {
                    if(other==i||input[other].classId!=d.classId) continue;
                    const double competing=IoU(input[other].body,previous[match].item.body);
                    if(competing>=.3 && competing>=best-.05) {match=MaxInstances;break;}
                }
            }
            std::uint64_t id=0;
            if(match<MaxInstances) {used[match]=true;id=previous[match].item.id;}
            else {id=++nextId_;}
            out.items[i]={id,d.body,d.score,d.classId,d.pose,d.hasPose,Facing::Unknown,d.geometry};
        }
        // Current observations first, then unobserved entries within the association TTL.
        std::array<Entry,MaxInstances> next{};std::size_t n=0;
        for(std::size_t i=0;i<count;++i) next[n++]={true,out.items[i],key.captureNs};
        for(std::size_t j=0;j<MaxInstances&&n<MaxInstances;++j)
            if(previous[j].used&&!used[j]) next[n++]=previous[j];
        entries_=next;last_=key;initialized_=true;return out;
    }
};
} // namespace Neurotic::Semantic::Character
