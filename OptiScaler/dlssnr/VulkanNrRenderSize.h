#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace DlssNr {
// Scalar observations only. No images, queue rights, frame tokens or GPU leases.
struct VkNrRenderSize { VkExtent2D extent{}; uint64_t featureGeneration=0; };
struct VkNrRenderSizeSelection {
    std::optional<VkNrRenderSize> value;
    const char* reason="No successful SR render-size observation since Present";
};
class VkNrRenderSizeObservations {
    struct Entry { VkExtent2D output; VkNrRenderSize size; };
    std::array<Entry,16> entries_{};
    size_t count_=0;
    bool rejected_=false;
  public:
    void Reset() {count_=0;rejected_=false;}
    void Reject() {count_=0;rejected_=true;}
    void Observe(VkExtent2D output,VkNrRenderSize size) {
        if(!size.featureGeneration||size.extent.width<8||size.extent.height<8||
            size.extent.width>output.width||size.extent.height>output.height||
            output.width>8192||output.height>8192) {Reject();return;}
        // A failed/overflowed interval remains rejected until the next Present.
        if(rejected_)return;
        for(size_t i=0;i<count_;++i) {
            const auto& e=entries_[i];
            if(e.output.width==output.width&&e.output.height==output.height&&
               e.size.featureGeneration==size.featureGeneration&&
               e.size.extent.width==size.extent.width&&e.size.extent.height==size.extent.height)return;
        }
        if(count_==entries_.size()) {Reject();return;}
        entries_[count_++]={output,size};
    }
    VkNrRenderSizeSelection Take(VkExtent2D output) {
        VkNrRenderSizeSelection result;
        if(rejected_)result.reason="SR render-size observation failed or exceeded capacity";
        else for(size_t i=0;i<count_;++i) {
            const auto& e=entries_[i];
            if(e.output.width!=output.width||e.output.height!=output.height)continue;
            if(result.value) {result.value.reset();result.reason="Multiple SR render-size observations match this output";break;}
            result.value=e.size;result.reason="";
        }
        Reset();return result;
    }
};
}
