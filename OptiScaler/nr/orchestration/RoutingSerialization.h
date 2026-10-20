#pragma once
// Off-hot-path replay only. The canonical codec bounds bytes, metadata, traversal and allocation.
// Deserialized values remain untrusted; replay never supplies an executable route or live owner.
#include "RoutingSelector.h"
#include <nr/contracts/FixtureCodec.h>
#include <any>
#include <list>
#include <typeindex>

namespace Neurotic::Orchestration
{
namespace ReplayDetail
{
namespace D=C::Codec::Detail;
template<class T>void Canonicalize(T& value)
{
    const auto symbolLess=[](const C::Symbol& a,const C::Symbol& b){return a.View()<b.View();};
    if constexpr(std::is_same_v<T,RoutingInput>)
    {
        std::sort(value.intent.require.begin(),value.intent.require.end(),symbolLess);
        std::sort(value.intent.forbid.begin(),value.intent.forbid.end(),symbolLess);
        std::sort(value.intent.optIns.begin(),value.intent.optIns.end(),[](const auto& a,const auto& b){
            if(a.rule!=b.rule)return a.rule.View()<b.rule.View();
            if(a.scope.key!=b.scope.key)return a.scope.key&&(!b.scope.key||Context::KeyLess(*a.scope.key,*b.scope.key));
            return Context::KeyLess(a.generations.record,b.generations.record);});
    }
    else if constexpr(std::is_same_v<T,CompletePlan>)
    {
        std::sort(value.features.begin(),value.features.end(),symbolLess);
        std::sort(value.softRestrictions.begin(),value.softRestrictions.end(),[](const auto& a,const auto& b){return a.rule.View()<b.rule.View();});
    }
    else if constexpr(requires{typename T::value_type;value.begin();value.end();})
    {
        using V=typename T::value_type;
        if constexpr(std::is_same_v<V,C::RecordKey>)std::sort(value.begin(),value.end(),Context::KeyLess);
        else if constexpr(std::is_same_v<V,OperationalFact>)std::sort(value.begin(),value.end(),[](const auto& a,const auto& b){return a.field.View()<b.field.View();});
        else if constexpr(std::is_same_v<V,C::PolicyReason>)std::sort(value.begin(),value.end(),[](const auto& a,const auto& b){return a.rule.View()<b.rule.View();});
    }
}
struct Seen
{
    std::string type;C::RecordKey key;C::OwnerDomain owner;std::uint64_t revision;
};
template<class T,class Store>void Collect(const T& value,const Store& store,C::Codec::MetadataCatalog& catalog,std::vector<Seen>& seen,std::size_t depth=0)
{
    D::Require(depth<C::Codec::Limits::MaxTraversalDepth,C::Error::LimitExceeded);
    if constexpr(D::MetadataInfo<T>::value)
    {
        using V=typename D::MetadataInfo<T>::element;const auto type=D::TypeName<V>();
        for(const auto& old:seen)if(old.type==type&&old.key==value.record&&old.owner==value.owner&&old.revision==value.revision)return;
        D::Require(seen.size()<C::Codec::Limits::MaxMetadataEntries,C::Error::LimitExceeded);
        const auto* resolved=Context::ResolveMetadata(value,store);D::Require(resolved!=nullptr,C::Error::MissingField);
        auto body=std::make_unique<V>(*resolved);Canonicalize(*body);
        seen.push_back({type,value.record,value.owner,value.revision});D::Require(catalog.Add(value,*body));
        Collect(*body,store,catalog,seen,depth+1);
    }
    else if constexpr(D::OptionalInfo<T>::value){if(value)Collect(*value,store,catalog,seen,depth+1);}
    else if constexpr(D::FactInfo<T>::value){if(value.IsKnown())Collect(value.KnownPart()->value,store,catalog,seen,depth+1);}
    else if constexpr(D::VariantInfo<T>::value)std::visit([&](const auto& v){Collect(v,store,catalog,seen,depth+1);},value);
    else if constexpr(requires{T::Fields();})std::apply([&](const auto&... field){(Collect(value.*(field.pointer),store,catalog,seen,depth+1),...);},T::Fields());
    else if constexpr(D::ListInfo<T>::value)for(const auto& item:value)Collect(item,store,catalog,seen,depth+1);
}
// Only a diagnostic checksum. Exact bytes and structural comparisons, not this checksum,
// determine replay equality and plan identity. Evidence tools additionally bind bytes with SHA256.
inline C::Symbol Digest(std::string_view bytes)
{
    std::uint64_t value=14695981039346656037ull;for(unsigned char c:bytes){value^=c;value*=1099511628211ull;}
    char text[26]="fnv1a64:";constexpr char hex[]="0123456789abcdef";
    for(unsigned i=0;i<16;++i)text[8+i]=hex[(value>>((15-i)*4))&15];
    return Symbol(std::string_view(text,24));
}
class Store
{
    struct Entry{std::type_index type;C::RecordKey key;C::OwnerDomain owner;std::uint64_t revision;std::any body;};
    const D::Json& metadata_;
    mutable std::list<Entry> cache_;
    mutable std::size_t bytes_=0;
  public:
    explicit Store(const D::Json& metadata):metadata_(metadata){}
    template<class T>Context::MetadataView<T> Resolve(const C::MetadataRef<T>& wanted)const
    {
        for(const auto& entry:cache_)if(entry.type==typeid(T)&&entry.key==wanted.record&&entry.owner==wanted.owner&&entry.revision==wanted.revision)
            return {wanted,std::any_cast<T>(&entry.body)};
        D::Require(cache_.size()<C::Codec::Limits::MaxMetadataEntries&&bytes_+sizeof(T)<=C::Codec::Limits::MaxTypedScratchBytes,C::Error::LimitExceeded);
        D::DecodeContext context(&metadata_);
        for(std::size_t i=0;i<context.count;++i)
        {
            const auto& reference=*context.entries[i].reference;
            if(D::View(D::Get(reference,"metadataType").text)!=D::TypeName<T>())continue;
            C::MetadataRef<T> actual;D::DecodeReference(reference,actual,context);if(actual!=wanted)continue;
            D::ResolveMetadata(reference,wanted,context,[&](const T& body){cache_.push_back({typeid(T),wanted.record,wanted.owner,wanted.revision,body});});
            bytes_+=sizeof(T);return {actual,std::any_cast<T>(&cache_.back().body)};
        }
        return {};
    }
};
}
template<class Store>C::Codec::EncodeResult EncodeInput(const RoutingInput& input,const Store& store)
{
    try
    {
        auto canonical=std::make_unique<RoutingInput>(input);ReplayDetail::Canonicalize(*canonical);
        for(const auto& offer:canonical->offers)if(!Context::ResolveMetadata(offer,store))return {{},C::Error::MissingField};
        std::sort(canonical->offers.begin(),canonical->offers.end(),[&](const auto& a,const auto& b){
            return Context::KeyLess(Context::ResolveMetadata(a,store)->anchor.plan,Context::ResolveMetadata(b,store)->anchor.plan);});
        C::Codec::MetadataCatalog catalog;std::vector<ReplayDetail::Seen> seen;ReplayDetail::Collect(*canonical,store,catalog,seen);
        return C::Codec::EncodeWithMetadata(*canonical,catalog);
    }
    catch(const ReplayDetail::D::Failure& f){return {{},f.error};}
    catch(const std::bad_alloc&){return {{},C::Error::AllocationFailure};}
    catch(const std::length_error&){return {{},C::Error::LimitExceeded};}
}
template<class Store>C::Codec::EncodeResult EncodeDecision(const ShadowDecision& decision,const Store& store)
{
    try
    {
        C::Codec::MetadataCatalog catalog;std::vector<ReplayDetail::Seen> seen;ReplayDetail::Collect(decision,store,catalog,seen);
        return C::Codec::EncodeWithMetadata(decision,catalog);
    }
    catch(const ReplayDetail::D::Failure& f){return {{},f.error};}
    catch(const std::bad_alloc&){return {{},C::Error::AllocationFailure};}
    catch(const std::length_error&){return {{},C::Error::LimitExceeded};}
}
struct ReplayResult
{
    ShadowDecision decision;std::string canonicalOutput;C::Symbol inputDigest,outputDigest;
    C::Error error=C::Error::None;
};
inline ReplayResult Replay(std::string_view bytes)
{
    ReplayResult result;
    try
    {
        const auto decoded=C::Codec::Decode<RoutingInput>(bytes);result.error=decoded.error;if(!decoded.record)return result;
        ReplayDetail::D::ArenaScope arena;const auto root=ReplayDetail::D::Parser(bytes).Parse();
        ReplayDetail::Store store(ReplayDetail::D::Get(root,"metadata"));
        result.decision=Select(decoded.record->Value(),store);
        const auto canonical=EncodeInput(decoded.record->Value(),store);
        if(canonical.error!=C::Error::None){result.error=canonical.error;return result;}
        result.inputDigest=ReplayDetail::Digest(canonical.text);
        const auto output=EncodeDecision(result.decision,store);result.error=output.error;
        if(output.error==C::Error::None){result.canonicalOutput=output.text;result.outputDigest=ReplayDetail::Digest(output.text);}
        return result;
    }
    catch(const ReplayDetail::D::Failure& f){result.error=f.error;}
    catch(const std::bad_alloc&){result.error=C::Error::AllocationFailure;}
    catch(const std::length_error&){result.error=C::Error::LimitExceeded;}
    return result;
}
}
