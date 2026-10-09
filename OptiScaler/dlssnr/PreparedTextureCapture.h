#pragma once
#include <inputs/universal_feeder/PreparedGuideContract.h>
#include "../../addons/prepared-guides/src/CpuMainlineClient.h"

namespace DlssNr::NativeGuides {
// Internal interface, never a DLL or wire ABI. The adapter retains immutable
// canonical inputs until all readers release normalized.lease. Resources are
// full-raster NON_PIXEL_SHADER_RESOURCE textures on the same logical device.
struct TextureCapture {
    Neurotic::Feed::Prepared::Descriptor description{};
    std::uint64_t adapterLuid=0;
    nrpg::CpuMainlineClient::GpuInputs normalized{};
    bool deriveMotion=true;
};
}
