#pragma once
#include "VulkanNrSession.h"
#include "NrReadiness.h"
#include <nvsdk_ngx.h>
#include <optional>
namespace DlssNr
{
struct VkNrPreSrAdmission
{
    bool enabled=false,nativeSelected=false,beforeRequested=false,selectedSr=false,
         rayReconstruction=false,bridged=false;
};
bool ShouldRecordVkNrPreSr(const VkNrPreSrAdmission&);
bool ShouldRecordVkNrAfterSr(const VkNrPreSrAdmission&);
// A seed's recording return is not temporal readiness. Its actual use must complete first.
class VkNrPreSrSeed
{
  public:
    explicit VkNrPreSrSeed(VkNrRecordingOwner& owner=VulkanNrRecordings()) : owner_(owner) {}
    ~VkNrPreSrSeed() { Reset(); }
    void Record(VkNrUseId use,bool succeeded);
    bool Ready();
    void Reset();
  private:
    VkNrRecordingOwner& owner_;VkNrUseId use_;bool ready_=false;
};
struct VkNrPreSrCarrier
{
    void* resource=nullptr;
    VkNrFrameContract frame;
    VkNrUseId use;
    bool seeded=false,forceSrReset=false;
    std::function<bool()> sourceCurrent;
};
inline bool SameVkNrPreSrRaster(const VkNrFrameContract& a,const VkNrFrameContract& b)
{
    return a.deviceGeneration&&a.deviceGeneration==b.deviceGeneration&&a.queueFamily!=UINT32_MAX&&a.queueFamily==b.queueFamily&&
        (!a.queue||!b.queue||a.queue==b.queue)&&
        a.route==b.route&&a.placement==b.placement&&a.routeEpoch==b.routeEpoch&&
        a.inputInterruptionEpoch==b.inputInterruptionEpoch&&
        a.output.width==b.output.width&&a.output.height==b.output.height&&a.representation==b.representation;
}
// Thread-local synchronous binding authority, never GPU lifetime proof.
template<class Parameters> class VkNrPreSrScope;
class VkNrPreSrCaptureBinding {
    template<class Parameters> friend class VkNrPreSrScope;
    inline static thread_local const VkNrPreSrCaptureBinding* current_=nullptr;
    const VkNrPreSrCaptureBinding* previous_=nullptr;
    const void* params_=nullptr;const void* carrier_=nullptr;const void* owner_=nullptr;
    VkNrEvaluationIdentity evaluation_;bool active_=false;
    bool (*matches_)(const void*) noexcept=nullptr;
    void Activate(const void* params,VkNrEvaluationIdentity evaluation,const void* carrier,
                  const void* owner,bool (*matches)(const void*) noexcept) noexcept {
        params_=params;evaluation_=evaluation;carrier_=carrier;owner_=owner;matches_=matches;
        previous_=current_;active_=true;current_=this;
    }
    void Revoke() noexcept {active_=false;if(current_==this)current_=previous_;}
public:
    VkNrPreSrCaptureBinding()=default;
    VkNrPreSrCaptureBinding(const VkNrPreSrCaptureBinding&)=delete;
    ~VkNrPreSrCaptureBinding(){Revoke();}
    static bool Current(const void* params,const VkNrEvaluationIdentity& evaluation,const void* carrier) noexcept {
        const auto* binding=current_;
        return binding&&binding->active_&&evaluation&&binding->params_==params&&binding->evaluation_==evaluation&&
            binding->carrier_==carrier&&binding->matches_&&binding->matches_(binding->owner_);
    }
};
inline bool VkNrPreSrCaptureAuthorized(const void* params,const VkNrEvaluationIdentity& evaluation,const void* carrier) noexcept {
    return VkNrPreSrCaptureBinding::Current(params,evaluation,carrier);
}
// The selected synchronous SR call owns this scope. GPU lifetime remains in the recording/generation owner.
template<class Parameters> class VkNrPreSrScope
{
  public:
    VkNrPreSrScope(Parameters* params,VkNrRecordingOwner& owner,VkNrFrameContract frame)
        : params_(params),owner_(owner),frame_(std::move(frame))
    {
        ready_=params_&&Read(NVSDK_NGX_Parameter_Color,color_)&&color_&&
            Read(NVSDK_NGX_Parameter_Output,output_)&&output_&&
            Read(NVSDK_NGX_Parameter_Jitter_Offset_X,jitterX_)&&
            Read(NVSDK_NGX_Parameter_Jitter_Offset_Y,jitterY_);
        Read(NVSDK_NGX_Parameter_Reset,reset_); // SDK optional default is zero.
        Read(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,colorX_);
        Read(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,colorY_);
    }
    ~VkNrPreSrScope() noexcept { Restore();if(use_)owner_.ReleaseUse(use_); }
    VkNrPreSrScope(const VkNrPreSrScope&)=delete;
    VkNrPreSrScope& operator=(const VkNrPreSrScope&)=delete;
    bool Ready() const { return ready_; }
    bool Bind(const VkNrPreSrCarrier& carrier,const VkNrFrameContract& current) noexcept
    {
        if(!ready_||attempted_||restoreAttempted_||!carrier.resource||!carrier.seeded||
            !current.evaluation||frame_.evaluation!=current.evaluation||carrier.frame.evaluation!=current.evaluation||
            owner_.Incarnation(current.evaluation.commandBuffer)!=current.evaluation.incarnation||
            !owner_.UseOnRecording(carrier.use,current.evaluation.commandBuffer)||
            !SameVkNrPreSrRaster(frame_,current)||!SameVkNrPreSrRaster(carrier.frame,current)||
            !Matches(NVSDK_NGX_Parameter_Color,color_)||!Matches(NVSDK_NGX_Parameter_Output,output_)||
            !Matches(NVSDK_NGX_Parameter_Jitter_Offset_X,jitterX_)||!Matches(NVSDK_NGX_Parameter_Jitter_Offset_Y,jitterY_)||
            !MatchesOptional(NVSDK_NGX_Parameter_Reset,reset_)||
            !MatchesOptional(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,colorX_)||
            !MatchesOptional(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,colorY_)||!SourceCurrent(carrier)||!owner_.RetainUse(carrier.use))return false;
        use_=carrier.use;attempted_=true;
        const bool bound=Put(NVSDK_NGX_Parameter_Color,carrier.resource)&&
            Put(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,0u)&&
            Put(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,0u)&&
            (!carrier.forceSrReset||Put(NVSDK_NGX_Parameter_Reset,1u));
        if(bound) {
            boundResource_=carrier.resource;boundReset_=carrier.forceSrReset?1u:reset_;
            capture_.Activate(params_,current.evaluation,carrier.resource,this,[](const void* value) noexcept {
                return static_cast<const VkNrPreSrScope*>(value)->BoundMatches();
            });
        } else Restore();
        return bound;
    }
    bool Restore() noexcept
    {
        capture_.Revoke();
        if(restoreAttempted_)return restored_;
        restoreAttempted_=true;
        // Carrier eligibility is separate from restoring an untouched call.
        if(!attempted_)return restored_=true;
        bool ok=Put(NVSDK_NGX_Parameter_Color,color_);
        ok=Put(NVSDK_NGX_Parameter_Output,output_)&&ok;
        ok=Put(NVSDK_NGX_Parameter_Jitter_Offset_X,jitterX_)&&ok;
        ok=Put(NVSDK_NGX_Parameter_Jitter_Offset_Y,jitterY_)&&ok;
        ok=Put(NVSDK_NGX_Parameter_Reset,reset_)&&ok;
        ok=Put(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,colorX_)&&ok;
        ok=Put(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,colorY_)&&ok;
        return restored_=ok;
    }
  private:
    bool BoundMatches() const noexcept {
        return !restoreAttempted_&&Matches(NVSDK_NGX_Parameter_Color,boundResource_)&&
            Matches(NVSDK_NGX_Parameter_Output,output_)&&Matches(NVSDK_NGX_Parameter_Jitter_Offset_X,jitterX_)&&
            Matches(NVSDK_NGX_Parameter_Jitter_Offset_Y,jitterY_)&&MatchesOptional(NVSDK_NGX_Parameter_Reset,boundReset_)&&
            MatchesOptional(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,0u)&&
            MatchesOptional(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,0u);
    }
    static bool SourceCurrent(const VkNrPreSrCarrier& carrier) noexcept
    { try {return !carrier.sourceCurrent||carrier.sourceCurrent();}catch(...){return false;} }
    template<class T> bool Read(const char* key,T& value) const noexcept
    { try { return static_cast<uint32_t>(params_->Get(key,&value))==1; }catch(...) {return false;} }
    template<class T> bool Matches(const char* key,T value) const noexcept { T observed{};return Read(key,observed)&&value==observed; }
    bool MatchesOptional(const char* key,unsigned value) const noexcept { unsigned observed=0;return (Read(key,observed)?observed:0u)==value; }
    template<class T> bool Put(const char* key,T value) noexcept
    { try { params_->Set(key,value);return Matches(key,value); }catch(...) { return false; } }
    Parameters* params_;VkNrRecordingOwner& owner_;VkNrFrameContract frame_;
    void* color_=nullptr;void* output_=nullptr;float jitterX_=0,jitterY_=0;unsigned reset_=0,colorX_=0,colorY_=0;
    VkNrPreSrCaptureBinding capture_;void* boundResource_=nullptr;unsigned boundReset_=0;
    VkNrUseId use_;bool ready_=false,attempted_=false,restored_=false,restoreAttempted_=false;
};
}
