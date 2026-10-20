#define NR_NATIVE_OWNER_TESTING
#include "../context/ContextFixtureStore.h"
#if __has_include(<nr/lifecycle/NativeOwnerSet.h>)
#include <nr/lifecycle/NativeOwnerSet.h>
#define NR_OWNER_SET 1
#endif
using namespace NrTest002;
template<class T>bool CompleteRoles(const T& owners)
{
    if constexpr(requires{owners.Topology();owners.Render();owners.Episode();owners.Objects();owners.Evaluation();
        owners.Provider();owners.Model();owners.Handoff();owners.Raster();owners.ContextRepresentation();})
        return owners.Topology()&&owners.Render()&&owners.Episode()&&owners.Objects()&&owners.Evaluation()&&
            owners.Provider()&&owners.Model()&&owners.Handoff()&&owners.Raster()&&owners.ContextRepresentation();
    else return false;
}
template<class T>constexpr bool PublicRoute=requires(T& owner){owner.Route();};
template<class T>constexpr bool PublicClose=requires(T& owner){owner.CloseDrained();};
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-owner-set",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_OWNER_SET
    namespace L=Neurotic::Lifecycle;
    auto owners=std::make_unique<L::NativeOwnerSet>(Sym("native.owners.fixture"));
    r.Check("T27.native-owner-set-ready",owners->Ready(),true,"NativeOwnerSet");
    r.Check("T30.native-owner-set-cold-roles",CompleteRoles(*owners),true,"NativeOwnerSet");
    const auto history=owners->History();
    const auto historyEvent=owners->HistoryJournal().Event();
    const auto generation=history->AppliedReset(historyEvent);
    r.Check("T29.native-owner-set-history-facade",generation.status==L::IdentityStatus::Ok,true,"NativeOwnerSet");
    const auto wrong=history->AppliedReset(owners->StrategyJournal().Event());
    r.Check("T29.native-owner-set-wrong-role",wrong.status==L::IdentityStatus::WrongOwner,true,"NativeOwnerSet");
    C::BoundedList<C::StableIdentity,16> identities;bool unique=true;
    for(std::size_t i=0;i<L::NativeOwnerSet::AllocationSlots;++i)
    {
        auto* slot=owners->Allocation(i);const auto event=slot->journal.Event();
        const auto identity=slot->resource->Resource(event);
        if(identity.status!=L::IdentityStatus::Ok){unique=false;break;}
        for(const auto& old:identities)if(old==identity.value.KnownPart()->value.Describe())unique=false;
        identities.Push(identity.value.KnownPart()->value.Describe());
    }
    r.Check("T16.native-owner-set-slot-isolation",unique&&identities.Size()==L::NativeOwnerSet::AllocationSlots,true,"NativeOwnerSet");
    r.Check("T16.native-owner-set-bounded",owners->Allocation(L::NativeOwnerSet::AllocationSlots)==nullptr,true,"NativeOwnerSet");
    const auto h1=owners->ContextJournal().Header(C::ContractId::C02,{Key("scope")});
    const auto h2=owners->ContextJournal().Header(C::ContractId::C04,{Key("scope")});
    r.Check("T30.native-owner-journal-sequence",h1.owner==C::OwnerDomain::Context&&h2.record.value>h1.record.value&&h1.scope==h2.scope,true,"OwnerPublicationJournal");
    r.Check("T30.native-owner-journal-not-copyable",!std::is_copy_constructible_v<L::OwnerPublicationJournal>&&
        !std::is_copy_assignable_v<L::OwnerPublicationJournal>,true,"OwnerPublicationJournal");
    auto moved=std::move(owners->ContextJournal());bool retired=false;
    try{owners->ContextJournal().Event();}catch(const std::exception&){retired=true;}
    const auto afterMove=moved.Header(C::ContractId::C02,{Key("scope")});
    r.Check("T30.native-owner-journal-move-transfers",retired&&afterMove.record.value>h2.record.value,true,"OwnerPublicationJournal");
    auto arena=owners->NewInvocationMetadata(1048576,64);
    auto context=arena->ForOwner(C::OwnerDomain::Context);
    auto resource=arena->ForOwner(C::OwnerDomain::Resource);
    const auto ref=context.Publish(C::GenerationVector{});
    r.Check("T30.native-owner-metadata-shared-read",resource.Resolve(ref).value!=nullptr,true,"NativeOwnerSet");
    r.Check("INIT.native-owner-set-private-route",!PublicRoute<L::NativeOwnerSet>,true,"Sole retained kernel route issuer");
    r.Check("INIT.native-owner-set-private-close",!PublicClose<L::NativeOwnerSet>,true,"Aggregate root closure required");
#else
    r.Check("T27.native-owner-set-production",false,true,"NativeOwnerSet");
#endif
    return r.Finish();});
}
