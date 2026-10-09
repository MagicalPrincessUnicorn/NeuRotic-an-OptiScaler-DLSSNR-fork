#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "SemanticDescriptions.h"
#include "NativeDelivery.h"

namespace Neurotic::Contracts
{
struct NrExecutionRecipe
{
    RecordHeader header = RecordHeader {ContractId::C06};
    EvaluationId evaluation {};
    MetadataRef<FrameIdentity> frame {};
    ContractRef<ContractId::C05> committedPlan {};
    ContractRef<ContractId::C02> context {};
    ProfileKey profile {};
    Symbol strategy {};
    Placement placement = Placement::NativeAfter;
    MetadataList<ContractRef<ContractId::C12>, 32> representations {};
    MetadataList<TransformStep, 16> conversions {};
    OwnerValueReference settingsSnapshot {};
    OptionalFact<ContractRef<ContractId::C08>> resetProjection {};
    MetadataRef<HistoryKey> history {};
    MetadataRef<MaskSet> masks {};
    ResourceDescriptor expectedOutput {};
    MetadataList<AccessRequirements, 16> leaseRequests {};
    FailureDisposition failureDisposition = FailureDisposition::PreserveOriginal;
    OptionalFact<RecordKey> graphNode {};
    std::optional<MetadataRef<NativeDeliveryContractV1>> nativeDelivery;
    std::optional<MetadataRef<NativeSampleIdentityV1>> nativeSample;
    inline static constexpr std::string_view WireName = "NrExecutionRecipe";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &NrExecutionRecipe::header),
            Field("evaluation", &NrExecutionRecipe::evaluation),
            Field("frame", &NrExecutionRecipe::frame),
            Field("committedPlan", &NrExecutionRecipe::committedPlan),
            Field("context", &NrExecutionRecipe::context),
            Field("profile", &NrExecutionRecipe::profile),
            Field("strategy", &NrExecutionRecipe::strategy),
            Field("placement", &NrExecutionRecipe::placement),
            Field("representations", &NrExecutionRecipe::representations),
            Field("conversions", &NrExecutionRecipe::conversions),
            Field("settingsSnapshot", &NrExecutionRecipe::settingsSnapshot),
            Field("resetProjection", &NrExecutionRecipe::resetProjection),
            Field("history", &NrExecutionRecipe::history),
            Field("masks", &NrExecutionRecipe::masks),
            Field("expectedOutput", &NrExecutionRecipe::expectedOutput),
            Field("leaseRequests", &NrExecutionRecipe::leaseRequests),
            Field("failureDisposition", &NrExecutionRecipe::failureDisposition),
            Field("graphNode", &NrExecutionRecipe::graphNode),
            Field("nativeDelivery", &NrExecutionRecipe::nativeDelivery),
            Field("nativeSample", &NrExecutionRecipe::nativeSample)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C06) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::RenderingProtocol) != Error::None ||
            settingsSnapshot.owner != OwnerDomain::Configuration)
            return Error::WrongOwner;
        if (nativeDelivery.has_value() != nativeSample.has_value())
            return Error::MissingField;
        if ((nativeDelivery && nativeDelivery->owner != OwnerDomain::StreamCoordinator) ||
            (nativeSample && nativeSample->owner != OwnerDomain::Provider))
            return Error::WrongOwner;
        if ((nativeDelivery && nativeDelivery->schemaVersion != SchemaVersion{1,0}) ||
            (nativeSample && nativeSample->schemaVersion != SchemaVersion{1,0}))return Error::UnsupportedVersion;
        return Error::None;
    }
    bool operator==(const NrExecutionRecipe&) const = default;
};

using RenderingProtocolInput = NrExecutionRecipe;
} // namespace Neurotic::Contracts
