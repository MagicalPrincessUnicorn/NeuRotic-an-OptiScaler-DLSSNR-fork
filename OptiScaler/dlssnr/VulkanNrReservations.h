#pragma once
#include "VulkanNrRecording.h"
#include <algorithm>
#include <array>
#include <functional>
#include <unordered_set>
namespace DlssNr
{
struct VkNrReservation { VkNrUseId use; std::vector<uint32_t> slots; };
class VkNrReservationPool
{
  public:
    static constexpr uint32_t InitialCapacity = 48, GrowthChunk = 32, HardCap = 256;
    using Grow = std::function<bool(uint32_t)>;
    explicit VkNrReservationPool(VkNrRecordingOwner& owner = VulkanNrRecordings(), Grow grow = {}) : owner_(owner),grow_(grow) {}
    ~VkNrReservationPool() { for (auto use : retained_) owner_.ReleaseUse(VkNrUseId{use}); }
    VkNrReservationPool(const VkNrReservationPool&) = delete;
    VkNrReservationPool& operator=(const VkNrReservationPool&) = delete;
    std::optional<VkNrReservation> Reserve(VkNrUseId use, uint32_t demand)
    {
        if (!use || !demand || demand > HardCap || retained_.contains(use.value)) return {};
        ReleaseCompleted(owner_);
        const auto freeSlots = [&] { uint32_t count=0; for(uint32_t i=0;i<capacity_;++i) if(!owners_[i]) ++count; return count; };
        while(freeSlots()<demand) {
            if(capacity_==HardCap) return {};
            const uint32_t next=capacity_==0?InitialCapacity:(std::min)(HardCap,capacity_+GrowthChunk);
            if(grow_&&!grow_(next)) return {};
            capacity_=next;
        }
        VkNrReservation reservation{use,{}}; reservation.slots.reserve(demand);
        for(uint32_t i=0;i<capacity_&&reservation.slots.size()<demand;++i) if(!owners_[i]) reservation.slots.push_back(i);
        if(!owner_.RetainUse(use)) return {};
        retained_.insert(use.value);
        for(auto slot:reservation.slots) { owners_[slot]=use; consumed_[slot]=false; }
        return reservation;
    }
    void ReleaseCompleted(const VkNrRecordingOwner& owner)
    {
        if(&owner!=&owner_) return;
        for(auto id=retained_.begin();id!=retained_.end();) {
            const VkNrUseId use{*id}; if(!owner.Reusable(use)){++id;continue;}
            for(uint32_t i=0;i<capacity_;++i) if(owners_[i]==use){owners_[i]={};consumed_[i]=false;}
            owner_.ReleaseUse(use);id=retained_.erase(id);
        }
    }
    bool SlotOwned(uint32_t slot) const { return slot<capacity_&&static_cast<bool>(owners_[slot]); }
    bool Consume(VkNrUseId use,uint32_t slot)
    {
        if(!use||slot>=capacity_||owners_[slot]!=use||consumed_[slot]||owner_.RecordingReleased(use)) return false;
        consumed_[slot]=true;return true;
    }
    uint32_t Capacity() const { return capacity_; }
    bool DrainReleased() { ReleaseCompleted(owner_); return retained_.empty(); }
  private:
    VkNrRecordingOwner& owner_; Grow grow_;
    uint32_t capacity_ = 0;
    std::array<VkNrUseId,HardCap> owners_{};
    std::array<bool,HardCap> consumed_{};
    std::unordered_set<uint64_t> retained_;
};
inline bool VkNrReadbackReady(const VkNrRecordingOwner& owner,VkNrUseId use,bool dataAvailable,bool coherent,bool invalidated)
{ return use&&dataAvailable&&owner.GpuComplete(use)&&(coherent||invalidated); }
} // namespace DlssNr
