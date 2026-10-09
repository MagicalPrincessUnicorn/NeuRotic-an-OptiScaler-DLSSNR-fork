#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "SemanticDescriptions.h"

namespace Neurotic::Contracts
{
enum class CopyClass : std::uint32_t
{
    None,
    InputPreparation,
    OutputComposition,
    Capture,
    Interop
};
template<> struct EnumTraits<CopyClass>
{
    inline static constexpr auto Values = std::array {
        std::pair {CopyClass::None, std::string_view {"None"}},
        std::pair {CopyClass::InputPreparation, std::string_view {"InputPreparation"}},
        std::pair {CopyClass::OutputComposition, std::string_view {"OutputComposition"}},
        std::pair {CopyClass::Capture, std::string_view {"Capture"}},
        std::pair {CopyClass::Interop, std::string_view {"Interop"}}
    };
};

enum class BarrierClass : std::uint32_t
{
    None,
    ReadTransition,
    WriteTransition,
    CrossQueueDependency
};
template<> struct EnumTraits<BarrierClass>
{
    inline static constexpr auto Values = std::array {
        std::pair {BarrierClass::None, std::string_view {"None"}},
        std::pair {BarrierClass::ReadTransition, std::string_view {"ReadTransition"}},
        std::pair {BarrierClass::WriteTransition, std::string_view {"WriteTransition"}},
        std::pair {BarrierClass::CrossQueueDependency, std::string_view {"CrossQueueDependency"}}
    };
};

enum class MemoryCategory : std::uint32_t
{
    Persistent,
    Transient,
    ProviderRetained,
    ReplayRetained,
    Readback,
    Upload
};
template<> struct EnumTraits<MemoryCategory>
{
    inline static constexpr auto Values = std::array {
        std::pair {MemoryCategory::Persistent, std::string_view {"Persistent"}},
        std::pair {MemoryCategory::Transient, std::string_view {"Transient"}},
        std::pair {MemoryCategory::ProviderRetained, std::string_view {"ProviderRetained"}},
        std::pair {MemoryCategory::ReplayRetained, std::string_view {"ReplayRetained"}},
        std::pair {MemoryCategory::Readback, std::string_view {"Readback"}},
        std::pair {MemoryCategory::Upload, std::string_view {"Upload"}}
    };
};

struct ResourceUseInterval
{
    ResourceIdentityToken resource {};
    ContractRef<ContractId::C03> lease {};
    RecordKey firstOperation {};
    RecordKey lastOperation {};
    inline static constexpr std::string_view WireName = "ResourceUseInterval";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("resource", &ResourceUseInterval::resource),
            Field("lease", &ResourceUseInterval::lease),
            Field("firstOperation", &ResourceUseInterval::firstOperation),
            Field("lastOperation", &ResourceUseInterval::lastOperation)
        };
    }
    bool operator==(const ResourceUseInterval&) const = default;
};

struct ExecutionEdge
{
    RecordKey before {};
    RecordKey after {};
    OptionalFact<ContractRef<ContractId::C03>> dependency {};
    inline static constexpr std::string_view WireName = "ExecutionEdge";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("before", &ExecutionEdge::before),
            Field("after", &ExecutionEdge::after),
            Field("dependency", &ExecutionEdge::dependency)
        };
    }
    bool operator==(const ExecutionEdge&) const = default;
};

// Proposal after route selection. Backend/resource owners alone admit execution; no scheduling operation is implemented.
struct LocalExecutionPlan
{
    RecordHeader header = RecordHeader {ContractId::C16};
    ContractRef<ContractId::C05> committedRoute {};
    OptionalFact<ContractRef<ContractId::C09>> graph {};
    MetadataList<ContractRef<ContractId::C12>, 32> preparations {};
    MetadataList<ResourceUseInterval, 32> uses {};
    MetadataList<ObjectIncarnation, 8> ownerPermittedQueues {};
    MetadataList<ExecutionEdge, 64> edges {};
    BoundedList<CopyClass, 16> copies {};
    BoundedList<BarrierClass, 16> barriers {};
    OptionalFact<Symbol> optionalWorkPolicy {};
    inline static constexpr std::string_view WireName = "LocalExecutionPlan";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &LocalExecutionPlan::header),
            Field("committedRoute", &LocalExecutionPlan::committedRoute),
            Field("graph", &LocalExecutionPlan::graph),
            Field("preparations", &LocalExecutionPlan::preparations),
            Field("uses", &LocalExecutionPlan::uses),
            Field("ownerPermittedQueues", &LocalExecutionPlan::ownerPermittedQueues),
            Field("edges", &LocalExecutionPlan::edges),
            Field("copies", &LocalExecutionPlan::copies),
            Field("barriers", &LocalExecutionPlan::barriers),
            Field("optionalWorkPolicy", &LocalExecutionPlan::optionalWorkPolicy)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C16) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Performance) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const LocalExecutionPlan&) const = default;
};

struct MemoryCost
{
    MemoryCategory category = MemoryCategory::Transient;
    Measurement measurement {};
    inline static constexpr std::string_view WireName = "MemoryCost";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("category", &MemoryCost::category),
            Field("measurement", &MemoryCost::measurement)
        };
    }
    bool operator==(const MemoryCost&) const = default;
};

struct CostRecord
{
    RecordHeader header = RecordHeader {ContractId::C16};
    ContractRef<ContractId::C16> plan {};
    OptionalFact<RecordKey> operation {};
    MetadataList<Measurement, 16> timings {};
    MetadataList<MemoryCost, 8> memory {};
    OptionalFact<Symbol> measurementMethod {};
    inline static constexpr std::string_view WireName = "CostRecord";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &CostRecord::header),
            Field("plan", &CostRecord::plan),
            Field("operation", &CostRecord::operation),
            Field("timings", &CostRecord::timings),
            Field("memory", &CostRecord::memory),
            Field("measurementMethod", &CostRecord::measurementMethod)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C16) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Strategy, OwnerDomain::Resource, OwnerDomain::FrameGeneration) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const CostRecord&) const = default;
};

} // namespace Neurotic::Contracts
