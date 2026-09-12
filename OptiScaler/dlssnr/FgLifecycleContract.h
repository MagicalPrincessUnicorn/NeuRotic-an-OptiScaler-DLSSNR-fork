#pragma once
#include <array>
#include <cstdint>

namespace DlssNr::FgLifecycle
{
// Observation only. Never feed these generations back into rendering admission.
struct Snapshot { uint64_t generation = 0, instance = 0; unsigned active = 0; };
class Instances
{
    struct Entry { uintptr_t handle = 0; uint64_t id = 0; };
    std::array<Entry, 128> entries {};
    uint64_t next = 0, generation = 0;
  public:
    uint64_t Find(uintptr_t handle) const
    {
        for (const auto& entry : entries) if (entry.id && entry.handle == handle) return entry.id;
        return 0;
    }
    uint64_t Create(uintptr_t handle, bool success)
    {
        if (!success || !handle) return 0;
        ++generation;
        for (auto& entry : entries)
            if (entry.id && entry.handle == handle) { entry = {handle, ++next}; return entry.id; }
        for (auto& entry : entries)
            if (!entry.id) { entry = {handle, ++next}; return entry.id; }
        return 0; // Explicitly unknown on capacity exhaustion.
    }
    void Release(uintptr_t handle, uint64_t expected, bool success)
    {
        if (!success) return;
        ++generation;
        for (auto& entry : entries)
            if (entry.handle == handle && entry.id == expected) entry = {};
    }
    Snapshot Read() const
    {
        Snapshot result {generation};
        for (const auto& entry : entries) if (entry.id) { ++result.active; result.instance = entry.id; }
        if (result.active != 1) result.instance = 0;
        return result;
    }
};
struct Budget
{
    static constexpr uint64_t limit = 8192;
    uint64_t used = 0;
    uint64_t Take() { return used < limit ? ++used : 0; }
};
}
