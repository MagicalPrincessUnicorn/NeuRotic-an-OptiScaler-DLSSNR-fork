#pragma once
#include <Windows.h>
#include <cstdint>
#include "../../../external/FidelityFX-CheckedClosure/ffx_neurotic_checked_closure.h"
namespace Neurotic::Lifecycle
{
enum class Fsr3SourceBoundPreflightStatus
{ MissingModule,MissingQualifiedExport,UnsupportedService,InvalidService,QualificationUnavailable };
struct Fsr3SourceBoundPreflight
{
    Fsr3SourceBoundPreflightStatus status=Fsr3SourceBoundPreflightStatus::MissingModule;
    FfxNrStatusV1 closureStatus=FFX_NR_UNSUPPORTED,dispatchStatus=FFX_NR_UNSUPPORTED;
    std::uint64_t provider=0,coverage=0;
};
// Read-only query of the retained actual module. Experimental services and
// owned allocation leases are not inputs: they carry lifetime facts, not the
// independent FG C04 qualification required before NR admission.
inline Fsr3SourceBoundPreflight ObserveFsr3SourceBoundPreflight(HMODULE retainedModule)noexcept
{
    Fsr3SourceBoundPreflight result;if(!retainedModule)return result;
    using Closure=FfxNrStatusV1(FFX_NR_CALL*)(FfxNrClosureServiceV1*);
    using Dispatch=FfxNrStatusV1(FFX_NR_CALL*)(FfxNrDispatchServiceV1*);
    const auto closure=reinterpret_cast<Closure>(GetProcAddress(retainedModule,"ffxNeuRoticQueryClosureServiceV1"));
    const auto dispatch=reinterpret_cast<Dispatch>(GetProcAddress(retainedModule,"ffxNeuRoticQueryDispatchServiceV1"));
    if(!closure||!dispatch){result.status=Fsr3SourceBoundPreflightStatus::MissingQualifiedExport;return result;}
    FfxNrClosureServiceV1 service{};service.size=sizeof(service);service.version=1;
    FfxNrDispatchServiceV1 dispatchService{};dispatchService.size=sizeof(dispatchService);dispatchService.version=1;
    try{result.closureStatus=closure(&service);result.dispatchStatus=dispatch(&dispatchService);}
    catch(...){result.status=Fsr3SourceBoundPreflightStatus::InvalidService;return result;}
    result.provider=service.build.provider_id;result.coverage=service.build.supported_feature_bits;
    if(result.closureStatus==FFX_NR_UNSUPPORTED||result.dispatchStatus==FFX_NR_UNSUPPORTED)
    {result.status=Fsr3SourceBoundPreflightStatus::UnsupportedService;return result;}
    if(result.closureStatus!=FFX_NR_COMPLETE||result.dispatchStatus!=FFX_NR_COMPLETE||
       service.size!=sizeof(service)||service.version!=1||dispatchService.size!=sizeof(dispatchService)||dispatchService.version!=1||
       !service.get_context||!service.begin||!service.poll||!service.read_domains||!service.consume_for_destroy||
       !dispatchService.enroll||!dispatchService.current_dispatch||!dispatchService.validate_dispatch||!dispatchService.set_observer)
    {result.status=Fsr3SourceBoundPreflightStatus::InvalidService;return result;}
    // No reviewed SourceBoundConsumer-v1 capability catalog/C04 issuer exists
    // for this ABI yet. Nonzero advertised coverage cannot select a route.
    result.status=Fsr3SourceBoundPreflightStatus::QualificationUnavailable;return result;
}
}
