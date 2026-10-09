#pragma once
#include "CapabilityOwnerAdapters.h"
namespace DlssNr::Capability {
enum class GraphicsPath : uint8_t { D3D11, D3D12, Vulkan };
enum class RequirementsOrigin : uint8_t { Raw, Effective };
struct RequirementsEnvelope {
    uint64_t sequence=0; GraphicsPath api=GraphicsPath::D3D12; RequirementsOrigin origin=RequirementsOrigin::Raw;
    uint64_t feature=0,result=0,supported=0,minimumArchitecture=0; bool outputDefined=false;
};
struct ParameterEnvelope {
    uint64_t sequence=0; OwnedText name,type,phase; bool readbackSucceeded=false,possibleWrite=false,synthetic=false;
};
using RequirementsObserver=void(*)(const RequirementsEnvelope&) noexcept;
using ParameterObserver=void(*)(const ParameterEnvelope&) noexcept;
inline std::atomic<RequirementsObserver> requirementsObserver{nullptr};
inline std::atomic<ParameterObserver> parameterObserver{nullptr};
inline void SetRequirementsObserver(RequirementsObserver f) noexcept { requirementsObserver.store(f); }
inline void SetParameterObserver(ParameterObserver f) noexcept { parameterObserver.store(f); }
// The original caller owns output for this expression only. Failed output is never read.
template<class Result,class Requirement> void CaptureRequirementsResult(GraphicsPath api,uint64_t feature,Result result,Result success,const Requirement* output,RequirementsOrigin origin) noexcept {
    if(auto callback=requirementsObserver.load()) {
        RequirementsEnvelope value; value.api=api; value.feature=feature; value.result=static_cast<uint64_t>(result); value.origin=origin;
        value.outputDefined=result==success&&output;
        if(value.outputDefined) { value.supported=static_cast<uint64_t>(output->FeatureSupported); value.minimumArchitecture=static_cast<uint64_t>(output->MinHWArchitecture); }
        callback(value);
    }
}
inline void CaptureParameterReadback(const char* key,std::string_view type,std::string_view phase,bool success,bool possibleWrite) noexcept {
    if(auto callback=parameterObserver.load()) {
        ParameterEnvelope value; if(!value.name.AssignC(key)||!value.type.Assign(type)||!value.phase.Assign(phase)) return;
        value.readbackSucceeded=success; value.possibleWrite=possibleWrite; callback(value);
    }
}
inline void CaptureSyntheticDefaults() noexcept { if(auto callback=parameterObserver.load()) { ParameterEnvelope value; value.synthetic=true; callback(value); } }
Batch AdaptRequirements(const WriterPort&,const RequirementsEnvelope&) noexcept;
Batch AdaptParameter(const WriterPort&,const ParameterEnvelope&) noexcept;
// Trusted collector initialization only; event paths never allocate/register ports.
void InitializePassiveObservers(AdapterFactory&) noexcept;
}
