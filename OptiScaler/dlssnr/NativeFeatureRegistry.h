#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace DlssNr
{
// Return values, never iterators, across NGX calls. A failed release keeps its
// mapping; a delayed release cannot erase a replacement with the same handle.
template<class Feature> class NativeFeatureRegistry
{
  public:
    struct Snapshot
    {
        Feature feature {};
        uint64_t generation = 0;
        explicit operator bool() const { return generation != 0; }
    };
  private:
    mutable std::mutex mutex;
    std::unordered_map<unsigned int, Snapshot> entries;
    uint64_t generation = 0;
  public:
    void Set(unsigned int handle, Feature feature)
    {
        std::lock_guard lock(mutex);
        if (!++generation) ++generation;
        entries[handle] = {feature, generation};
    }
    Snapshot Read(unsigned int handle) const
    {
        std::lock_guard lock(mutex);
        const auto it = entries.find(handle);
        return it != entries.end() ? it->second : Snapshot{};
    }
    bool Has(Feature feature) const
    {
        std::lock_guard lock(mutex);
        for (const auto& [handle, entry] : entries)
            if (entry.feature == feature) return true;
        return false;
    }
    bool Released(unsigned int handle, const Snapshot& expected, bool success)
    {
        if (!success || !expected) return false;
        std::lock_guard lock(mutex);
        const auto it = entries.find(handle);
        if (it == entries.end() || it->second.generation != expected.generation) return false;
        entries.erase(it);
        return true;
    }
};
}
