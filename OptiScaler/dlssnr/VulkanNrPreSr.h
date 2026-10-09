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
};
inline bool SameVkNrPreSrRaster(const VkNrFrameContract& a,const VkNrFrameContract& b)
{
    return a.deviceGeneration&&a.deviceGeneration==b.deviceGeneration&&a.queueFamily!=UINT32_MAX&&a.queueFamily==b.queueFamily&&
        (!a.queue||!b.queue||a.queue==b.queue)&&
        a.route==b.route&&a.placement==b.placement&&a.routeEpoch==b.routeEpoch&&
        a.inputInterruptionEpoch==b.inputInterruptionEpoch&&
        a.output.width==b.output.width&&a.output.height==b.output.height&&a.representation==b.representation;
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
            !MatchesOptional(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,colorY_)||!owner_.RetainUse(carrier.use))return false;
        use_=carrier.use;attempted_=true;
        const bool bound=Put(NVSDK_NGX_Parameter_Color,carrier.resource)&&
            Put(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,0u)&&
            Put(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,0u)&&
            (!carrier.forceSrReset||Put(NVSDK_NGX_Parameter_Reset,1u));
        if(!bound)Restore();
        return bound;
    }
    bool Restore() noexcept
    {
        if(restoreAttempted_)return restored_;
        restoreAttempted_=true;
        if(!attempted_)return restored_=ready_;
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
    template<class T> bool Read(const char* key,T& value) noexcept
    { try { return static_cast<uint32_t>(params_->Get(key,&value))==1; }catch(...) {return false;} }
    template<class T> bool Matches(const char* key,T value) noexcept { T observed{};return Read(key,observed)&&value==observed; }
    bool MatchesOptional(const char* key,unsigned value) noexcept { unsigned observed=0;return (Read(key,observed)?observed:0u)==value; }
    template<class T> bool Put(const char* key,T value) noexcept
    { try { params_->Set(key,value);return Matches(key,value); }catch(...) { return false; } }
    Parameters* params_;VkNrRecordingOwner& owner_;VkNrFrameContract frame_;
    void* color_=nullptr;void* output_=nullptr;float jitterX_=0,jitterY_=0;unsigned reset_=0,colorX_=0,colorY_=0;
    VkNrUseId use_;bool ready_=false,attempted_=false,restored_=false,restoreAttempted_=false;
};
}
