#pragma once

namespace DlssNr::NativeTemporalInputs
{
constexpr bool Authoritative(bool superResolution, bool dlssBackend, bool dx12, bool adapter)
{
    return superResolution && dlssBackend && dx12 && !adapter;
}
}
