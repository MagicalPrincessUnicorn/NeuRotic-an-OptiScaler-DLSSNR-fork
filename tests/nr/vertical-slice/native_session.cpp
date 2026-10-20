#include "../context/ContextFixtureStore.h"
#include "../identity/IdentityTestSupport.h"
#if __has_include(<nr/lifecycle/SessionComposition.h>)
#include <nr/lifecycle/SessionComposition.h>
#define NR_SESSION_COMPOSITION 1
#endif
using namespace NrTest002;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-session",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_SESSION_COMPOSITION
    namespace L=Neurotic::Lifecycle;
    const auto event=LifeFixture::Event(C::OwnerDomain::Session,LifeFixture::Key("session"),1);
    const auto owner=LifeFixture::Binding(C::OwnerDomain::Finalizer,"finalizer");
    auto session=std::make_unique<L::SessionComposition>(Sym("native.session.fixture"),event,owner,8);
    r.Check("T27.native-bootstrap-ready",session->Ready()&&session->Finalizer()!=nullptr,true,"SessionComposition");
    const auto identity=LifeFixture::Binding(C::OwnerDomain::IdentityRegistry,"identity");
    auto topology=session->RegisterTopology(identity);auto render=session->RegisterRender(identity);
    r.Check("T27.native-bootstrap-narrow-owners",topology&&render,true,"SessionComposition");
    r.Check("T27.native-bootstrap-no-rebind",!session->RegisterRender(identity),true,"SessionComposition");
    r.Check("T27.native-bootstrap-wrong-role",!session->RegisterResource(identity),true,"SessionComposition");
    session->FinishRegistration();
    r.Check("T27.native-bootstrap-no-hot-registration",!session->RegisterResource(LifeFixture::Binding(C::OwnerDomain::Resource,"resource")),true,"SessionComposition");
    r.Check("T27.native-bootstrap-close-drained",session->CloseDrained()&&!session->Ready(),true,"SessionComposition");
    const auto late=render->NewBase({LifeFixture::Event(identity.owner,identity.subject,2),L::RenderObservation::NewGameContent});
    r.Check("T27.native-bootstrap-facade-stale",late.status==L::IdentityStatus::Closed,true,"SessionComposition");
    auto bad=event;bad.owner=C::OwnerDomain::Diagnostics;
    L::SessionComposition refused(Sym("native.session.refused"),bad,owner,8);
    r.Check("T27.native-bootstrap-wrong-session-owner",!refused.Ready(),true,"SessionComposition");
#else
    r.Check("T27.native-bootstrap-production",false,true,"SessionComposition");
#endif
    return r.Finish();});
}
