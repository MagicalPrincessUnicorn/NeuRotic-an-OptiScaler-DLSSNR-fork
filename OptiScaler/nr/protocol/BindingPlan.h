#pragma once
#include "RecipeTypes.h"
#include <nr/context/NativeJitterBasis.h>

namespace Neurotic::Protocol
{
inline const Binding* FindBinding(const BindingPlan& plan,std::string_view key)
{for(const auto& value:plan.bindings)if(value.key.View()==key)return &value;return nullptr;}
inline bool AddBinding(BindingPlan& plan,const Binding& binding)
{return !binding.key.Empty()&&!FindBinding(plan,binding.key.View())&&Context::ValidValues(binding)&&plan.bindings.Push(binding);}
inline bool Scalar(BindingPlan& plan,Purpose purpose,std::string_view key,const C::ScalarValue& value,
                   const C::EvidenceRef& evidence,std::string_view source="ProfileRule")
{
    Binding binding;binding.purpose=purpose;binding.key=Symbol(key);binding.mode=Symbol("Scalar");
    binding.effective=C::OptionalFact<C::ScalarValue>::FromKnown(value,evidence);binding.effectiveSource=Symbol(source);
    binding.rule=Symbol("LegacyNative.Binding.v1");return AddBinding(plan,binding);
}
inline bool ClearOptionalResources(const C::EvidenceRef& evidence,BindingPlan& output)
{
    for(const auto& [purpose,key]:std::array{std::pair{Purpose::UILayer,"DLSSNR.UI"},
        std::pair{Purpose::UIAlpha,"DLSSNR.UIAlpha"},std::pair{Purpose::BackbufferForUICorrection,"DLSSNR.Backbuffer"}})
    {
        Binding binding;binding.purpose=purpose;binding.key=Symbol(key);binding.mode=Symbol("ExplicitNull");
        binding.effectiveSource=Symbol("ProfileRule");binding.rule=Symbol("LegacyNative.NoUiResources.v1");
        binding.stage=Symbol("CreateAndEvaluate");
        if(!AddBinding(output,binding))return false;
        for(const auto suffix:{"SubrectBaseX","SubrectBaseY","SubrectWidth","SubrectHeight"})
        {
            if(!Scalar(output,purpose,std::string(key)+suffix,C::ScalarValue{std::uint64_t{0}},evidence))return false;
            output.bindings.Get(output.bindings.Size()-1)->stage=Symbol("CreateAndEvaluate");
        }
    }
    return true;
}
inline const double* Number(const C::OptionalFact<C::ScalarValue>& value)
{return Context::Established(value)?std::get_if<double>(&value.KnownPart()->value):nullptr;}

template<class Reader> bool BuildFrameControls(const C::CanonicalFrameContext& context,const C::EvidenceRef& evidence,
                                               const Reader& reader,BindingPlan& output)
{
    const C::BoundedList<C::SemanticClaim,16>* fields=nullptr;
    if(!Lifecycle::ValidEvidence(evidence)||!Context::ResolveList(context.fields,reader,fields))return false;
    if(fields)for(std::size_t i=0;i<fields->Size();++i)for(std::size_t j=0;j<i;++j)
        if(fields->Get(i)->field==fields->Get(j)->field)return false;
    const auto claim=[&](std::string_view key)->const C::SemanticClaim*{
        if(fields)for(const auto& value:*fields)if(value.field.View()==key)return &value;return nullptr;};
    const auto number=[&](std::string_view key)->const double*{const auto* c=claim(key);return c?Number(c->effective):nullptr;};
    const auto numeric=[&](Purpose purpose,std::string_view field,std::string_view key,double fallback,bool force=false){
        const auto* c=claim(field);const auto* n=number(field);
        if(c&&c->effective.IsKnown()&&!n)return false;
        Binding binding;binding.purpose=purpose;binding.key=Symbol(key);binding.mode=Symbol("Scalar");
        if(c)binding.raw=c->raw;
        if(n&&!force){binding.effective=c->effective;binding.effectiveSource=Symbol(c->overrideSource.IsKnown()?"ExplicitOverride":"QualifiedObservation");}
        else {binding.effective=C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{fallback},evidence);binding.effectiveSource=Symbol("LegacyDefault");}
        binding.rule=Symbol("LegacyNative.OptionalControls.v1");return AddBinding(output,binding);
    };
    const bool jitterPair=number("Jitter.Offset.X")&&number("Jitter.Offset.Y");
    if(jitterPair)
    {
        const auto* units=claim("jitter.units");const auto* basis=claim("jitter.basis");
        if(!units||!basis||!Context::Established(units->effective)||!Context::Established(basis->effective))return false;
        const auto* u=std::get_if<C::Symbol>(&units->effective.KnownPart()->value);
        const auto* b=std::get_if<C::Symbol>(&basis->effective.KnownPart()->value);
        if(!u||!b||u->View()!="Pixels")return false;
        if(b->View()=="payload.raster")
        {if(!Context::QualifiedNativePayloadJitter(context,*number("Jitter.Offset.X"),*number("Jitter.Offset.Y"),reader))return false;}
        else if(b->View()!="motion.currentRaster")return false;
    }
    if(!numeric(Purpose::MotionVectorGuide,"MV.Scale.X","DLSSNR.MVecScaleX",1)||
       !numeric(Purpose::MotionVectorGuide,"MV.Scale.Y","DLSSNR.MVecScaleY",1)||
       !numeric(Purpose::ProjectionJitter,"Jitter.Offset.X","Jitter.Offset.X",0,!jitterPair)||
       !numeric(Purpose::ProjectionJitter,"Jitter.Offset.Y","Jitter.Offset.Y",0,!jitterPair)||
       !numeric(Purpose::PreExposureScalar,"DLSS.Pre.Exposure","DLSS.Pre.Exposure",1,
                number("DLSS.Pre.Exposure")&&*number("DLSS.Pre.Exposure")<=static_cast<double>(1e-6f)))return false;
    Binding reset;reset.purpose=Purpose::ColorModelInput;reset.key=Symbol("DLSSNR.Reset");reset.mode=Symbol("Scalar");
    reset.rule=Symbol("LegacyNative.OptionalControls.v1");reset.effectiveSource=Symbol("LegacyDefaultNoReset");
    bool requested=false;
    if(const auto* c=claim("Reset"))
    {
        reset.raw=c->raw;
        if(Context::Established(c->effective))
        {
            if(const auto* b=std::get_if<bool>(&c->effective.KnownPart()->value))requested=*b;
            else if(const auto* n=std::get_if<std::uint64_t>(&c->effective.KnownPart()->value))requested=*n!=0;
            else return false;
            reset.effectiveSource=Symbol("QualifiedObservation");
        }
    }
    reset.effective=C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{static_cast<std::uint64_t>(requested)},evidence);
    return AddBinding(output,reset);
}
}
