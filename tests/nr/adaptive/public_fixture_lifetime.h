#pragma once

// Test composition only. Without the explicit flag this header declares
// nothing, so it cannot add a provider to the shipping lifetime catalog.
#if defined(NR_ADAPTIVE_PUBLIC_PROVIDER_FIXTURE)
#if NR_ADAPTIVE_PUBLIC_PROVIDER_FIXTURE != 1
#error NR_ADAPTIVE_PUBLIC_PROVIDER_FIXTURE must be explicitly set to 1
#endif
#if !defined(NR_GPU_SAFETY_TEST)
#error Public provider fixture requires the isolated GPU safety test build
#endif

#include <dlssnr/NativeProviderLifetime.h>
#include <optional>
#include <string_view>

#define NR_ADAPTIVE_PUBLIC_PROVIDER_PORT_AVAILABLE 1
namespace DlssNr::AdaptivePublicFixture
{
inline constexpr std::string_view Identity =
    "NeuRotic.Adaptive.PublicGpuFixture/1;ControlledCallerList";

// Exact identity matching grants only the existing public test-provider lifetime
// contract. This neither creates a recording borrow nor publishes image content.
inline std::optional<ProviderLifetimeContract> ResolveLifetime(std::string_view identity)
{
    if (identity != Identity) return std::nullopt;
    return ProviderLifetimeContract::ControlledCallerList();
}
}
#endif
