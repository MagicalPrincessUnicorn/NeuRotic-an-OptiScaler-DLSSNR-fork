#pragma once
#include <atomic>
#include <cstdint>

namespace DlssNr
{
template<class Feature> class NativeFeatureRegistry;
// Durable observation of one real registry generation. Only that registry may
// change these facts. Holding it never keeps a callback or provider entry open.
class NativeFeatureLifetime
{
    template<class Feature> friend class NativeFeatureRegistry;
    const std::uint64_t generation_;
    std::atomic<std::uint64_t> callbacks_{0},entries_{0};
    std::atomic<bool> closed_{false},released_{false},unknown_{false};
    explicit NativeFeatureLifetime(std::uint64_t generation):generation_(generation){}
  public:
    struct Observation
    {
        std::uint64_t activeCallbacks=0,opaqueEntries=0;
        bool admissionClosed=false,released=false,coverageUnknown=false;
    };
    std::uint64_t Generation()const noexcept{return generation_;}
    Observation Inspect()const noexcept
    {
        // Release is published last under the registry lock; successful release
        // permanently excludes new callbacks/entries for this exact generation.
        const bool released=released_.load();
        return {callbacks_.load(),entries_.load(),closed_.load(),released,unknown_.load()};
    }
};
}
