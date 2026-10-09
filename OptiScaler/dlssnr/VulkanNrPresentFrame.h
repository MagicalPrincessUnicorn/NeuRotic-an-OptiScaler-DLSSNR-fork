#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

namespace DlssNr {
struct VkNrPublicPresentFrame {
    uint64_t provider=0,frame=0;
    uint32_t viewport=UINT32_MAX;
};

// Scalar association only. Image rights still come from the exact acquired WSI
// image and guide rights from a successfully submitted, retained producer.
class VkNrPresentFrames {
    struct Frame {VkNrPublicPresentFrame id;bool used=false,invalid=false,consumed=false;};
    struct Scope {uint64_t provider=0,frame=0,thread=0;bool used=false;};
    std::mutex mutex_;
    std::array<Frame,128> frames_{};
    std::array<Scope,64> scopes_{};
    uint64_t provider_=0;
    size_t next_=0;
  public:
    void Provider(uint64_t provider){
        std::lock_guard lock(mutex_);
        if(provider_!=provider){provider_=provider;frames_={};scopes_={};next_=0;}
    }
    void Tags(uint64_t provider,uint64_t frame,uint32_t viewport,bool succeeded){
        std::lock_guard lock(mutex_);
        if(!provider||provider!=provider_||viewport==UINT32_MAX)return;
        Frame* found=nullptr;
        for(auto& value:frames_)if(value.used&&value.id.frame==frame){found=&value;break;}
        if(!found){found=&frames_[next_++%frames_.size()];*found={{provider,frame,viewport},true,false,false};}
        // Ambiguity/failure stays attached to this frame. A later single tag
        // cannot erase evidence that another viewport also claimed it.
        found->invalid|=!succeeded||found->id.viewport!=viewport;
    }
    void Start(uint64_t provider,uint64_t frame,uint64_t thread,bool succeeded){
        std::lock_guard lock(mutex_);
        for(auto& scope:scopes_)if(scope.used&&scope.thread==thread)scope={};
        if(!succeeded||!provider||provider!=provider_||!thread)return;
        for(auto& scope:scopes_)if(!scope.used){scope={provider,frame,thread,true};return;}
        // Exhaustion refuses this thread; never evict another live scope.
    }
    void End(uint64_t provider,uint64_t frame,uint64_t thread,bool succeeded){
        (void)provider;(void)frame;(void)succeeded;
        std::lock_guard lock(mutex_);
        for(auto& scope:scopes_)if(scope.used&&scope.thread==thread)scope={};
    }
    std::optional<VkNrPublicPresentFrame> Take(uint64_t provider,uint64_t thread){
        std::lock_guard lock(mutex_);
        if(!provider||provider!=provider_)return {};
        for(auto& scope:scopes_)if(scope.used&&scope.thread==thread){
            const auto frame=scope.frame;scope={};
            for(auto& entry:frames_)if(entry.used&&entry.id.frame==frame){
                if(entry.invalid||entry.consumed)return {};
                entry.consumed=true;return entry.id;
            }
            return {};
        }
        return {};
    }
};
inline VkNrPresentFrames& VulkanPublicPresentFrames(){static VkNrPresentFrames frames;return frames;}
}
