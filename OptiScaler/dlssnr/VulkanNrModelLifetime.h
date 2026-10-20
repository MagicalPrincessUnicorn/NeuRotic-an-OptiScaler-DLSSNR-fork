#pragma once
#include <cstdint>
#include <optional>
namespace DlssNr {
// Actual model-owner identity; zero resume/input epochs are valid initial values.
// This carries no resource ownership or GPU/WSI completion capability.
struct VkNrPresentModelLifetime {
    uintptr_t device=0;
    uint64_t deviceGeneration=0,routeEpoch=0,resumeGeneration=0,inputInterruptionEpoch=0;
    bool Qualified()const noexcept{return device&&deviceGeneration&&routeEpoch;}
    bool operator==(const VkNrPresentModelLifetime&)const=default;
};
// Unknown on unavailable configuration, inactive route or owner contention.
// Call outside both model locks; recording->state order uses nonblocking tries.
std::optional<VkNrPresentModelLifetime> CurrentVulkanPresentModelLifetimeVk();
}
