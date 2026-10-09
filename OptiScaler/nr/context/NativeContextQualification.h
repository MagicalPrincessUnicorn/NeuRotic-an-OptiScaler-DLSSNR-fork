#pragma once
#include "CapabilityQualification.h"
#include "RepresentationCacheKeys.h"
#include <inputs/universal_feeder/providers/NativeObservationOwner.h>

namespace Neurotic::Context
{
struct NativeContextResult
{
    std::optional<C::CanonicalFrameContext> context;
    std::optional<C::QualificationCertificate> certificate;
    C::Symbol reason;
};
inline constexpr std::array<std::string_view,8> NativeControlNames={"MV.Scale.X","MV.Scale.Y","Jitter.Offset.X",
    "Jitter.Offset.Y","DLSS.Pre.Exposure","Reset","jitter.units","jitter.basis"};
inline bool NativeControl(std::string_view name)
{return std::find(NativeControlNames.begin(),NativeControlNames.end(),name)!=NativeControlNames.end();}
inline bool NativeResourceRole(std::string_view name)
{return name=="Color"||name=="Output"||name=="Depth"||name=="MotionVectors"||name=="ExposureTexture"||name=="DLSS.Input.Bias.Current.Color.Mask";}
inline bool UnpublishedNativeControl(const C::OptionalFact<C::ScalarValue>& value)
{return value.UnknownPart()&&*value.UnknownPart()==C::UnknownFact{};}

// CTX.NativeNgx.v1: exact NGX raw-only scalar interpretation for the current
// authenticated callback. No D3D query, default substitution or C01 rewriting.
// Original owner observations remain intact; NFC publishes its own evidence
// revisions and C02/C04. C03 still independently establishes all live rights.
template<class Store>NativeContextResult BuildNativeContext(Feed::NativeObservationOwner& owner,
    const ContextRequest& request,const SemanticProfile& profile,const C::RecordHeader& certificateHeader,Store& store)
{
    NativeContextResult result;
    const auto refuse=[&](std::string_view reason){result.context.reset();result.certificate.reset();result.reason.Assign(reason);return result;};
    try
    {
        const auto observations=owner.Observations();const auto& source=owner.Source();
        const auto* frame=ResolveMetadata(request.frame,store);
        if(!owner.Healthy()||!observations||!frame||!SameObservationScope(*frame,*frame,store)||request.frame!=source.seed.frame||
           request.generations!=source.generations||request.header.scope!=source.enrolledScope.value_or(C::ScopeRef{source.callback})||
           !SameObservationBoundary(request.boundary,source.seed.boundary,*frame,store)||!ValidProfile(profile))return refuse("CTX.Native.ScopeUnproven");
        const auto* sample=frame->nativeSample?ResolveNativeSample(*frame,store):nullptr;
        if(frame->nativeSample.has_value()!=(profile.nativeSampleDomainVersion==1)||
            (frame->nativeSample&&(!sample||sample->callback!=source.callback)))return refuse("CTX.Native.SampleUnproven");
        // ObserveNgxEvaluation v1 emits each of these even when Get failed. This
        // distinguishes a missing value from a callback aborted halfway through.
        constexpr std::array required={"DLSS.Feature.Create.Flags","Width","Height","OutWidth","OutHeight","PerfQualityValue",
            "Reset","DLSS.Render.Subrect.Dimensions.Width","DLSS.Render.Subrect.Dimensions.Height",
            "DLSS.Input.Color.Subrect.Base.X","DLSS.Input.Color.Subrect.Base.Y","DLSS.Input.Depth.Subrect.Base.X",
            "DLSS.Input.Depth.Subrect.Base.Y","DLSS.Input.MV.Subrect.Base.X","DLSS.Input.MV.Subrect.Base.Y",
            "Jitter.Offset.X","Jitter.Offset.Y","MV.Scale.X","MV.Scale.Y","DLSS.Pre.Exposure","DLSS.Exposure.Scale",
            "Color","Output","Depth","MotionVectors","ExposureTexture","DLSS.Input.Bias.Current.Color.Mask","jitter"};
        std::array<bool,required.size()> seen{};
        auto selected=std::make_unique<C::BoundedList<C::AcquisitionCandidate,64>>();
        C::BoundedList<SourceQualification,64> qualified;
        C::BoundedList<C::Symbol,16> projection;
        for(const auto name:NativeControlNames){C::Symbol key;key.Assign(name);projection.Push(key);}
        for(const auto& candidate:owner.Candidates())
        {
            const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
            if(!ResolveList(candidate.claims,store,claims)||!claims||claims->Size()==0)return refuse("CTX.Native.MalformedObservation");
            const auto role=claims->Get(0)->field.View();
            for(std::size_t i=0;i<required.size();++i)if(role==required[i])seen[i]=true;
            if(NativeNgxAdapterProfile(profile)&&!NativeNgxAdapterClaims(candidate,store))return refuse("CTX.Native.AdapterSchemaUnproven");
            if(!NativeControl(role)&&!NativeResourceRole(role)&&role!="jitter")continue;
            const auto declared=[&](std::string_view name,const C::ScalarValue& expected){
                std::size_t count=0;
                for(const auto& claim:*claims)if(claim.field.View()==name)
                {
                    if(!Established(claim.raw)||claim.raw.KnownPart()->value!=expected||
                       !UnpublishedNativeControl(claim.effective)||claim.overrideSource.IsKnown())return false;
                    ++count;
                }
                return count==1;
            };
            C::Symbol api,boundary;api.Assign("D3D12");boundary.Assign("evaluate");
            if(!declared("source.api",C::ScalarValue{api})||!declared("source.boundary",C::ScalarValue{boundary})||
               !declared("adapter.version",C::ScalarValue{std::uint64_t{1}}))return refuse("CTX.Native.AcquisitionBoundaryUnproven");
            const auto* original=ResolveMetadata(candidate.evidence,store);
            const auto* candidateFrame=ResolveMetadata(candidate.frame,store);
            if(!original||!candidateFrame||!SameObservationScope(*frame,*candidateFrame,store)||
               candidate.frame!=source.seed.frame||candidate.provider!=source.seed.provider||
               !SameFact(candidate.providerIncarnation,source.seed.providerIncarnation)||
               candidate.sourceSchema.View()!="NGX"||(!NativeNgxAdapterProfile(profile)&&
               (!Established(candidate.sourceVersion)||candidate.sourceVersion.KnownPart()->value!=C::VersionNumber{1,0}))||
               !SameFact(candidate.descriptiveLifetime.callbackScope,C::OptionalFact<C::RecordKey>::FromKnown(source.callback,source.evidence))||
               !Established(original->provenance)||original->provenance.KnownPart()->value!=C::SourceClass::Native||
               !Established(original->semanticCertainty)||original->semanticCertainty.KnownPart()->value!=C::SemanticCertainty::Claimed||
               original->aliasOf.IsKnown())return refuse("CTX.Native.UnprovenSource");
            if((NativeResourceRole(role)||role=="jitter")&&candidate.payload.IsKnown())
            {
                const auto* ref=std::get_if<C::MetadataRef<C::ResourceView>>(&candidate.payload.KnownPart()->value);
                const auto* view=ref?ResolveMetadata(*ref,store):nullptr;
                if(!view||ref->owner!=C::OwnerDomain::Resource||!CompleteResourceStructure(*view)||!ValidRaster(view->raster))
                    return refuse("CTX.Native.ResourceCoverageUnproven");
            }
            for(const auto& claim:*claims)if(NativeControl(claim.field.View()))
            {
                if((!claim.raw.IsKnown()&&!UnpublishedNativeControl(claim.raw))||
                   (!claim.effective.IsKnown()&&!UnpublishedNativeControl(claim.effective))||claim.overrideSource.IsKnown())
                    return refuse("CTX.Native.ControlUnavailable");
            }
            auto evidence=*original;const C::EvidenceRef interpretation{request.header.record};
            if(sample)
            {
                if(original->ageInRealFrames.IsKnown())return refuse("CTX.Native.MixedFreshnessDomain");
                evidence.nativeFreshness=NativeFreshness(*frame->nativeSample,*sample);
            }
            else evidence.ageInRealFrames=C::OptionalFact<std::uint64_t>::FromKnown(0,interpretation);
            evidence.coverage=C::OptionalFact<C::CoverageKind>::FromKnown(C::CoverageKind::Complete,interpretation);
            evidence.semanticCertainty=C::OptionalFact<C::SemanticCertainty>::FromKnown(C::SemanticCertainty::Qualified,interpretation);
            C::Symbol type;type.Assign(C::AcquisitionCandidate::WireName);
            if(!selected->Push(candidate)||!qualified.Push({{type,candidate.header.record,candidate.header.revision},store.Publish(evidence)}))
                return refuse("CTX.Native.CapacityExceeded");
        }
        if(std::find(seen.begin(),seen.end(),false)!=seen.end())return refuse("CTX.Native.IncompleteCallback");
        const std::span<const SourceQualification> qualifications{qualified.begin(),qualified.Size()};
        const std::span<const C::AcquisitionCandidate> sources{selected->begin(),selected->Size()};
        auto built=BuildContextForPurpose(*observations,sources,request,profile,store,&projection,qualifications);
        if(!built.context)return refuse("CTX.Native.ContextRefused");
        auto context=built.context->Value();
        const C::BoundedList<C::SemanticClaim,16>* fields=nullptr;
        if(!ResolveList(context.fields,store,fields)||!fields)return refuse("CTX.Native.ControlsMissing");
        auto effective=*fields;
        for(auto& claim:effective)
        {
            if((!claim.raw.IsKnown()&&!UnpublishedNativeControl(claim.raw))||
               (!claim.effective.IsKnown()&&!UnpublishedNativeControl(claim.effective)))return refuse("CTX.Native.ControlContradiction");
            if(claim.raw.IsKnown()&&claim.effective.IsKnown()&&!SemanticEqual(claim.raw,claim.effective))return refuse("CTX.Native.ControlOverrideUnproven");
            if(UnpublishedNativeControl(claim.effective)&&Established(claim.raw))
                claim.effective=C::OptionalFact<C::ScalarValue>::FromKnown(claim.raw.KnownPart()->value,C::EvidenceRef{request.header.record});
        }
        if(!PublishList(effective,store,context.fields))return refuse("CTX.Native.PublicationRefused");
        auto capability=EvaluateSemanticCapability(context,sources,profile,certificateHeader,store,qualifications);
        if(!capability.certificate)return refuse("CTX.Native.CapabilityRefused");
        result.context=std::move(context);result.certificate=capability.certificate->Value();return result;
    }
    catch(...){return refuse("CTX.Native.OwnerFailure");}
}
}
