#pragma once
#include <cstdint>
#include <optional>

namespace DlssNr {
template<class Feature> class NativeFeatureRegistry;
// Identity of an observed Native SR source in one existing feature registry.
// This is deliberately not C14/BaseRealFrameId or a final-consumption proof.
// Only the registry may publish continuity after matching the actual enclosing
// Streamline frame/viewport and the previous successful SR return.
class NativeTemporalSource {
    template<class Feature> friend class NativeFeatureRegistry;
    std::uintptr_t owner_=0;
    std::uint64_t generation_=0,serial_=0;
    NativeTemporalSource(std::uintptr_t owner,std::uint64_t generation,std::uint64_t serial)
        :owner_(owner),generation_(generation),serial_(serial){}
  public:
    NativeTemporalSource()=default;
    bool Valid()const noexcept{return owner_&&generation_&&serial_;}
    std::uint64_t Serial()const noexcept{return serial_;}
    bool operator==(const NativeTemporalSource&)const=default;
#ifdef NR_AFNR_TEST
    static NativeTemporalSource Fixture(std::uint64_t serial){return {1,1,serial};}
#endif
};
struct NativeTemporalSourceObservation {
    std::optional<NativeTemporalSource> source,predecessor;
    bool duplicate=false,contradictoryDuplicate=false;
    unsigned viewport=0;
};
}
