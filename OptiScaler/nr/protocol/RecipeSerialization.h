#pragma once
// Off-hot-path only. Fixture decoding describes a recipe; it grants no live owner authority.
#include "RecipeTypes.h"
#include <nr/contracts/FixtureCodec.h>

namespace Neurotic::Protocol
{
namespace RecipeWire
{
namespace D=C::Codec::Detail;
struct Seen{std::string type;C::RecordKey key;C::OwnerDomain owner;std::uint64_t revision;};
template<class T,class Store>void Collect(const T& value,const Store& store,C::Codec::MetadataCatalog& catalog,std::vector<Seen>& seen,std::size_t depth=0)
{
    D::Require(depth<C::Codec::Limits::MaxTraversalDepth,C::Error::LimitExceeded);
    if constexpr(D::MetadataInfo<T>::value)
    {
        using V=typename D::MetadataInfo<T>::element;const auto type=D::TypeName<V>();
        for(const auto& prior:seen)if(prior.type==type&&prior.key==value.record&&prior.owner==value.owner&&prior.revision==value.revision)return;
        D::Require(seen.size()<C::Codec::Limits::MaxMetadataEntries,C::Error::LimitExceeded);
        const auto* body=Context::ResolveMetadata(value,store);D::Require(body!=nullptr,C::Error::MissingField);
        seen.push_back({type,value.record,value.owner,value.revision});D::Require(catalog.Add(value,*body));
        Collect(*body,store,catalog,seen,depth+1);
    }
    else if constexpr(D::OptionalInfo<T>::value){if(value)Collect(*value,store,catalog,seen,depth+1);}
    else if constexpr(D::FactInfo<T>::value){if(value.IsKnown())Collect(value.KnownPart()->value,store,catalog,seen,depth+1);}
    else if constexpr(D::VariantInfo<T>::value)std::visit([&](const auto& v){Collect(v,store,catalog,seen,depth+1);},value);
    else if constexpr(requires{T::Fields();})std::apply([&](const auto&... field){(Collect(value.*(field.pointer),store,catalog,seen,depth+1),...);},T::Fields());
    else if constexpr(D::ListInfo<T>::value)for(const auto& item:value)Collect(item,store,catalog,seen,depth+1);
}
}
template<class Store>C::Codec::EncodeResult EncodeRecipe(const RecipeProduct& product,const Store& store)
{
    try
    {
        C::Codec::MetadataCatalog catalog;std::vector<RecipeWire::Seen> seen;
        RecipeWire::Collect(product,store,catalog,seen);
        return C::Codec::EncodeWithMetadata(product,catalog);
    }
    catch(const RecipeWire::D::Failure& f){return {{},f.error};}
    catch(const std::bad_alloc&){return {{},C::Error::AllocationFailure};}
    catch(const std::length_error&){return {{},C::Error::LimitExceeded};}
}
}
