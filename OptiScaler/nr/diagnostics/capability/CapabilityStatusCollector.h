#pragma once
#include "CapabilityContract.h"
namespace DlssNr::Capability {
struct CollectionResult {
    CaptureResult capture;
    std::optional<uint64_t> configRevision;
    std::vector<CurrentStamp> configurationContext;
    std::vector<PublishResult> publications;
};
// Explicit non-render diagnostics action. Consumers cannot supply owner values.
CollectionResult CollectStatus(std::optional<bool> vulkan=std::nullopt) noexcept;
// Cheap UI dirty hint from passive HDR owner callbacks, including Native route.
uint64_t HdrChangeRevision() noexcept;
}
