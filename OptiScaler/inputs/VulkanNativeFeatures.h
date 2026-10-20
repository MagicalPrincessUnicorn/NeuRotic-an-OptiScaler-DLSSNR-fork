#pragma once
#include <cstdint>
#include <unordered_map>

namespace VulkanNativeFeatures
{
struct Identity
{
    uint32_t type = 0;
    uintptr_t device = 0;
    uint64_t generation = 0, evaluations = 0;
};
class Registry
{
    std::unordered_map<uint32_t, Identity> entries;
    uint64_t generation = 0;
public:
    Identity Register(uint32_t handle, uint32_t type, uintptr_t device)
    {
        return entries[handle] = {type, device, ++generation, 0};
    }
    Identity Observe(uint32_t handle)
    {
        auto it = entries.find(handle);
        if (it == entries.end()) return {};
        ++it->second.evaluations;
        return it->second;
    }
    Identity Current(uint32_t handle) const
    {
        const auto it=entries.find(handle);
        return it==entries.end()?Identity{}:it->second;
    }
    void Release(uint32_t handle, bool succeeded) { if (succeeded) entries.erase(handle); }
    uint64_t ReleaseDevice(uintptr_t device) {
        uint64_t retired=0;
        for(auto it=entries.begin();it!=entries.end();) {
            if(it->second.device==device){
                if(it->second.generation>retired)retired=it->second.generation;
                it=entries.erase(it);
            }else ++it;
        }
        return retired;
    }
    void Clear() { entries.clear(); }
};
// Retires CPU source identity only; never establishes or releases GPU ownership.
uint64_t BeginDeviceRetirement(uintptr_t device);
}
