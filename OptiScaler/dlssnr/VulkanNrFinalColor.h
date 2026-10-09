#pragma once
#include "VulkanNrSession.h"
namespace DlssNr {
struct VkNrFinalColorSource {
    VkNrRecordedOutput observation;
    bool selectedSrSucceeded=false,performance=false;
};
inline bool VkNrFinalColorAdmits(const VkNrFinalColorSource& source,VkNrRecordingOwner& recordings,
    VkCommandBuffer command,uint64_t provider,uint64_t frame,uint32_t viewport,VkImage taggedImage,
    const VkNrEvaluationIdentity& selected){
    const auto& s=source.observation;if(!source.selectedSrSucceeded||!s.use||!s.frame.temporal||!taggedImage||
        s.gameInput!=taggedImage||!recordings.UseOnRecording(s.use,command)||recordings.ModelRecordingReason(command)||!recordings.BindingsQualified(command)||
        !selected||s.frame.evaluation!=selected||selected.commandBuffer!=command||
        selected.incarnation!=recordings.Incarnation(command)||selected.featureGeneration!=s.frame.temporal->featureGeneration)return false;
    const auto& t=*s.frame.temporal;
    return provider&&t.providerFrameKnown&&t.providerGeneration==provider&&t.frameToken==frame&&t.viewport==viewport&&
        (!source.performance||(s.requestedPasses&&s.requestedPasses==s.completedPasses));
}
class VkNrFinalColorAttempt {
    VkNrUseId attempted_;
public:
    bool Claim(VkNrUseId source){if(!source||source==attempted_)return false;attempted_=source;return true;}
};
}
