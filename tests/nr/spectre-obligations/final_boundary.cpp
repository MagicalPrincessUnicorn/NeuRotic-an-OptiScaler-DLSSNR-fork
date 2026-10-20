#include "../finalizer/finalizer_fixture.h"

using namespace NrTest002;
namespace L=Neurotic::Lifecycle;
namespace
{
std::unique_ptr<FinalizerTests::Fixture> Ready()
{
    auto f=std::make_unique<FinalizerTests::Fixture>();
    return f;
}
void Association(Report& r)
{
    auto canonical=Ready();auto claim=canonical->Claim();
    if(!claim.claim||!canonical->Produce(*claim.claim).accepted)
        throw std::runtime_error("canonical finalizer claim/produce failed");
    r.Check("F06.canonical-association",canonical->Prepare(*claim.claim).accepted,true,
        "exact synthetic presentation scope, candidate and Resource C03 owner facts qualify sole finalizer");
    auto changedFrame=[&](const char* id,auto mutate)
    {
        auto f=Ready();auto c=f->Claim();if(!c.claim||!f->Produce(*c.claim).accepted)
            throw std::runtime_error("association fixture failed");
        auto frame=*f->store.Resolve(f->boundary.frame).value;mutate(frame);
        f->boundary.frame=f->store.Publish(frame,C::OwnerDomain::Presentation);
        r.Check(id,!f->Prepare(*c.claim).accepted,true,"exact selected final scope refuses substituted presentation frame");
    };
    changedFrame("F06.wrong-view",[](auto& frame){frame.viewId=Known(Id<C::ViewId>("view",2));});
    changedFrame("F06.wrong-base",[](auto& frame){frame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",11));});
    changedFrame("F06.wrong-stream",[](auto& frame){frame.renderStreamId=Known(Id<C::RenderStreamId>("stream",2));});
    const auto changedBoundary=[&](const char* id,auto mutate)
    {
        auto f=Ready();auto c=f->Claim();if(!c.claim||!f->Produce(*c.claim).accepted)
            throw std::runtime_error("boundary fixture failed");
        mutate(*f);
        r.Check(id,!f->Prepare(*c.claim).accepted,true,"changed provider or handoff cannot use selected exact runtime lease");
    };
    changedBoundary("F06.wrong-provider",[](auto& f){f.boundary.provider=Id<C::ProviderIncarnation>("provider",2);});
    changedBoundary("F06.wrong-handoff",[](auto& f){f.boundary.handoff=Id<C::HandoffContractGeneration>("handoff",2);});
}
void Boundary(Report& r)
{
    auto absent=Ready();absent->request.expectedConsumer.reset();
    r.Check("F07.required-color-contract-absent",!absent->Claim().accepted,true,"missing FG expected contract refuses before effects");
    auto foreign=Ready();foreign->request.expectedConsumer->owner=C::OwnerDomain::Presentation;
    r.Check("F07.required-color-owner-mismatch",!foreign->Claim().accepted,true,"observation owner cannot issue the FG requirement");
    auto unavailable=Ready();unavailable->store.Withdraw(*unavailable->request.expectedConsumer);
    r.Check("F07.required-color-unresolvable",!unavailable->Claim().accepted,true,"unavailable expected contract is not a default color");
    const auto refused=[&](const char* id,auto mutate)
    {
        auto f=Ready();auto c=f->Claim();if(!c.claim||!f->Produce(*c.claim).accepted)
            throw std::runtime_error("final boundary fixture failed");
        mutate(*f);
        r.Check(id,!f->Prepare(*c.claim).accepted,true,"sole finalizer rejects unqualified boundary fact");
    };
    refused("F07.required-rendering-incomplete",[](auto& f){f.boundary.boundary.afterRequiredHostRendering=Known(false);});
    refused("F07.hud-false",[](auto& f){f.boundary.boundary.hudIncluded=Known(false);});
    refused("F07.hud-unknown",[](auto& f){f.boundary.boundary.hudIncluded={};});
    refused("F07.required-tone-map-false",[](auto& f){f.boundary.requireToneMapped=true;f.boundary.boundary.toneMapped=Known(false);});
    refused("F07.required-tone-map-unknown",[](auto& f){f.boundary.requireToneMapped=true;f.boundary.boundary.toneMapped={};});
    refused("F07.color-unknown",[](auto& f){f.boundary.color.domain={};});
    refused("F07.color-incompatible",[](auto& f){f.boundary.color.domain=Known(C::ColorDomain::EncodedDisplay);});
    refused("F07.lineage-unknown",[](auto& f){f.boundary.lineageIncludesCandidate={};});
    auto positive=Ready();auto c=positive->Claim();
    if(!c.claim||!positive->Produce(*c.claim).accepted)throw std::runtime_error("positive boundary fixture failed");
    positive->boundary.requireToneMapped=true;
    auto prepared=positive->Prepare(*c.claim);
    r.Check("F07.complete-required-boundary",prepared.accepted,true,
        "known matching HUD, tone-map, color, rendering and candidate lineage qualify the boundary");
}
void IndependentScratch(Report& r)
{
    auto f=Ready();auto c=f->Claim();
    if(!c.claim||!f->Produce(*c.claim).accepted||!f->Prepare(*c.claim).accepted)
        throw std::runtime_error("final packet fixture failed");
    auto sealed=f->Commit(*c.claim);
    r.Check("F12.final-packet-retains-provider-hold",sealed.packet.has_value()&&
        f->facts.holds.Size()==1&&f->facts.holds.Get(0)->released==L::Fact::No,true,
        "sole finalizer has one sealed packet with an unresolved exact provider hold");
    auto scratch=f->binding;scratch.description.resource=NrContextTests::Resource(11,{}).identity;
    scratch.description.use=C::UsageKind::OrderedGpuRead;
    scratch.consumer=Key("scratch.consumer");scratch.purpose=Sym("ScratchRead");
    auto facts=f->facts;facts.current=scratch;facts.holds={};
    facts.dependency.completed=L::Fact::Yes;facts.dependency.nonReplayable=L::Fact::Yes;
    facts.dependency.reusable=L::Fact::Yes;facts.consumerTicketsRetired=L::Fact::Yes;
    facts.ownerCanRecycle=L::Fact::Yes;
    auto lease=L::BeginConsumption(scratch,facts);
    if(!lease)throw std::runtime_error("distinct scratch lease fixture rejected");
    L::CloseAdmission(*lease);
    r.Check("F12.terminal-distinct-scratch",L::CanOwnerReuse(*lease,facts).allowed,true,
        "synthetic Resource owner facts close only the distinct scratch allocation");
    L::CloseAdmission(*f->lease);
    auto finalReady=f->facts;
    finalReady.dependency.completed=L::Fact::Yes;
    finalReady.dependency.nonReplayable=L::Fact::Yes;
    finalReady.dependency.reusable=L::Fact::Yes;
    finalReady.consumerTicketsRetired=L::Fact::Yes;
    finalReady.ownerCanRecycle=L::Fact::Yes;
    r.Check("F12.final-held-after-scratch",!L::CanOwnerReuse(*f->lease,finalReady).allowed&&
        f->finalizer->Snapshot(sealed.packet->seal).has_value(),true,
        "all other final C03 retirement predicates are terminal, yet the exact provider hold remains active");
    auto released=*finalReady.holds.Get(0);
    released.released=L::Fact::Yes;released.releaseEvidence=Key("final.provider.release");
    finalReady.holds={};finalReady.holds.Push(released);
    r.Check("F12.final-release-control",L::CanOwnerReuse(*f->lease,finalReady).allowed,true,
        "only a stated exact Resource-owner provider-release fact opens the final C03 reuse predicate");
}
}
int main(int argc,char** argv)
{
    return Guard([&]{Report r("spectre-final-boundary",argc==2?std::string_view(argv[1]):std::string_view{});
        Association(r);Boundary(r);IndependentScratch(r);return r.Finish();});
}
