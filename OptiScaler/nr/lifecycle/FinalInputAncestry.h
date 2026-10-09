#pragma once
#include <nr/contracts/C03_Consumption.h>
namespace Neurotic::Lifecycle
{
// Immutable description of a Resource-owned full-subresource copy. Construction
// is private to Resource; this handle grants no lease, release or recording right.
class FinalInputAncestry
{
    friend class NativeResourceRegistry;
    Contracts::ResourceView source_,endpoint_;
    Contracts::RecordKey recording_;
    std::uint64_t ordinal_=0;
    FinalInputAncestry(Contracts::ResourceView source,Contracts::ResourceView endpoint,
        Contracts::RecordKey recording,std::uint64_t ordinal):source_(std::move(source)),endpoint_(std::move(endpoint)),
        recording_(recording),ordinal_(ordinal){}
  public:
    const Contracts::ResourceView& Source()const noexcept{return source_;}
    const Contracts::ResourceView& Endpoint()const noexcept{return endpoint_;}
    const Contracts::RecordKey& Recording()const noexcept{return recording_;}
    std::uint64_t Ordinal()const noexcept{return ordinal_;}
    bool operator==(const FinalInputAncestry&)const=default;
};
}
