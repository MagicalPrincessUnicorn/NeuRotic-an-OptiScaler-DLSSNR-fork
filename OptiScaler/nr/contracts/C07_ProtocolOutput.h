#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "C03_Consumption.h"

namespace Neurotic::Contracts
{
enum class OutcomeStage : std::uint32_t
{
    NotAttempted,
    Bypassed,
    Recorded,
    Submitted,
    Produced,
    Failed,
    Interrupted
};
template<> struct EnumTraits<OutcomeStage>
{
    inline static constexpr auto Values = std::array {
        std::pair {OutcomeStage::NotAttempted, std::string_view {"NotAttempted"}},
        std::pair {OutcomeStage::Bypassed, std::string_view {"Bypassed"}},
        std::pair {OutcomeStage::Recorded, std::string_view {"Recorded"}},
        std::pair {OutcomeStage::Submitted, std::string_view {"Submitted"}},
        std::pair {OutcomeStage::Produced, std::string_view {"Produced"}},
        std::pair {OutcomeStage::Failed, std::string_view {"Failed"}},
        std::pair {OutcomeStage::Interrupted, std::string_view {"Interrupted"}}
    };
};

enum class HistoryAdvancement : std::uint32_t
{
    NotAdvanced,
    MayHaveAdvanced,
    Advanced
};
template<> struct EnumTraits<HistoryAdvancement>
{
    inline static constexpr auto Values = std::array {
        std::pair {HistoryAdvancement::NotAdvanced, std::string_view {"NotAdvanced"}},
        std::pair {HistoryAdvancement::MayHaveAdvanced, std::string_view {"MayHaveAdvanced"}},
        std::pair {HistoryAdvancement::Advanced, std::string_view {"Advanced"}}
    };
};

enum class CompositionStatus : std::uint32_t
{
    NotAttempted,
    Recorded,
    Submitted,
    Accepted,
    Failed
};
template<> struct EnumTraits<CompositionStatus>
{
    inline static constexpr auto Values = std::array {
        std::pair {CompositionStatus::NotAttempted, std::string_view {"NotAttempted"}},
        std::pair {CompositionStatus::Recorded, std::string_view {"Recorded"}},
        std::pair {CompositionStatus::Submitted, std::string_view {"Submitted"}},
        std::pair {CompositionStatus::Accepted, std::string_view {"Accepted"}},
        std::pair {CompositionStatus::Failed, std::string_view {"Failed"}}
    };
};

struct EvaluationResult
{
    RecordHeader header = RecordHeader {ContractId::C07};
    EvaluationId evaluation {};
    MetadataRef<FrameIdentity> originalLineage {};
    OutcomeStage stage = OutcomeStage::NotAttempted;
    MetadataList<Symbol, 32> actualFieldsUsed {};
    OptionalFact<MetadataRef<ResourceView>> output {};
    OptionalFact<ContractRef<ContractId::C03>> dependency {};
    OptionalFact<HistoryAdvancement> historyAdvancement {};
    MetadataList<ContractRef<ContractId::C08>, 16> resetAcknowledgments {};
    OptionalFact<CompositionStatus> composition {};
    OptionalFact<ReasonId> failure {};
    OptionalFact<bool> originalPreserved {};
    MetadataList<RetentionRegistration, 16> retentions {};
    OptionalFact<Measurement> modelTiming {};
    inline static constexpr std::string_view WireName = "EvaluationResult";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &EvaluationResult::header),
            Field("evaluation", &EvaluationResult::evaluation),
            Field("originalLineage", &EvaluationResult::originalLineage),
            Field("stage", &EvaluationResult::stage),
            Field("actualFieldsUsed", &EvaluationResult::actualFieldsUsed),
            Field("output", &EvaluationResult::output),
            Field("dependency", &EvaluationResult::dependency),
            Field("historyAdvancement", &EvaluationResult::historyAdvancement),
            Field("resetAcknowledgments", &EvaluationResult::resetAcknowledgments),
            Field("composition", &EvaluationResult::composition),
            Field("failure", &EvaluationResult::failure),
            Field("originalPreserved", &EvaluationResult::originalPreserved),
            Field("retentions", &EvaluationResult::retentions),
            Field("modelTiming", &EvaluationResult::modelTiming)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C07) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Strategy, OwnerDomain::Multipass) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const EvaluationResult&) const = default;
};

using RenderingProtocolOutput = EvaluationResult;
} // namespace Neurotic::Contracts
