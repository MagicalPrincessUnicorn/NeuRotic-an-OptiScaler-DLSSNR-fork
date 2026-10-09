#pragma once
#include "NativeDispatchOutcome.h"
#include "NativeRendererPreparation.h"

class DlssNr_Dx12;
namespace DlssNr
{
// A capability issued only at the real resolve dispatch. The observation
// payload alone is deliberately insufficient to publish caller content.
class NativeOutputPassToken
{
    friend class ::DlssNr_Dx12;
    const NativeRendererInvocationBorrow* borrow_;
    NativeDispatchOutcome outcome_;
    NativeOutputPassToken(const NativeRendererInvocationBorrow* borrow,
        const NativeDispatchOutcome& outcome)noexcept:borrow_(borrow),outcome_(outcome){}
  public:
    NativeOutputPassToken(const NativeOutputPassToken&)=delete;
    NativeOutputPassToken& operator=(const NativeOutputPassToken&)=delete;
    const NativeDispatchOutcome& Outcome()const noexcept{return outcome_;}
    bool Matches(const NativeRendererInvocationBorrow& borrow,ID3D12GraphicsCommandList* list,
        ID3D12Resource* target)const noexcept
    {
        return borrow_==&borrow&&borrow.Current()&&
            outcome_.CommandList()==list&&outcome_.Target()==target&&
            outcome_.Effect()==NativeDispatchEffect::DispatchRecorded;
    }
};
}
