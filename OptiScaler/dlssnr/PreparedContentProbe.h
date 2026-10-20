#pragma once
#include "PreparedTransportProtocol.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace DlssNr::PreparedContent
{
using Identity=Neurotic::Feed::Prepared::Ipc::Identity;
inline Identity IdentityOf(const Neurotic::Feed::Prepared::Descriptor& d) {return Neurotic::Feed::Prepared::Ipc::IdentityOf(d);}
inline constexpr std::size_t StageCount=6,SamplePoints=16,MaxPixelBytes=16,MaxSampleBytes=SamplePoints*MaxPixelBytes;
enum class Stage:std::uint32_t {Capture,Prepared,Processed,CopyBack,PresentAttempt,ObservedDisplay};
enum class Mode:std::uint32_t {Processed,TransportOnlyPattern};
enum class Evidence:std::uint32_t {CompletedReadback,DisplayObservation};
enum class Code:std::uint32_t {Accepted,InvalidSample,InvalidEvidence,IdentityMismatch,StaleCapture,StaleMask,
    BindingMismatch,StageOrder,RepresentationMismatch,ContentMismatch,PatternMismatch,
    SuspectedStaleOutput,PresentAttemptOnly,DisplaySampleObserved};
struct ResourceIdentity {
    std::uint64_t device=0,generation=0,handle=0;
    bool operator==(const ResourceIdentity&)const=default;
};
using Bindings=std::array<ResourceIdentity,StageCount>;
// Borrowed mapped readback. The owner must establish actual completion before
// passing it here; this diagnostic neither waits nor grants GPU/resource rights.
// representation identifies typed format, color transfer and channel ordering.
struct View {
    std::span<const std::byte> bytes;std::uint64_t rowPitch=0;
    std::uint32_t width=0,height=0,pixelBytes=0,representation=0;bool completed=false;
};
struct Observation {Identity identity{};Stage stage=Stage::Capture;ResourceIdentity resource{};
    std::uint64_t maskCapture=0;View view{};Evidence evidence=Evidence::CompletedReadback;};
struct Report {
    Code code=Code::InvalidSample;bool accepted=false,displayObserved=false;
    std::uint64_t digest=0;std::uint32_t sampleBytes=0;
    bool inputChanged=false,outputRepeated=false;
};
// Digest equality concerns bounded samples only; it is not whole-image equality.
// displayObserved means a supplied DisplayObservation sample was matched, never
// that a successful API Present return established displayed pixels.
inline bool ValidView(const View& v) {
    if(!v.completed||!v.width||!v.height||v.width>16384||v.height>16384||!v.pixelBytes||v.pixelBytes>MaxPixelBytes||!v.representation)return false;
    const std::uint64_t rowBytes=std::uint64_t(v.width)*v.pixelBytes;
    if(v.rowPitch<rowBytes)return false;
    const std::uint64_t rows=v.height-1;
    // Division validates arithmetic before constructing a potentially overflowing address.
    if(rowBytes>v.bytes.size()||(rows&&v.rowPitch>(v.bytes.size()-rowBytes)/rows))return false;
    return true;
}
inline std::uint64_t Mix(std::uint64_t v) {
    v^=v>>30;v*=0xbf58476d1ce4e5b9ULL;v^=v>>27;v*=0x94d049bb133111ebULL;return v^(v>>31);
}
// Deterministic byte pattern for an explicit transport-only diagnostic texture.
// The same pattern is expected at every stage only in that mode. Never overwrite
// game/model inputs with it outside the caller's selected transport-only test.
inline std::byte PatternByte(const Identity& id,std::uint32_t x,std::uint32_t y,std::uint32_t channel) {
    auto seed=Mix(id.producer)^Mix(id.session+1)^Mix(id.stream+2)^Mix(id.device+3)^Mix(id.generation+4)^Mix(id.capture+5);
    return std::byte(Mix(seed^(std::uint64_t(x)<<32)^y^(std::uint64_t(channel)*0x9e3779b97f4a7c15ULL))&255);
}
inline bool FillPattern(const Identity& id,std::span<std::byte> bytes,std::uint64_t rowPitch,
    std::uint32_t width,std::uint32_t height,std::uint32_t pixelBytes) {
    View view{bytes,rowPitch,width,height,pixelBytes,1,true};
    if(!Neurotic::Feed::Prepared::Ipc::ValidIdentity(id)||!ValidView(view))return false;
    for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x)for(std::uint32_t c=0;c<pixelBytes;++c)
        bytes[static_cast<std::size_t>(std::uint64_t(y)*rowPitch+std::uint64_t(x)*pixelBytes+c)]=PatternByte(id,x,y,c);
    return true;
}
// One bounded probe per serialized transport stream/slot. Retains only six
// hashes and previous input/output hashes, never readback buffers or GPU objects.
// Reinitialization revokes earlier capture observations. Pipeline clients keep a
// probe with the corresponding slot until its copy-back observation is collected.
class Probe {
    struct Sample {std::uint64_t digest=0;std::uint32_t width=0,height=0,pixelBytes=0,representation=0;};
    Identity identity_{};Bindings bindings_{};Mode mode_=Mode::Processed;
    std::array<Sample,StageCount> stages_{};std::size_t next_=0;bool active_=false,hasMask_=false;
    Sample previousInput_{},previousOutput_{};bool previousAvailable_=false;
    static bool SameEpisode(const Identity& a,const Identity& b) {return a.producer==b.producer&&a.session==b.session&&
        a.stream==b.stream&&a.device==b.device&&a.generation==b.generation;}
    static bool SameRepresentation(const Sample& a,const Sample& b) {return a.width==b.width&&a.height==b.height&&
        a.pixelBytes==b.pixelBytes&&a.representation==b.representation;}
    static Report Refuse(Code code){Report r{};r.code=code;return r;}
public:
    Report Begin(const Neurotic::Feed::Prepared::Descriptor& descriptor,const Bindings& bindings,Mode mode) {
        using namespace Neurotic::Feed::Prepared;
        if(descriptor.size!=sizeof(Descriptor)||descriptor.version!=Version||!Ipc::ValidIdentity(IdentityOf(descriptor)))return Refuse(Code::IdentityMismatch);
        if(mode!=Mode::Processed&&mode!=Mode::TransportOnlyPattern)return Refuse(Code::InvalidSample);
        if((descriptor.flags&HasDistrust)&&descriptor.maskCapture!=descriptor.capture)return Refuse(Code::StaleMask);
        const auto identity=IdentityOf(descriptor);
        if(active_&&SameEpisode(identity,identity_)&&identity.capture<=identity_.capture)return Refuse(Code::StaleCapture);
        // The display observation can be supplied by a different device/system,
        // but its binding must be declared explicitly like every other stage.
        for(std::size_t i=0;i<StageCount-1;++i){const auto& r=bindings[i];
            if(!r.device||!r.generation||!r.handle)return Refuse(Code::BindingMismatch);}
        // Display capture is optional. Without a declared display binding the
        // first five stages remain usable and observed display is unavailable.
        previousAvailable_=active_&&SameEpisode(identity,identity_)&&mode_==mode&&next_>static_cast<std::size_t>(Stage::Processed);
        if(previousAvailable_){previousInput_=stages_[0];previousOutput_=stages_[2];}
        identity_=identity;bindings_=bindings;mode_=mode;next_=0;active_=true;hasMask_=(descriptor.flags&HasDistrust)!=0;stages_={};
        Report r{};r.code=Code::Accepted;r.accepted=true;return r;
    }
    Report Observe(const Observation& observation) {
        if(!active_||!SameEpisode(observation.identity,identity_))return Refuse(Code::IdentityMismatch);
        if(observation.identity.capture!=identity_.capture)return Refuse(observation.identity.capture<identity_.capture?Code::StaleCapture:Code::IdentityMismatch);
        auto stage=static_cast<std::size_t>(observation.stage);
        if(stage>=StageCount||stage!=next_)return Refuse(Code::StageOrder);
        if(!observation.resource.device||!observation.resource.generation||!observation.resource.handle||
           observation.resource!=bindings_[stage])return Refuse(Code::BindingMismatch);
        if(hasMask_&&observation.maskCapture!=identity_.capture)return Refuse(Code::StaleMask);
        if(!ValidView(observation.view))return Refuse(Code::InvalidSample);
        if((stage==static_cast<std::size_t>(Stage::ObservedDisplay))!=(observation.evidence==Evidence::DisplayObservation)||
            (observation.evidence!=Evidence::DisplayObservation&&observation.evidence!=Evidence::CompletedReadback))return Refuse(Code::InvalidEvidence);
        const auto& view=observation.view;Sample sample{14695981039346656037ULL,view.width,view.height,view.pixelBytes,view.representation};
        std::uint32_t sampled=0;bool pattern=true;
        for(std::uint32_t i=0;i<SamplePoints;++i){const auto x=(view.width-1)*(i%4)/3,y=(view.height-1)*(i/4)/3;
            for(std::uint32_t c=0;c<view.pixelBytes;++c){auto byte=view.bytes[static_cast<std::size_t>(std::uint64_t(y)*view.rowPitch+std::uint64_t(x)*view.pixelBytes+c)];
                sample.digest^=std::to_integer<std::uint8_t>(byte);sample.digest*=1099511628211ULL;++sampled;
                if(mode_==Mode::TransportOnlyPattern&&byte!=PatternByte(identity_,x,y,c))pattern=false;}}
        if(!pattern)return Refuse(Code::PatternMismatch);
        // The model may edit its output. Compare delivery with actual postprocess
        // bytes, never with original input. Different representations need caller
        // normalization before byte identity can establish conformance.
        if(stage>=static_cast<std::size_t>(Stage::CopyBack)){
            const auto& expected=stages_[stage-1];
            if(!SameRepresentation(sample,expected))return Refuse(Code::RepresentationMismatch);
            if(sample.digest!=expected.digest)return Refuse(Code::ContentMismatch);
        }
        stages_[stage]=sample;++next_;
        Report report{};report.accepted=true;report.digest=sample.digest;report.sampleBytes=sampled;report.code=Code::Accepted;
        if(stage==static_cast<std::size_t>(Stage::Processed)&&previousAvailable_&&
           SameRepresentation(stages_[0],previousInput_)&&SameRepresentation(sample,previousOutput_)){
            report.inputChanged=stages_[0].digest!=previousInput_.digest;report.outputRepeated=sample.digest==previousOutput_.digest;
            // A constant/quantized model answer is possible. This is a suspicion
            // for diagnosis, not proof that a consumer failed or output is stale.
            if(report.inputChanged&&report.outputRepeated)report.code=Code::SuspectedStaleOutput;
        }
        if(stage==static_cast<std::size_t>(Stage::PresentAttempt))report.code=Code::PresentAttemptOnly;
        if(stage==static_cast<std::size_t>(Stage::ObservedDisplay)){report.code=Code::DisplaySampleObserved;report.displayObserved=true;}
        return report;
    }
};
}
