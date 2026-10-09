#pragma once
#include "NrAlternateFrameDx12.h"
#include <utility>

namespace DlssNr::AlternateFrame {
enum class CarryResult {FullRequired,Carried,FailedAfterEffects};
// The production dispatch coordinator. It never equates a recorded carry with
// a model attempt, reset consumption, GPU completion or consumer acceptance.
class Session {
    PolicyState state_;
    DecisionInputs input_;
    Decision decision_;
    std::unique_ptr<Renderer> renderer_;
    std::uint64_t coverageSerial_=0;
    Reason reason_=Reason::Off;
    std::optional<GpuMeasurement> measurement_;
    std::optional<NativeSourceView> previous_;
    std::optional<NativeDispatchOutcome> output_;
  public:
    Decision Begin(ID3D12Device* device,NativeSourceView& view,bool enabled,bool invalidated,
                   bool outputRequiresModelAttempt=false,bool duplicate=false) {
        output_.reset();
        if(renderer_)while(auto sample=renderer_->Poll()) {
            measurement_=sample;
            if(sample->carry&&sample->pixels[0]==0) {
                PolicyEvent e{EventKind::ZeroCoverage};e.association=sample->association;e.observation=++coverageSerial_;
                state_=Reduce(state_,e);
            }
        }
        if(previous_&&enabled) {
            const auto& p=*previous_;
            invalidated=invalidated||p.rgba16f!=view.rgba16f||p.rgb11f!=view.rgb11f||p.scope!=view.scope||p.width!=view.width||p.height!=view.height||
                p.depth!=view.depth||p.motion!=view.motion||p.depthInverted!=view.depthInverted||
                p.includesJitter!=view.includesJitter||p.motionToSceneUv!=view.motionToSceneUv||
                p.linearDomain!=view.linearDomain||p.scale.scale.has_value()!=view.scale.scale.has_value();
        }
        auto admission=BuildAlternateFrameInputs(view,nullptr,{enabled,outputRequiresModelAttempt});
        if(admission.accepted&&renderer_&&renderer_->Anchor()) {
            const auto pair=BuildAlternateFrameInputs(view,renderer_->Anchor(),{true,outputRequiresModelAttempt});
            if(pair.reason==Reason::ExposureRatioOutOfRange||pair.reason==Reason::ExposureScaleTransition) {
                invalidated=true;admission=pair;
            }
        }
        input_={};input_.enabled=enabled;input_.supported=admission.accepted;input_.refusal=admission.reason;
        input_.source=view.source;input_.predecessor=view.predecessor;input_.timeUs=view.timeUs;input_.scope=view.scope;
        input_.clockKnown=view.clockKnown;input_.resetDue=view.resetDue;input_.invalidate=invalidated;
        input_.anchorReady=renderer_&&renderer_->Anchor();input_.eventClass=duplicate?SourceEventClass::Duplicate:SourceEventClass::Real;
        state_=Reduce(state_,{EventKind::Admit,input_});decision_=Decide(state_,input_);reason_=decision_.reason;
        view.association=state_.association;
        if(!enabled||!admission.accepted||invalidated||view.resetDue) {
            if(renderer_)renderer_->Invalidate();
        }
        if(enabled&&admission.accepted&&!renderer_)renderer_=std::make_unique<Renderer>(device);
        previous_=enabled?std::optional(view):std::nullopt;
        return decision_;
    }
    CarryResult TryCarry(const GpuRequest& request) {
        if(decision_.kind!=DecisionKind::Carry||!renderer_)return CarryResult::FullRequired;
        auto prepared=renderer_->PrepareCarry(request);
        if(!prepared) {reason_=renderer_->LastReason();decision_={DecisionKind::Full,reason_,true};return CarryResult::FullRequired;}
        const auto result=renderer_->RecordCarry(*prepared);
        output_=result.output;
        if(!result.possibleEffects) {reason_=Reason::RecordingFailure;decision_={DecisionKind::Full,reason_,true};return CarryResult::FullRequired;}
        Finish(result.recorded?EventKind::CarryCommitted:EventKind::FailedAfterEffects,false);
        reason_=result.recorded?Reason::None:Reason::PartialEffects;
        return result.recorded?CarryResult::Carried:CarryResult::FailedAfterEffects;
    }
    // This is the model-entry branch used by the Native renderer and the owned
    // integration fixture. The full callback is never entered after possible
    // carry effects. Common services are required even when the model is omitted.
    template<class Common,class Full>CarryResult Dispatch(const GpuRequest& request,Common&& common,Full&& full) {
        if(input_.enabled&&decision_.kind==DecisionKind::Carry) {
            common();const auto result=TryCarry(request);
            if(result!=CarryResult::FullRequired)return result;
        }
        full();return CarryResult::FullRequired;
    }
    // Called only after the real model call and ordinary resolve both succeeded.
    // Capture is optional; lack of storage never cancels a good dense output.
    bool CompleteFull(const GpuRequest& request) {
        bool anchor=false,safe=true;
        if(decision_.capture&&input_.supported&&renderer_) {
            if(auto prepared=renderer_->PrepareCapture(request)) {
                const auto recorded=renderer_->RecordCapture(*prepared);
                safe=recorded.recorded||!recorded.possibleEffects;
                if(recorded.recorded)anchor=renderer_->AcceptCapture(*input_.source);
            } else reason_=renderer_->LastReason();
        }
        Finish(safe?EventKind::CleanFull:EventKind::FullFailed,anchor);return safe;
    }
    void FailFull(){if(state_.pending)Finish(EventKind::FullFailed,false);}
    void Disable(){input_={};state_=Reduce(state_,{EventKind::Admit,input_});renderer_.reset();previous_.reset();reason_=Reason::Off;}
    const PolicyState& State()const{return state_;}
    const std::optional<NativeDispatchOutcome>& Output()const{return output_;}
    Reason LastReason()const{return reason_;}
    const std::optional<GpuMeasurement>& Measurement()const{return measurement_;}
    std::optional<GpuMeasurement> TakeMeasurement(){return std::exchange(measurement_,std::nullopt);}
  private:
    void Finish(EventKind kind,bool anchor){PolicyEvent e{kind,input_};e.association=state_.association;e.anchorAvailable=anchor;state_=Reduce(state_,e);}
};
}
