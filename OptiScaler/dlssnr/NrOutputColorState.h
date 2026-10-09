#pragma once

#include "PresentColor.h"
#include "NrPreflightState.h"

namespace DlssNr::OutputColorState
{
enum class Api { Unknown, Dxgi, Vulkan };

// Diagnostic classification only. Detection does not authorize rendering.
inline NrPreflightState::State Read(Api api, bool fresh, bool inUse,
                                   const HdrObservation::Snapshot& dxgi,
                                   DXGI_FORMAT actualFormat, bool vkFormatObserved)
{
    using NrPreflightState::State;
    if (!fresh || api == Api::Unknown)
        return State::Inactive;

    // Vulkan owns its typed observation; DXGI setter/descriptor facts do not
    // participate in that path. DXGI uses the renderer's existing classifier,
    // which accepts supported registered defaults without SetColorSpace1.
    const bool supported = api == Api::Vulkan
        ? vkFormatObserved : PresentColor::Select(dxgi, actualFormat).Supported();
    return NrPreflightState::Usage(supported ? State::Detected : State::Fault, inUse);
}
}
