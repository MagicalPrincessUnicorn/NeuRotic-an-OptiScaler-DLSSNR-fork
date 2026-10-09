#pragma once
#include <cstdint>

struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace DlssNr
{
// Local CPU observations of one actual pass, not a Resource write revision,
// caller-target authorization, restoration result, or GPU completion receipt.
enum class NativeDispatchEffect : std::uint8_t
{
    RefusedBeforeCommands,
    StateMutationPossible,
    DispatchPossible,
    DispatchRecorded
};
enum class NativeOutputDerivation : std::uint8_t {ModelComposition,AlternateFrameCarry};

class NativeDispatchOutcome
{
    ID3D12GraphicsCommandList* list_;
    ID3D12Resource* target_;
    std::uint32_t width_, height_;
    NativeDispatchEffect effect_=NativeDispatchEffect::RefusedBeforeCommands;
    NativeOutputDerivation derivation_=NativeOutputDerivation::ModelComposition;

    void Advance(NativeDispatchEffect effect)noexcept
    {if(effect>effect_)effect_=effect;}
  public:
    NativeDispatchOutcome(ID3D12GraphicsCommandList* list,ID3D12Resource* target,
        std::uint32_t width,std::uint32_t height,NativeOutputDerivation derivation=NativeOutputDerivation::ModelComposition)noexcept:
        list_(list),target_(target),width_(width),height_(height),derivation_(derivation){}

    NativeDispatchEffect Effect()const noexcept{return effect_;}
    NativeOutputDerivation Derivation()const noexcept{return derivation_;}
    ID3D12GraphicsCommandList* CommandList()const noexcept{return list_;}
    ID3D12Resource* Target()const noexcept{return target_;}
    std::uint32_t Width()const noexcept{return width_;}
    std::uint32_t Height()const noexcept{return height_;}
    // DispatchPass binds mip zero of its current 2D UAV and uses origin zero.
    // This local description does not qualify an arbitrary caller region.
    std::uint32_t Subresource()const noexcept{return 0;}

    void ObserveStateMutation()noexcept{Advance(NativeDispatchEffect::StateMutationPossible);}
    void ObserveDispatchPossible()noexcept{Advance(NativeDispatchEffect::DispatchPossible);}
    void ObserveDispatchRecorded()noexcept{Advance(NativeDispatchEffect::DispatchRecorded);}
    bool operator==(const NativeDispatchOutcome&)const=default;
};
}
