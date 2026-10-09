#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "SemanticDescriptions.h"

namespace Neurotic::Contracts
{
enum class ResetAction : std::uint32_t
{
    InvalidateUse,
    ResetHistory,
    Recreate,
    Requalify,
    Retire
};
template<> struct EnumTraits<ResetAction>
{
    inline static constexpr auto Values = std::array {
        std::pair {ResetAction::InvalidateUse, std::string_view {"InvalidateUse"}},
        std::pair {ResetAction::ResetHistory, std::string_view {"ResetHistory"}},
        std::pair {ResetAction::Recreate, std::string_view {"Recreate"}},
        std::pair {ResetAction::Requalify, std::string_view {"Requalify"}},
        std::pair {ResetAction::Retire, std::string_view {"Retire"}}
    };
};

enum class ResetEventKind : std::uint32_t
{
    FirstUse,
    CameraCut,
    Teleport,
    Loading,
    Fade,
    RenderRasterChanged,
    OutputRasterChanged,
    DynamicResolution,
    SwapchainRecreated,
    ColorChanged,
    ExposureDiscontinuity,
    RrChanged,
    FgPolicyChanged,
    HandoffChanged,
    ProviderReplaced,
    GuideChanged,
    ResourceReplaced,
    ModelChanged,
    PlacementChanged,
    GraphChanged,
    GuideExpired,
    GuideRecovered,
    RepeatedOrGenerated,
    DeviceLost,
    BudgetPressure,
    FocusGap
};
template<> struct EnumTraits<ResetEventKind>
{
    inline static constexpr auto Values = std::array {
        std::pair {ResetEventKind::FirstUse, std::string_view {"FirstUse"}},
        std::pair {ResetEventKind::CameraCut, std::string_view {"CameraCut"}},
        std::pair {ResetEventKind::Teleport, std::string_view {"Teleport"}},
        std::pair {ResetEventKind::Loading, std::string_view {"Loading"}},
        std::pair {ResetEventKind::Fade, std::string_view {"Fade"}},
        std::pair {ResetEventKind::RenderRasterChanged, std::string_view {"RenderRasterChanged"}},
        std::pair {ResetEventKind::OutputRasterChanged, std::string_view {"OutputRasterChanged"}},
        std::pair {ResetEventKind::DynamicResolution, std::string_view {"DynamicResolution"}},
        std::pair {ResetEventKind::SwapchainRecreated, std::string_view {"SwapchainRecreated"}},
        std::pair {ResetEventKind::ColorChanged, std::string_view {"ColorChanged"}},
        std::pair {ResetEventKind::ExposureDiscontinuity, std::string_view {"ExposureDiscontinuity"}},
        std::pair {ResetEventKind::RrChanged, std::string_view {"RrChanged"}},
        std::pair {ResetEventKind::FgPolicyChanged, std::string_view {"FgPolicyChanged"}},
        std::pair {ResetEventKind::HandoffChanged, std::string_view {"HandoffChanged"}},
        std::pair {ResetEventKind::ProviderReplaced, std::string_view {"ProviderReplaced"}},
        std::pair {ResetEventKind::GuideChanged, std::string_view {"GuideChanged"}},
        std::pair {ResetEventKind::ResourceReplaced, std::string_view {"ResourceReplaced"}},
        std::pair {ResetEventKind::ModelChanged, std::string_view {"ModelChanged"}},
        std::pair {ResetEventKind::PlacementChanged, std::string_view {"PlacementChanged"}},
        std::pair {ResetEventKind::GraphChanged, std::string_view {"GraphChanged"}},
        std::pair {ResetEventKind::GuideExpired, std::string_view {"GuideExpired"}},
        std::pair {ResetEventKind::GuideRecovered, std::string_view {"GuideRecovered"}},
        std::pair {ResetEventKind::RepeatedOrGenerated, std::string_view {"RepeatedOrGenerated"}},
        std::pair {ResetEventKind::DeviceLost, std::string_view {"DeviceLost"}},
        std::pair {ResetEventKind::BudgetPressure, std::string_view {"BudgetPressure"}},
        std::pair {ResetEventKind::FocusGap, std::string_view {"FocusGap"}}
    };
};

struct GenerationChange
{
    OptionalFact<GenerationToken> before {};
    OptionalFact<GenerationToken> after {};
    inline static constexpr std::string_view WireName = "GenerationChange";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("before", &GenerationChange::before),
            Field("after", &GenerationChange::after)
        };
    }
    bool operator==(const GenerationChange&) const = default;
};

struct ResetEvent
{
    RecordKey event {};
    ResetEventKind kind = ResetEventKind::FirstUse;
    MetadataRef<EvidenceVector> evidence {};
    inline static constexpr std::string_view WireName = "ResetEvent";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("event", &ResetEvent::event),
            Field("kind", &ResetEvent::kind),
            Field("evidence", &ResetEvent::evidence)
        };
    }
    bool operator==(const ResetEvent&) const = default;
};

struct ResetPlan
{
    RecordHeader header = RecordHeader {ContractId::C08};
    MetadataList<ResetEvent, 16> events {};
    MetadataList<GenerationChange, 32> generationChanges {};
    std::uint32_t dependencyRuleVersion = 1;
    MetadataList<RecordKey, 32> affectedOwners {};
    MetadataList<RecordKey, 32> affectedHistories {};
    MetadataList<ResourceIdentityToken, 16> affectedResources {};
    BoundedList<ResetAction, 5> actions {};
    BoundaryDescription boundary {};
    OptionalFact<RecordKey> deadline {};
    MetadataList<RecordKey, 32> pendingAcknowledgments {};
    MetadataList<RecordReference, 32> survivingState {};
    OptionalFact<TypedMask<MaskPurpose::LocalHistoryRejection>> localMask {};
    OptionalFact<TransformStep> exposureContinuity {};
    inline static constexpr std::string_view WireName = "ResetPlan";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &ResetPlan::header),
            Field("events", &ResetPlan::events),
            Field("generationChanges", &ResetPlan::generationChanges),
            Field("dependencyRuleVersion", &ResetPlan::dependencyRuleVersion),
            Field("affectedOwners", &ResetPlan::affectedOwners),
            Field("affectedHistories", &ResetPlan::affectedHistories),
            Field("affectedResources", &ResetPlan::affectedResources),
            Field("actions", &ResetPlan::actions),
            Field("boundary", &ResetPlan::boundary),
            Field("deadline", &ResetPlan::deadline),
            Field("pendingAcknowledgments", &ResetPlan::pendingAcknowledgments),
            Field("survivingState", &ResetPlan::survivingState),
            Field("localMask", &ResetPlan::localMask),
            Field("exposureContinuity", &ResetPlan::exposureContinuity)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C08) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::ResetRules) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const ResetPlan&) const = default;
};

struct ResetAcknowledgment
{
    RecordHeader header = RecordHeader {ContractId::C08};
    ContractRef<ContractId::C08> plan {};
    RecordKey affectedOwner {};
    BoundaryDescription acceptedBoundary {};
    OptionalFact<bool> accepted {};
    OptionalFact<bool> applied {};
    OptionalFact<GenerationToken> resultingLineage {};
    BoundedList<ResetAction, 5> appliedActions {};
    inline static constexpr std::string_view WireName = "ResetAcknowledgment";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &ResetAcknowledgment::header),
            Field("plan", &ResetAcknowledgment::plan),
            Field("affectedOwner", &ResetAcknowledgment::affectedOwner),
            Field("acceptedBoundary", &ResetAcknowledgment::acceptedBoundary),
            Field("accepted", &ResetAcknowledgment::accepted),
            Field("applied", &ResetAcknowledgment::applied),
            Field("resultingLineage", &ResetAcknowledgment::resultingLineage),
            Field("appliedActions", &ResetAcknowledgment::appliedActions)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C08) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Resource, OwnerDomain::Strategy, OwnerDomain::History, OwnerDomain::Context, OwnerDomain::FrameGeneration, OwnerDomain::Provider, OwnerDomain::Multipass, OwnerDomain::Acquisition, OwnerDomain::ColorContinuity, OwnerDomain::RayReconstruction, OwnerDomain::StreamCoordinator, OwnerDomain::RenderingProtocol, OwnerDomain::Presentation, OwnerDomain::Finalizer) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const ResetAcknowledgment&) const = default;
};

} // namespace Neurotic::Contracts
