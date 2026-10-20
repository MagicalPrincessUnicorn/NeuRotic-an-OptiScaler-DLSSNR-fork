#include "../contracts/Test002Support.h"
#if __has_include(<nr/context/ContextMetadata.h>)
#include <nr/context/ContextMetadata.h>
#define NR_CTX_METADATA 1
#endif
using namespace NrTest002;
#ifdef NR_CTX_METADATA
namespace S=Neurotic::Context;
template<class T> struct Reader
{
    S::MetadataView<T> publication;
    S::MetadataView<T> Resolve(const C::MetadataRef<T>&) const {return publication;}
};
template<class T> C::MetadataRef<T> Ref()
{
    C::MetadataRef<T> result;
    result.owner=C::OwnerDomain::Context;result.record=Key("metadata",1);result.revision=1;
    return result;
}
#endif
int main(int argc,char** argv)
{
    return Guard([&]{
        Report r("ctx-metadata",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_CTX_METADATA
        auto reference=Ref<C::RasterDescription>(); C::RasterDescription raster;
        Reader<C::RasterDescription> reader{{reference,&raster}};
        r.Check("T06.exact-body", S::ResolveMetadata(reference,reader)==&raster,true,"ResolveMetadata");
        reader.publication.value=nullptr;
        r.Check("T06.missing-body",S::ResolveMetadata(reference,reader)==nullptr,true,"ResolveMetadata");
        reader.publication.value=&raster;reader.publication.reference.revision=2;
        r.Check("T06.stale-revision",S::ResolveMetadata(reference,reader)==nullptr,true,"ResolveMetadata");
        reader.publication.reference=reference;reader.publication.reference.owner=C::OwnerDomain::Provider;
        r.Check("T06.owner-mismatch",S::ResolveMetadata(reference,reader)==nullptr,true,"ResolveMetadata");
        reader.publication.reference=reference;reader.publication.reference.record.nameSpace=Sym("other");
        r.Check("T06.namespace-mismatch",S::ResolveMetadata(reference,reader)==nullptr,true,"ResolveMetadata");
        reference.schemaVersion.major=2;reader.publication.reference=reference;
        r.Check("T06.future-schema",S::ResolveMetadata(reference,reader)==nullptr,true,"ResolveMetadata");
        using List=C::BoundedList<C::SemanticClaim,16>;
        C::MetadataList<C::SemanticClaim,16> list;List body;
        Reader<List> listReader{{Ref<List>(),&body}};const List* result=nullptr;
        r.Check("T06.explicit-empty",S::ResolveList(list,listReader,result)&&!result,true,"ResolveList");
        list.count=1;list.backing=listReader.publication.reference;
        r.Check("T06.count-body-mismatch",S::ResolveList(list,listReader,result),false,"ResolveList");
        C::SemanticClaim claim;claim.field=Sym("flag");claim.raw=Known(C::ScalarValue{false});body.Push(claim);
        r.Check("T06.exact-list",S::ResolveList(list,listReader,result)&&result==&body,true,"ResolveList");
        body.Get(0)->raw=Known(C::ScalarValue{std::numeric_limits<double>::infinity()});
        r.Check("T06.nonfinite-nested-value",S::ResolveList(list,listReader,result),false,"ResolveList");
        body.Get(0)->raw={};
        r.Check("T04.unknown-preserved",S::ResolveList(list,listReader,result)&&!result->Get(0)->raw.IsKnown(),true,"ResolveList");
        body.Get(0)->raw=C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{false},{});
        r.Check("T06.missing-known-provenance",S::ResolveList(list,listReader,result),false,"ResolveList");
        r.Check("T06.malformed-category",S::Missing("CTX.MalformedObservation").reason.category==C::ReasonCategory::InvalidInvariant,true,"Missing");
        r.Check("T06.future-category",S::Missing("CTX.UnsupportedSchema").reason.category==C::ReasonCategory::MissingImplementation,true,"Missing");
        r.Check("T05.conflict-category",S::Missing("CTX.ContradictoryEvidence").reason.category==C::ReasonCategory::InvalidInvariant,true,"Missing");
        r.Check("T05.alias-category",S::Missing("CTX.AliasEvidence").reason.category==C::ReasonCategory::Degraded,true,"Missing");
#else
        r.Check("CTX.metadata.production-binding",false,true,"Missing ContextMetadata implementation");
#endif
        return r.Finish();
    });
}
