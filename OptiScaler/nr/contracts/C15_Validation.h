#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "SemanticDescriptions.h"

namespace Neurotic::Contracts
{
enum class Verdict : std::uint32_t
{
    PASS,
    FAIL,
    SKIP_UNSUPPORTED,
    INCONCLUSIVE,
    INFRA_ERROR
};
template<> struct EnumTraits<Verdict>
{
    inline static constexpr auto Values = std::array {
        std::pair {Verdict::PASS, std::string_view {"PASS"}},
        std::pair {Verdict::FAIL, std::string_view {"FAIL"}},
        std::pair {Verdict::SKIP_UNSUPPORTED, std::string_view {"SKIP_UNSUPPORTED"}},
        std::pair {Verdict::INCONCLUSIVE, std::string_view {"INCONCLUSIVE"}},
        std::pair {Verdict::INFRA_ERROR, std::string_view {"INFRA_ERROR"}}
    };
};

// Execution progress only. TEST assigns state/verdict from evidence; ARCH does not qualify a gate.
// Keep surviving draft ordinals; retired FailedToLaunch ordinal 3 is not reused.
// Launch failure detail belongs in AxisResult::reason (ReasonId::ownerCode), never in Verdict.
enum class ExecutionState : std::uint32_t
{
    NotRun = 0,
    Attempted = 4,
    Partial = 5,
    Completed = 1,
    BlockedByEnvironment = 2
};
template<> struct EnumTraits<ExecutionState>
{
    // Canonical TEST-003 serialized/display names; C++ names follow the candidate convention.
    inline static constexpr auto Values = std::array {
        std::pair {ExecutionState::NotRun, std::string_view {"NOT_RUN"}},
        std::pair {ExecutionState::Attempted, std::string_view {"ATTEMPTED"}},
        std::pair {ExecutionState::Partial, std::string_view {"PARTIAL"}},
        std::pair {ExecutionState::Completed, std::string_view {"COMPLETED"}},
        std::pair {ExecutionState::BlockedByEnvironment, std::string_view {"BLOCKED_BY_ENVIRONMENT"}}
    };
};

enum class DocumentEvidence : std::uint32_t
{
    BlueprintNormative,
    PackedSource,
    ReviewReconciled,
    MachineObserved,
    ExternalObserved
};
template<> struct EnumTraits<DocumentEvidence>
{
    inline static constexpr auto Values = std::array {
        std::pair {DocumentEvidence::BlueprintNormative, std::string_view {"BlueprintNormative"}},
        std::pair {DocumentEvidence::PackedSource, std::string_view {"PackedSource"}},
        std::pair {DocumentEvidence::ReviewReconciled, std::string_view {"ReviewReconciled"}},
        std::pair {DocumentEvidence::MachineObserved, std::string_view {"MachineObserved"}},
        std::pair {DocumentEvidence::ExternalObserved, std::string_view {"ExternalObserved"}}
    };
};

enum class FixtureOrigin : std::uint32_t
{
    Synthetic,
    Captured
};
template<> struct EnumTraits<FixtureOrigin>
{
    inline static constexpr auto Values = std::array {
        std::pair {FixtureOrigin::Synthetic, std::string_view {"Synthetic"}},
        std::pair {FixtureOrigin::Captured, std::string_view {"Captured"}}
    };
};

enum class RedistributionStatus : std::uint32_t
{
    NotEstablished,
    Approved,
    Restricted
};
template<> struct EnumTraits<RedistributionStatus>
{
    inline static constexpr auto Values = std::array {
        std::pair {RedistributionStatus::NotEstablished, std::string_view {"NotEstablished"}},
        std::pair {RedistributionStatus::Approved, std::string_view {"Approved"}},
        std::pair {RedistributionStatus::Restricted, std::string_view {"Restricted"}}
    };
};

struct AxisResult
{
    ExecutionState execution = ExecutionState::NotRun;
    OptionalFact<Verdict> verdict {};
    MetadataList<Symbol, 16> missingCoverage {};
    OptionalFact<ReasonId> reason {};
    inline static constexpr std::string_view WireName = "AxisResult";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("execution", &AxisResult::execution),
            Field("verdict", &AxisResult::verdict),
            Field("missingCoverage", &AxisResult::missingCoverage),
            Field("reason", &AxisResult::reason)
        };
    }
    Error Check() const
    {
        if (execution != ExecutionState::Completed && verdict.IsKnown() && verdict.KnownPart()->value == Verdict::PASS)
            return Error::Malformed;
        return Error::None;
    }
    bool operator==(const AxisResult&) const = default;
};

struct FourAxisResult
{
    AxisResult protocolSemantic {};
    AxisResult gpuResource {};
    AxisResult temporalImageQuality {};
    AxisResult performanceLatency {};
    inline static constexpr std::string_view WireName = "FourAxisResult";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("protocolSemantic", &FourAxisResult::protocolSemantic),
            Field("gpuResource", &FourAxisResult::gpuResource),
            Field("temporalImageQuality", &FourAxisResult::temporalImageQuality),
            Field("performanceLatency", &FourAxisResult::performanceLatency)
        };
    }
    bool operator==(const FourAxisResult&) const = default;
};

// Logical identifiers and hashes, not file paths, native handles, executable addresses or DLL-loading instructions.
struct ResourcePayload
{
    ResourceDescriptor descriptor {};
    Symbol sha256 {};
    OptionalFact<Symbol> logicalPayloadId {};
    inline static constexpr std::string_view WireName = "ResourcePayload";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("descriptor", &ResourcePayload::descriptor),
            Field("sha256", &ResourcePayload::sha256),
            Field("logicalPayloadId", &ResourcePayload::logicalPayloadId)
        };
    }
    Error Check() const
    {
        if (sha256.View().size() != 64)
            return Error::Malformed;
        for (const char c : sha256.View())
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return Error::Malformed;
        return Error::None;
    }
    bool operator==(const ResourcePayload&) const = default;
};

struct ValidationFixture
{
    RecordHeader header = RecordHeader {ContractId::C15};
    FixtureOrigin origin = FixtureOrigin::Synthetic;
    OptionalFact<DocumentEvidence> provenance {};
    RedistributionStatus redistribution = RedistributionStatus::NotEstablished;
    MetadataList<RecordReference, 64> frameSequence {};
    MetadataList<RecordReference, 64> groundTruth {};
    MetadataList<RecordReference, 64> metadataAndEvents {};
    MetadataList<ResourcePayload, 16> resources {};
    ProfileKey requiredProfile {};
    MetadataList<SemanticClaim, 32> environment {};
    MetadataList<Symbol, 32> expectedInvariants {};
    OptionalFact<RecordReference> warmupProtocol {};
    OptionalFact<RecordReference> resetProtocol {};
    FourAxisResult result {};
    inline static constexpr std::string_view WireName = "ValidationFixture";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &ValidationFixture::header),
            Field("origin", &ValidationFixture::origin),
            Field("provenance", &ValidationFixture::provenance),
            Field("redistribution", &ValidationFixture::redistribution),
            Field("frameSequence", &ValidationFixture::frameSequence),
            Field("groundTruth", &ValidationFixture::groundTruth),
            Field("metadataAndEvents", &ValidationFixture::metadataAndEvents),
            Field("resources", &ValidationFixture::resources),
            Field("requiredProfile", &ValidationFixture::requiredProfile),
            Field("environment", &ValidationFixture::environment),
            Field("expectedInvariants", &ValidationFixture::expectedInvariants),
            Field("warmupProtocol", &ValidationFixture::warmupProtocol),
            Field("resetProtocol", &ValidationFixture::resetProtocol),
            Field("result", &ValidationFixture::result)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C15) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Validation) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const ValidationFixture&) const = default;
};

} // namespace Neurotic::Contracts
