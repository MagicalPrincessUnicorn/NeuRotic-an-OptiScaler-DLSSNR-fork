#include "../contracts/Test002Support.h"
#include "RoutingFixture.h"
#include <nr/orchestration/RoutingSelector.h>
#include <nr/config/IntentSemantics.h>
#if __has_include(<nr/orchestration/RoutingPublication.h>)
#include <nr/orchestration/RoutingPublication.h>
#include <nr/orchestration/LegacyShadowComparison.h>
#define NR_ORCH_PUBLICATION 1
#endif
using namespace NrTest002;using namespace NrRoutingTests;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("routing-publication",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_ORCH_PUBLICATION
    Fixture f;auto decision=O::Select(f.input,f.store);
    auto header=NrContextTests::Header(C::ContractId::C05,C::OwnerDomain::OrchestratorPolicy,900);header.scope=f.input.scope;
    const auto c05=O::PublishDecision(f.input,decision,header,7,f.store);
    r.Check("T22.c05-pure-proposal",c05&&c05->strategy==Sym("Native")&&c05->planningEpoch==7&&c05->signature==f.a.anchor.signature,true,"PublishDecision");
    r.Check("T22.c05-config-owner",c05&&c05->userIntent==f.input.intent.source,true,"PublishDecision");
    auto fake=decision;fake.proposed=f.b.anchor;
    r.Check("T22.forged-decision-refused",!O::PublishDecision(f.input,fake,header,7,f.store),true,"PublishDecision");
    header.owner=C::OwnerDomain::StreamCoordinator;
    r.Check("T22.no-commit-authority",!O::PublishDecision(f.input,decision,header,7,f.store),true,"PublishDecision");
    header=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::OrchestratorPolicy,901);header.scope=f.input.scope;
    const auto receipt=O::PublishReceipt(f.input,decision,header,f.generations,f.store);
    r.Check("T22.c11-owner-receipt",receipt&&receipt->operation==Sym("ORCH.ShadowDecision"),true,"PublishReceipt");
    f.input.legacyEffective=f.b.anchor;const auto delta=O::CompareLegacy(f.input,decision,f.store);
    r.Check("T22.legacy-delta-values-only",delta==Sym("DifferentPlan")&&O::Select(f.input,f.store)==decision,true,"CompareLegacy");
    namespace G=Neurotic::ConfigIntent;
    auto snapshot=G::Capture(G::ParseLossless("[DlssNr]\nRoute=auto\n"),G::CurrentSchema,"fixture.ini",1);
    auto source=f.input.intent.source;source.recordType=Sym("NR.CFG.IntentSnapshot");
    const auto legacy=O::ProjectLegacy(snapshot,source,Sym("FixedLegacy"));
    r.Check("T24.parser-auto-stays-fixed-default",legacy&&legacy->mode==Sym("FixedLegacy")&&legacy->fixedStrategy==Sym("UnifiedPresent")&&
        legacy->require.Size()==1&&*legacy->require.Get(0)==Sym("PresentEnhanced"),true,"ProjectLegacy");
    snapshot=G::Capture(G::ParseLossless("[DlssNr]\nRoute=0\nRenderingMode=0\n"),G::CurrentSchema,"fixture.ini",1);
    auto native=O::ProjectLegacy(snapshot,source,Sym("FixedLegacy"));
    r.Check("T24.fixed-native-placement",native&&native->fixedStrategy==Sym("NativeTemporal")&&native->fixedPlacement==C::Placement::NativeAfter,true,"ProjectLegacy");
    const auto shadow=O::ProjectLegacy(snapshot,source,Sym("Shadow"));
    r.Check("T24.shadow-preserves-fixed-intent",shadow&&shadow->mode==Sym("FixedLegacy"),true,"ProjectLegacy");
    r.Check("T24.projection-no-auto",!O::ProjectLegacy(snapshot,source,Sym("Auto")),true,"ProjectLegacy");
    source.revision=2;r.Check("T24.projection-exact-revision",!O::ProjectLegacy(snapshot,source,Sym("Shadow")),true,"ProjectLegacy");
    source.revision=1;snapshot.status=G::AnalysisStatus::UnsupportedFutureSchema;
    r.Check("T24.projection-future-refusal",!O::ProjectLegacy(snapshot,source,Sym("Shadow")),true,"ProjectLegacy");
#else
    r.Check("T22.c05-pure-proposal",false,true,"PublishDecision");
#endif
    return r.Finish();});
}
