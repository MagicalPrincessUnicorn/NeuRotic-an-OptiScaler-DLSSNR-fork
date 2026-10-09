#pragma once
#include "OwnerPublicationJournal.h"
#include <dlssnr/NativeNgxCallCapture.h>
#include <nr/context/RepresentationPlanner.h>
#include <nr/protocol/NativeEncodeControls.h>

namespace Neurotic::Lifecycle
{
struct NativeRepresentationBundle
{
    C::MetadataRef<C::CanonicalFrameContext> context;
    C::MetadataRef<C::QualificationCertificate> semantic;
    C::BoundedList<Protocol::RepresentationUse,8> representations;
};
struct NativeRepresentationPreparation
{
    std::optional<NativeRepresentationBundle> bundle;
    C::Symbol reason;
};
// The caller holds the genuine Context publication lock. All D3D queries were
// captured at ingress outside that lock. This describes four consumer bindings;
// it allocates no GPU target, issues no lease and asserts no current contents.
template<class Store>NativeRepresentationPreparation PrepareCapturedNativeRepresentations(
    const C::CanonicalFrameContext& context,const C::QualificationCertificate& semantic,
    const Context::SemanticProfile& policy,std::span<const C::AcquisitionCandidate> candidates,
    const DlssNr::NativeNgxCallCapture& call,C::Placement placement,const Protocol::NativeEncodeControls& encode,
    OwnerPublicationJournal& journal,Store& store)
{
    namespace S=Context;namespace P=Protocol;
    NativeRepresentationPreparation result;
    const auto refuse=[&](std::string_view reason){result.reason=P::Symbol(reason);return result;};
    if(placement!=C::Placement::NativeBefore&&placement!=C::Placement::NativeAfter)
        return refuse("CTX.Native.RepresentationPlacement");
    if(!S::CertificateMatches(semantic,context,policy,store)||semantic.eligibility!=C::Eligibility::Eligible)
        return refuse("CTX.Native.RepresentationUnqualified");
    const auto* qualifiedColor=S::ResolveOptional(context.color,store);
    if(!qualifiedColor||!P::NativeEncodeMatchesColor(encode,*qualifiedColor))
        return refuse("CTX.Native.EncodeColorMismatch");
    const auto* frame=S::ResolveMetadata(context.frame,store);
    if(!frame||!frame->nativeSample||!S::ResolveNativeSample(*frame,store))return refuse("CTX.Native.RepresentationSample");
    const auto evidence=journal.Event().evidence;
    const auto known=[&]<class T>(const T& value){return C::OptionalFact<T>::FromKnown(value,evidence);};
    const std::array<C::ScalarValue,7> values={std::uint64_t{encode.passthrough},double(encode.whitePoint),
        std::uint64_t{encode.useGameExposure},double(encode.exposurePreMul),std::uint64_t{encode.reversibleMode},
        std::uint64_t{encode.width},std::uint64_t{encode.height}};
    C::BoundedList<C::SemanticClaim,8> parameters;
    for(std::size_t i=0;i<values.size();++i)
    {C::SemanticClaim claim;claim.field=P::Symbol(P::NativeEncodeKeys[i]);claim.effective=known(values[i]);parameters.Push(claim);}
    const auto decoded=P::DecodeNativeEncode([&](std::string_view key)->const C::OptionalFact<C::ScalarValue>*{
        for(const auto& parameter:parameters)if(parameter.field.View()==key)return &parameter.effective;return nullptr;});
    if(!decoded||*decoded!=encode)return refuse("CTX.Native.EncodeConstants");
    auto bundle=std::make_unique<NativeRepresentationBundle>();
    bundle->context=store.Publish(context,C::OwnerDomain::Context);bundle->semantic=store.Publish(semantic,C::OwnerDomain::Context);
    const std::array roles={placement==C::Placement::NativeAfter?"Output":"Color",
        placement==C::Placement::NativeAfter?"Output":"Color","Depth","MotionVectors"};
    for(std::size_t i=0;i<roles.size();++i)
    {
        const C::AcquisitionCandidate* selected=nullptr;
        for(const auto& candidate:candidates)
        {
            const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
            if(!S::ResolveList(candidate.claims,store,claims)||!claims||claims->Size()==0)
                return refuse("CTX.Native.RepresentationObservation");
            if(claims->Get(0)->field.View()==roles[i])
            {if(selected)return refuse("CTX.Native.RepresentationAmbiguousRole");selected=&candidate;}
        }
        const auto* reference=selected&&S::Established(selected->payload)?
            std::get_if<C::MetadataRef<C::ResourceView>>(&selected->payload.KnownPart()->value):nullptr;
        const auto* source=reference?S::ResolveMetadata(*reference,store):nullptr;
        const auto* descriptor=call.Description(roles[i]);const auto* support=call.FormatSupport(roles[i]);
        if(!source||reference->owner!=C::OwnerDomain::Resource||!S::CompleteResourceStructure(*source)||!descriptor||!support||
           descriptor->Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||descriptor->DepthOrArraySize!=1||descriptor->MipLevels!=1||
           descriptor->SampleDesc.Count!=1||descriptor->Format!=support->Format||
           source->descriptor.format.KnownPart()->value!=P::Symbol("DXGI.Format."+std::to_string(static_cast<unsigned>(descriptor->Format)))||
           source->descriptor.allocation.KnownPart()->value!=C::Extent{static_cast<std::uint32_t>(descriptor->Width),descriptor->Height}||
           !(support->Support1&D3D12_FORMAT_SUPPORT1_TEXTURE2D))return refuse("CTX.Native.RepresentationResource");
        // The initial color shader binds a typed float4 SRV and typed UAVs of
        // this exact format. No typeless reinterpretation or precision guess.
        if(i<2&&descriptor->Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&descriptor->Format!=DXGI_FORMAT_R32G32B32A32_FLOAT&&
           descriptor->Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&descriptor->Format!=DXGI_FORMAT_R10G10B10A2_UNORM)
            return refuse("CTX.Native.RepresentationFormat");
        C::BoundedList<C::ResourceCapability,4> capabilities;
        if(!(descriptor->Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)&&
           (support->Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD)&&(support->Support1&D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE))
            capabilities.Push(C::ResourceCapability::Sampled);
        const bool typedStorage=(support->Support1&D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW)&&
            (support->Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
        if((descriptor->Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)&&typedStorage)capabilities.Push(C::ResourceCapability::Storage);
        if(i<2)
        {
            const auto size=source->descriptor.allocation.KnownPart()->value;
            if(source->raster.active.KnownPart()->value!=C::Rectangle{0,0,size.width,size.height}||
               encode.width!=size.width||encode.height!=size.height||!typedStorage||
               (placement==C::Placement::NativeBefore&&(size.width%8||size.height%8)))
                return refuse("CTX.Native.RepresentationTargetUnsupported");
        }
        auto requirement=std::make_unique<S::RepresentationRequirement>();auto& req=*requirement;
        const auto purpose=static_cast<P::Purpose>(i);
        req.requested=req.required=true;req.kind=i<2?C::SemanticKind::Color:i==2?C::SemanticKind::Depth:C::SemanticKind::Motion;
        req.field=P::Symbol(i<2?"color.domain":i==2?"depth.device":"motion.native");
        req.consumerPurpose=P::Symbol(C::EnumName(purpose));
        req.selected={P::Symbol(C::AcquisitionCandidate::WireName),selected->header.record,selected->header.revision};
        req.mapping.source=source->raster;req.mapping.target=source->raster;req.mapping.scale=known(C::Vec2{1,1});req.mapping.offset=known(C::Vec2{0,0});
        req.mapping.mappingEvidence=known(C::RecordReference{C::ContractId::C02,P::Symbol(C::CanonicalFrameContext::WireName),context.header.record,context.header.revision});
        req.output=source->descriptor;req.precision=source->descriptor.format.KnownPart()->value;
        req.capabilities.Push(C::ResourceCapability::Sampled);req.allocationClass=P::Symbol("Native.ModelScratch.v1");
        if(i==0)
        {
            const auto meaning=S::SourceMeaning(context,*selected,store);
            if(!meaning)return refuse("CTX.Native.ColorMeaning");
            C::TransformStep step;step.kind=C::TransformKind::ColorTransfer;step.operation=P::Symbol("LegacyNative.Encode.v1");step.version=1;
            step.sourceUnits=known(P::Symbol("QualifiedColor"));step.targetUnits=known(P::Symbol("LegacyNative.ModelEncoded.v1"));
            step.qualification=known(*meaning);step.alreadyApplied=known(false);
            if(!S::PublishList(parameters,store,step.parameters))return refuse("CTX.Native.EncodePublication");
            req.transforms.Push(step);S::FusionRule fusion;fusion.operation=step.operation;fusion.version=1;fusion.steps=req.transforms;
            fusion.mapping=req.mapping;fusion.output=req.output;fusion.precision=req.precision;fusion.capabilities=req.capabilities;req.consumerTransform=fusion;
        }
        else if(i==1)
        {
            req.intent=S::RepresentationIntent::AllocateTarget;req.capabilities.Push(C::ResourceCapability::Storage);
            C::AccessRequirements target;target.device=source->device;target.queue=selected->descriptiveLifetime.queue;
            target.callbackScope=selected->descriptiveLifetime.callbackScope;target.requiresExactContent=known(false);
            target.uses.Push(C::UsageKind::WriteExclusive);target.retirementContract=known(P::Symbol("Native.ModelScratch.v1"));req.targetAccess=target;
        }
        S::SourceRepresentationFacts facts;facts.source=source->identity;facts.precision=known(req.precision);facts.capabilities=known(capabilities);
        const auto decision=S::DeriveStructuralRepresentationPlan(context,semantic,policy,req,facts,store);
        const auto wanted=i==0?S::PreparationPath::ConsumerTransform:i==1?S::PreparationPath::TargetAllocation:S::PreparationPath::DirectUse;
        if(decision.path!=S::PreparationPath::StructuralOnly||decision.plannedPath!=wanted||decision.prepared||
           decision.evidence.conversions||decision.evidence.preparedLookups)return refuse("CTX.Native.RepresentationPlanUnavailable");
        const auto published=S::PublishRepresentationPlan(decision,journal.Header(C::ContractId::C12,context.header.scope),evidence,store);
        if(!published)return refuse("CTX.Native.RepresentationPublication");
        P::RepresentationUse use;const auto& plan=published->Value();
        use.source={C::OwnerDomain::Context,P::Symbol("C12.ConsumerUse"),{1,0},journal.Event().evidence.record,1};
        use.context=P::Reference<C::ContractId::C02>(context);use.purpose=purpose;
        use.kind=P::Symbol(i==0?"ModelProxy":i==1?"ModelTarget":"RawGuideOriginal");use.consumerContract=P::Symbol("LegacyNative.Binding.v1");
        use.plan=store.Publish(plan,C::OwnerDomain::Context);use.currentSource=*reference;use.nativeSample=frame->nativeSample;
        use.sourceCandidate=req.selected;
        use.currentContent=source->identity.contentRevision;
        if(!S::PublishList(req.transforms,store,use.required))return refuse("CTX.Native.RepresentationLineagePublication");
        if(i==0)use.consumerDeferred=use.required;
        if(!P::LineageMatches(use,plan,store)||(i==0&&P::NativeEncodeForUse(use,store)!=encode)||!bundle->representations.Push(use))
            return refuse("CTX.Native.RepresentationBinding");
    }
    result.bundle=std::move(*bundle);return result;
}
}
