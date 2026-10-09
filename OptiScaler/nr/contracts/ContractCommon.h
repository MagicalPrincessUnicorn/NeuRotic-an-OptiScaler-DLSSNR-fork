#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Neurotic::Contracts
{
// Structural errors only: none of these checks grants a right or resolves a semantic dispute.
enum class Error
{
    None, Malformed, LimitExceeded, Overflow, UnknownEnum, WrongValueType,
    DuplicateField, UnknownField, MissingField, UnsupportedVersion, WrongRecordType,
    MissingProvenance, WrongOwner, WrongIdentityKind, DuplicateEntry, ContradictoryEntry,
    NonFinite, UnsafeExtension, AllocationFailure
};

template<class E> struct EnumTraits;
template<class E> constexpr std::string_view EnumName(E value)
{
    for (const auto& entry : EnumTraits<E>::Values)
        if (entry.first == value)
            return entry.second;
    return {};
}
template<class E> constexpr bool ParseEnum(std::string_view text, E& out)
{
    for (const auto& entry : EnumTraits<E>::Values)
        if (entry.second == text)
        {
            out = entry.first;
            return true;
        }
    return false;
}

template<class C, class T> struct Member
{
    std::string_view name;
    T C::* pointer;
};
template<class C, class T> constexpr auto Field(std::string_view name, T C::* pointer)
{
    return Member<C, T> {name, pointer};
}

// Capacities below are candidate engineering limits, not G1 constants or runtime feature ceilings.
template<std::size_t N> class Text
{
    std::array<char, N> bytes_ {};
    std::size_t size_ = 0;
  public:
    static constexpr std::size_t Capacity = N;
    bool Assign(std::string_view text) noexcept
    {
        if (text.size() > N || text.find('\0') != std::string_view::npos)
            return false;
        // Stage before mutation: text may be this object's entire view or an overlapping subview.
        std::array<char, N> replacement {};
        std::copy(text.begin(), text.end(), replacement.begin());
        bytes_ = replacement;
        size_ = text.size();
        return true;
    }
    std::string_view View() const noexcept { return {bytes_.data(), size_}; }
    bool operator==(const Text& other) const noexcept { return View() == other.View(); }
};

class Symbol
{
    Text<96> text_;
  public:
    bool Assign(std::string_view text) noexcept
    {
        if (text.empty())
            return false;
        for (const unsigned char c : text)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':'))
                return false;
        return text_.Assign(text);
    }
    std::string_view View() const noexcept { return text_.View(); }
    bool Empty() const noexcept { return View().empty(); }
    bool operator==(const Symbol&) const = default;
};

template<class T, std::size_t N> class BoundedList
{
    std::array<T, N> values_ {};
    std::size_t size_ = 0;
  public:
    using value_type = T;
    static constexpr std::size_t Capacity = N;
    bool Push(const T& value)
    {
        if (size_ == N)
            return false;
        values_[size_] = value;
        ++size_;
        return true;
    }
    // Mutable construction only. A failed decode discards its private destination.
    T* AppendDefaultForConstruction()
    {
        if (size_ == N) return nullptr;
        return &values_[size_++];
    }
    std::size_t Size() const noexcept { return size_; }
    const T* Get(std::size_t index) const noexcept { return index < size_ ? &values_[index] : nullptr; }
    T* Get(std::size_t index) noexcept { return index < size_ ? &values_[index] : nullptr; }
    const T* begin() const noexcept { return values_.data(); }
    const T* end() const noexcept { return values_.data() + size_; }
    T* begin() noexcept { return values_.data(); }
    T* end() noexcept { return values_.data() + size_; }
    bool operator==(const BoundedList& other) const
    {
        return size_ == other.size_ && std::equal(begin(), end(), other.begin());
    }
};

inline Error ParseDecimal(std::string_view text, std::uint64_t& out) noexcept
{
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text.front() == '0'))
        return Error::Malformed;
    std::uint64_t value = 0;
    for (const unsigned char c : text)
    {
        if (c < '0' || c > '9')
            return Error::Malformed;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10)
            return Error::Overflow;
        value = value * 10 + digit;
    }
    out = value; // Failure leaves the caller's destination unchanged.
    return Error::None;
}
inline std::string Decimal(std::uint64_t value)
{
    std::array<char, 20> buffer {};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), result.ptr);
}
enum class ContractId : std::uint32_t
{
    C01,
    C02,
    C03,
    C04,
    C05,
    C06,
    C07,
    C08,
    C09,
    C10,
    C11,
    C12,
    C13,
    C14,
    C15,
    C16
};
template<> struct EnumTraits<ContractId>
{
    inline static constexpr auto Values = std::array {
        std::pair {ContractId::C01, std::string_view {"C01"}},
        std::pair {ContractId::C02, std::string_view {"C02"}},
        std::pair {ContractId::C03, std::string_view {"C03"}},
        std::pair {ContractId::C04, std::string_view {"C04"}},
        std::pair {ContractId::C05, std::string_view {"C05"}},
        std::pair {ContractId::C06, std::string_view {"C06"}},
        std::pair {ContractId::C07, std::string_view {"C07"}},
        std::pair {ContractId::C08, std::string_view {"C08"}},
        std::pair {ContractId::C09, std::string_view {"C09"}},
        std::pair {ContractId::C10, std::string_view {"C10"}},
        std::pair {ContractId::C11, std::string_view {"C11"}},
        std::pair {ContractId::C12, std::string_view {"C12"}},
        std::pair {ContractId::C13, std::string_view {"C13"}},
        std::pair {ContractId::C14, std::string_view {"C14"}},
        std::pair {ContractId::C15, std::string_view {"C15"}},
        std::pair {ContractId::C16, std::string_view {"C16"}}
    };
};

enum class OwnerDomain : std::uint32_t
{
    Unspecified,
    Session,
    IdentityRegistry,
    Topology,
    Provider,
    Acquisition,
    Context,
    Resource,
    ColorContinuity,
    RayReconstruction,
    OrchestratorPolicy,
    StreamCoordinator,
    RenderingProtocol,
    Strategy,
    History,
    ResetRules,
    Multipass,
    FrameGeneration,
    Finalizer,
    Presentation,
    Diagnostics,
    Configuration,
    Validation,
    Performance
};
template<> struct EnumTraits<OwnerDomain>
{
    inline static constexpr auto Values = std::array {
        std::pair {OwnerDomain::Unspecified, std::string_view {"Unspecified"}},
        std::pair {OwnerDomain::Session, std::string_view {"Session"}},
        std::pair {OwnerDomain::IdentityRegistry, std::string_view {"IdentityRegistry"}},
        std::pair {OwnerDomain::Topology, std::string_view {"Topology"}},
        std::pair {OwnerDomain::Provider, std::string_view {"Provider"}},
        std::pair {OwnerDomain::Acquisition, std::string_view {"Acquisition"}},
        std::pair {OwnerDomain::Context, std::string_view {"Context"}},
        std::pair {OwnerDomain::Resource, std::string_view {"Resource"}},
        std::pair {OwnerDomain::ColorContinuity, std::string_view {"ColorContinuity"}},
        std::pair {OwnerDomain::RayReconstruction, std::string_view {"RayReconstruction"}},
        std::pair {OwnerDomain::OrchestratorPolicy, std::string_view {"OrchestratorPolicy"}},
        std::pair {OwnerDomain::StreamCoordinator, std::string_view {"StreamCoordinator"}},
        std::pair {OwnerDomain::RenderingProtocol, std::string_view {"RenderingProtocol"}},
        std::pair {OwnerDomain::Strategy, std::string_view {"Strategy"}},
        std::pair {OwnerDomain::History, std::string_view {"History"}},
        std::pair {OwnerDomain::ResetRules, std::string_view {"ResetRules"}},
        std::pair {OwnerDomain::Multipass, std::string_view {"Multipass"}},
        std::pair {OwnerDomain::FrameGeneration, std::string_view {"FrameGeneration"}},
        std::pair {OwnerDomain::Finalizer, std::string_view {"Finalizer"}},
        std::pair {OwnerDomain::Presentation, std::string_view {"Presentation"}},
        std::pair {OwnerDomain::Diagnostics, std::string_view {"Diagnostics"}},
        std::pair {OwnerDomain::Configuration, std::string_view {"Configuration"}},
        std::pair {OwnerDomain::Validation, std::string_view {"Validation"}},
        std::pair {OwnerDomain::Performance, std::string_view {"Performance"}}
    };
};

// Observed external version numbers are descriptive; they are not constrained to the canonical schema major.
struct VersionNumber
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    inline static constexpr std::string_view WireName = "VersionNumber";
    static constexpr auto Fields()
    {
        return std::tuple {Field("major", &VersionNumber::major), Field("minor", &VersionNumber::minor)};
    }
    bool operator==(const VersionNumber&) const = default;
};

struct SchemaVersion
{
    std::uint32_t major = 1;
    std::uint32_t minor = 0;
    inline static constexpr std::string_view WireName = "SchemaVersion";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("major", &SchemaVersion::major),
            Field("minor", &SchemaVersion::minor)
        };
    }
    Error Check() const
    {
        return major == 1 ? Error::None : Error::UnsupportedVersion;
    }
    bool operator==(const SchemaVersion&) const = default;
};

// A scoped reference supplied by its owner, not an allocator or an actionable handle. Zero is legal when explicitly supplied.
struct RecordKey
{
    Symbol nameSpace {};
    Symbol issuer {};
    std::uint64_t value {};
    inline static constexpr std::string_view WireName = "RecordKey";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("nameSpace", &RecordKey::nameSpace),
            Field("issuer", &RecordKey::issuer),
            Field("value", &RecordKey::value)
        };
    }
    Error Check() const
    {
        return nameSpace.Empty() || issuer.Empty() ? Error::MissingProvenance : Error::None;
    }
    bool operator==(const RecordKey&) const = default;
};

// Explicit Unknown scope without fabricating a scope ID. Fixture codec preserves its discriminant.
struct ScopeRef
{
    std::optional<RecordKey> key;
    bool operator==(const ScopeRef&) const = default;
};
// Construction is mutable; publication is through const owner snapshots. No constructor issues identity or authority.
struct RecordHeader
{
    ContractId contract = ContractId::C01;
    SchemaVersion schemaVersion {};
    OwnerDomain owner = OwnerDomain::Unspecified;
    RecordKey record {};
    std::uint64_t revision {};
    ScopeRef scope {};
    inline static constexpr std::string_view WireName = "RecordHeader";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("contract", &RecordHeader::contract),
            Field("schemaVersion", &RecordHeader::schemaVersion),
            Field("owner", &RecordHeader::owner),
            Field("record", &RecordHeader::record),
            Field("revision", &RecordHeader::revision),
            Field("scope", &RecordHeader::scope)
        };
    }
    Error Check() const
    {
        if (owner == OwnerDomain::Unspecified || !scope.key)
            return Error::MissingProvenance;
        return Error::None;
    }
    bool operator==(const RecordHeader&) const = default;
};

struct RecordReference
{
    ContractId contract = ContractId::C01;
    Symbol recordType {};
    RecordKey record {};
    std::uint64_t revision {};
    inline static constexpr std::string_view WireName = "RecordReference";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("contract", &RecordReference::contract),
            Field("recordType", &RecordReference::recordType),
            Field("record", &RecordReference::record),
            Field("revision", &RecordReference::revision)
        };
    }
    Error Check() const
    {
        return recordType.Empty() ? Error::MissingProvenance : Error::None;
    }
    bool operator==(const RecordReference&) const = default;
};

// Non-contract owner data (for example CFG intent/settings). This is a reference, not a second identity system.
struct OwnerValueReference
{
    OwnerDomain owner = OwnerDomain::Unspecified;
    Symbol recordType {};
    VersionNumber schemaVersion {};
    RecordKey record {};
    std::uint64_t revision {};
    inline static constexpr std::string_view WireName = "OwnerValueReference";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("owner", &OwnerValueReference::owner),
            Field("recordType", &OwnerValueReference::recordType),
            Field("schemaVersion", &OwnerValueReference::schemaVersion),
            Field("record", &OwnerValueReference::record),
            Field("revision", &OwnerValueReference::revision)
        };
    }
    Error Check() const
    {
        return owner == OwnerDomain::Unspecified || recordType.Empty() ? Error::MissingProvenance : Error::None;
    }
    bool operator==(const OwnerValueReference&) const = default;
};

// Typed reference to immutable CPU metadata published by the named owner. It stores no pointer,
// allocator, refcount, GPU object, lease or lookup function. The existing RecordKey identifies the
// publication; this is not a new C14 identity kind. Missing backing metadata never means a default.
// The publisher owns a bounded, initialization-provisioned metadata store and its revision lifetime.
// ARCH declares that seam only. Fixtures carry every referenced metadata body in a bounded closure.
template<class T> struct MetadataRef
{
    using value_type = T;
    OwnerDomain owner = OwnerDomain::Unspecified;
    RecordKey record {};
    std::uint64_t revision = 0;
    SchemaVersion schemaVersion {};
    static constexpr auto Fields()
    {
        return std::tuple {Field("owner", &MetadataRef::owner), Field("record", &MetadataRef::record),
                           Field("revision", &MetadataRef::revision), Field("schemaVersion", &MetadataRef::schemaVersion)};
    }
    Error Check() const
    {
        return owner == OwnerDomain::Unspecified ? Error::MissingProvenance : Error::None;
    }
    bool operator==(const MetadataRef&) const = default;
};

// Empty is an explicit known-empty collection, not an unobserved optional fact. Nonempty contents
// live in one immutable bounded metadata body. count must equal its body size during fixture/owner
// resolution; count alone grants neither completeness nor access. Capacities retain the prior draft.
template<class T, std::size_t N> struct MetadataList
{
    using value_type = T;
    using storage_type = BoundedList<T, N>;
    static constexpr std::size_t Capacity = N;
    std::uint32_t count = 0;
    std::optional<MetadataRef<storage_type>> backing;
    static constexpr auto Fields()
    {
        return std::tuple {Field("count", &MetadataList::count), Field("backing", &MetadataList::backing)};
    }
    Error Check() const
    {
        if (count > N) return Error::LimitExceeded;
        if ((count == 0) != !backing.has_value()) return Error::Malformed;
        return Error::None;
    }
    bool operator==(const MetadataList&) const = default;
};

// Immutable storage for an owner-selected value. Construction does not authenticate, validate or publish it.
template<class T> class ImmutableRecord
{
    const T value_;
  public:
    explicit ImmutableRecord(const T& value) : value_(value) {}
    explicit ImmutableRecord(T&& value) : value_(std::move(value)) {}
    const T& Value() const noexcept { return value_; }
    ImmutableRecord(const ImmutableRecord&) = default;
    ImmutableRecord& operator=(const ImmutableRecord&) = delete;
};

template<ContractId C> struct ContractRef
{
    Symbol recordType;
    RecordKey record;
    std::uint64_t revision = 0;
    inline static constexpr ContractId Contract = C;
    static constexpr auto Fields()
    {
        return std::tuple {Field("recordType", &ContractRef::recordType),
                           Field("record", &ContractRef::record), Field("revision", &ContractRef::revision)};
    }
    Error Check() const { return recordType.Empty() ? Error::MissingProvenance : Error::None; }
    bool operator==(const ContractRef&) const = default;
};
// This marker is a freshness value, never an IdentityKind or a GenerationToken.
struct ContentRevision
{
    std::uint64_t value = 0;
    bool operator==(const ContentRevision&) const = default;
};
inline Error CheckHeader(const RecordHeader& header, ContractId expected)
{
    return header.contract == expected ? Error::None : Error::WrongRecordType;
}
template<class... Owners> inline Error CheckOwner(OwnerDomain owner, Owners... allowed)
{
    return ((owner == allowed) || ...) ? Error::None : Error::WrongOwner;
}
} // namespace Neurotic::Contracts
