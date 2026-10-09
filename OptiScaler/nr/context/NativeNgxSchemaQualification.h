#pragma once
#include "ContextMetadata.h"
#include <nr/contracts/C01_Acquisition.h>

namespace Neurotic::Context
{
// Internal NFC policy domains. Provider remains the default. The second domain
// describes the original observation adapter, never a provider DLL/API version.
enum class SourceSchemaDomain {Provider,NativeNgxAdapter};
template<class Reader>bool NativeNgxAdapterClaims(const C::AcquisitionCandidate& source,const Reader& reader)
{
    if(source.sourceSchema.View()!="NGX"||source.header.owner!=C::OwnerDomain::Provider)return false;
    const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
    if(!ResolveList(source.claims,reader,claims)||!claims)return false;
    const auto exactly=[&](std::string_view field,const C::ScalarValue& expected){
        unsigned count=0;
        for(const auto& claim:*claims)if(claim.field.View()==field)
        {
            if(!Established(claim.raw)||claim.raw.KnownPart()->value!=expected||
               !claim.effective.UnknownPart()||*claim.effective.UnknownPart()!=C::UnknownFact{}||
               !claim.overrideSource.UnknownPart()||*claim.overrideSource.UnknownPart()!=C::UnknownFact{})return false;
            ++count;
        }
        return count==1;
    };
    C::Symbol api,boundary;api.Assign("D3D12");boundary.Assign("evaluate");
    return exactly("source.api",C::ScalarValue{api})&&exactly("source.boundary",C::ScalarValue{boundary})&&
        exactly("adapter.version",C::ScalarValue{std::uint64_t{1}});
}
}
