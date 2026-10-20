#include "../context/ContextFixtureStore.h"
#include "../oracles/Test002AnalyticCases.h"
#if __has_include(<nr/context/RepresentationCacheKeys.h>)
#include <nr/context/RepresentationCacheKeys.h>
#define HAS_KEYS 1
#endif
using namespace NrTest002;
int main(int argc,char** argv)
{
    return Guard([&]{Report r("representation-cache",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef HAS_KEYS
        namespace S=Neurotic::Context;
        NrContextTests::Store store;
        auto source=NrContextTests::Resource(1,Analytic::MotionInput().motion.currentRaster);
        source.descriptor.allocation=Known(C::Extent{1024,512});source.descriptor.format=Known(Sym("RGBA16F"));
        source.descriptor.sampleCount=Known(std::uint32_t{1});source.descriptor.mipLevels=Known(std::uint32_t{1});
        source.descriptor.arrayLayers=Known(std::uint32_t{1});source.descriptor.api=Known(C::GraphicsApi::D3D12);
        source.mip=Known(std::uint32_t{0});source.arrayLayer=Known(std::uint32_t{0});source.plane=Known(std::uint32_t{0});
        source.device=Known(Id<C::ObjectIncarnation>("device",1));source.adapter=Known(Sym("adapter"));
        auto target=source;target.identity=NrContextTests::Resource(2,source.raster).identity;
        C::RecordReference meaning{C::ContractId::C02,Sym("ColorDescription"),Key("meaning"),1};
        C::BoundaryDescription boundary;
        auto frame=NrContextTests::Frame();
        auto a=S::BuildSemanticKey(meaning,C::SemanticKind::Color,boundary,frame,source,{},store);
        C::RasterMapping mapping;mapping.source=source.raster;mapping.target=target.raster;
        mapping.scale=Known(C::Vec2{1,1});mapping.offset=Known(C::Vec2{0,0});mapping.mappingEvidence=Known(meaning);
        C::ProfileKey profile{Sym("NR"),Sym("test"),1};C::BoundedList<C::ResourceCapability,4> caps;
        auto b=a?S::BuildRepresentationPlanKey(*a,source,profile,Sym("color"),mapping,target.descriptor,Sym("fp16"),caps,{},store):std::nullopt;
        auto c=S::BuildAllocationRequirementKey(frame,source.device,target.descriptor,caps,Sym("scratch"));
        auto d=b?S::BuildPreparedContentKey(*b,source,target,frame,{},false,store):std::nullopt;
        r.Check("T13.complete-keys",a&&b&&c&&d,true,"Build key families");
        if(!a||!b||!c||!d)return r.Finish();
        auto keys=[&](const C::ResourceView& changed){
            auto sa=S::BuildSemanticKey(meaning,C::SemanticKind::Color,boundary,frame,changed,{},store);
            return sa?S::BuildRepresentationPlanKey(*sa,changed,profile,Sym("color"),mapping,target.descriptor,Sym("fp16"),caps,{},store):std::nullopt;
        };
        auto changed=source;changed.identity.contentRevision=Known(C::ContentRevision{2});
        r.Check("T13.content-semantic-stable",S::BuildSemanticKey(meaning,C::SemanticKind::Color,boundary,frame,changed,{},store)==a,true,"BuildSemanticKey");
        r.Check("T13.content-plan-stable",keys(changed)==b,true,"BuildRepresentationPlanKey");
        r.Check("T13.content-reject",S::BuildPreparedContentKey(*b,changed,target,frame,{},false,store)!=d,true,"BuildPreparedContentKey");
        r.Check("T13.content-allocation-stable",S::BuildAllocationRequirementKey(frame,source.device,target.descriptor,caps,Sym("scratch"))==c,true,"BuildAllocationRequirementKey");
        changed=source;changed.identity.objectIncarnation=NrContextTests::Resource(3,source.raster).identity.objectIncarnation;
        r.Check("T13.object-plan-stable",keys(changed)==b,true,"BuildRepresentationPlanKey");
        r.Check("T13.object-reject",S::BuildPreparedContentKey(*b,changed,target,frame,{},false,store)!=d,true,"BuildPreparedContentKey");
        changed=source;changed.identity.resourceIncarnation=NrContextTests::Resource(3,source.raster).identity.resourceIncarnation;
        r.Check("T13.resource-incarnation",S::BuildPreparedContentKey(*b,changed,target,frame,{},false,store)!=d,true,"BuildPreparedContentKey");
        changed=source;changed.identity.resourceViewIncarnation=NrContextTests::Resource(3,source.raster).identity.resourceViewIncarnation;
        r.Check("T13.view-incarnation",S::BuildPreparedContentKey(*b,changed,target,frame,{},false,store)!=d,true,"BuildPreparedContentKey");
        changed=source;changed.identity.representationGeneration=Known(Gen(C::IdentityKind::RepresentationGeneration,"representation",2));
        r.Check("T13.representation-plan",keys(changed)!=b,true,"BuildRepresentationPlanKey");
        r.Check("T13.representation-content",S::BuildPreparedContentKey(*b,changed,target,frame,{},false,store)!=d,true,"BuildPreparedContentKey");
        changed=source;changed.identity.resourceGeneration=Known(Gen(C::IdentityKind::ResourceGeneration,"resource",2));
        r.Check("T13.resource-generation-plan",keys(changed)!=b,true,"BuildRepresentationPlanKey");
        r.Check("T13.resource-generation-content",S::BuildPreparedContentKey(*b,changed,target,frame,{},false,store)!=d,true,"BuildPreparedContentKey");
        changed=source;changed.identity.contentRevision={};
        r.Check("T13.unknown-content",S::BuildPreparedContentKey(*b,changed,target,frame,{},false,store).has_value(),false,"BuildPreparedContentKey");
        changed=source;changed.identity.objectIncarnation={};
        r.Check("T13.unknown-incarnation",S::BuildPreparedContentKey(*b,changed,target,frame,{},false,store).has_value(),false,"BuildPreparedContentKey");
        changed=target;changed.identity=NrContextTests::Resource(4,source.raster).identity;
        r.Check("T13.target-recycled",S::BuildPreparedContentKey(*b,source,changed,frame,{},false,store)!=d,true,"BuildPreparedContentKey");
        auto frame2=frame;frame2.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",11));
        auto temporal=S::BuildPreparedContentKey(*b,source,target,frame,{},true,store);
        r.Check("T13.required-pair-missing",temporal.has_value(),false,"BuildPreparedContentKey");
        auto prior=frame;prior.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",9));
        auto td=S::BuildPreparedContentKey(*b,source,target,frame,prior,true,store);
        r.Check("T13.frame-pair",td&&S::BuildPreparedContentKey(*b,source,target,frame2,prior,true,store)!=td,true,"BuildPreparedContentKey");
        auto desc=target.descriptor;desc.sampleCount=Known(std::uint32_t{2});
        r.Check("T13.allocation-samples",S::BuildAllocationRequirementKey(frame,source.device,desc,caps,Sym("scratch"))!=c,true,"BuildAllocationRequirementKey");
        auto device=Known(Id<C::ObjectIncarnation>("device",2));
        r.Check("T13.allocation-device",S::BuildAllocationRequirementKey(frame,device,target.descriptor,caps,Sym("scratch"))!=c,true,"BuildAllocationRequirementKey");
        desc=target.descriptor;desc.format={};
        r.Check("T13.unknown-format",S::BuildAllocationRequirementKey(frame,source.device,desc,caps,Sym("scratch")).has_value(),false,"BuildAllocationRequirementKey");
        changed=source;changed.descriptor.allocation=Known(C::Extent{800,400});
        r.Check("T13.raster-allocation-consistency",S::CompleteContent(changed),false,"CompleteContent");
        auto otherScope=frame;otherScope.sessionId=Known(Id<C::SessionId>("session",2));
        r.Check("T13.session-structural",S::BuildSemanticKey(meaning,C::SemanticKind::Color,boundary,otherScope,source,{},store)!=a,true,"BuildSemanticKey");
        otherScope=frame;otherScope.viewId=Known(Id<C::ViewId>("view",2));
        r.Check("T13.view-structural",S::BuildSemanticKey(meaning,C::SemanticKind::Color,boundary,otherScope,source,{},store)!=a,true,"BuildSemanticKey");
        otherScope=frame;otherScope.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",11));
        r.Check("T13.frame-not-structural",S::BuildSemanticKey(meaning,C::SemanticKind::Color,boundary,otherScope,source,{},store)==a,true,"BuildSemanticKey");
#else
        r.Check("T13.owner-missing",false,true,"Representation keys unavailable");
#endif
        return r.Finish();});
}
