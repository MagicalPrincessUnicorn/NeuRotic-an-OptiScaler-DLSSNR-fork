#pragma once
#include <cstdint>
#include <type_traits>
#include <atomic>
namespace DlssNr
{
// Checked shutdown observation, never an owner authority or module-unload permit.
// NR success is issued only by the renderer's existing GPU/provider/release gates.
struct NativeTeardownObservationV1
{
    std::uint32_t size=sizeof(NativeTeardownObservationV1),version=1;
    std::uint32_t sourceClosed=0,callbacks=0,opaqueCaptures=0,controlledProducers=0;
    std::uint32_t scopeClosed=0,finalizerStopped=0,nrShutdownAttempted=0,nrShutdownSucceeded=0;
};
static_assert(sizeof(NativeTeardownObservationV1)==40);
static_assert(std::is_trivially_copyable_v<NativeTeardownObservationV1>);
inline bool SourceCapturesDrained(const NativeTeardownObservationV1& value)noexcept
{
    return value.size==sizeof(value)&&value.version==1&&value.sourceClosed==1&&
        value.callbacks==0&&value.opaqueCaptures==0&&value.controlledProducers==0;
}
// Process-scoped actual-attempt gate used by the controlled product export.
// A preflight refusal may be polled; once a foreign shutdown is entered, even
// an exception cannot permit a second attempt or reuse an old success result.
class NativeTeardownAttempt
{
    std::atomic<bool> attempted_{false};
  public:
    template<class Shutdown>void Observe(NativeTeardownObservationV1& output,Shutdown&& shutdown)noexcept
    {
        output.nrShutdownAttempted=0;output.nrShutdownSucceeded=0;
        if(!SourceCapturesDrained(output))return;
        output.nrShutdownAttempted=1;
        if(attempted_.exchange(true))return;
        try{output.nrShutdownSucceeded=shutdown()?1u:0u;}
        catch(...){/* Entered but outcome uncertain; keep the attempt latched. */}
    }
};
using PrepareNativeTeardownV1=unsigned(__cdecl*)(NativeTeardownObservationV1*,std::uint32_t) noexcept;
}
