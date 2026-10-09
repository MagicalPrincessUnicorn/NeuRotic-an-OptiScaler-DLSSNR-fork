#pragma once
#include "NrAlternateFrameContract.h"

namespace DlssNr::AlternateFrame
{
inline bool Valid(const std::optional<SourceRef>& s) noexcept
{return s&&s->Valid();}
inline void Recover(PolicyState& s,Reason reason) noexcept
{
    s.state=StateKind::Recovery;s.required=RecoveryFulls;s.cleanFulls=0;
    s.anchor.reset();s.pending=false;s.reason=reason;++s.association;
}
inline Decision Decide(const PolicyState& s,const DecisionInputs& i) noexcept
{
    if(!i.enabled)return {DecisionKind::Full,Reason::Off};
    if(i.eventClass!=SourceEventClass::Real||!s.pending||!Valid(i.source)||s.current!=i.source)
        return {DecisionKind::NoSourceDecision,Reason::InputIdentityUnknown};
    if(!i.supported)return {DecisionKind::Full,i.refusal};
    if(i.resetDue)return {DecisionKind::Full,Reason::ResetDue};
    if(s.state==StateKind::Bootstrap||s.state==StateKind::Recovery)
        return {DecisionKind::Full,s.state==StateKind::Bootstrap?Reason::BootstrapDue:Reason::RecoveryDue,
                s.cleanFulls+1>=s.required};
    if(s.state==StateKind::RefreshDue)return {DecisionKind::Full,Reason::RefreshDue,true};
    if(!s.anchor||!i.anchorReady)return {DecisionKind::Full,Reason::NoAnchor,true};
    if(i.predecessor!=s.anchor)return {DecisionKind::Full,Reason::PredecessorMismatch,false};
    if(!i.clockKnown)return {DecisionKind::Full,Reason::ClockUnknown,false};
    // Compare before subtracting; no signed overflow at corrupt clock values.
    if(i.timeUs<=s.anchorTimeUs||s.anchorTimeUs<0||
       static_cast<std::uint64_t>(i.timeUs)-static_cast<std::uint64_t>(s.anchorTimeUs)>MaximumAgeUs)
        return {DecisionKind::Full,Reason::AnchorTooOld,false};
    return {DecisionKind::Carry,Reason::None,false};
}
inline PolicyState Reduce(const PolicyState& previous,const PolicyEvent& e) noexcept
{
    auto s=previous;const auto& i=e.input;
    if(e.kind==EventKind::Admit)
    {
        if(!i.enabled) {
            if(s.state!=StateKind::Disabled){const auto generation=s.association+1;s={};s.association=generation;}
            return s;
        }
        if(s.state==StateKind::Disabled){s.state=StateKind::Bootstrap;s.reason=Reason::BootstrapDue;++s.association;}
        // Global invalidations apply even to a generated or duplicate callback.
        if(i.invalidate||i.resetDue)Recover(s,i.resetDue?Reason::ResetDue:Reason::ConfigRevoked);
        if(i.contradictoryDuplicate)Recover(s,Reason::DuplicateContradiction);
        if(i.eventClass!=SourceEventClass::Real)return s;
        if(!Valid(i.source)){Recover(s,Reason::InputIdentityUnknown);s.lastSource.reset();return s;}
        if(s.current==i.source) {
            if(s.currentTimeUs!=i.timeUs||s.scope!=i.scope)Recover(s,Reason::DuplicateContradiction);
            return s;
        }
        if(s.recoverAfterPending){Recover(s,Reason::AnchorInvalid);s.recoverAfterPending=false;}
        else if(s.pending)Recover(s,Reason::PartialEffects); // prior output never accepted
        if(s.everActive&&s.scope!=i.scope)Recover(s,Reason::ConfigRevoked);
        if(s.lastSource&&i.predecessor!=s.lastSource)Recover(s,Reason::PredecessorMismatch);
        if(s.anchor&&(!i.clockKnown||i.timeUs<=s.anchorTimeUs||s.anchorTimeUs<0||
           static_cast<std::uint64_t>(i.timeUs)-static_cast<std::uint64_t>(s.anchorTimeUs)>MaximumAgeUs))
            Recover(s,i.clockKnown?Reason::AnchorTooOld:Reason::ClockUnknown);
        s.current=i.source;s.currentTimeUs=i.timeUs;s.scope=i.scope;s.pending=true;
        if(!i.supported){s.state=StateKind::Suspended;s.anchor.reset();s.reason=i.refusal;}
        else if(s.state==StateKind::Suspended) {
            s.state=s.required==RecoveryFulls?StateKind::Recovery:StateKind::Bootstrap;
            s.cleanFulls=0;
        }
        if(i.supported)s.everActive=true;
        return s;
    }
    if(e.association!=s.association)return s;
    if(e.kind==EventKind::ZeroCoverage) {
        if(!e.observation||e.observation<=s.lastCoverageObservation)return s;
        s.lastCoverageObservation=e.observation;
        // Never revise a decision already admitted for the current source.
        // A pending decision is retained; recovery takes effect on next Admit.
        if(s.pending){s.recoverAfterPending=true;return s;}
        Recover(s,Reason::AnchorInvalid);return s;
    }
    if(!s.pending||s.current!=i.source||!Valid(i.source))return s;
    if(e.kind==EventKind::AnchorUnavailable){s.anchor.reset();return s;}
    s.pending=false;s.lastSource=i.source;
    if(e.kind==EventKind::CarryCommitted){++s.omissions;s.state=StateKind::RefreshDue;s.anchor.reset();return s;}
    if(e.kind==EventKind::FailedAfterEffects){++s.omissions;Recover(s,Reason::PartialEffects);return s;}
    if(e.kind==EventKind::FullFailed){Recover(s,Reason::RecordingFailure);return s;}
    ++s.fullCalls;s.lastModelSource=i.source;
    if(s.state==StateKind::Suspended)return s;
    if(s.state==StateKind::RefreshDue||++s.cleanFulls>=s.required)s.state=StateKind::Eligible;
    s.anchor=e.anchorAvailable?i.source:std::nullopt;s.anchorTimeUs=i.timeUs;
    return s;
}
}
