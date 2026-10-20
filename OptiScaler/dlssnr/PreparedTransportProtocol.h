#pragma once
#include <inputs/universal_feeder/PreparedGuideContract.h>
#include <type_traits>

namespace Neurotic::Feed::Prepared::Ipc
{
inline constexpr std::uint32_t Magic=0x4750524e,ProtocolVersion=1,MaxBytes=512;
enum class Kind:std::uint32_t { Frame=1,Result=2 };
enum class Outcome:std::uint32_t { Pending=0,CompletedSuccess=1,Failed=2,Cancelled=3 };
enum Facts:std::uint32_t { Recorded=1,Submitted=2,GpuCompleted=4,OutputValid=8 };
#pragma pack(push,8)
struct Header { std::uint32_t magic=Magic,version=ProtocolVersion,bytes=0;Kind kind=Kind::Frame; };
struct Identity {
    std::uint64_t producer=0,session=0,stream=0,device=0,generation=0,capture=0;
    bool operator==(const Identity&)const=default;
};
struct Frame { Header header{Magic,ProtocolVersion,sizeof(Frame),Kind::Frame};Descriptor guides{}; };
struct Result {
    Header header{Magic,ProtocolVersion,sizeof(Result),Kind::Result};Identity identity{};
    std::uint64_t outputCapture=0,completionValue=0;
    Outcome state=Outcome::Pending;std::uint32_t facts=0;
    std::uint32_t reason=0,reserved=0;
};
#pragma pack(pop)
static_assert(sizeof(Header)==16 && sizeof(Frame)==264 && sizeof(Result)==96);
static_assert(sizeof(Frame)<=MaxBytes && sizeof(Result)<=MaxBytes);
static_assert(std::is_trivially_copyable_v<Frame> && std::is_standard_layout_v<Frame>);
static_assert(std::is_trivially_copyable_v<Result> && std::is_standard_layout_v<Result>);
inline Identity IdentityOf(const Descriptor& d) { return {d.producer,d.session,d.stream,d.device,d.generation,d.capture}; }
inline bool ValidIdentity(const Identity& i) { return i.producer&&i.session&&i.stream&&i.device&&i.generation&&i.capture; }
inline bool ValidHeader(const Header& h,Kind kind,std::uint32_t size) {
    return h.magic==Magic&&h.version==ProtocolVersion&&h.bytes==size&&size<=MaxBytes&&h.kind==kind;
}
// Envelope validation is not guide semantic qualification, handle ownership or
// GPU completion. The receiving acquisition/resource owners still admit them.
inline bool Valid(const Frame& f) {
    if(!ValidHeader(f.header,Kind::Frame,sizeof(Frame))||f.guides.size!=sizeof(Descriptor)||
       f.guides.version!=Version||!ValidIdentity(IdentityOf(f.guides)))return false;
    for(auto v:f.guides.reserved)if(v)return false;
    return true;
}
inline bool Valid(const Result& r) {
    if(!ValidHeader(r.header,Kind::Result,sizeof(Result))||!ValidIdentity(r.identity)||r.reserved||
       (r.facts&~(Recorded|Submitted|GpuCompleted|OutputValid)))return false;
    if(((r.facts&Submitted)&&!(r.facts&Recorded))||
       ((r.facts&GpuCompleted)&&!(r.facts&Submitted)))return false;
    switch(r.state) {
    case Outcome::Pending:case Outcome::Failed:case Outcome::Cancelled:return !(r.facts&OutputValid);
    case Outcome::CompletedSuccess:return r.facts==(Recorded|Submitted|GpuCompleted|OutputValid)&&
        r.outputCapture==r.identity.capture&&r.completionValue!=0&&r.completionValue!=UINT64_MAX;
    default:return false;
    }
}
// A fence signal alone cannot produce this receipt. Even an accepted receipt
// needs the receiver's authenticated session and actual GPU-completion proof.
inline bool AcceptCompleted(const Frame& f,const Result& r) {
    return Valid(f)&&Valid(r)&&r.identity==IdentityOf(f.guides)&&r.state==Outcome::CompletedSuccess;
}
}
