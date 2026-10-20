#include "../context/ContextFixtureStore.h"
#include <nr/contracts/NativeStageDelivery.h>
#include <cstdlib>
#include <new>
namespace {bool countAllocations=false;std::size_t allocations=0;}
void* operator new(std::size_t size)
{if(countAllocations)++allocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc{};}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete[](void* p)noexcept{::operator delete(p);}
#if __has_include(<nr/lifecycle/OwnerMetadataArena.h>)
#include <nr/lifecycle/OwnerMetadataArena.h>
#define NR_OWNER_ARENA 1
#endif
using namespace NrTest002;
struct ThrowingValidation
{
    C::Error Check()const{throw 7;}
    static constexpr auto Fields(){return std::tuple{};}
};
struct TrackedMetadata
{
    inline static int destroyed=0;
    int value=1;
    ~TrackedMetadata(){++destroyed;}
    static constexpr auto Fields(){return std::tuple{C::Field("value",&TrackedMetadata::value)};}
};
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-metadata-arena",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_OWNER_ARENA
    namespace L=Neurotic::Lifecycle;
    L::OwnerMetadataArena arena(Key("publication-domain"),131072,4);
    auto writer=arena.ForOwner(C::OwnerDomain::Resource);
    C::ResourceView view=NrContextTests::Resource(1,{});
    const auto first=writer.Publish(view,C::OwnerDomain::Resource);
    view.identity.contentRevision=Known(C::ContentRevision{2});
    const auto second=writer.Publish(view,C::OwnerDomain::Resource);
    r.Check("T30.arena-immutable-content",arena.Resolve(first).value&&arena.Resolve(first).value->identity.contentRevision.KnownPart()->value.value==1,true,"OwnerMetadataArena");
    r.Check("T30.arena-distinct-publication",first.record!=second.record&&arena.Resolve(second).value->identity.contentRevision.KnownPart()->value.value==2,true,"OwnerMetadataArena");
    auto foreign=first;foreign.owner=C::OwnerDomain::Diagnostics;
    r.Check("T30.arena-owner-resolution",!arena.Resolve(foreign).value,true,"OwnerMetadataArena");
    ++foreign.revision;foreign.owner=C::OwnerDomain::Resource;
    r.Check("T30.arena-revision-resolution",!arena.Resolve(foreign).value,true,"OwnerMetadataArena");
    bool wrongOwner=false;try{writer.Publish(view,C::OwnerDomain::Context);}catch(const L::MetadataRefusal&){wrongOwner=true;}
    r.Check("T30.arena-writer-confinement",wrongOwner,true,"OwnerMetadataArena");
    C::MetadataRef<C::FrameIdentity> wrongType;wrongType.record=first.record;wrongType.revision=first.revision;wrongType.owner=first.owner;
    r.Check("T30.arena-type-confinement",!arena.Resolve(wrongType).value,true,"OwnerMetadataArena");
    L::OwnerMetadataArena other(Key("other-publication-domain"),131072,4);
    r.Check("T30.arena-foreign-domain",!other.Resolve(first).value,true,"OwnerMetadataArena");
    writer.Publish(std::uint64_t{3});writer.Publish(std::uint64_t{4});
    bool capacity=false;try{writer.Publish(std::uint64_t{5});}catch(const L::MetadataRefusal&){capacity=true;}
    r.Check("T30.arena-bounded-capacity",capacity&&arena.Resolve(first).value,true,"OwnerMetadataArena");
    L::OwnerMetadataArena small(Key("small-publication-domain"),8,4);
    bool bytes=false;try{small.ForOwner(C::OwnerDomain::Resource).Publish(view);}catch(const L::MetadataRefusal&){bytes=true;}
    r.Check("T30.arena-bounded-bytes",bytes,true,"OwnerMetadataArena");
    r.Check("T30.arena-no-copy",!std::is_copy_constructible_v<L::OwnerMetadataArena>,true,"OwnerMetadataArena");
#ifdef NR_OWNER_METADATA_RESERVATION_V1
    using Reservation=L::OwnerMetadataArena::Reservation<C::ResourceView>;
    static_assert(std::is_trivially_copyable_v<C::NativeStageDeliveryV1>);
    static_assert(!std::is_copy_constructible_v<Reservation> && !std::is_copy_assignable_v<Reservation>);
    static_assert(std::is_nothrow_move_constructible_v<Reservation> && std::is_nothrow_move_assignable_v<Reservation>);
    static_assert(noexcept(std::declval<Reservation&>().Commit(view)));
    static_assert(noexcept(std::declval<Reservation&>().Cancel()));
    L::OwnerMetadataArena reserved(Key("reserved-domain"),131072,3);
    auto reserveWriter=reserved.ForOwner(C::OwnerDomain::Resource);
    static_assert(noexcept(reserveWriter.Reserve<C::ResourceView>()));
    auto pending=reserveWriter.Reserve<C::ResourceView>();
    r.Check("T30.reserve-available",bool(pending),true,"OwnerMetadataArena");
    const auto reservedRef=pending->Reference();
    r.Check("T30.reserve-unpublished",!reserved.Resolve(reservedRef).value,true,"OwnerMetadataArena");
    const auto interleaved=reserveWriter.Publish(std::uint64_t{9});
    r.Check("T30.reserve-interleaved-distinct",interleaved.record!=reservedRef.record&&!reserved.Resolve(reservedRef).value,true,"OwnerMetadataArena");
    Reservation moved(std::move(*pending));
    r.Check("T30.reserve-moved-from-refuses",!pending->Commit(view)&&!pending->Cancel(),true,"OwnerMetadataArena");
    allocations=0;countAllocations=true;
    const bool committed=moved.Commit(view);
    countAllocations=false;
    const auto commitAllocations=allocations;
    r.Check("T30.reserve-commit-no-allocation",committed&&commitAllocations==0,true,"OwnerMetadataArena");
    r.Check("T30.reserve-published-exact",reserved.Resolve(reservedRef).value&&*reserved.Resolve(reservedRef).value==view,true,"OwnerMetadataArena");
    r.Check("T30.reserve-duplicate-refuses",!moved.Commit(view)&&!moved.Cancel(),true,"OwnerMetadataArena");
    auto wrongReservedOwner=reservedRef;wrongReservedOwner.owner=C::OwnerDomain::Provider;
    r.Check("T30.reserve-owner-confined",!reserved.Resolve(wrongReservedOwner).value,true,"OwnerMetadataArena");
    auto cancelled=reserveWriter.Reserve<std::uint64_t>();const auto cancelledRef=cancelled->Reference();
    r.Check("T30.reserve-cancel-once",cancelled->Cancel()&&!cancelled->Cancel()&&!cancelled->Commit(5),true,"OwnerMetadataArena");
    r.Check("T30.reserve-cancel-unresolved",!reserved.Resolve(cancelledRef).value,true,"OwnerMetadataArena");
    r.Check("T30.reserve-entry-exhaustion",!reserveWriter.Reserve<std::uint64_t>()&&reserved.Resolve(reservedRef).value,true,"OwnerMetadataArena");
    L::OwnerMetadataArena byteBound(Key("byte-bound"),sizeof(std::uint64_t),4);
    auto byteWriter=byteBound.ForOwner(C::OwnerDomain::Resource);
    r.Check("T30.reserve-byte-refusal",!byteWriter.Reserve<C::ResourceView>(),true,"OwnerMetadataArena");
    auto exactBytes=byteWriter.Reserve<std::uint64_t>();
    r.Check("T30.reserve-failed-reserve-no-consumption",bool(exactBytes),true,"OwnerMetadataArena");
    exactBytes->Cancel();
    r.Check("T30.reserve-cancel-no-recycle",!byteWriter.Reserve<std::uint64_t>(),true,"OwnerMetadataArena");
    L::OwnerMetadataArena invalid(Key("invalid-domain"),131072,4);
    auto invalidWriter=invalid.ForOwner(C::OwnerDomain::Resource);
    auto nonfinite=invalidWriter.Reserve<double>();const auto invalidRef=nonfinite->Reference();
    r.Check("T30.reserve-invalid-consumed",!nonfinite->Commit(std::numeric_limits<double>::infinity())&&!nonfinite->Commit(1.0)&&!invalid.Resolve(invalidRef).value,true,"OwnerMetadataArena");
    auto throwing=invalidWriter.Reserve<ThrowingValidation>();const auto throwingRef=throwing->Reference();
    r.Check("T30.reserve-validation-throw-refuses",!throwing->Commit(ThrowingValidation{})&&!throwing->Cancel()&&!invalid.Resolve(throwingRef).value,true,"OwnerMetadataArena");
    auto moveTarget=invalidWriter.Reserve<std::uint64_t>();const auto abandonedRef=moveTarget->Reference();
    auto moveSource=invalidWriter.Reserve<std::uint64_t>();const auto retainedRef=moveSource->Reference();
    *moveTarget=std::move(*moveSource);
    *moveTarget=std::move(*moveTarget);
    r.Check("T30.reserve-move-assignment",!moveSource->Commit(4)&&moveTarget->Commit(6)&&!invalid.Resolve(abandonedRef).value&&invalid.Resolve(retainedRef).value&&*invalid.Resolve(retainedRef).value==6,true,"OwnerMetadataArena");
    std::optional<Reservation> surviving;
    {
        auto dying=std::make_unique<L::OwnerMetadataArena>(Key("dying-domain"),131072,4);
        auto dyingWriter=dying->ForOwner(C::OwnerDomain::Resource);
        surviving=dyingWriter.Reserve<C::ResourceView>();
        {auto dropped=dyingWriter.Reserve<std::uint64_t>();}
        TrackedMetadata tracked;dyingWriter.Publish(tracked);
        TrackedMetadata::destroyed=0;dying.reset();
        r.Check("T30.reserve-destroy-only-constructed",TrackedMetadata::destroyed==1,true,"OwnerMetadataArena");
    }
    r.Check("T30.reserve-arena-destruction",!surviving->Commit(view)&&!surviving->Cancel(),true,"OwnerMetadataArena");
    surviving.reset();
#else
    r.Check("T30.reserve-production",false,true,"OwnerMetadataArena reservation unavailable");
#endif
#else
    r.Check("T30.arena-production",false,true,"OwnerMetadataArena unavailable");
#endif
    return r.Finish();});
}
