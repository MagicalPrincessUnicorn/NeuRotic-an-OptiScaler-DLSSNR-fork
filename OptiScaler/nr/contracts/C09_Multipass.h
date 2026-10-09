#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "C07_ProtocolOutput.h"

namespace Neurotic::Contracts
{
enum class CompositionKind : std::uint32_t
{
    LegacySerial,
    CanonicalPrefix,
    SharedSourceResidual,
    ProfileRefinement
};
template<> struct EnumTraits<CompositionKind>
{
    inline static constexpr auto Values = std::array {
        std::pair {CompositionKind::LegacySerial, std::string_view {"LegacySerial"}},
        std::pair {CompositionKind::CanonicalPrefix, std::string_view {"CanonicalPrefix"}},
        std::pair {CompositionKind::SharedSourceResidual, std::string_view {"SharedSourceResidual"}},
        std::pair {CompositionKind::ProfileRefinement, std::string_view {"ProfileRefinement"}}
    };
};

enum class NodeDisposition : std::uint32_t
{
    Executed,
    Skipped,
    Failed
};
template<> struct EnumTraits<NodeDisposition>
{
    inline static constexpr auto Values = std::array {
        std::pair {NodeDisposition::Executed, std::string_view {"Executed"}},
        std::pair {NodeDisposition::Skipped, std::string_view {"Skipped"}},
        std::pair {NodeDisposition::Failed, std::string_view {"Failed"}}
    };
};

struct MultipassNode
{
    RecordKey node {};
    MetadataList<RecordKey, 16> predecessors {};
    MetadataList<RecordReference, 16> inputLineage {};
    ProfileKey profile {};
    OwnerValueReference settings {}; // Configuration-owned immutable snapshot, not a C09 operation.
    OptionalFact<double> strength {};
    MetadataList<ContractRef<ContractId::C12>, 16> representations {};
    MetadataRef<HistoryKey> history {};
    CompositionKind composition = CompositionKind::LegacySerial;
    ColorDomain domain = ColorDomain::SceneLinear;
    bool required = true;
    FailureDisposition failure = FailureDisposition::PreserveOriginal;
    inline static constexpr std::string_view WireName = "MultipassNode";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("node", &MultipassNode::node),
            Field("predecessors", &MultipassNode::predecessors),
            Field("inputLineage", &MultipassNode::inputLineage),
            Field("profile", &MultipassNode::profile),
            Field("settings", &MultipassNode::settings),
            Field("strength", &MultipassNode::strength),
            Field("representations", &MultipassNode::representations),
            Field("history", &MultipassNode::history),
            Field("composition", &MultipassNode::composition),
            Field("domain", &MultipassNode::domain),
            Field("required", &MultipassNode::required),
            Field("failure", &MultipassNode::failure)
        };
    }
    Error Check() const
    {
        return settings.owner == OwnerDomain::Configuration ? Error::None : Error::WrongOwner;
    }
    bool operator==(const MultipassNode&) const = default;
};

// A graph description only; no scheduling, history sharing, execution or finalization occurs here.
struct MultipassPlan
{
    RecordHeader header = RecordHeader {ContractId::C09};
    std::uint32_t graphVersion = 1;
    MetadataList<MultipassNode, 16> nodes {};
    MetadataList<RecordKey, 16> topologicalOrder {};
    RecordKey expectedFinalizer {};
    OptionalFact<std::uint64_t> memoryCeilingBytes {};
    OptionalFact<double> costCeilingNanoseconds {};
    inline static constexpr std::string_view WireName = "MultipassPlan";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &MultipassPlan::header),
            Field("graphVersion", &MultipassPlan::graphVersion),
            Field("nodes", &MultipassPlan::nodes),
            Field("topologicalOrder", &MultipassPlan::topologicalOrder),
            Field("expectedFinalizer", &MultipassPlan::expectedFinalizer),
            Field("memoryCeilingBytes", &MultipassPlan::memoryCeilingBytes),
            Field("costCeilingNanoseconds", &MultipassPlan::costCeilingNanoseconds)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C09) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Multipass) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const MultipassPlan&) const = default;
};

struct NodeResult
{
    RecordKey node {};
    NodeDisposition disposition = NodeDisposition::Skipped;
    OptionalFact<ContractRef<ContractId::C07>> evaluation {};
    OptionalFact<ReasonId> reason {};
    inline static constexpr std::string_view WireName = "NodeResult";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("node", &NodeResult::node),
            Field("disposition", &NodeResult::disposition),
            Field("evaluation", &NodeResult::evaluation),
            Field("reason", &NodeResult::reason)
        };
    }
    bool operator==(const NodeResult&) const = default;
};

struct MultipassResult
{
    RecordHeader header = RecordHeader {ContractId::C09};
    ContractRef<ContractId::C09> plan {};
    MetadataList<NodeResult, 16> nodes {};
    OptionalFact<RecordKey> acceptedNode {};
    OptionalFact<bool> acceptedPrefix {};
    OptionalFact<ContractRef<ContractId::C07>> acceptedOutput {};
    OptionalFact<ContractRef<ContractId::C13>> finalPacket {};
    inline static constexpr std::string_view WireName = "MultipassResult";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &MultipassResult::header),
            Field("plan", &MultipassResult::plan),
            Field("nodes", &MultipassResult::nodes),
            Field("acceptedNode", &MultipassResult::acceptedNode),
            Field("acceptedPrefix", &MultipassResult::acceptedPrefix),
            Field("acceptedOutput", &MultipassResult::acceptedOutput),
            Field("finalPacket", &MultipassResult::finalPacket)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C09) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Multipass) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const MultipassResult&) const = default;
};

} // namespace Neurotic::Contracts
