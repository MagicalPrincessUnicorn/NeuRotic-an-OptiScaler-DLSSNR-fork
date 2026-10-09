#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "SemanticDescriptions.h"

namespace Neurotic::Contracts
{
enum class DiagnosticDrop : std::uint32_t
{
    None,
    QueueFull,
    Contention,
    ValidationRejected,
    ExportRejected,
    OwnerUnavailable,
    UnsupportedReceipt,
    MissingAssociation,
    MissingGeneration,
    Disabled,
    TransportUnavailable,
    Saturated
};
template<> struct EnumTraits<DiagnosticDrop>
{
    inline static constexpr auto Values = std::array {
        std::pair {DiagnosticDrop::None, std::string_view {"None"}},
        std::pair {DiagnosticDrop::QueueFull, std::string_view {"QueueFull"}},
        std::pair {DiagnosticDrop::Contention, std::string_view {"Contention"}},
        std::pair {DiagnosticDrop::ValidationRejected, std::string_view {"ValidationRejected"}},
        std::pair {DiagnosticDrop::ExportRejected, std::string_view {"ExportRejected"}},
        std::pair {DiagnosticDrop::OwnerUnavailable, std::string_view {"OwnerUnavailable"}},
        std::pair {DiagnosticDrop::UnsupportedReceipt, std::string_view {"UnsupportedReceipt"}},
        std::pair {DiagnosticDrop::MissingAssociation, std::string_view {"MissingAssociation"}},
        std::pair {DiagnosticDrop::MissingGeneration, std::string_view {"MissingGeneration"}},
        std::pair {DiagnosticDrop::Disabled, std::string_view {"Disabled"}},
        std::pair {DiagnosticDrop::TransportUnavailable, std::string_view {"TransportUnavailable"}},
        std::pair {DiagnosticDrop::Saturated, std::string_view {"Saturated"}}
    };
};

struct DiagnosticCoverage
{
    OptionalFact<std::uint64_t> produced {};
    OptionalFact<std::uint64_t> accepted {};
    OptionalFact<std::uint64_t> dropped {};
    MetadataList<Symbol, 16> missingOwners {};
    OptionalFact<bool> complete {};
    OptionalFact<bool> saturated {};
    inline static constexpr std::string_view WireName = "DiagnosticCoverage";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("produced", &DiagnosticCoverage::produced),
            Field("accepted", &DiagnosticCoverage::accepted),
            Field("dropped", &DiagnosticCoverage::dropped),
            Field("missingOwners", &DiagnosticCoverage::missingOwners),
            Field("complete", &DiagnosticCoverage::complete),
            Field("saturated", &DiagnosticCoverage::saturated)
        };
    }
    bool operator==(const DiagnosticCoverage&) const = default;
};

struct OwnerFact
{
    Symbol field {};
    OptionalFact<ScalarValue> value {};
    inline static constexpr std::string_view WireName = "OwnerFact";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("field", &OwnerFact::field),
            Field("value", &OwnerFact::value)
        };
    }
    Error Check() const
    {
        return field.Empty() ? Error::MissingField : Error::None;
    }
    bool operator==(const OwnerFact&) const = default;
};

struct MirroredOwnerFact
{
    OwnerDomain owner = OwnerDomain::Unspecified;
    RecordKey publisher {};
    Symbol field {};
    OptionalFact<ScalarValue> value {};
    inline static constexpr std::string_view WireName = "MirroredOwnerFact";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("owner", &MirroredOwnerFact::owner),
            Field("publisher", &MirroredOwnerFact::publisher),
            Field("field", &MirroredOwnerFact::field),
            Field("value", &MirroredOwnerFact::value)
        };
    }
    Error Check() const
    {
        return owner == OwnerDomain::Unspecified || field.Empty() ? Error::MissingProvenance : Error::None;
    }
    bool operator==(const MirroredOwnerFact&) const = default;
};

// Operational-owner-authored record. Deserialization preserves a claim; it cannot authenticate or authorize the issuer.
struct OwnerReceipt
{
    RecordHeader header = RecordHeader {ContractId::C11};
    std::uint64_t ownerSequence {};
    Symbol operation {};
    MetadataList<RecordReference, 32> causalRecords {};
    MetadataList<OwnerFact, 16> facts {};
    MetadataRef<GenerationVector> relevantGenerations {};
    OptionalFact<MetadataRef<ResourceIdentityToken>> resourceIdentity {};
    inline static constexpr std::string_view WireName = "OwnerReceipt";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &OwnerReceipt::header),
            Field("ownerSequence", &OwnerReceipt::ownerSequence),
            Field("operation", &OwnerReceipt::operation),
            Field("causalRecords", &OwnerReceipt::causalRecords),
            Field("facts", &OwnerReceipt::facts),
            Field("relevantGenerations", &OwnerReceipt::relevantGenerations),
            Field("resourceIdentity", &OwnerReceipt::resourceIdentity)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C11) != Error::None)
            return Error::WrongRecordType;
        if (header.owner == OwnerDomain::Diagnostics || header.owner == OwnerDomain::Unspecified)
            return Error::WrongOwner;
        if (operation.Empty())
            return Error::MissingField;
        return Error::None;
    }
    bool operator==(const OwnerReceipt&) const = default;
};

// Bounded values, not transport. Enqueue, drop counters, formatting and logging are DIAG work, not ARCH work.
struct DiagnosticEvent
{
    RecordHeader header = RecordHeader {ContractId::C11};
    std::uint64_t producerSequence {};
    OptionalFact<MetadataRef<Measurement>> timestamp {};
    Symbol eventType {};
    MetadataList<RecordReference, 32> causalRecords {};
    OptionalFact<ReasonId> reason {};
    MetadataRef<GenerationVector> relevantGenerations {};
    OptionalFact<RecordReference> ownerReceipt {};
    MetadataList<MirroredOwnerFact, 16> mirroredFacts {};
    OptionalFact<MetadataRef<ResourceIdentityToken>> resourceIdentity {};
    MetadataRef<FrameIdentity> associations {};
    MetadataRef<DiagnosticCoverage> coverage {};
    DiagnosticDrop drop = DiagnosticDrop::None;
    MetadataList<OwnerFact, 16> envelopeFacts {};
    inline static constexpr std::string_view WireName = "DiagnosticEvent";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &DiagnosticEvent::header),
            Field("producerSequence", &DiagnosticEvent::producerSequence),
            Field("timestamp", &DiagnosticEvent::timestamp),
            Field("eventType", &DiagnosticEvent::eventType),
            Field("causalRecords", &DiagnosticEvent::causalRecords),
            Field("reason", &DiagnosticEvent::reason),
            Field("relevantGenerations", &DiagnosticEvent::relevantGenerations),
            Field("ownerReceipt", &DiagnosticEvent::ownerReceipt),
            Field("mirroredFacts", &DiagnosticEvent::mirroredFacts),
            Field("resourceIdentity", &DiagnosticEvent::resourceIdentity),
            Field("associations", &DiagnosticEvent::associations),
            Field("coverage", &DiagnosticEvent::coverage),
            Field("drop", &DiagnosticEvent::drop),
            Field("envelopeFacts", &DiagnosticEvent::envelopeFacts)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C11) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Diagnostics) != Error::None)
            return Error::WrongOwner;
        if (eventType.Empty())
            return Error::MissingField;
        if (const auto* receipt = ownerReceipt.KnownPart())
        {
            if (receipt->value.contract != ContractId::C11 ||
                receipt->value.recordType.View() != OwnerReceipt::WireName)
                return Error::WrongRecordType;
        }
        return Error::None;
    }
    bool operator==(const DiagnosticEvent&) const = default;
};

} // namespace Neurotic::Contracts
