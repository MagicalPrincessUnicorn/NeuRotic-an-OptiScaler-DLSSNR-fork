#include "IdentityTestSupport.h"
#include <array>
#include <thread>
using namespace LifeFixture;

template<class T> concept HasSeal = requires(T& v,const L::OwnerEvent& e) { v.FinalSeal(e); };
template<class T> concept HasBinding = requires(T& v,const L::OwnerBinding& b) { v.BindFinalizer(b); };
template<class T> concept PublicIssue = requires(T& v,const L::Detail::ChannelHandle& h,const L::OwnerEvent& e) {
    v.template Issue<C::IdentityKind::FinalSealId>(h,L::Detail::ChannelRole::Handoff,e);
};
static_assert(!HasSeal<L::HandoffIssuer> && !HasSeal<L::FgPolicyIssuer> && !HasSeal<L::HostIdentityDomain>);
static_assert(!PublicIssue<L::HostIdentityDomain>);

// Dependent dispatch makes the pre-implementation test compile and fail normally.
template<class Host> int Exercise()
{
    Checks c;
    c.Is("FS-I01.facade-available",HasBinding<Host>);
    if constexpr (HasBinding<Host>)
    {
        auto h=std::make_unique<Host>(Sym("fixture.final-seal"));
        const auto session=h->OpenSession(Event(C::OwnerDomain::Session,Key("session.scope"),1));
        c.Status("FS-I01.session",session.status,L::IdentityStatus::Ok);
        const auto b=Binding(C::OwnerDomain::Finalizer,"finalizer.primary");
        auto f=h->BindFinalizer(b);
        using Issuer=typename decltype(f)::value_type;
        static_assert(HasSeal<Issuer> && !std::is_default_constructible_v<Issuer>);
        c.Is("FS-I01.bind",f.has_value());
        for(auto owner:{C::OwnerDomain::FrameGeneration,C::OwnerDomain::IdentityRegistry,C::OwnerDomain::Strategy,C::OwnerDomain::Presentation})
            c.Is("FS-I02.other-owner-refused",!h->BindFinalizer(Binding(owner,"forbidden")));
        c.Is("FS-I01.duplicate-bind-refused",!h->BindFinalizer(b));
        if(!f || !HasValue(session)) return c.Finish();
        const auto first=f->FinalSeal(Event(b.owner,b.subject,1));
        c.Status("FS-I03.first",first.status,L::IdentityStatus::Ok);
        if(!HasValue(first)) return c.Finish();
        c.Equal("FS-I03.value",Value(first).value,1);
        c.Is("FS-I03.kind",Value(first).Kind==C::IdentityKind::FinalSealId);
        c.Status("FS-I06.wrong-owner",f->FinalSeal(Event(C::OwnerDomain::FrameGeneration,b.subject,2)).status,L::IdentityStatus::WrongOwner);
        auto foreign=Event(b.owner,b.subject,2);foreign.subject=Key("foreign.subject");
        c.Is("FS-I06.foreign-subject",f->FinalSeal(foreign).status!=L::IdentityStatus::Ok);
        foreign=Event(b.owner,b.subject,2);foreign.publisher=Key("foreign.publisher");
        c.Is("FS-I06.foreign-publisher",f->FinalSeal(foreign).status!=L::IdentityStatus::Ok);
        foreign=Event(b.owner,b.subject,2);foreign.evidence.record.nameSpace=Sym("foreign.evidence");
        c.Is("FS-I06.foreign-evidence",f->FinalSeal(foreign).status!=L::IdentityStatus::Ok);
        c.Status("FS-I05.duplicate",f->FinalSeal(Event(b.owner,b.subject,1)).status,L::IdentityStatus::StaleEvidence);
        const auto second=f->FinalSeal(Event(b.owner,b.subject,2));
        c.Status("FS-I04.second",second.status,L::IdentityStatus::Ok);
        if(!HasValue(second))return c.Finish();
        c.Equal("FS-I04.no-rejected-allocation",Value(second).value,2);
        c.Is("FS-I04.distinct",Value(first)!=Value(second));
        c.Status("FS-I05.stale",f->FinalSeal(Event(b.owner,b.subject,1)).status,L::IdentityStatus::StaleEvidence);
        auto handoff=h->BindHandoff(b);
        c.Is("FS-I15.same-subject-distinct-role",handoff.has_value());
        if(handoff) {
            const auto changed=handoff->ContractChanged(Event(b.owner,b.subject,1));
            c.Status("FS-I15.finalizer-handoff",changed.status,L::IdentityStatus::Ok);
            if(HasValue(changed))c.Is("FS-I15.separate-namespace",Value(changed).nameSpace!=Value(first).nameSpace);
        }
        const auto fg=Binding(C::OwnerDomain::FrameGeneration,"fg.handoff");
        auto fgHandoff=h->BindHandoff(fg);
        c.Is("FS-I15.fg-handoff-preserved",fgHandoff.has_value());
        if(fgHandoff)c.Status("FS-I15.fg-handoff-issues",fgHandoff->ContractChanged(Event(fg.owner,fg.subject,1)).status,L::IdentityStatus::Ok);
        // Same owner event racing on copies of one facade may succeed only once.
        std::array<L::Outcome<C::FinalSealId>,8> outcomes{};
        std::array<std::thread,8> workers;
        for(std::size_t i=0;i<workers.size();++i)workers[i]=std::thread([&,i]{outcomes[i]=f->FinalSeal(Event(b.owner,b.subject,3));});
        for(auto& worker:workers)worker.join();
        unsigned accepted=0;bool lawful=true;
        for(const auto& o:outcomes){if(HasValue(o))++accepted;else lawful=lawful&&(o.status==L::IdentityStatus::Busy||o.status==L::IdentityStatus::StaleEvidence);}
        c.Equal("FS-I16.racing-duplicate-single-allocation",accepted,1);
        c.Is("FS-I16.nonblocking-refusals",lawful);
        c.Status("FS-I07.close",h->CloseSession(Value(session)),L::IdentityStatus::Ok);
        c.Status("FS-I07.closed",f->FinalSeal(Event(b.owner,b.subject,4)).status,L::IdentityStatus::Closed);
        c.Status("FS-I08.reopen",h->OpenSession(Event(C::OwnerDomain::Session,Key("session.scope"),2)).status,L::IdentityStatus::Ok);
        auto fresh=h->BindFinalizer(b);c.Is("FS-I08.rebind",fresh.has_value());
        if(fresh){const auto n=fresh->FinalSeal(Event(b.owner,b.subject,1));c.Status("FS-I08.fresh",n.status,L::IdentityStatus::Ok);
            if(HasValue(n)){c.Equal("FS-I08.counter",Value(n).value,1);c.Is("FS-I08.non-reuse",Value(n)!=Value(first));}}
        c.Status("FS-I09.old-facade",f->FinalSeal(Event(b.owner,b.subject,5)).status,L::IdentityStatus::Closed);
        auto small=std::make_unique<Host>(Sym("fixture.exhaustion"),1);
        c.Status("FS-I10.session",small->OpenSession(Event(C::OwnerDomain::Session,Key("session.scope"),1)).status,L::IdentityStatus::Ok);
        auto limited=small->BindFinalizer(b);c.Is("FS-I10.bind",limited.has_value());
        if(limited){c.Status("FS-I10.first",limited->FinalSeal(Event(b.owner,b.subject,1)).status,L::IdentityStatus::Ok);
            c.Status("FS-I10.exhausted",limited->FinalSeal(Event(b.owner,b.subject,2)).status,L::IdentityStatus::Exhausted);
            c.Status("FS-I10.no-wrap",limited->FinalSeal(Event(b.owner,b.subject,3)).status,L::IdentityStatus::Exhausted);}
    }
    return c.Finish();
}
int main(){return Exercise<L::HostIdentityDomain>();}
