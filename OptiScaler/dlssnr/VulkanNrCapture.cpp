#include "VulkanNrCapture.h"
namespace DlssNr {
VulkanNrCapture::~VulkanNrCapture(){AbandonDevice();}
bool VulkanNrCapture::Record(const VkNrCaptureRequest& request,VkImage original,VkImage final){
 if(!armed_||!original||!final||original==final||!request.use||!request.requestedPasses||request.requestedPasses!=request.completedPasses||
    (epoch_&&epoch_!=request.frame.routeEpoch))return false;
 constexpr uint64_t budget=512ull*1024*1024;
 if(entries_.size()>=8||bytes_>=budget){status_="Capture capacity awaits completion and recording release";return false;}
 // Finish host allocations before recording GPU work. A diagnostic allocation failure
 // must never drop the only owner of buffers referenced by an accepted command buffer.
 Entry entry;
 try{entry.request=request;entries_.reserve(entries_.size()+1);}catch(...){armed_=false;return false;}
 if(!owner_.RetainUse(request.use)){status_="Capture recording use unavailable";return false;}
 VkNrCaptureCreation creation;
 try{creation=create_(request,original,final,budget-bytes_);}catch(...){owner_.ReleaseUse(request.use);armed_=false;return false;}
 if(!creation.payload||!creation.recorded){if(creation.payload)release_(*creation.payload,true);owner_.ReleaseUse(request.use);
  status_=creation.reason.empty()?"Capture readback unavailable; rendering continues":creation.reason;armed_=false;return false;}
 bytes_+=creation.payload->bytes;entry.payload=std::move(creation.payload);entries_.push_back(std::move(entry));armed_=false;
 status_="Capture recorded; waiting for its actual GPU completion";return true;
}
void VulkanNrCapture::PublishCompleted(const VkNrRecordingOwner& owner){
 if(&owner!=&owner_)return;
 for(auto it=entries_.begin();it!=entries_.end();){
  if(!it->published&&owner.GpuComplete(it->request.use)&&owner.RecordingReleased(it->request.use)&&it->payload->HostVisible()){
   const bool ok=it->payload->Publish(it->request);it->published=true;status_=ok?"Saved matched Vulkan captures":"Capture file publication failed; rendering continues";}
  if((it->published||owner.Reusable(it->request.use)&&!owner.GpuComplete(it->request.use))&&owner.Reusable(it->request.use)&&release_(*it->payload,true)){
   bytes_-=it->payload->bytes;owner_.ReleaseUse(it->request.use);it=entries_.erase(it);
  }else ++it;
 }
}
void VulkanNrCapture::AbandonDevice(){armed_=false;for(auto& e:entries_){release_(*e.payload,false);owner_.ReleaseUse(e.request.use);}entries_.clear();bytes_=0;status_="Capture device unavailable";}
}
