#pragma once
#include "NativeParameterDestruction.h"
namespace DlssNr
{
// Diagnostic POD only; it cannot authorize resource or scope retirement.
struct NativeRendererShutdownObservationV1
{
    std::uint32_t size=sizeof(NativeRendererShutdownObservationV1),version=1;
    std::uint32_t attempted=0,succeeded=0;
    NativeParameterDestructionObservation internalParameters;
};
static_assert(sizeof(NativeRendererShutdownObservationV1)==40);
using QueryNativeRendererShutdownV1=unsigned(__cdecl*)(NativeRendererShutdownObservationV1*,std::uint32_t) noexcept;
}
