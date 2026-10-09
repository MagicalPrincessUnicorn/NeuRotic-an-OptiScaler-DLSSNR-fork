#pragma once
#include "VulkanNrPassPlan.h"
#include "VulkanNrReservations.h"
namespace DlssNr {
struct VkNrHistoryIdentity {uint64_t generation=0,revision=0;bool operator==(const VkNrHistoryIdentity&)const=default;};
class VkNrPassHistory {
    std::array<std::vector<VkNrHistoryIdentity>,10> upstream_;
  public:
    bool Observe(uint32_t pass,const std::vector<VkNrHistoryIdentity>& prefix) {
        if(!pass||pass>=upstream_.size()||prefix.size()!=pass)return false;
        if(upstream_[pass]==prefix)return false;
        upstream_[pass]=prefix;return true;
    }
};
struct VkNrPassStep { VkRecordResult recording;NVSDK_NGX_Resource_VK* output=nullptr;uint64_t outputVersion=0; uint32_t passIndex=UINT32_MAX,requestedPasses=0,completedPasses=0;
    std::optional<NVSDK_NGX_Resource_VK> preparedColor;
};
struct VkNrPassCallbacks {
    std::function<VkNrPassStep(uint32_t,const VkFrameRequest&,const VkNrGenerationKey&,const VkNrReservation&,uint64_t)> prepare;
    std::function<VkNrPassStep(uint32_t,const VkFrameRequest&,const VkNrGenerationKey&,const VkNrReservation&,uint64_t)> record;
    std::function<bool(const VkNrPassStep&,uint32_t)> deliver;
};
constexpr uint32_t VkNrPassDemand(uint32_t count){return count>=1&&count<=10?count*8+2:0;}
VkNrChainResult RecordVkNrPassChain(const VkFrameRequest&,const VkNrPassPlan&,const VkNrReservation&,const VkNrPassCallbacks&);
}
