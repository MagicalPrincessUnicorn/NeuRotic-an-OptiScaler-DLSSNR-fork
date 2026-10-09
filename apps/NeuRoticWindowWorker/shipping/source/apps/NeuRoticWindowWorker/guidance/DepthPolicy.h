// GPU-independent policy adapted from the user-supplied MIT DAV2 packet.
// Original packet notices are retained under references/depth-anything-v2/20261005.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
namespace nrw::depth {
enum class Provider { Off, Dav2 };
enum class Profile { Reference, Fast };
struct Settings {Provider provider=Provider::Off;Profile profile=Profile::Reference;unsigned hz=30;bool preview=false;};
inline bool ValidSettings(const Settings& s) noexcept {
    return (s.provider==Provider::Off||s.provider==Provider::Dav2)&&
        (s.profile==Profile::Reference||s.profile==Profile::Fast)&&(s.hz==30||s.hz==60)&&
        (!s.preview||s.provider==Provider::Dav2);
}
inline bool NeedsNrModel(const Settings& s) noexcept {return !s.preview||s.provider!=Provider::Dav2;}
// Publication freshness never proves GPU retirement. Bound retained-color latency to 250 ms.
inline bool PublicationFresh(uint64_t capture,uint64_t now,uint64_t frequency)noexcept{
    return capture&&frequency&&now>=capture&&now-capture<=frequency/4;
}
struct FrameKey {
    std::uint64_t sourceId{},streamEpoch{},captureSequence{},geometryEpoch{},configEpoch{};
    bool operator==(const FrameKey& other)const noexcept{return sourceId==other.sourceId&&streamEpoch==other.streamEpoch&&captureSequence==other.captureSequence&&geometryEpoch==other.geometryEpoch&&configEpoch==other.configEpoch;}
    bool operator!=(const FrameKey& other)const noexcept{return !(*this==other);}
    bool Valid()const noexcept{return sourceId&&streamEpoch&&captureSequence&&geometryEpoch&&configEpoch;}
};
enum class Encoding {Unknown,EstimatedRelativeRaw,EstimatedRelativeNearHigh01};
struct Receipt {FrameKey key;Encoding encoding=Encoding::Unknown;bool completed=false,numericValid=false;};
enum class Admission {Accepted,InvalidIdentity,WrongFrame,NotComplete,InvalidNumeric,UnknownEncoding,ConsumerUnsupported};
inline Admission Validate(const Receipt& r,const FrameKey& wanted,bool supported)noexcept {
    if(!r.key.Valid()||!wanted.Valid())return Admission::InvalidIdentity;
    if(r.key!=wanted)return Admission::WrongFrame;
    if(!r.completed)return Admission::NotComplete;
    if(!r.numericValid)return Admission::InvalidNumeric;
    if(r.encoding!=Encoding::EstimatedRelativeRaw&&r.encoding!=Encoding::EstimatedRelativeNearHigh01)return Admission::UnknownEncoding;
    return supported?Admission::Accepted:Admission::ConsumerUnsupported;
}
inline bool ValidTensorShape(unsigned w,unsigned h)noexcept{return w>=14&&h>=14&&w<=2048&&h<=2048&&w%14==0&&h%14==0;}
struct Letterbox {unsigned sourceWidth,sourceHeight,tensorWidth,tensorHeight,contentWidth,contentHeight,left,top;};
inline Letterbox MakeLetterbox(unsigned sw,unsigned sh,unsigned tw,unsigned th){
    if(!sw||!sh||sw>16384||sh>16384||!ValidTensorShape(tw,th))throw std::invalid_argument("Invalid source/model shape");
    const double scale=std::min(double(tw)/sw,double(th)/sh);
    const auto cw=std::clamp(static_cast<unsigned>(std::floor(sw*scale+.5)),1u,tw),ch=std::clamp(static_cast<unsigned>(std::floor(sh*scale+.5)),1u,th);
    return {sw,sh,tw,th,cw,ch,(tw-cw)/2,(th-ch)/2};
}
enum class Stage {NeverSubmitted,Submitted,Complete,Untrackable};
struct CompletionEvidence {Stage preprocess{},inference{},materialize{},consumer{};};
inline bool Retired(Stage s)noexcept{return s==Stage::NeverSubmitted||s==Stage::Complete;}
inline bool CanRetire(const CompletionEvidence& e)noexcept{return Retired(e.preprocess)&&Retired(e.inference)&&Retired(e.materialize)&&Retired(e.consumer);}
struct NormalizedValue {float value{};bool valid=false;};
inline NormalizedValue NormalizeRelative(float value,float low,float high)noexcept {
    const float range=high-low;
    if(!std::isfinite(value)||value<0||!std::isfinite(low)||!std::isfinite(high)||!std::isfinite(range)||range<=1e-6f)return {};
    const float result=(value-low)/range;
    if(!std::isfinite(result))return {};
    return {std::clamp(result,0.f,1.f),true};
}
class FreshCounter {
    FrameKey last_{};std::uint64_t count_=0;
public:
    bool Record(const FrameKey& key)noexcept {
        if(!key.Valid())return false;
        if(last_.Valid()&&(key.sourceId!=last_.sourceId||key.streamEpoch<last_.streamEpoch||
           (key.streamEpoch==last_.streamEpoch&&key.captureSequence<=last_.captureSequence)))return false;
        last_=key;++count_;return true;
    }
    std::uint64_t Count()const noexcept{return count_;}
    void Reset()noexcept{last_={};count_=0;}
};
// Metadata only: the host retains source/GPU allocations independently until retirement.
class JobQueue {
    std::optional<FrameKey> active_,pending_;std::uint64_t dropped_=0;bool closed_=false;
public:
    bool Offer(const FrameKey& key)noexcept {
        if(closed_||!key.Valid()||active_==key||pending_==key)return false;
        if(pending_)++dropped_;
        pending_=key;return true;
    }
    std::optional<FrameKey> Begin()noexcept {
        if(closed_||active_||!pending_)return {};
        active_=pending_;pending_.reset();return active_;
    }
    bool Finish(const FrameKey& key)noexcept {if(active_!=key)return false;active_.reset();return true;}
    void Invalidate()noexcept{closed_=true;if(pending_){++dropped_;pending_.reset();}}
    void DiscardPending()noexcept{if(pending_){++dropped_;pending_.reset();}}
    std::optional<FrameKey> Active()const noexcept{return active_;}
    std::optional<FrameKey> Pending()const noexcept{return pending_;}
    std::uint64_t Dropped()const noexcept{return dropped_;}
};
}
