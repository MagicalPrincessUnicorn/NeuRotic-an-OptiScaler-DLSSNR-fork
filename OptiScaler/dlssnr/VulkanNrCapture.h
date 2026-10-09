#pragma once
#include "VulkanNrFrameContract.h"
#include "VulkanNrRecording.h"
#include <shaders/dlssnr/DlssNr_Common.h>
#include <functional>
#include <algorithm>
#include <memory>
#include <string>
namespace DlssNr {
struct VkNrCaptureImage {
 std::string name;VkImage image=VK_NULL_HANDLE;VkFormat format=VK_FORMAT_UNDEFINED;
 VkImageLayout layout=VK_IMAGE_LAYOUT_GENERAL;VkExtent2D extent{};float whitePoint=0;
};
struct VkNrCaptureRequest {
 VkNrFrameContract frame;VkNrUseId use;uint32_t requestedPasses=0,completedPasses=0;
 std::vector<VkNrCaptureImage> images;bool stages=false,saveOriginal=true,saveFinal=true;std::string settings,buildIdentity;
};
struct VkNrCapturePayload {
 uint64_t bytes=0;virtual bool HostVisible() const=0;
 virtual bool Publish(const VkNrCaptureRequest&)=0;virtual ~VkNrCapturePayload()=default;
};
struct VkNrCaptureCreation { std::unique_ptr<VkNrCapturePayload> payload;bool recorded=false;std::string reason; };
class VulkanNrCapture {
 public:
  using Create=std::function<VkNrCaptureCreation(const VkNrCaptureRequest&,VkImage,VkImage,uint64_t)>;
  using Release=std::function<bool(VkNrCapturePayload&,bool deviceAlive)>;
  VulkanNrCapture(VkNrRecordingOwner& owner,Create create,Release release):owner_(owner),create_(std::move(create)),release_(std::move(release)){}
  ~VulkanNrCapture();
  void Arm(uint64_t epoch){if(!Busy()){epoch_=epoch;armed_=true;status_="Waiting for a complete matched Vulkan frame";}}
  bool Record(const VkNrCaptureRequest&,VkImage original,VkImage final);
  void PublishCompleted(const VkNrRecordingOwner&);
  void RevokeNewRequests(uint64_t routeEpoch){if(!epoch_||epoch_!=routeEpoch){armed_=false;status_="Capture interrupted by route change; recorded copies remain owned";}}
  void Waiting(const std::string& reason){if(armed_)status_=reason;}
  void CancelNew(const std::string& reason){armed_=false;status_=reason;}
  void AbandonDevice();
  bool WantsFrame() const{return armed_;}bool Busy() const{return armed_||!entries_.empty();}
  uint64_t PrivateBytes() const{return bytes_;}const std::string& Status() const{return status_;}
 private:
  struct Entry { VkNrCaptureRequest request;std::unique_ptr<VkNrCapturePayload> payload;bool published=false; };
  VkNrRecordingOwner& owner_;Create create_;Release release_;std::vector<Entry> entries_;
  uint64_t epoch_=0,bytes_=0;bool armed_=false;std::string status_="Ready for Vulkan matched captures";
};
inline void PopulateVkNrComparisonConstants(uint32_t mode,float split,float zoom,bool swap,DlssNrConstants& c){
 c.CompareMode=std::min(mode,2u);c.CompareSplit=std::clamp(split,0.f,1.f);c.CompareZoom=std::clamp(zoom,1.f,2.f);c.CompareSwap=swap?1u:0u;
}
}
