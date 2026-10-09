bool VkPresentPrivateFrame::Check(const VkPresentImageRequest& image)
{
    request_.waitBudget = image.waitBudget;
    request_.privateAllocationBudget = image.privateAllocationBudget;
    request_.frame = image.frame; request_.guides = image.guides;
    request_.capturedGuides = image.capturedGuides;
    request_.observedRenderSize=image.observedRenderSize;
    request_.inputDecision = image.inputDecision; request_.settings = image.settings;request_.captureOnly=image.captureOnly;
    return Ready() && image.commandBuffer == commandBuffer_ && image.image == request_.image &&
        image.format == request_.format && image.colorSpace == request_.colorSpace && image.generation == request_.generation &&
        image.extent.width == request_.extent.width && image.extent.height == request_.extent.height;
}
