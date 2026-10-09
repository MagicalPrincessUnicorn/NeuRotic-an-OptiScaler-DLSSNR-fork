#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "ContractCommon.h"

namespace Neurotic::Contracts
{
enum class ReasonCategory : std::uint32_t
{
    MissingImplementation,
    InvalidInvariant,
    KnownUnsafe,
    CoverageRestriction,
    Untested,
    QualityUnqualified,
    Degraded,
    OpaqueRuntimeFailure,
    BudgetReduction
};
template<> struct EnumTraits<ReasonCategory>
{
    inline static constexpr auto Values = std::array {
        std::pair {ReasonCategory::MissingImplementation, std::string_view {"MissingImplementation"}},
        std::pair {ReasonCategory::InvalidInvariant, std::string_view {"InvalidInvariant"}},
        std::pair {ReasonCategory::KnownUnsafe, std::string_view {"KnownUnsafe"}},
        std::pair {ReasonCategory::CoverageRestriction, std::string_view {"CoverageRestriction"}},
        std::pair {ReasonCategory::Untested, std::string_view {"Untested"}},
        std::pair {ReasonCategory::QualityUnqualified, std::string_view {"QualityUnqualified"}},
        std::pair {ReasonCategory::Degraded, std::string_view {"Degraded"}},
        std::pair {ReasonCategory::OpaqueRuntimeFailure, std::string_view {"OpaqueRuntimeFailure"}},
        std::pair {ReasonCategory::BudgetReduction, std::string_view {"BudgetReduction"}}
    };
};

enum class UnknownReason : std::uint32_t
{
    NotObserved,
    OwnerUnpublished,
    AssociationUnproven,
    ContradictoryEvidence,
    Expired,
    Revoked,
    Malformed,
    OutOfBounds,
    UnsupportedSchema,
    OpaqueRuntimeState
};
template<> struct EnumTraits<UnknownReason>
{
    inline static constexpr auto Values = std::array {
        std::pair {UnknownReason::NotObserved, std::string_view {"NotObserved"}},
        std::pair {UnknownReason::OwnerUnpublished, std::string_view {"OwnerUnpublished"}},
        std::pair {UnknownReason::AssociationUnproven, std::string_view {"AssociationUnproven"}},
        std::pair {UnknownReason::ContradictoryEvidence, std::string_view {"ContradictoryEvidence"}},
        std::pair {UnknownReason::Expired, std::string_view {"Expired"}},
        std::pair {UnknownReason::Revoked, std::string_view {"Revoked"}},
        std::pair {UnknownReason::Malformed, std::string_view {"Malformed"}},
        std::pair {UnknownReason::OutOfBounds, std::string_view {"OutOfBounds"}},
        std::pair {UnknownReason::UnsupportedSchema, std::string_view {"UnsupportedSchema"}},
        std::pair {UnknownReason::OpaqueRuntimeState, std::string_view {"OpaqueRuntimeState"}}
    };
};

struct ReasonId
{
    std::uint32_t version = 1;
    ReasonCategory category = ReasonCategory::Untested;
    UnknownReason code = UnknownReason::NotObserved;
    std::optional<Symbol> ownerCode; // Owner-defined stable code; null means only the shared coarse code is provided.
    inline static constexpr std::string_view WireName = "ReasonId";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("version", &ReasonId::version),
            Field("category", &ReasonId::category),
            Field("code", &ReasonId::code),
            Field("ownerCode", &ReasonId::ownerCode)
        };
    }
    Error Check() const
    {
        return version == 1 ? Error::None : Error::UnsupportedVersion;
    }
    bool operator==(const ReasonId&) const = default;
};

// Hard/soft is supplied by the owning rule, not computed by the reason taxonomy.
struct PolicyReason
{
    ReasonId reason {};
    Symbol rule {};
    std::uint32_t ruleVersion = 1;
    bool hard {};
    inline static constexpr std::string_view WireName = "PolicyReason";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("reason", &PolicyReason::reason),
            Field("rule", &PolicyReason::rule),
            Field("ruleVersion", &PolicyReason::ruleVersion),
            Field("hard", &PolicyReason::hard)
        };
    }
    bool operator==(const PolicyReason&) const = default;
};

} // namespace Neurotic::Contracts
