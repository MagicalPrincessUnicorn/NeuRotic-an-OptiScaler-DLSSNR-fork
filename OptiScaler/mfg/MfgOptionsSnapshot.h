#pragma once

#include <sl_dlss_g.h>

#include <cstddef>
#include <cstdint>

namespace Neurotic::Mfg
{
struct MfgOwnedOptions
{
    sl::DLSSGOptions value {};
    std::size_t callerVersion = 0;
    std::size_t copiedBytes = 0;
    bool supported = false;
    bool replayable = false;
    bool hasCallback = false;
    bool hasExtensions = false;
};

// Decode named fields from the pinned append-only ABI. Historical sizeof
// includes tail padding which later versions reuse for new fields; copying
// that padding into a current object would turn it into fabricated options.
inline MfgOwnedOptions CaptureOptions(const sl::DLSSGOptions& source) noexcept
{
    MfgOwnedOptions snapshot;
    snapshot.callerVersion = source.structVersion;
    switch (snapshot.callerVersion)
    {
    case 1: snapshot.copiedBytes = 104; break;
    case 2:
    case 3: snapshot.copiedBytes = 112; break;
    case 4: snapshot.copiedBytes = 120; break;
    case 5: snapshot.copiedBytes = sizeof(sl::DLSSGOptions); break;
    default: return snapshot;
    }
    static_assert(sizeof(sl::DLSSGOptions) == 120);
    if (source.structType != sl::DLSSGOptions::s_structType) return snapshot;
    auto& value = snapshot.value;
    value.mode = source.mode;
    value.numFramesToGenerate = source.numFramesToGenerate;
    value.flags = source.flags;
    value.dynamicResWidth = source.dynamicResWidth;
    value.dynamicResHeight = source.dynamicResHeight;
    value.numBackBuffers = source.numBackBuffers;
    value.mvecDepthWidth = source.mvecDepthWidth;
    value.mvecDepthHeight = source.mvecDepthHeight;
    value.colorWidth = source.colorWidth;
    value.colorHeight = source.colorHeight;
    value.colorBufferFormat = source.colorBufferFormat;
    value.mvecBufferFormat = source.mvecBufferFormat;
    value.depthBufferFormat = source.depthBufferFormat;
    value.hudLessBufferFormat = source.hudLessBufferFormat;
    value.uiBufferFormat = source.uiBufferFormat;
    if (snapshot.callerVersion >= 2) value.bReserved15 = source.bReserved15;
    if (snapshot.callerVersion >= 3) value.queueParallelismMode = source.queueParallelismMode;
    if (snapshot.callerVersion >= 4) value.enableUserInterfaceRecomposition = source.enableUserInterfaceRecomposition;
    if (snapshot.callerVersion >= 5) value.dynamicTargetFrameRate = source.dynamicTargetFrameRate;
    // Pointer presence affects retained replay, not passive scalar observation.
    // Never retain or traverse either caller-owned pointer.
    snapshot.hasCallback = source.onErrorCallback != nullptr;
    snapshot.hasExtensions = source.next != nullptr;
    snapshot.supported = true;
    snapshot.replayable = !snapshot.hasCallback && !snapshot.hasExtensions;
    return snapshot;
}
}
