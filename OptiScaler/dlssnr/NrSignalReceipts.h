#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>

namespace DlssNr::SignalReceipts {
// Diagnostic facts only. These local sequence numbers are NOT render-scene
// identities and never grant access, resource lifetime or rendering authority.
enum class Signal : uint8_t { Color, Output, Depth, Motion, Jitter, Exposure,
    PreExposure, History, Evaluation, Delivery, Count };
enum class State { Pending, Off, NotApplicable, Detected, Usable, Consumed, Stale, Unavailable, Blocked };
enum class Stage { Unknown, PreSr, PostSr, PostRr, PostToneMap };
enum class Domain { Unknown, LinearHdr, EncodedDisplay };
enum class Mode { Active, Off, NotApplicable, Failed };
struct Epoch {
    uint64_t lifecycle=0,revision=0,resume=0,input=0,resources=0,resets=0;
    unsigned route=0;
    bool operator==(const Epoch&)const=default;
};
struct Fact {
    bool present=false,admitted=false;
    uint32_t width=0,height=0,format=0;
    Stage stage=Stage::Unknown;
    Domain domain=Domain::Unknown;
    const char* reason=nullptr;
    bool valuesKnown=false;float x=0,y=0;
    uint32_t originX=0,originY=0;
};
struct Reading {State state=State::Pending;Fact fact{};uint64_t sequence=0,age=0;Epoch epoch{};};
struct Proof {bool valid=false,submitted=false,completed=false;};
constexpr uint32_t Bit(Signal s){return 1u<<static_cast<unsigned>(s);}
inline const char* Name(State s) {
    switch(s){case State::Pending:return "Pending runtime / GPU completion";
    case State::Off:return "Off";case State::NotApplicable:return "Not applicable";
    case State::Detected:return "Detected";case State::Usable:return "Validated, not consumed";
    case State::Consumed:return "Consumed (GPU completed)";case State::Stale:return "Stale";
    case State::Unavailable:return "Unavailable";default:return "Blocked";}
}
inline const char* Name(Stage s) {
    switch(s){case Stage::PreSr:return "pre-SR";case Stage::PostSr:return "post-SR";
    case Stage::PostRr:return "post-RR";case Stage::PostToneMap:return "post-tone-map";default:return "unknown stage";}
}
inline const char* Name(Domain d){return d==Domain::LinearHdr?"linear HDR":d==Domain::EncodedDisplay?"encoded display":"unknown color domain";}

inline std::string Describe(const Reading& r) {
    return std::string(Name(r.state))+"; "+Name(r.fact.stage)+"; "+Name(r.fact.domain)+
        "; extent "+std::to_string(r.fact.width)+"x"+std::to_string(r.fact.height)+
        "; origin "+std::to_string(r.fact.originX)+","+std::to_string(r.fact.originY)+
        (r.fact.valuesKnown?"; model values ("+std::to_string(r.fact.x)+", "+std::to_string(r.fact.y)+")":"")+
        "; route "+std::to_string(r.epoch.route)+"; format "+(r.fact.format?std::to_string(r.fact.format):"unknown")+
        "; age "+std::to_string(r.age)+" ms; lifecycle "+std::to_string(r.epoch.lifecycle)+
        "; revision "+std::to_string(r.epoch.revision)+"; resources "+std::to_string(r.epoch.resources)+
        "; observation "+std::to_string(r.sequence)+". "+(r.fact.reason?r.fact.reason:
        !r.fact.present?"No current producer observation.":"No semantic scene identity is implied by this diagnostic receipt.");
}

// The caller serializes this owner and polls its actual recording authority.
// Bounded dropped diagnostics do not release the renderer's resource ownership.
template<class Ticket> class Tracker {
    static constexpr size_t Count=static_cast<size_t>(Signal::Count);
    struct Sample {uint64_t sequence=0,time=0;std::array<Fact,Count> facts{};uint32_t queued=0,consumed=0;};
    struct Pending {Ticket ticket{};Sample sample{};};
    Epoch epoch_{};bool initialized_=false;
    uint64_t serial_=0;
    Sample current_{};
    std::array<Fact,Count> previous_{};
    std::array<Sample,Count> used_{};
    std::array<Pending,32> pending_{};
    void Match(Epoch epoch){if(!initialized_||epoch_!=epoch){Clear();epoch_=epoch;initialized_=true;}}
    void Clear(){current_={};previous_={};used_={};pending_={};}
public:
    uint64_t Begin(Epoch epoch,uint64_t now){Match(epoch);previous_=current_.facts;current_={};current_.sequence=++serial_;current_.time=now;return serial_;}
    void Observe(uint64_t sequence,Signal signal,Fact fact){if(sequence==current_.sequence){
        const auto& old=previous_[size_t(signal)];
        if(old.present!=fact.present||old.width!=fact.width||old.height!=fact.height||old.format!=fact.format||old.stage!=fact.stage||old.domain!=fact.domain){
            used_={};pending_={}; // a changed raster/domain/optional source revokes old frame receipts
        }
        current_.facts[size_t(signal)]=fact;
    }}
    void Values(uint64_t sequence,Signal signal,float x,float y){if(sequence==current_.sequence){
        auto& f=current_.facts[size_t(signal)];if(f.present){f.valuesKnown=true;f.x=x;f.y=y;}}}
    void Admit(uint64_t sequence,Signal signal){if(sequence==current_.sequence){auto& f=current_.facts[size_t(signal)];
        if(f.present){f.admitted=true;f.reason="Resource and semantic admission accepted";}}}
    void Reject(uint64_t sequence,Signal signal,const char* why){if(sequence==current_.sequence){auto& f=current_.facts[size_t(signal)];f.admitted=false;f.reason=why;current_.queued&=~Bit(signal);used_[size_t(signal)]={};
        for(auto& p:pending_)p.sample.queued&=~Bit(signal);
    }}
    void Record(uint64_t sequence,Ticket ticket,uint32_t mask){
        if(!ticket||sequence!=current_.sequence)return;
        for(size_t i=0;i<Count;++i)if(!current_.facts[i].admitted)mask&=~(1u<<i);
        if(!mask)return;
        for(auto& p:pending_)if(!p.ticket){current_.queued|=mask;p={ticket,current_};p.sample.queued=mask;return;}
        // Full observation storage is not a completion receipt.
    }
    template<class Inspect> void Poll(Inspect inspect){for(auto& p:pending_){
        if(!p.ticket)continue;const auto proof=inspect(p.ticket);
        if(proof.valid&&proof.submitted&&proof.completed){
            for(size_t i=0;i<Count;++i)if((p.sample.queued&(1u<<i))&&p.sample.sequence>used_[i].sequence){
                used_[i]=p.sample;used_[i].consumed=p.sample.queued;
            }
            p={};
        }else if(!proof.valid){
            if(p.sample.sequence==current_.sequence)for(size_t i=0;i<Count;++i)if(p.sample.queued&(1u<<i))
                Reject(current_.sequence,static_cast<Signal>(i),"Recording revoked or abandoned");
            p={};
        }
    }}
    Reading Read(Signal signal,Epoch epoch,uint64_t now,Mode mode){
        Match(epoch);Reading out;out.epoch=epoch;
        if(mode!=Mode::Active){Clear();out.state=mode==Mode::Off?State::Off:mode==Mode::Failed?State::Blocked:State::NotApplicable;return out;}
        const auto i=size_t(signal);const auto& used=used_[i];
        const auto& sample=used.sequence&&now>=used.time&&now-used.time<=2000&&current_.facts[i].admitted?used:current_;
        if(!sample.sequence)return out;
        out.fact=sample.facts[i];out.sequence=sample.sequence;out.age=now>=sample.time?now-sample.time:UINT64_MAX;
        if(out.age>2000){out.state=State::Stale;return out;}
        if(!out.fact.present){out.state=State::Unavailable;return out;}
        if(!out.fact.admitted){out.state=out.fact.reason==nullptr?State::Detected:State::Blocked;return out;}
        out.state=(sample.consumed&Bit(signal))?State::Consumed:(sample.queued&Bit(signal))?State::Pending:State::Usable;
        return out;
    }
};
}
