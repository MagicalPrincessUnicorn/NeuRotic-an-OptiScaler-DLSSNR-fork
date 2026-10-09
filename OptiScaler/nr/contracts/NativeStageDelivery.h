#pragma once
#include "NativeDelivery.h"
#include "C03_Consumption.h"

namespace Neurotic::Contracts
{
// Values only. The Resource owner publishes the exact view after its content
// write; a description of the recording neither submits it nor grants a lease.
struct NativeOutputContentV1
{
    std::uint32_t version=1;
    MetadataRef<ResourceView> view;
    RecordKey recording;
    std::uint64_t producerOrdinal=0;
    Symbol semanticDomain;
    inline static constexpr std::string_view WireName="NativeOutputContentV1";
    static constexpr auto Fields()
    {
        return std::tuple{Field("version",&NativeOutputContentV1::version),Field("view",&NativeOutputContentV1::view),
            Field("recording",&NativeOutputContentV1::recording),Field("producerOrdinal",&NativeOutputContentV1::producerOrdinal),
            Field("semanticDomain",&NativeOutputContentV1::semanticDomain)};
    }
    Error Check()const
    {
        if(version!=1||view.schemaVersion!=SchemaVersion{1,0})return Error::UnsupportedVersion;
        if(view.owner!=OwnerDomain::Resource)return Error::WrongOwner;
        return producerOrdinal&&!semanticDomain.Empty()?Error::None:Error::MissingProvenance;
    }
    bool operator==(const NativeOutputContentV1&)const=default;
};
enum class NativeStageOutcome : std::uint8_t {Invalid=0,HostReturnRecorded=1,DeliveryRejected=2};
template<>struct EnumTraits<NativeStageOutcome>
{
    inline static constexpr auto Values=std::array{
        std::pair{NativeStageOutcome::Invalid,std::string_view{"Invalid"}},
        std::pair{NativeStageOutcome::HostReturnRecorded,std::string_view{"HostReturnRecorded"}},
        std::pair{NativeStageOutcome::DeliveryRejected,std::string_view{"DeliveryRejected"}}};
};
// The authenticated outer return owner publishes this separately from C07.
// It contains no seal, GPU-completed, provider-released or retired assertion.
struct NativeStageDeliveryV1
{
    std::uint32_t version=1;
    OwnerValueReference publication;
    ContractRef<ContractId::C05> plan;
    RouteGeneration route;
    MetadataRef<NativeDeliveryContractV1> delivery;
    ContractRef<ContractId::C06> recipe;
    ContractRef<ContractId::C07> executionResult;
    MetadataRef<NativeSampleIdentityV1> sample;
    EvaluationId evaluation;
    RecordKey admissionEpoch;
    Placement placement=Placement::NativeAfter;
    OwnerValueReference inputBindings;
    std::optional<MetadataRef<NativeOutputContentV1>> nativeOutput,returnedOutput;
    NativeReturnBoundary boundary=NativeReturnBoundary::Invalid;
    ObjectIncarnation caller;
    RecordKey lastRecording;
    std::uint64_t lastRecordedOrdinal=0;
    // Original NGX API return word, including its sign bit if interpreted as
    // signed by a caller. No result-code-to-delivery inference is performed.
    std::uint32_t hostResult=0;
    NativeStageOutcome outcome=NativeStageOutcome::Invalid;
    Symbol reason;
    MetadataList<OwnerValueReference,16> causes;
    inline static constexpr std::string_view WireName="NativeStageDeliveryV1";
    static constexpr auto Fields()
    {
        return std::tuple{Field("version",&NativeStageDeliveryV1::version),Field("publication",&NativeStageDeliveryV1::publication),
            Field("plan",&NativeStageDeliveryV1::plan),Field("route",&NativeStageDeliveryV1::route),
            Field("delivery",&NativeStageDeliveryV1::delivery),Field("recipe",&NativeStageDeliveryV1::recipe),
            Field("executionResult",&NativeStageDeliveryV1::executionResult),Field("sample",&NativeStageDeliveryV1::sample),
            Field("evaluation",&NativeStageDeliveryV1::evaluation),Field("admissionEpoch",&NativeStageDeliveryV1::admissionEpoch),
            Field("placement",&NativeStageDeliveryV1::placement),Field("inputBindings",&NativeStageDeliveryV1::inputBindings),
            Field("nativeOutput",&NativeStageDeliveryV1::nativeOutput),Field("returnedOutput",&NativeStageDeliveryV1::returnedOutput),
            Field("boundary",&NativeStageDeliveryV1::boundary),Field("caller",&NativeStageDeliveryV1::caller),
            Field("lastRecording",&NativeStageDeliveryV1::lastRecording),Field("lastRecordedOrdinal",&NativeStageDeliveryV1::lastRecordedOrdinal),
            Field("hostResult",&NativeStageDeliveryV1::hostResult),Field("outcome",&NativeStageDeliveryV1::outcome),
            Field("reason",&NativeStageDeliveryV1::reason),Field("causes",&NativeStageDeliveryV1::causes)};
    }
    Error Check()const
    {
        if(version!=1||publication.schemaVersion!=VersionNumber{1,0}||inputBindings.schemaVersion!=VersionNumber{1,0}||
            delivery.schemaVersion!=SchemaVersion{1,0}||sample.schemaVersion!=SchemaVersion{1,0})return Error::UnsupportedVersion;
        if(publication.owner!=OwnerDomain::Provider||delivery.owner!=OwnerDomain::StreamCoordinator||
            sample.owner!=OwnerDomain::Provider||inputBindings.owner!=OwnerDomain::RenderingProtocol)return Error::WrongOwner;
        if(publication.recordType.View()!=WireName||!publication.revision||inputBindings.recordType.View()!="RENDER.BindingPlan"||
            !inputBindings.revision||plan.recordType.View()!="PlanCommit"||recipe.recordType.View()!="NrExecutionRecipe"||
            executionResult.recordType.View()!="EvaluationResult")return Error::WrongRecordType;
        if(placement!=Placement::NativeBefore&&placement!=Placement::NativeAfter)return Error::Malformed;
        if(boundary!=NativeReturnBoundary::NgxEvaluateReturn||reason.Empty())return Error::MissingField;
        if(outcome==NativeStageOutcome::HostReturnRecorded&&(!nativeOutput||!returnedOutput||!lastRecordedOrdinal||!causes.count))
            return Error::MissingProvenance;
        if(outcome!=NativeStageOutcome::HostReturnRecorded&&outcome!=NativeStageOutcome::DeliveryRejected)return Error::Malformed;
        for(const auto* output:{&nativeOutput,&returnedOutput})if(*output)
        {
            if((*output)->owner!=OwnerDomain::Resource)return Error::WrongOwner;
            if((*output)->schemaVersion!=SchemaVersion{1,0})return Error::UnsupportedVersion;
        }
        return Error::None;
    }
    bool operator==(const NativeStageDeliveryV1&)const=default;
};
}
