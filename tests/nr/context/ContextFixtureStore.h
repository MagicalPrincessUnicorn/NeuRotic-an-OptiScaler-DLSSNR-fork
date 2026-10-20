#pragma once
// Test-only immutable publication store. Production CTX accepts an owner-provided bounded store.
#include "../contracts/Test002Support.h"
#include <nr/context/ContextMetadata.h>
#include <any>
#include <list>
#include <typeindex>

namespace NrContextTests
{
namespace C=Neurotic::Contracts;
class Store
{
    struct Entry {std::type_index type; C::RecordKey key; std::uint64_t revision; C::OwnerDomain owner; std::any body;};
    std::list<Entry> entries;
    std::uint64_t serial=1000;
  public:
    template<class T> C::MetadataRef<T> Publish(const T& value,C::OwnerDomain owner=C::OwnerDomain::Context)
    {
        C::MetadataRef<T> reference;
        reference.record=NrTest002::Key("ctx.fixture.metadata",++serial);reference.revision=1;reference.owner=owner;
        entries.push_back({typeid(T),reference.record,reference.revision,reference.owner,value});
        return reference;
    }
    template<class T> Neurotic::Context::MetadataView<T> Resolve(const C::MetadataRef<T>& reference) const
    {
        for (const auto& entry:entries)
            if (entry.type==typeid(T) && entry.key==reference.record && entry.revision==reference.revision && entry.owner==reference.owner)
                return {reference,std::any_cast<T>(&entry.body)};
        return {};
    }
    template<class T> void Withdraw(const C::MetadataRef<T>& reference)
    {
        entries.remove_if([&](const Entry& entry){return entry.type==typeid(T) && entry.key==reference.record;});
    }
    template<class T> void Replace(const C::MetadataRef<T>& reference,const T& value)
    {
        for (auto& entry:entries) if (entry.type==typeid(T) && entry.key==reference.record) {entry.body=value;++entry.revision;}
    }
};
template<class T,std::size_t N> C::MetadataList<T,N> PublishList(Store& store,const C::BoundedList<T,N>& values)
{
    C::MetadataList<T,N> result;result.count=static_cast<std::uint32_t>(values.Size());
    if (result.count) result.backing=store.Publish(values);
    return result;
}
inline C::RecordHeader Header(C::ContractId contract,C::OwnerDomain owner,std::uint64_t serial)
{
    C::RecordHeader result;result.contract=contract;result.owner=owner;result.record=NrTest002::Key("ctx.fixture.record",serial);
    result.revision=1;result.scope.key=NrTest002::Key("ctx.fixture.scope",1);return result;
}
inline C::FrameIdentity Frame()
{
    using namespace NrTest002;
    C::FrameIdentity frame;frame.sessionId=Known(Id<C::SessionId>("session",1));
    frame.renderStreamId=Known(Id<C::RenderStreamId>("stream",1));frame.viewId=Known(Id<C::ViewId>("view",1));
    frame.episodeId=Known(Id<C::EpisodeId>("episode",1));frame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",10));
    frame.associationEvidence.Push(Ev(1));return frame;
}
inline void DeclareJitterBasis(Store& store,C::AcquisitionCandidate& candidate,const C::RasterDescription& raster,
                               std::string_view units="Pixels")
{
    using namespace NrTest002;
    C::MotionDescription description;description.currentRaster=raster;
    candidate.motion=Known(store.Publish(description,C::OwnerDomain::Provider));
    C::BoundedList<C::SemanticClaim,16> claims;
    C::SemanticClaim unit;unit.field=Sym("jitter.units");unit.raw=Known(C::ScalarValue{Sym(units)});unit.effective=unit.raw;claims.Push(unit);
    C::SemanticClaim basis;basis.field=Sym("jitter.basis");basis.raw=Known(C::ScalarValue{Sym("motion.currentRaster")});basis.effective=basis.raw;claims.Push(basis);
    candidate.claims=PublishList(store,claims);
}
inline C::ResourceView Resource(std::uint64_t incarnation,const C::RasterDescription& raster)
{
    using namespace NrTest002;C::ResourceView resource;resource.raster=raster;
    resource.identity.objectIncarnation=Known(C::StableIdentity{C::IdentityKind::ObjectIncarnation,Sym("object"),Sym("owner"),incarnation});
    resource.identity.resourceIncarnation=Known(C::StableIdentity{C::IdentityKind::ResourceIncarnation,Sym("resource"),Sym("owner"),incarnation});
    resource.identity.resourceViewIncarnation=Known(C::StableIdentity{C::IdentityKind::ResourceViewIncarnation,Sym("view"),Sym("owner"),incarnation});
    resource.identity.resourceGeneration=Known(Gen(C::IdentityKind::ResourceGeneration,"resource",1));
    resource.identity.representationGeneration=Known(Gen(C::IdentityKind::RepresentationGeneration,"representation",1));
    resource.identity.contentRevision=Known(C::ContentRevision{1});return resource;
}
}
