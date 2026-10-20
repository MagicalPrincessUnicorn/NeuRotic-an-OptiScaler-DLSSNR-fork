#pragma once
#include "../context/ContextFixtureStore.h"
#include "../oracles/Test002AnalyticCases.h"
#include <nr/context/RepresentationCacheKeys.h>
namespace NrRepresentationTests
{
using namespace NrTest002;
namespace S=Neurotic::Context;
// Synthetic owner admission permutation only. No live C03/GPU admission is claimed.
inline bool SyntheticAdmission(bool semanticMatch,int externalState){return semanticMatch&&externalState==0;}
struct Fixture
{
    NrContextTests::Store store;
    C::ResourceView source,target;
    C::FrameIdentity frame=NrContextTests::Frame();
    C::RecordReference meaning{C::ContractId::C02,Sym("ColorDescription"),Key("meaning"),1};
    C::RasterMapping mapping;
    C::ProfileKey profile{Sym("NR"),Sym("test"),1};
    C::BoundedList<C::ResourceCapability,4> caps;
    C::BoundaryDescription boundary;
    S::TransformLineage transforms;
    Fixture()
    {
        source=NrContextTests::Resource(1,Analytic::MotionInput().motion.currentRaster);
        source.descriptor.allocation=Known(C::Extent{1024,512});source.descriptor.format=Known(Sym("RGBA16F"));
        source.descriptor.sampleCount=Known(std::uint32_t{1});source.descriptor.mipLevels=Known(std::uint32_t{1});
        source.descriptor.arrayLayers=Known(std::uint32_t{1});source.descriptor.api=Known(C::GraphicsApi::D3D12);
        source.mip=Known(std::uint32_t{0});source.arrayLayer=Known(std::uint32_t{0});source.plane=Known(std::uint32_t{0});
        source.device=Known(Id<C::ObjectIncarnation>("device",1));source.adapter=Known(Sym("adapter"));
        target=source;target.identity=NrContextTests::Resource(2,source.raster).identity;
        mapping.source=source.raster;mapping.target=target.raster;mapping.scale=Known(C::Vec2{1,1});
        mapping.offset=Known(C::Vec2{0,0});mapping.mappingEvidence=Known(meaning);
        boundary.kind=Known(C::BoundaryKind::BeforeUpscale);boundary.episode=frame.episodeId;
        caps.Push(C::ResourceCapability::Sampled);
    }
    S::RepresentationPlanKey KeyFor()
    {
        auto a=S::BuildSemanticKey(meaning,C::SemanticKind::Color,boundary,frame,source,{},store);
        if(!a)throw std::runtime_error("semantic fixture incomplete");
        auto b=S::BuildRepresentationPlanKey(*a,source,profile,Sym("color"),mapping,target.descriptor,Sym("fp16"),caps,transforms,store);
        if(!b)throw std::runtime_error("plan fixture incomplete");return *b;
    }
    C::RepresentationPlan Plan()
    {
        C::RepresentationPlan p;p.header=NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Context,50);
        p.source=source.identity;p.profile=profile;p.purpose=Sym("color");p.raster=store.Publish(mapping);
        p.transforms=NrContextTests::PublishList(store,transforms);p.precision=Known(Sym("fp16"));p.requiredCapabilities=caps;
        p.output=target.descriptor;p.keys.semantic=Key("semantic");p.keys.plan=Key("plan");p.keys.content=source.identity;
        const auto& g=source.identity.representationGeneration.KnownPart()->value.identity;
        p.keys.generation=Known(C::RepresentationGeneration{g.nameSpace,g.issuer,g.value});return p;
    }
    C::PreparedView Prepared(const C::RepresentationPlan& p)
    {
        C::PreparedView v;v.header=NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Resource,51);
        v.plan={Sym(C::RepresentationPlan::WireName),p.header.record,p.header.revision};v.view=store.Publish(target,C::OwnerDomain::Resource);
        v.keys=p.keys;auto applied=transforms;for(auto& t:applied)t.alreadyApplied=Known(true);
        v.appliedTransforms=NrContextTests::PublishList(store,applied);return v;
    }
};
}
