#pragma once
#include "../../nr/context/PreparedRasterBounds.h"
// PROPOSAL: GPU-helper v2 control subrecord, not the existing CPU v1 protocol.
// This codec is NOT authentication, descriptor normalization, handle validation,
// or GPU completion proof. The existing serialized resource owner supplies those.
#include <array>
#include <cstdint>
#include <span>
#include <limits>

namespace Neurotic::Connections {
struct Identity {
    std::uint64_t producer=0,session=0,stream=0,device=0,generation=0,capture=0;
    bool operator==(const Identity&)const=default;
};
struct FrameTicket {
    Identity identity{};
    std::uint32_t slot=0,flags=0; // bit0 reset; bit1 external guides attached
    std::uint64_t producerValue=0,outputValue=0,sequence=0;
    std::array<std::uint64_t,2> nonce{};
    bool operator==(const FrameTicket&)const=default;
};
using TicketBytes=std::array<std::uint8_t,120>;
inline constexpr std::uint32_t TicketMagic=0x3247524eU; // ASCII NRG2
inline constexpr std::uint16_t TicketVersion=2,TicketKind=3;
namespace detail {
inline void Put(std::span<std::uint8_t> b,std::size_t off,std::uint64_t v,unsigned n) noexcept {
    for(unsigned i=0;i<n;++i)b[off+i]=static_cast<std::uint8_t>(v>>(8*i));
}
inline std::uint64_t Get(std::span<const std::uint8_t>b,std::size_t off,unsigned n) noexcept {
    std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(b[off+i])<<(8*i);return v;
}
inline bool Value(std::uint64_t v) noexcept {return v!=0 && v!=std::numeric_limits<std::uint64_t>::max();}
inline bool Prefix(const Identity&a,const Identity&b) noexcept {
    return a.producer==b.producer&&a.session==b.session&&a.stream==b.stream&&a.device==b.device&&a.generation==b.generation;
}
}
inline bool Valid(const FrameTicket&t) noexcept {
    const auto&i=t.identity;
    return i.producer&&i.session&&i.stream&&i.device&&i.generation&&detail::Value(i.capture)&&
        t.slot<2&&!(t.flags&~3U)&&detail::Value(t.producerValue)&&detail::Value(t.outputValue)&&
        detail::Value(t.sequence)&&(t.nonce[0]||t.nonce[1]);
}
// Encode is a lossless field writer, including invalid values for negative fixtures.
// Call Valid before sending. Decode never exposes a partially decoded ticket.
inline TicketBytes Encode(const FrameTicket&t) noexcept {
    TicketBytes b{};auto put=[&](std::size_t o,std::uint64_t v,unsigned n=8){detail::Put(b,o,v,n);};
    put(0,TicketMagic,4);put(4,TicketVersion,2);put(6,TicketKind,2);put(8,b.size(),4);put(12,t.flags,4);
    put(16,t.identity.producer);put(24,t.identity.session);put(32,t.identity.stream);
    put(40,t.identity.device);put(48,t.identity.generation);put(56,t.identity.capture);
    put(64,t.slot,4); // 68..71 reserved
    put(72,t.producerValue);put(80,t.outputValue);put(88,t.sequence);put(96,t.nonce[0]);put(104,t.nonce[1]);
    return b; // 112..119 reserved
}
inline bool Decode(std::span<const std::uint8_t>b,FrameTicket&out) noexcept {
    out={};if(b.size()!=TicketBytes{}.size())return false;
    auto get=[&](std::size_t o,unsigned n=8){return detail::Get(b,o,n);};
    if(get(0,4)!=TicketMagic||get(4,2)!=TicketVersion||get(6,2)!=TicketKind||get(8,4)!=b.size()||get(68,4)||get(112))return false;
    FrameTicket t{};t.flags=static_cast<std::uint32_t>(get(12,4));
    t.identity={get(16),get(24),get(32),get(40),get(48),get(56)};t.slot=static_cast<std::uint32_t>(get(64,4));
    t.producerValue=get(72);t.outputValue=get(80);t.sequence=get(88);t.nonce={get(96),get(104)};
    if (!Valid(t)) return false;
    out=t;
    return true;
}
// One instance per authenticated, pinned peer and existing owner generation.
// Not thread-safe by design; call only under that owner's serialization.
class TicketSequence {
    Identity episode_{};std::array<std::uint64_t,2> nonce_{};
    std::uint64_t lastCapture_=0,lastSequence_=0,lastProducer_=0,lastOutput_=0;
public:
    TicketSequence(Identity episode,std::array<std::uint64_t,2> nonce) noexcept:episode_(episode),nonce_(nonce){}
    bool Accept(const FrameTicket&t) noexcept {
        if(!Valid(t)||!detail::Prefix(episode_,t.identity)||nonce_!=t.nonce||
            t.identity.capture<=lastCapture_||t.sequence<=lastSequence_||
            t.producerValue<=lastProducer_||t.outputValue<=lastOutput_)return false;
        lastCapture_=t.identity.capture;lastSequence_=t.sequence;lastProducer_=t.producerValue;lastOutput_=t.outputValue;return true;
    }
};
// Pure state reducer only: contains no resource list, handles, callbacks, or leases.
// Call Complete only after the owner has independently proven GPU completion.
class LeasePhase {
    enum class Phase {Idle,Reserved,Referenced,Submitted,ValidComplete,InvalidComplete,Leased,Quarantined};
    Phase phase_=Phase::Idle;
    bool Move(Phase from,Phase to) noexcept {if(phase_!=from)return false;phase_=to;return true;}
public:
    bool Begin() noexcept{return Move(Phase::Idle,Phase::Reserved);}
    bool Reference() noexcept{return Move(Phase::Reserved,Phase::Referenced);}
    bool Submit() noexcept{return Move(Phase::Referenced,Phase::Submitted);}
    bool CancelBeforeReference() noexcept{return Move(Phase::Reserved,Phase::Idle);}
    bool Complete(bool outputValid) noexcept{return Move(Phase::Submitted,outputValid?Phase::ValidComplete:Phase::InvalidComplete);}
    bool Deliver() noexcept{return Move(Phase::ValidComplete,Phase::Leased);}
    bool DiscardCompleted() noexcept{return Move(Phase::InvalidComplete,Phase::Idle);}
    bool ReturnOutput(bool copybackCompleteAndStateRestored) noexcept {
        return copybackCompleteAndStateRestored&&Move(Phase::Leased,Phase::Idle);
    }
    void Uncertain() noexcept{phase_=Phase::Quarantined;}
    bool Quarantined()const noexcept{return phase_==Phase::Quarantined;}
};
// Existing CPU-mainline scalar ceiling; format/flow minima/device checks remain separate.
inline bool AdmitRaster(std::uint32_t w,std::uint32_t h) noexcept {
    return Neurotic::PreparedRaster::Admit(w,h);
}
}
