#pragma once
// Callback-scoped views into immutable, owner-published CPU metadata. No resolver is retained.
#include "EvidenceQualification.h"
#include "../lifecycle/IdentityDependencies.h"

namespace Neurotic::Context
{
template<class T> struct MetadataView
{
    C::MetadataRef<T> reference;
    const T* value=nullptr;
};

// Native callers have not necessarily passed through the fixture decoder. Validate the
// same canonical value shapes without serialization, allocation, or semantic defaults.
template<class T> bool ValidValues(const T& value)
{
    if constexpr (requires {value.Check();}) if (value.Check()!=C::Error::None) return false;
    if constexpr (std::is_floating_point_v<T>) return std::isfinite(value);
    else if constexpr (std::is_integral_v<T>) return true;
    else if constexpr (std::is_enum_v<T>) return !C::EnumName(value).empty();
    else if constexpr (requires {value.KnownPart();value.UnknownPart();})
    {
        if (const auto* known=value.KnownPart())
            return Lifecycle::ValidEvidence(known->evidence) && ValidValues(known->value);
        return ValidValues(*value.UnknownPart());
    }
    else if constexpr (requires {value.has_value();*value;}) return !value || ValidValues(*value);
    else if constexpr (requires {value.index();})
        return !value.valueless_by_exception() && std::visit([](const auto& part){return ValidValues(part);},value);
    else if constexpr (requires {T::Fields();})
        return std::apply([&](const auto&... field){return (ValidValues(value.*(field.pointer)) && ...);},T::Fields());
    else if constexpr (std::is_same_v<T,C::ScopeRef>) return !value.key || ValidValues(*value.key);
    else if constexpr (requires {value.Describe();}) return Lifecycle::ValidIdentity(value.Describe());
    else if constexpr (requires {value.Entries();})
    {
        for (const auto& entry:value.Entries()) if (!ValidValues(entry)) return false;
        return true;
    }
    else if constexpr (requires {value.begin();value.end();})
    {
        for (const auto& entry:value) if (!ValidValues(entry)) return false;
        return true;
    }
    else if constexpr (requires {value.View();}) return true; // bounded text already enforces its representation
    else if constexpr (std::is_same_v<T,C::ContentRevision>) return true;
    else static_assert(sizeof(T)==0,"Add explicit validation for this canonical value type");
}

// Reader.Resolve returns both the actual publication identity and the immutable body.
// Exact equality prevents a caller accidentally supplying a different revision/owner/body.
// Authentication and snapshot stability remain the source owner's responsibility.
template<class T,class Reader> const T* ResolveMetadata(const C::MetadataRef<T>& reference,const Reader& reader)
{
    if (!Lifecycle::ValidMetadataDescriptor(reference)) return nullptr;
    const auto resolved=reader.Resolve(reference);
    if (resolved.reference!=reference || !resolved.value || !ValidValues(*resolved.value)) return nullptr;
    return resolved.value;
}
template<class T,std::size_t N,class Reader>
bool ResolveList(const C::MetadataList<T,N>& reference,const Reader& reader,const C::BoundedList<T,N>*& output)
{
    output=nullptr;
    if (reference.Check()!=C::Error::None) return false;
    if (!reference.backing) return true;
    const auto* body=ResolveMetadata(*reference.backing,reader);
    if (!body || body->Size()!=reference.count) return false;
    output=body;
    return true;
}
template<class T,class Reader> const T* ResolveOptional(const C::OptionalFact<C::MetadataRef<T>>& reference,const Reader& reader)
{
    return Established(reference)?ResolveMetadata(reference.KnownPart()->value,reader):nullptr;
}
// Compare semantic values separately from their provenance. Equal facts published by
// two owners need not have identical EvidenceRefs; provenance remains in coherent C01.
template<class T> bool SemanticEqual(const T& a,const T& b)
{
    if constexpr (requires {a.KnownPart();})
        return a.IsKnown()==b.IsKnown() && (!a.IsKnown() || SemanticEqual(a.KnownPart()->value,b.KnownPart()->value));
    else if constexpr (requires {a.has_value();*a;})
        return a.has_value()==b.has_value() && (!a || SemanticEqual(*a,*b));
    else if constexpr (requires {T::Fields();})
        return std::apply([&](const auto&... f){return (SemanticEqual(a.*(f.pointer),b.*(f.pointer)) && ...);},T::Fields());
    else if constexpr (requires {a.Size();a.Get(0);})
    {
        if (a.Size()!=b.Size()) return false;
        for (std::size_t i=0;i<a.Size();++i) if (!SemanticEqual(*a.Get(i),*b.Get(i))) return false;
        return true;
    }
    else return a==b;
}
} // namespace Neurotic::Context
