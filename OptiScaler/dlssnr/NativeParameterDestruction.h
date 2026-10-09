#pragma once
#include <cstdint>
namespace DlssNr
{
struct NativeParameterDestructionObservation
{
    std::uint64_t parameters=0;
    std::uint32_t attempted=0,disposition=0,nativeResult=0,reserved=0;
};
// Substate of the actual parameter-map owner. Unknown/failed destruction keeps
// the map identity and prevents any subsequent attempt for this generation.
class NativeParameterDestruction
{
    NativeParameterDestructionObservation observed_;
  public:
    auto Observe()const noexcept{return observed_;}
    template<class Parameter,class Destroy>bool Destroy(Parameter*& parameters,Destroy&& destroy)noexcept
    {
        if(observed_.attempted)return observed_.disposition==1&&observed_.nativeResult==1&&parameters==nullptr;
        if(!parameters)return true;
        observed_.parameters=reinterpret_cast<std::uintptr_t>(parameters);observed_.attempted=1;
        observed_.disposition=2; // any exception leaves the effect uncertain
        try{observed_.nativeResult=static_cast<std::uint32_t>(destroy(parameters));observed_.disposition=1;}
        catch(...){return false;}
        if(observed_.nativeResult!=1)return false;
        parameters=nullptr;return true;
    }
};
}
