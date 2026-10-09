#include "VulkanPresent.h"

namespace DlssNr
{

VkPresentRecordResult RecordVkPresentImage(const VkPresentImageRequest& request,
                                           IVkPresentCommands& commands, bool reset)
{
    if (request.image == VK_NULL_HANDLE || request.commandBuffer == VK_NULL_HANDLE ||
        request.generation == 0 || request.extent.width == 0 || request.extent.height == 0 ||
        !commands.Check(request))
        return { VkRecordStatus::NoWork, "present image or private resources unavailable" };

    if (!commands.Begin())
        return { VkRecordStatus::NoWork, "could not begin private command buffer" };

    commands.SourceToTransfer();
    commands.Capture();
    commands.SourceToPresent();
    if (!commands.ConvertToModel())
    {
        commands.Discard();
        return { VkRecordStatus::Partial, "private color conversion could not be recorded" };
    }
    const bool evaluated=commands.Evaluate(reset);
    const auto chain=commands.ModelResult();
    if(chain.preparationOnly&&chain.modelRecorded&&!chain.outputWriteRecorded) {
        if(!commands.End()) {
            commands.Discard();
            return {VkRecordStatus::Partial,"model preparation command buffer could not be sealed",true};
        }
        return {VkRecordStatus::Complete,chain.reason,true,false,chain.requestedPasses,0,
            0,false,chain.use,false,true};
    }
    if (!evaluated)
    {
        commands.Discard();
        return { VkRecordStatus::Partial, chain.reason.empty()?"model or composition was refused":chain.reason,chain.modelRecorded,false,
            chain.requestedPasses,chain.completedPasses,chain.outputVersion,false };
    }
    if(!chain.chainDeliverable||!chain.requestedPasses||chain.completedPasses!=chain.requestedPasses) {
        commands.Discard();
        return {VkRecordStatus::Partial,"Present requires the entire requested model chain",chain.modelRecorded};
    }
    if (!commands.ConvertFromModel())
    {
        commands.Discard();
        return { VkRecordStatus::Partial, "private output conversion could not be recorded", true };
    }

    if(!request.captureOnly){commands.TargetToTransfer();commands.CopyBack();commands.TargetToPresent();}
    if (!commands.End())
    {
        commands.Discard();
        return { VkRecordStatus::Partial, "private command buffer could not be sealed", true, true };
    }
    return { VkRecordStatus::Complete, "", true, !request.captureOnly,chain.requestedPasses,chain.completedPasses,
             chain.outputVersion,chain.chainDeliverable,chain.use,request.captureOnly };
}

} // namespace DlssNr
