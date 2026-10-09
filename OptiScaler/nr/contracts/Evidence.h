#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "Reasons.h"
#include <variant>

namespace Neurotic::Contracts
{
enum class SourceClass : std::uint32_t
{
    Native,
    HostObserved,
    Discovered,
    Derived,
    Synthetic,
    External
};
template<> struct EnumTraits<SourceClass>
{
    inline static constexpr auto Values = std::array {
        std::pair {SourceClass::Native, std::string_view {"Native"}},
        std::pair {SourceClass::HostObserved, std::string_view {"HostObserved"}},
        std::pair {SourceClass::Discovered, std::string_view {"Discovered"}},
        std::pair {SourceClass::Derived, std::string_view {"Derived"}},
        std::pair {SourceClass::Synthetic, std::string_view {"Synthetic"}},
        std::pair {SourceClass::External, std::string_view {"External"}}
    };
};

enum class SemanticCertainty : std::uint32_t
{
    Claimed,
    Interpreted,
    Qualified,
    Contradictory
};
template<> struct EnumTraits<SemanticCertainty>
{
    inline static constexpr auto Values = std::array {
        std::pair {SemanticCertainty::Claimed, std::string_view {"Claimed"}},
        std::pair {SemanticCertainty::Interpreted, std::string_view {"Interpreted"}},
        std::pair {SemanticCertainty::Qualified, std::string_view {"Qualified"}},
        std::pair {SemanticCertainty::Contradictory, std::string_view {"Contradictory"}}
    };
};

enum class CoverageKind : std::uint32_t
{
    None,
    Partial,
    Complete
};
template<> struct EnumTraits<CoverageKind>
{
    inline static constexpr auto Values = std::array {
        std::pair {CoverageKind::None, std::string_view {"None"}},
        std::pair {CoverageKind::Partial, std::string_view {"Partial"}},
        std::pair {CoverageKind::Complete, std::string_view {"Complete"}}
    };
};

enum class DescriptiveAccess : std::uint32_t
{
    CallbackBorrow,
    OwnerRetained,
    OrderedReadClaim,
    WriteExclusiveClaim,
    RevokedClaim
};
template<> struct EnumTraits<DescriptiveAccess>
{
    inline static constexpr auto Values = std::array {
        std::pair {DescriptiveAccess::CallbackBorrow, std::string_view {"CallbackBorrow"}},
        std::pair {DescriptiveAccess::OwnerRetained, std::string_view {"OwnerRetained"}},
        std::pair {DescriptiveAccess::OrderedReadClaim, std::string_view {"OrderedReadClaim"}},
        std::pair {DescriptiveAccess::WriteExclusiveClaim, std::string_view {"WriteExclusiveClaim"}},
        std::pair {DescriptiveAccess::RevokedClaim, std::string_view {"RevokedClaim"}}
    };
};

// Reference to owner-published evidence. Existence of this value does not authenticate the referenced issuer.
struct EvidenceRef
{
    RecordKey record {};
    inline static constexpr std::string_view WireName = "EvidenceRef";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("record", &EvidenceRef::record)
        };
    }
    bool operator==(const EvidenceRef&) const = default;
};

struct UnknownFact
{
    ReasonId reason {};
    ScopeRef scope {};
    Text<256> detail {};
    inline static constexpr std::string_view WireName = "UnknownFact";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("reason", &UnknownFact::reason),
            Field("scope", &UnknownFact::scope),
            Field("detail", &UnknownFact::detail)
        };
    }
    bool operator==(const UnknownFact&) const = default;
};

template<class T> struct Known
{
    T value;
    EvidenceRef evidence;
    bool operator==(const Known&) const = default;
};

template<class T> class OptionalFact
{
    std::variant<UnknownFact, Known<T>> storage_ {UnknownFact {}};
  public:
    using value_type = T;
    static OptionalFact FromKnown(const T& value, const EvidenceRef& evidence)
    {
        OptionalFact result;
        result.storage_ = Known<T> {value, evidence};
        return result;
    }
    static OptionalFact FromUnknown(const UnknownFact& unknown)
    {
        OptionalFact result;
        result.storage_ = unknown;
        return result;
    }
    // Only construction storage is mutable; published owner snapshots remain const.
    Known<T>& EmplaceKnownForConstruction() { return storage_.template emplace<Known<T>>(); }
    UnknownFact& EmplaceUnknownForConstruction() { return storage_.template emplace<UnknownFact>(); }
    bool IsKnown() const noexcept { return std::holds_alternative<Known<T>>(storage_); }
    const Known<T>* KnownPart() const noexcept { return std::get_if<Known<T>>(&storage_); }
    const UnknownFact* UnknownPart() const noexcept { return std::get_if<UnknownFact>(&storage_); }
    bool operator==(const OptionalFact&) const = default;
    // Deliberately no implicit bool, dereference, value_or or conversion to T.
};
using ScalarValue = std::variant<bool, std::uint64_t, double, Symbol>;
// Exact publication/callback freshness. No conversion to real-frame age exists.
struct NativeSampleFreshnessV1
{
    std::uint32_t version=1;
    OwnerValueReference sample;
    RecordKey callback;
    inline static constexpr std::string_view WireName="NativeSampleFreshnessV1";
    static constexpr auto Fields()
    {
        return std::tuple{Field("version",&NativeSampleFreshnessV1::version),
            Field("sample",&NativeSampleFreshnessV1::sample),Field("callback",&NativeSampleFreshnessV1::callback)};
    }
    Error Check()const
    {
        if(version!=1||sample.schemaVersion!=VersionNumber{1,0})return Error::UnsupportedVersion;
        if(sample.owner!=OwnerDomain::Provider)return Error::WrongOwner;
        if(sample.recordType.View()!="NativeSampleIdentityV1"||!sample.revision)return Error::MissingProvenance;
        return Error::None;
    }
    bool operator==(const NativeSampleFreshnessV1&)const=default;
};
// Independent dimensions; no confidence aggregation, alias deduplication or NFC qualification is performed.
struct EvidenceVector
{
    OptionalFact<SourceClass> provenance {};
    OptionalFact<SemanticCertainty> semanticCertainty {};
    OptionalFact<std::uint64_t> ageInRealFrames {};
    std::optional<NativeSampleFreshnessV1> nativeFreshness;
    OptionalFact<CoverageKind> coverage {};
    OptionalFact<DescriptiveAccess> access {};
    BoundedList<RecordKey, 16> lineage {};
    OptionalFact<RecordKey> aliasOf {};
    OptionalFact<Symbol> scoreDefinition {};
    OptionalFact<double> providerScore {};
    inline static constexpr std::string_view WireName = "EvidenceVector";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("provenance", &EvidenceVector::provenance),
            Field("semanticCertainty", &EvidenceVector::semanticCertainty),
            Field("ageInRealFrames", &EvidenceVector::ageInRealFrames),
            Field("nativeFreshness", &EvidenceVector::nativeFreshness),
            Field("coverage", &EvidenceVector::coverage),
            Field("access", &EvidenceVector::access),
            Field("lineage", &EvidenceVector::lineage),
            Field("aliasOf", &EvidenceVector::aliasOf),
            Field("scoreDefinition", &EvidenceVector::scoreDefinition),
            Field("providerScore", &EvidenceVector::providerScore)
        };
    }
    Error Check()const
    {
        return nativeFreshness&&ageInRealFrames.IsKnown()?Error::ContradictoryEntry:Error::None;
    }
    bool operator==(const EvidenceVector&) const = default;
};

struct SemanticClaim
{
    Symbol field {};
    OptionalFact<ScalarValue> raw {};
    OptionalFact<ScalarValue> effective {};
    OptionalFact<OwnerValueReference> overrideSource {};
    inline static constexpr std::string_view WireName = "SemanticClaim";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("field", &SemanticClaim::field),
            Field("raw", &SemanticClaim::raw),
            Field("effective", &SemanticClaim::effective),
            Field("overrideSource", &SemanticClaim::overrideSource)
        };
    }
    Error Check() const
    {
        return field.Empty() ? Error::MissingField : Error::None;
    }
    bool operator==(const SemanticClaim&) const = default;
};

// The domain owner supplies the conflict relationship; ARCH neither chooses a winner nor erases evidence.
struct Contradiction
{
    Symbol field {};
    BoundedList<RecordKey, 16> claims {};
    ReasonId reason {};
    inline static constexpr std::string_view WireName = "Contradiction";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("field", &Contradiction::field),
            Field("claims", &Contradiction::claims),
            Field("reason", &Contradiction::reason)
        };
    }
    Error Check() const
    {
        return field.Empty() || claims.Size() < 2 ? Error::MissingField : Error::None;
    }
    bool operator==(const Contradiction&) const = default;
};

} // namespace Neurotic::Contracts
