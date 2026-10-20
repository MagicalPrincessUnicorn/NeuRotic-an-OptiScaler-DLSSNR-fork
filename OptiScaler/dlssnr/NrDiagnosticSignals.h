#pragma once
#include "NrSignalReceipts.h"
#include "NrPreflightState.h"
namespace DlssNr::DiagnosticSignals {
using Readings=std::array<SignalReceipts::Reading,static_cast<size_t>(SignalReceipts::Signal::Count)>;
inline bool Current(const SignalReceipts::Reading& r) {
    return r.sequence && r.age<=2000 && r.fact.present &&
        (r.state==SignalReceipts::State::Detected || r.state==SignalReceipts::State::Usable ||
         r.state==SignalReceipts::State::Pending || r.state==SignalReceipts::State::Consumed);
}
inline bool Used(const Readings& r,SignalReceipts::Signal signal){return r[size_t(signal)].state==SignalReceipts::State::Consumed && Current(r[size_t(signal)]);}
inline SignalReceipts::State EffectiveState(const SignalReceipts::Reading& r){
    return r.sequence&&r.age>2000&&r.state!=SignalReceipts::State::Off&&r.state!=SignalReceipts::State::NotApplicable?SignalReceipts::State::Stale:r.state;
}
inline NrPreflightState::State State(const SignalReceipts::Reading& r){
    using S=SignalReceipts::State;using D=NrPreflightState::State;
    switch(EffectiveState(r)){case S::Consumed:return D::Consumed;case S::Detected:return D::Detected;
    case S::Usable:return D::Usable;case S::Stale:return D::Stale;case S::Off:return D::Off;
    case S::NotApplicable:return D::NotApplicable;case S::Unavailable:return D::Unavailable;
    case S::Blocked:return D::Fault;default:return D::Pending;}
}
inline std::string Pair(const SignalReceipts::Reading& r){
    if(!Current(r)||!r.fact.valuesKnown)return SignalReceipts::Name(EffectiveState(r));
    return "("+std::to_string(r.fact.x)+", "+std::to_string(r.fact.y)+"); first NR pass; "+SignalReceipts::Name(EffectiveState(r));
}
inline std::string Extent(const SignalReceipts::Reading& r){
    return Current(r)&&r.fact.width&&r.fact.height?std::to_string(r.fact.width)+" x "+std::to_string(r.fact.height):SignalReceipts::Name(EffectiveState(r));
}
}
