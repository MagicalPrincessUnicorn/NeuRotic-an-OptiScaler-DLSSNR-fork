#include "../contracts/Test002Support.h"
#include "../oracles/Test002AnalyticCases.h"
#include <nr/context/CapabilityQualification.h>
#include "ContextFixtureStore.h"
#include <memory>
using namespace NrTest002;
namespace S=Neurotic::Context;
struct Fixture
{
    NrContextTests::Store store;
    S::ContextRequest request;
    C::ObservationSet observations;
    std::array<C::AcquisitionCandidate,1> sources;
    Fixture()
    {
        using namespace NrContextTests;
        request.header=NrContextTests::Header(C::ContractId::C02,C::OwnerDomain::Context,2);
        request.frame=store.Publish(Frame(),C::OwnerDomain::IdentityRegistry);
        request.generations=store.Publish(C::GenerationVector{},C::OwnerDomain::IdentityRegistry);
        request.renderRaster=store.Publish(Analytic::Raster({0,0,800,400},{1024,512}));request.outputRaster=request.renderRaster;
        request.boundary.kind=Known(C::BoundaryKind::BeforeUpscale);request.boundary.episode=Frame().episodeId;
        C::EvidenceVector evidence;evidence.provenance=Known(C::SourceClass::Native);
        evidence.semanticCertainty=Known(C::SemanticCertainty::Qualified);evidence.ageInRealFrames=Known(uint64_t{0});
        evidence.coverage=Known(C::CoverageKind::Complete);evidence.access=Known(C::DescriptiveAccess::CallbackBorrow);
        request.continuity=store.Publish(evidence,C::OwnerDomain::ColorContinuity);
        auto& source=sources[0];source.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Provider,1);
        source.provider=Key("provider",1);source.providerIncarnation=Known(Id<C::ProviderIncarnation>("provider",1));
        source.sourceSchema=Sym("ctx.fixture.source");source.sourceVersion=Known(C::VersionNumber{1,0});
        source.frame=request.frame;source.boundary=request.boundary;source.semantic=C::SemanticKind::Motion;
        source.evidence=store.Publish(evidence,C::OwnerDomain::Provider);source.masks=store.Publish(C::MaskSet{},C::OwnerDomain::Provider);
        source.motion=Known(store.Publish(Analytic::MotionInput().motion));
        observations.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Acquisition,3);observations.boundary=request.boundary;
        C::BoundedList<C::ContractRef<C::ContractId::C01>,64> refs;refs.Push({Sym(C::AcquisitionCandidate::WireName),source.header.record,1});
        observations.candidates=NrContextTests::PublishList(store,refs);
    }
};
int main(int argc,char** argv)
{
    return Guard([&]{
        Report r("ctx-stack",argc==2?std::string_view(argv[1]):std::string_view{});
        // Fixture storage is explicit test-owned heap memory; the production call
        // executes on the linker's ordinary 1 MiB stack without a larger reserve.
        const auto fixture=std::make_unique<Fixture>();
        const auto result=S::BuildCanonicalFrameContext(fixture->observations,fixture->sources,fixture->request,fixture->store);
        r.Check("T06.default-stack-context",result.context && result.context->Value().motion.IsKnown(),true,"BuildCanonicalFrameContext");
        return r.Finish();
    });
}
