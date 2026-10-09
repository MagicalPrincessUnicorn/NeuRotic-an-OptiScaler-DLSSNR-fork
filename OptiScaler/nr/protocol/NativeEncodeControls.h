#pragma once
#include "BindingPlan.h"
#include "ProfileQualification.h"
#include <cmath>

namespace Neurotic::Protocol
{
// Exact ABI of the existing initial-slice encode dispatch. These values are
// semantic inputs, never evidence that pixels were produced or rights acquired.
struct NativeEncodeControls
{
    std::uint32_t passthrough=0;
    float whitePoint=0;
    std::uint32_t useGameExposure=0;
    float exposurePreMul=0;
    std::uint32_t reversibleMode=0,width=0,height=0;
    bool operator==(const NativeEncodeControls&)const=default;
};
#define NR_NATIVE_ENCODE_COLOR_V1 1
inline bool NativeEncodeMatchesColor(const NativeEncodeControls& encode,const C::ColorDescription& color)
{
    if(!Context::Established(color.domain))return false;
    const auto domain=color.domain.KnownPart()->value;
    if(domain==C::ColorDomain::Control)return false;
    const bool linear=domain==C::ColorDomain::SceneLinear||domain==C::ColorDomain::DisplayLinear;
    if(encode.passthrough!=(linear?0u:1u))return false;
    if(linear&&Context::Established(color.transfer)&&color.transfer.KnownPart()->value.View()!="Linear")return false;
    return linear||domain==C::ColorDomain::EncodedDisplay;
}
inline constexpr std::array<std::string_view,7> NativeEncodeKeys={
    "Native.Encode.Passthrough","Native.Encode.WhitePoint","Native.Encode.UseGameExposure",
    "Native.Encode.ExposurePreMul","Native.Encode.ReversibleMode","Native.Encode.Width","Native.Encode.Height"};
template<class Lookup>std::optional<NativeEncodeControls> DecodeNativeEncode(Lookup lookup)
{
    std::array<std::uint64_t,7> integers{};std::array<double,7> numbers{};
    for(std::size_t i=0;i<NativeEncodeKeys.size();++i)
    {
        const auto* value=lookup(NativeEncodeKeys[i]);if(!value||!Context::Established(*value))return {};
        const auto& scalar=value->KnownPart()->value;
        if(i==1||i==3)
        {
            const auto* n=std::get_if<double>(&scalar);
            if(!n||!std::isfinite(*n)||std::abs(*n)>(std::numeric_limits<float>::max)()||
                static_cast<double>(static_cast<float>(*n))!=*n)return {};
            numbers[i]=*n;
        }
        else {const auto* n=std::get_if<std::uint64_t>(&scalar);if(!n)return {};integers[i]=*n;}
    }
    if(integers[0]>1||numbers[1]<=0||integers[2]!=0||numbers[3]!=0||integers[4]>4||
        integers[5]<8||integers[5]>8192||integers[6]<8||integers[6]>8192)return {};
    return NativeEncodeControls{static_cast<std::uint32_t>(integers[0]),static_cast<float>(numbers[1]),
        0,0,static_cast<std::uint32_t>(integers[4]),static_cast<std::uint32_t>(integers[5]),static_cast<std::uint32_t>(integers[6])};
}
template<class Reader>std::optional<NativeEncodeControls> NativeEncodeForUse(const RepresentationUse& use,const Reader& reader)
{
    if(use.purpose!=Purpose::ColorModelInput||use.kind.View()!="ModelProxy"||Context::Established(use.prepared))return {};
    const auto* plan=Context::ResolveMetadata(use.plan,reader);
    const Context::TransformLineage* deferred=nullptr;
    if(!plan||!LineageMatches(use,*plan,reader)||!Context::ResolveList(use.consumerDeferred,reader,deferred)||!deferred)return {};
    const C::TransformStep* encode=nullptr;
    for(const auto& step:*deferred)if(step.operation.View()=="LegacyNative.Encode.v1")
    {if(encode)return {};encode=&step;}
    if(!encode||encode->kind!=C::TransformKind::ColorTransfer||encode->version!=1||
        !Context::Established(encode->sourceUnits)||encode->sourceUnits.KnownPart()->value.View()!="QualifiedColor"||
        !Context::Established(encode->targetUnits)||encode->targetUnits.KnownPart()->value.View()!="LegacyNative.ModelEncoded.v1")return {};
    const C::BoundedList<C::SemanticClaim,8>* parameters=nullptr;
    if(!Context::ResolveList(encode->parameters,reader,parameters)||!parameters||parameters->Size()!=7)return {};
    const auto result=DecodeNativeEncode([&](std::string_view key)->const C::OptionalFact<C::ScalarValue>*{
        const C::OptionalFact<C::ScalarValue>* found=nullptr;
        for(const auto& parameter:*parameters)if(parameter.field.View()==key){if(found)return nullptr;found=&parameter.effective;}return found;});
    const auto* mapping=Context::ResolveMetadata(plan->raster,reader);
    if(!result||!mapping||!Context::ValidMapping(*mapping)||
        !Context::SemanticEqual(mapping->source,mapping->target)||
        mapping->scale.KnownPart()->value!=C::Vec2{1,1}||mapping->offset.KnownPart()->value!=C::Vec2{0,0})return {};
    const auto rect=mapping->target.active.KnownPart()->value;
    if(rect.x!=0||rect.y!=0||rect.width!=result->width||rect.height!=result->height)return {};
    return result;
}
inline std::optional<NativeEncodeControls> ReadNativeEncodeBindings(const BindingPlan& plan)
{
    std::size_t count=0;
    for(const auto& binding:plan.bindings)if(binding.key.View().starts_with("Native.Encode."))
    {
        ++count;
        if(binding.purpose!=Purpose::ColorModelInput||binding.mode.View()!="Scalar"||binding.stage.View()!="Frame"||
            binding.rule.View()!="LegacyNative.Encode.v1"||binding.effectiveSource.View()!="C12.ConsumerTransform"||binding.representation)return {};
    }
    if(count!=7)return {};
    return DecodeNativeEncode([&](std::string_view key)->const C::OptionalFact<C::ScalarValue>*{
        const C::OptionalFact<C::ScalarValue>* found=nullptr;
        for(const auto& binding:plan.bindings)if(binding.key.View()==key){if(found)return nullptr;found=&binding.effective;}return found;});
}
template<class Reader>bool AddNativeEncodeBindings(const RepresentationUse& use,const Reader& reader,BindingPlan& plan)
{
    if(!NativeEncodeForUse(use,reader))return false;
    const Context::TransformLineage* deferred=nullptr;
    if(!Context::ResolveList(use.consumerDeferred,reader,deferred)||!deferred)return false;
    for(const auto& step:*deferred)if(step.operation.View()=="LegacyNative.Encode.v1")
    {
        const C::BoundedList<C::SemanticClaim,8>* parameters=nullptr;
        if(!Context::ResolveList(step.parameters,reader,parameters)||!parameters)return false;
        for(const auto& parameter:*parameters)
        {
            Binding binding;binding.purpose=Purpose::ColorModelInput;binding.key=parameter.field;binding.mode=Symbol("Scalar");
            binding.stage=Symbol("Frame");binding.raw=parameter.raw;binding.effective=parameter.effective;
            binding.effectiveSource=Symbol("C12.ConsumerTransform");binding.rule=step.operation;
            if(!AddBinding(plan,binding))return false;
        }
    }
    return true;
}
}
