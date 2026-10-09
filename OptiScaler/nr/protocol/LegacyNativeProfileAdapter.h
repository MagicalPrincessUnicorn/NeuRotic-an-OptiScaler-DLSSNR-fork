#pragma once
// Pointer-free command projection only. Execution/C03 resolution belongs to the Native owner.
#include "BindingPlan.h"
#include <limits>

namespace Neurotic::Protocol
{
enum class LegacyStage{Create,Evaluate};
enum class CommandKind{UInt,Float,NullResource,Representation};
struct LegacyCommand
{
    CommandKind kind=CommandKind::UInt;C::Symbol key;
    std::uint32_t integer=0;float number=0;
    std::optional<C::ContractRef<C::ContractId::C12>> representation;
};
inline bool LegacyResourceKey(std::string_view key)
{
    for(const auto allowed:{"DLSSNR.Color","DLSSNR.Depth","DLSSNR.MVec","DLSSNR.Output","DLSSNR.UI","DLSSNR.UIAlpha","DLSSNR.Backbuffer"})
        if(key==allowed)return true;return false;
}
inline bool LegacyScalarKey(std::string_view key,bool number)
{
    if(number)
    {
        for(const auto allowed:{"DLSSNR.MVecScaleX","DLSSNR.MVecScaleY","Jitter.Offset.X","Jitter.Offset.Y","DLSSNR.Intensity",
            "DLSSNR.LocalStructureStrength","DLSSNR.LocalToneStrength","DLSSNR.SkinStructureStrength"})if(key==allowed)return true;
        return false;
    }
    for(const auto allowed:{"DLSSNR.Enabled","DLSSNR.Width","DLSSNR.Height","DLSSNR.DepthInverted","DLSSNR.Reset",
        "DLSSNR.Hint.Render.Preset","DLSSNR.Style","DLSSNR.UseAutoMask","DLSSNR.UICorrection","CreationNodeMask","VisibilityNodeMask"})
        if(key==allowed)return true;
    for(const auto prefix:{"DLSSNR.Color","DLSSNR.Depth","DLSSNR.MVec","DLSSNR.Output","DLSSNR.UI","DLSSNR.UIAlpha","DLSSNR.Backbuffer"})
        for(const auto suffix:{"SubrectBaseX","SubrectBaseY","SubrectWidth","SubrectHeight"})if(key==std::string(prefix)+suffix)return true;
    return false;
}
inline std::optional<C::BoundedList<LegacyCommand,64>> BuildLegacyCommands(const BindingPlan& plan,LegacyStage stage)
{
    C::BoundedList<LegacyCommand,64> result;
    if(!Context::ValidValues(plan)||plan.historyAdvanceRequested)return {};
    if(!plan.modelEvaluation)return plan.bindings.Size()==0?std::optional{result}:std::nullopt;
    for(std::size_t i=0;i<plan.bindings.Size();++i)
    {
        const auto& binding=*plan.bindings.Get(i);
        for(std::size_t j=0;j<i;++j)if(plan.bindings.Get(j)->key==binding.key)return {};
        const auto scope=binding.stage.View();
        if(scope!="Create"&&scope!="Evaluate"&&scope!="CreateAndEvaluate"&&scope!="Frame")return {};
        if(scope=="Frame"||(stage==LegacyStage::Create&&scope=="Evaluate")||(stage==LegacyStage::Evaluate&&scope=="Create"))continue;
        LegacyCommand command;command.key=binding.key;
        if(binding.mode.View()=="ExplicitNull")
        {if(!LegacyResourceKey(command.key.View())||binding.representation)return {};command.kind=CommandKind::NullResource;}
        else if(binding.mode.View()=="Representation")
        {if(!LegacyResourceKey(command.key.View())||!binding.representation)return {};command.kind=CommandKind::Representation;command.representation=binding.representation;}
        else if(binding.mode.View()=="Scalar"&&Context::Established(binding.effective))
        {
            const auto& value=binding.effective.KnownPart()->value;
            if(const auto* integer=std::get_if<std::uint64_t>(&value))
            {if(!LegacyScalarKey(command.key.View(),false)||*integer>(std::numeric_limits<std::uint32_t>::max)())return {};command.integer=static_cast<std::uint32_t>(*integer);}
            else if(const auto* number=std::get_if<double>(&value))
            {if(!LegacyScalarKey(command.key.View(),true)||!std::isfinite(*number)||std::abs(*number)>(std::numeric_limits<float>::max)())return {};command.kind=CommandKind::Float;command.number=static_cast<float>(*number);}
            else return {};
        }
        else return {};
        if(!result.Push(command))return {};
    }
    return result;
}
}
