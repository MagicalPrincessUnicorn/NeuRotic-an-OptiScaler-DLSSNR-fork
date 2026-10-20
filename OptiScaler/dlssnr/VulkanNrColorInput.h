#pragma once
#include "VulkanNrFrameParams.h"
#include "VulkanNrImageFacts.h"
#include "VulkanNrRecording.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace DlssNr {
inline bool SameVkNrColorWrapper(const NVSDK_NGX_Resource_VK& a,const NVSDK_NGX_Resource_VK& b) noexcept {
    if(a.Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW||b.Type!=a.Type||a.ReadWrite!=b.ReadWrite)return false;
    const auto& x=a.Resource.ImageViewInfo;const auto& y=b.Resource.ImageViewInfo;
    return x.Image==y.Image&&x.ImageView==y.ImageView&&x.Format==y.Format&&x.Width==y.Width&&x.Height==y.Height&&
        x.SubresourceRange.aspectMask==y.SubresourceRange.aspectMask&&x.SubresourceRange.baseMipLevel==y.SubresourceRange.baseMipLevel&&
        x.SubresourceRange.levelCount==y.SubresourceRange.levelCount&&x.SubresourceRange.baseArrayLayer==y.SubresourceRange.baseArrayLayer&&
        x.SubresourceRange.layerCount==y.SubresourceRange.layerCount;
}
inline bool SameVkNrColorTemporalValues(const VkNrTemporalMetadata& a,const VkNrTemporalMetadata& b) noexcept {
    return a.color==b.color&&a.depth==b.depth&&a.motion==b.motion&&a.motionScaleX==b.motionScaleX&&a.motionScaleY==b.motionScaleY&&
        a.jitterX==b.jitterX&&a.jitterY==b.jitterY&&a.featureFlags==b.featureFlags&&a.featureGeneration==b.featureGeneration&&
        a.depthInverted==b.depthInverted&&a.reset==b.reset;
}
inline bool SameVkNrColorTemporal(const VkNrTemporalMetadata& a,const VkNrTemporalMetadata& b) noexcept {
    return SameVkNrColorTemporalValues(a,b)&&a.frameToken==b.frameToken&&a.providerFrameKnown==b.providerFrameKnown&&
        a.providerGeneration==b.providerGeneration&&a.viewport==b.viewport;
}

// This is a synchronous ingress capability, not a GPU lease. Closing it revokes
// authority even while immutable metadata survives in a copied request.
class VkNrColorIngress {
public:
    template<class Params> static std::shared_ptr<const VkNrColorIngress> Observe(Params* params,VkDevice device,uint64_t deviceGeneration,
        VkNrEvaluationIdentity evaluation,const VkNrTemporalMetadata& raw,const VkNrTemporalMetadata& authenticated,
        VkExtent2D output,uint32_t family,std::function<bool()> selectedStillCurrent) {
        NVSDK_NGX_Resource_VK* original=nullptr;VkFrame::Failure failure;
        if(!params||!device||!deviceGeneration||!evaluation||evaluation.featureGeneration!=raw.featureGeneration||
           family==UINT32_MAX||!ValidVkNrTemporal(raw)||!ValidVkNrTemporal(authenticated)||
           !SameVkNrColorTemporalValues(raw,authenticated)||!selectedStillCurrent)return {};
        try {
            if(params->Get(NVSDK_NGX_Parameter_Color,reinterpret_cast<void**>(&original))!=NVSDK_NGX_Result_Success||
               !VkFrame::ValidateImage("SR.Color",original,false,failure))return {};
        }catch(...){return {};}
        const auto snapshot=*original;
        auto storageCurrent=[original,snapshot,selectedStillCurrent]{return selectedStillCurrent()&&SameVkNrColorWrapper(*original,snapshot);};
        auto readCurrent=[params,original,snapshot,raw,output,selectedStillCurrent=std::move(selectedStillCurrent)]{
            if(!selectedStillCurrent())return false;
            NVSDK_NGX_Resource_VK* now=nullptr;
            if(params->Get(NVSDK_NGX_Parameter_Color,reinterpret_cast<void**>(&now))!=NVSDK_NGX_Result_Success||now!=original||
               !SameVkNrColorWrapper(*now,snapshot))return false;
            const auto temporal=VkFrame::ObserveTemporal(params,raw.featureFlags,raw.featureGeneration,raw.frameToken,output);
            return temporal&&SameVkNrColorTemporal(*temporal,raw);
        };
        auto result=std::shared_ptr<VkNrColorIngress>(new VkNrColorIngress(params,device,deviceGeneration,evaluation,authenticated,family,original,snapshot,std::move(readCurrent),std::move(storageCurrent)));
        return result->Current(params,device,deviceGeneration,evaluation,family,authenticated)?result:nullptr;
    }
    bool Current(const void* params,VkDevice device,uint64_t generation,const VkNrEvaluationIdentity& evaluation,
                 uint32_t family,const VkNrTemporalMetadata& authenticated) const noexcept {
        if(!active_.load(std::memory_order_acquire)||params!=params_||device!=device_||generation!=deviceGeneration_||
           evaluation!=evaluation_||family!=family_||!SameVkNrColorTemporal(authenticated,authenticated_))return false;
        try{return readCurrent_();}catch(...){return false;}
    }
    void Close() const noexcept {active_.store(false,std::memory_order_release);}
    bool OriginalStorageCurrent() const noexcept {
        if(!active_.load(std::memory_order_acquire))return false;
        try{return storageCurrent_();}catch(...){return false;}
    }
    const NVSDK_NGX_Resource_VK& Original() const{return original_;}
    NVSDK_NGX_Resource_VK* OriginalPointer() const{return originalPointer_;}
    VkDevice Device() const{return device_;}
    uint64_t DeviceGeneration() const{return deviceGeneration_;}
    const VkNrTemporalMetadata& Temporal() const{return authenticated_;}
private:
    VkNrColorIngress(const void* params,VkDevice device,uint64_t generation,VkNrEvaluationIdentity evaluation,
        VkNrTemporalMetadata authenticated,uint32_t family,NVSDK_NGX_Resource_VK* original,NVSDK_NGX_Resource_VK snapshot,
        std::function<bool()> current,std::function<bool()> storageCurrent):params_(params),device_(device),deviceGeneration_(generation),evaluation_(evaluation),
        authenticated_(authenticated),family_(family),originalPointer_(original),original_(snapshot),readCurrent_(std::move(current)),storageCurrent_(std::move(storageCurrent)){}
    const void* params_;VkDevice device_;uint64_t deviceGeneration_;VkNrEvaluationIdentity evaluation_;
    VkNrTemporalMetadata authenticated_;uint32_t family_;NVSDK_NGX_Resource_VK* originalPointer_;
    NVSDK_NGX_Resource_VK original_;std::function<bool()> readCurrent_,storageCurrent_;mutable std::atomic<bool> active_{true};
};
class VkNrColorIngressScope {
public:
    explicit VkNrColorIngressScope(std::shared_ptr<const VkNrColorIngress> input):input_(std::move(input)){}
    ~VkNrColorIngressScope(){if(input_)input_->Close();}
    VkNrColorIngressScope(const VkNrColorIngressScope&)=delete;
    VkNrColorIngressScope& operator=(const VkNrColorIngressScope&)=delete;
private:std::shared_ptr<const VkNrColorIngress> input_;
};
class VkNrQualifiedColor {
public:
    static std::shared_ptr<const VkNrQualifiedColor> Qualify(const VkNrImageFacts& facts,
        std::shared_ptr<const VkNrColorIngress> ingress,std::string& reason,std::string* diagnostic=nullptr) {
        if(diagnostic)diagnostic->clear();
        if(!ingress){reason="Native color ingress is unproved";return {};}
        const auto& original=ingress->Original();const auto& w=original.Resource.ImageViewInfo;
        const auto observed=facts.ImageView(ingress->Device(),w.Image,w.ImageView);
        if(!observed){reason="Native color image/view creation or generation linkage is unproved";return {};}
        const auto* image=&observed->image;
        auto local=original;auto& read=local.Resource.ImageViewInfo;
        read.Width=std::min(w.Width,image->extent.width);read.Height=std::min(w.Height,image->extent.height);
        VkFrame::Failure failure;const auto& rect=ingress->Temporal().color;
        if(!VkFrame::ValidateRect("SR.Color",&original,rect.x,rect.y,rect.width,rect.height,false,failure)||
           !VkFrame::ValidateRect("NR.Color",&local,rect.x,rect.y,rect.width,rect.height,false,failure)){
            reason="Native active color rectangle exceeds declared or observed image bounds";return {};
        }
        const char* unavailable=nullptr;auto rights=facts.Rights(ingress->Device(),local,VK_IMAGE_USAGE_SAMPLED_BIT,false,&unavailable,diagnostic);
        if(!rights){
            // Encode samples this authenticated VkImageView, not the wrapper's
            // claimed range. Adapt only our private read; preserve already-valid
            // descriptors (including equivalent remaining counts) and the caller.
            read.SubresourceRange=observed->view.range;
            rights=facts.Rights(ingress->Device(),local,VK_IMAGE_USAGE_SAMPLED_BIT,false,&unavailable,diagnostic);
        }
        if(!rights){
            reason=unavailable?unavailable:"Native sampled color rights are unproved";
            if(diagnostic&&!diagnostic->empty())try{
                *diagnostic="incomingWrapperExtent="+std::to_string(w.Width)+"x"+std::to_string(w.Height)+
                    " extentBasisImageGeneration="+std::to_string(image->generation)+" qualifiedRightsFacts{"+*diagnostic+"}";
            }catch(...){diagnostic->clear();}
            return {};
        }
        if(rights->image.generation!=image->generation||rights->view.generation!=observed->view.generation||
           rights->view.imageGeneration!=observed->view.imageGeneration||
           rights->image.extent.width!=image->extent.width||rights->image.extent.height!=image->extent.height){
            reason="Native image or view changed during qualification";return {};
        }
        return std::shared_ptr<const VkNrQualifiedColor>(new VkNrQualifiedColor(std::move(ingress),local,*rights));
    }
    bool Current(const VkNrImageFacts& facts,const void* params,VkDevice device,uint64_t generation,
        const VkNrEvaluationIdentity& evaluation,uint32_t family,const VkNrTemporalMetadata& temporal) const {
        if(!ingress_->Current(params,device,generation,evaluation,family,temporal))return false;
        const auto now=facts.Rights(device,read_,VK_IMAGE_USAGE_SAMPLED_BIT,false);
        return now&&now->image.generation==rights_.image.generation&&now->view.generation==rights_.view.generation&&
            now->view.imageGeneration==rights_.view.imageGeneration;
    }
    bool Adapted() const{return !SameVkNrColorWrapper(read_,ingress_->Original());}
    // Diagnostic SR runs inside an intentional carrier binding. It may only use
    // the original strict descriptor, after ordinary publication/bind checks.
    bool StorageCurrent(const VkNrImageFacts& facts) const {
        if(!ingress_->OriginalStorageCurrent())return false;
        const auto now=facts.Rights(ingress_->Device(),ingress_->Original(),VK_IMAGE_USAGE_SAMPLED_BIT,false);
        return now&&now->image.generation==rights_.image.generation&&now->view.generation==rights_.view.generation&&
            now->view.imageGeneration==rights_.view.imageGeneration;
    }
    const NVSDK_NGX_Resource_VK& ReadDescriptor() const{return read_;}
    const VkNrImageRights& Rights() const{return rights_;}
    const std::shared_ptr<const VkNrColorIngress>& Ingress() const{return ingress_;}
private:
    VkNrQualifiedColor(std::shared_ptr<const VkNrColorIngress> ingress,NVSDK_NGX_Resource_VK read,VkNrImageRights rights)
        :ingress_(std::move(ingress)),read_(read),rights_(std::move(rights)){}
    std::shared_ptr<const VkNrColorIngress> ingress_;NVSDK_NGX_Resource_VK read_;VkNrImageRights rights_;
};

// Both production Color rereads use this selector. The small mutable CPU copy
// exists only for legacy read APIs; immutable capability metadata is authoritative.
template<class Request,class Params> bool SelectVkNrPreSrColor(Request& frame,Params* params,const VkNrImageFacts& facts,
    const VkNrRecordingOwner& owner,uint64_t deviceGeneration,bool reserved,std::string& reason) {
    const auto reject=[&](const char* why){reason=why;return false;};
    if(frame.route!=VkNrRoute::Native||frame.contract.placement!=VkNrPlacement::BeforeSR||frame.passIndex!=0||
       !frame.ngxSrInputContract||!frame.colorIngress||!frame.qualifiedColor||
       frame.qualifiedColor->Ingress()!=frame.colorIngress||!frame.contract.temporal)
        return reject("Native color capability used outside its selected first Pre-SR pass");
    const auto family=owner.CommandFamily(frame.commandBuffer);
    if(owner.CommandDevice(frame.commandBuffer)!=frame.device||owner.Incarnation(frame.commandBuffer)!=frame.contract.evaluation.incarnation||
       frame.commandBuffer!=frame.contract.evaluation.commandBuffer||
       (frame.contract.deviceGeneration&&frame.contract.deviceGeneration!=deviceGeneration)||
       (frame.contract.queueFamily!=UINT32_MAX&&frame.contract.queueFamily!=family)||
       !frame.qualifiedColor->Current(facts,params,frame.device,deviceGeneration,frame.contract.evaluation,family,*frame.contract.temporal))
        return reject("Native color provenance or observed image/view changed");
    if(reserved&&(!frame.use||!owner.UseOnRecording(frame.use,frame.commandBuffer)||owner.UseDevice(frame.use)!=frame.device||owner.UseFamily(frame.use)!=family))
        return reject("Native color actual reserved recording use or family changed");
    if(frame.qualifiedColor->Adapted()){
        if(!frame.colorReadStorage)frame.colorReadStorage=std::make_shared<NVSDK_NGX_Resource_VK>(frame.qualifiedColor->ReadDescriptor());
        if(!SameVkNrColorWrapper(*frame.colorReadStorage,frame.qualifiedColor->ReadDescriptor()))return reject("Native private CPU read descriptor changed");
        frame.color=frame.colorReadStorage.get();
    }else {frame.colorReadStorage.reset();frame.color=frame.colorIngress->OriginalPointer();}
    return true;
}
template<class Request> void ClearVkNrColorInput(Request& frame) noexcept {
    if(frame.colorReadStorage&&frame.color==frame.colorReadStorage.get())frame.color=nullptr;
    frame.colorReadStorage.reset();frame.qualifiedColor.reset();frame.colorIngress.reset();
}
inline const char* VkNrAdaptedDlaaRefusal(bool before,bool adapted,bool requested) noexcept {
    return before&&adapted&&requested?"Native adapted color with private DLAA awaits a descriptor lifetime contract":nullptr;
}
} // namespace DlssNr
