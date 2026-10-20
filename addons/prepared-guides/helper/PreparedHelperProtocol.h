#pragma once
#include <dlssnr/PreparedTransportProtocol.h>
#include <nr/context/PreparedGuideNormalization.h>
#include <optional>
#include <type_traits>
namespace Neurotic::Feed::Prepared::Helper
{
inline constexpr std::uint32_t Magic=0x4850524e,Version=1;
inline constexpr std::uint64_t MaxPayloadBytes=160ULL*1024*1024;
enum Flags:std::uint32_t {CallerReadbackCompleted=1,Cancel=2};
enum Diagnostics:std::uint32_t {DistrustDiagnosticOnly=1,DlaaEvaluated=2,DistrustPublicDlaa=4};
enum class Reason:std::uint32_t {None,InvalidMessage,InvalidPayload,StaleFrame,Cancelled,HostRefused,ReadbackFailed};
#pragma pack(push,8)
struct Header {std::uint32_t magic=Magic,version=Version,bytes=0,kind=0;std::uint64_t payloadBytes=0;};
struct Request {Header header{Magic,Version,sizeof(Request),1,0};Descriptor guides{};
    std::uint64_t timestampQpc=0;std::uint32_t flags=CallerReadbackCompleted,reserved=0;};
struct Reply {Header header{Magic,Version,sizeof(Reply),2,0};Ipc::Result result{};
    std::uint32_t width=0,height=0,diagnostics=0,reserved=0;};
#pragma pack(pop)
static_assert(sizeof(Header)==24&&sizeof(Request)==288&&sizeof(Reply)==136);
static_assert(std::is_standard_layout_v<Request>&&std::is_trivially_copyable_v<Request>);
static_assert(std::is_standard_layout_v<Reply>&&std::is_trivially_copyable_v<Reply>);
inline std::optional<std::uint64_t> PayloadBytes(const Descriptor& d) {
    const std::uint64_t color=std::uint64_t(d.color.width)*d.color.height,motion=std::uint64_t(d.motion.width)*d.motion.height;
    if(!color||!motion||color>MaxPayloadBytes/8||motion>MaxPayloadBytes/8)return {};
    const auto bytes=color*(8+((d.flags&HasDistrust)?4:0))+motion*8;
    if(bytes>MaxPayloadBytes)return {};return bytes;
}
inline bool Validate(const Request& request) {
    const auto& h=request.header;
    if(h.magic!=Magic||h.version!=Version||h.bytes!=sizeof(Request)||h.kind!=1||h.payloadBytes>MaxPayloadBytes||
       request.reserved||(request.flags&~(CallerReadbackCompleted|Cancel)))return false;
    if(request.flags&Cancel){
        for(auto value:request.guides.reserved)if(value)return false;
        return request.flags==Cancel&&h.payloadBytes==0&&request.guides.size==sizeof(Descriptor)&&
            request.guides.version==Prepared::Version&&!(request.guides.flags&~(Reset|HasDistrust|HudIncluded|ZeroJitterPolicy))&&
            Ipc::ValidIdentity(Ipc::IdentityOf(request.guides));}
    if(!(request.flags&CallerReadbackCompleted)||!request.timestampQpc||
       request.guides.colorDomain!=Contracts::ColorDomain::EncodedDisplay||!Context::NormalizePreparedGuides(request.guides))return false;
    const auto size=PayloadBytes(request.guides);return size&&*size==h.payloadBytes;
}
inline bool CompletedReply(const Reply& reply,const Request& request) {
    const bool mask=(request.guides.flags&HasDistrust)!=0,dlaa=(reply.diagnostics&DlaaEvaluated)!=0;
    const auto expected=(dlaa?DlaaEvaluated:0u)|(mask?(dlaa?DistrustPublicDlaa:DistrustDiagnosticOnly):0u);
    Ipc::Frame frame;frame.guides=request.guides;
    return Validate(request)&&!(request.flags&Cancel)&&reply.header.magic==Magic&&reply.header.version==Version&&reply.header.kind==2&&
        reply.header.bytes==sizeof(Reply)&&reply.reserved==0&&reply.diagnostics==expected&&
        reply.width==request.guides.color.width&&reply.height==request.guides.color.height&&
        reply.header.payloadBytes==std::uint64_t(reply.width)*reply.height*4&&Ipc::AcceptCompleted(frame,reply.result);
}
class Sequence {
    Descriptor previous_{};std::uint64_t timestamp_=0;bool present_=false;
    bool Episode(const Descriptor& d)const{return previous_.producer==d.producer&&previous_.session==d.session&&
        previous_.stream==d.stream&&previous_.device==d.device&&previous_.generation==d.generation;}
public:
    bool NeedsReset(const Request& request)const{return !present_||!Context::PreparedHistoryContinuous(previous_,request.guides);}
    bool Admit(const Request& request){
        if(!Validate(request)||request.flags&Cancel)return false;
        if(present_&&((Episode(request.guides)&&(request.guides.capture<=previous_.capture||request.timestampQpc<=timestamp_))||
           (!Episode(request.guides)&&!(request.guides.flags&Reset))))return false;
        previous_=request.guides;timestamp_=request.timestampQpc;present_=true;return true;
    }
};
}
