#pragma once
#include <cstdint>

namespace Neurotic::D3D12
{
// Internal replay failure carries an actionable reason without owning objects.
struct NativeReplayFailure
{
    const char* reason;
    std::uint32_t slot;
};
}
