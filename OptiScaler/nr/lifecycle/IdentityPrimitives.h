#pragma once
// NR-LIFE-001 candidate. Unapplied; compilation and tests NOT_RUN.
#include "../contracts/C14_Identity.h"
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string_view>

namespace Neurotic::Lifecycle
{
namespace C = Neurotic::Contracts;
// Local computation status, NOT a second C14 kind, a capability certificate, or a C15 verdict.
enum class IdentityStatus
{
    Ok, Unknown, Different, WrongOwner, WrongKind, InvalidEvidence, InvalidScope,
    InvalidRule, StaleEvidence, Exhausted, Busy, Closed, AlreadyOpen, NotNewReal
};
inline constexpr std::string_view StatusName(IdentityStatus value) noexcept
{
    switch (value)
    {
    case IdentityStatus::Ok: return "Ok";
    case IdentityStatus::Unknown: return "Unknown";
    case IdentityStatus::Different: return "Different";
    case IdentityStatus::WrongOwner: return "WrongOwner";
    case IdentityStatus::WrongKind: return "WrongKind";
    case IdentityStatus::InvalidEvidence: return "InvalidEvidence";
    case IdentityStatus::InvalidScope: return "InvalidScope";
    case IdentityStatus::InvalidRule: return "InvalidRule";
    case IdentityStatus::StaleEvidence: return "StaleEvidence";
    case IdentityStatus::Exhausted: return "Exhausted";
    case IdentityStatus::Busy: return "Busy";
    case IdentityStatus::Closed: return "Closed";
    case IdentityStatus::AlreadyOpen: return "AlreadyOpen";
    case IdentityStatus::NotNewReal: return "NotNewReal";
    }
    return "InvalidStatus";
}
template<class T> struct Outcome
{
    IdentityStatus status = IdentityStatus::Unknown;
    C::OptionalFact<T> value {};
};
inline C::UnknownFact Missing(IdentityStatus status, const C::ScopeRef& scope = {})
{
    C::UnknownFact u;
    u.scope = scope;
    u.reason.category = C::ReasonCategory::InvalidInvariant;
    u.reason.code = C::UnknownReason::Malformed;
    if (status == IdentityStatus::Unknown)
    {
        u.reason.category = C::ReasonCategory::Untested;
        u.reason.code = C::UnknownReason::AssociationUnproven;
    }
    else if (status == IdentityStatus::Different || status == IdentityStatus::StaleEvidence)
        u.reason.code = C::UnknownReason::Expired;
    else if (status == IdentityStatus::Closed)
        u.reason.code = C::UnknownReason::Revoked;
    else if (status == IdentityStatus::Busy || status == IdentityStatus::Exhausted)
    {
        u.reason.category = C::ReasonCategory::BudgetReduction;
        u.reason.code = C::UnknownReason::OwnerUnpublished;
    }
    C::Symbol code;
    // StatusName contains only Symbol-safe characters and has a fixed small maximum.
    if (code.Assign(StatusName(status))) u.reason.ownerCode = code;
    return u;
}
template<class T> Outcome<T> Refuse(IdentityStatus status, const C::ScopeRef& scope = {})
{
    return {status,C::OptionalFact<T>::FromUnknown(Missing(status,scope))};
}
template<class T> Outcome<T> PreserveUnknown(const C::UnknownFact& unknown)
{
    return {IdentityStatus::Unknown,C::OptionalFact<T>::FromUnknown(unknown)};
}
template<class T> Outcome<T> Established(const T& value, const C::EvidenceRef& evidence)
{
    return {IdentityStatus::Ok,C::OptionalFact<T>::FromKnown(value,evidence)};
}
inline bool ValidIdentity(const C::StableIdentity& id) noexcept
{
    return !C::EnumName(id.kind).empty() && id.Check()==C::Error::None;
}
inline bool ValidEvidence(const C::EvidenceRef& evidence) noexcept
{
    return evidence.record.Check()==C::Error::None;
}
// Ignore evidence publication IDs when comparing the semantic identity values themselves.
inline IdentityStatus CompareIdentity(const C::OptionalFact<C::StableIdentity>& a,
                                      const C::OptionalFact<C::StableIdentity>& b)
{
    if (!a.IsKnown() || !b.IsKnown()) return IdentityStatus::Unknown;
    const auto& left=a.KnownPart()->value;
    const auto& right=b.KnownPart()->value;
    if (!ValidIdentity(left) || !ValidIdentity(right)) return IdentityStatus::InvalidScope;
    return left==right ? IdentityStatus::Ok : IdentityStatus::Different;
}
template<class T> IdentityStatus CompareTypedIdentity(const C::OptionalFact<T>& a,
                                                      const C::OptionalFact<T>& b)
{
    if (!a.IsKnown() || !b.IsKnown()) return IdentityStatus::Unknown;
    const auto& left=a.KnownPart()->value;
    const auto& right=b.KnownPart()->value;
    if (!ValidIdentity(left.Describe()) || !ValidIdentity(right.Describe())) return IdentityStatus::InvalidScope;
    return left==right ? IdentityStatus::Ok : IdentityStatus::Different;
}
// An arithmetic primitive only: it neither names an identity kind nor advances any live counter.
// No wrap, zero skip, automatic namespace restart, or recovery by reusing old tokens.
inline IdentityStatus CheckedSuccessor(std::uint64_t current,std::uint64_t ceiling,std::uint64_t& output) noexcept
{
    if (ceiling==0 || current>=ceiling) return IdentityStatus::Exhausted;
    output=current+1; // current < ceiling <= UINT64_MAX proves representability.
    return IdentityStatus::Ok; // Arithmetic only; no Known fact or owner evidence is invented.
}
struct OwnerBinding
{
    C::OwnerDomain owner = C::OwnerDomain::Unspecified;
    C::RecordKey publisher {};
    C::RecordKey subject {};
    C::Symbol evidenceNamespace {};
    bool operator==(const OwnerBinding&) const = default;
};
struct OwnerEvent
{
    C::OwnerDomain owner = C::OwnerDomain::Unspecified;
    C::RecordKey publisher {};
    C::RecordKey subject {};
    C::EvidenceRef evidence {};
};
inline bool ValidBinding(const OwnerBinding& binding) noexcept
{
    return binding.owner!=C::OwnerDomain::Unspecified && !C::EnumName(binding.owner).empty() &&
        binding.publisher.Check()==C::Error::None && binding.subject.Check()==C::Error::None &&
        !binding.evidenceNamespace.Empty();
}
inline IdentityStatus CheckOwnerEvent(const OwnerBinding& binding,const OwnerEvent& event) noexcept
{
    if (!ValidBinding(binding)) return IdentityStatus::InvalidScope;
    if (event.owner!=binding.owner || event.publisher!=binding.publisher) return IdentityStatus::WrongOwner;
    if (event.subject!=binding.subject) return IdentityStatus::InvalidScope;
    if (!ValidEvidence(event.evidence) || event.evidence.record.nameSpace!=binding.evidenceNamespace ||
        event.evidence.record.issuer!=binding.publisher.issuer) return IdentityStatus::InvalidEvidence;
    return IdentityStatus::Ok;
}
// Known scope fields must be supplied by the topology/identity owner. These are canonical wrappers,
// not new ID types. OptionalFact preserves absence; no default view, stream, or incarnation exists.
struct ScopeSnapshot
{
    C::OptionalFact<C::SessionId> session {};
    C::OptionalFact<C::RenderStreamId> stream {};
    C::OptionalFact<C::ViewId> view {};
    C::OptionalFact<C::ObjectIncarnation> topologyIncarnation {};
};
// Establishment validates each retained Known envelope; semantic comparison below deliberately
// ignores evidence-publication identity. A valid edge event cannot repair missing topology evidence.
template<class T> IdentityStatus CheckEstablishedScopeFact(const C::OptionalFact<T>& fact)
{
    if (!fact.IsKnown()) return IdentityStatus::Unknown;
    const auto& known=*fact.KnownPart();
    if (!ValidIdentity(known.value.Describe())) return IdentityStatus::InvalidScope;
    if (!ValidEvidence(known.evidence)) return IdentityStatus::InvalidEvidence;
    return IdentityStatus::Ok;
}
inline IdentityStatus CheckEstablishedScope(const ScopeSnapshot& scope,bool requireTopologyIncarnation)
{
    for (const auto status : {CheckEstablishedScopeFact(scope.session),CheckEstablishedScopeFact(scope.stream),
                              CheckEstablishedScopeFact(scope.view)})
        if (status!=IdentityStatus::Ok) return status;
    // Optional Unknown topology is permitted. Any supplied Known topology is still validated,
    // particularly when the complete scope is retained by an imported generated-owner fact.
    return requireTopologyIncarnation || scope.topologyIncarnation.IsKnown()
        ? CheckEstablishedScopeFact(scope.topologyIncarnation) : IdentityStatus::Ok;
}
inline IdentityStatus CompareScope(const ScopeSnapshot& expected,const ScopeSnapshot& observed,
                                   bool requireTopologyIncarnation)
{
    for (const auto status : {CompareTypedIdentity(expected.session,observed.session),
                              CompareTypedIdentity(expected.stream,observed.stream),
                              CompareTypedIdentity(expected.view,observed.view)})
        if (status!=IdentityStatus::Ok) return status;
    return requireTopologyIncarnation ? CompareTypedIdentity(expected.topologyIncarnation,observed.topologyIncarnation)
                                      : IdentityStatus::Ok;
}
} // namespace Neurotic::Lifecycle
