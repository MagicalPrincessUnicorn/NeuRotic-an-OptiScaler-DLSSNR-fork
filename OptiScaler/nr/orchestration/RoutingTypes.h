#pragma once
// Pure owner-value projections. Canonical contracts and identity remain under ARCH/C14.
#include <nr/contracts/C05_Routing.h>
#include <nr/contracts/C11_Diagnostics.h>
#include <nr/context/ContextMetadata.h>

namespace Neurotic::Orchestration
{
namespace C=Contracts;
inline constexpr std::uint32_t PolicyVersion=1;
#define NR_ORCH_PROGRESS_PROVENANCE 1
inline C::Symbol Symbol(std::string_view text){C::Symbol result;result.Assign(text);return result;}
#define NR_ORCH_FIELD(member) C::Field(#member,&Self::member)

struct SoftOptIn
{
    C::Symbol rule;C::ScopeRef scope;C::MetadataRef<C::GenerationVector> generations;
    inline static constexpr std::string_view WireName="ORCH.SoftOptIn";
    static constexpr auto Fields(){using Self=SoftOptIn;return std::tuple{NR_ORCH_FIELD(rule),NR_ORCH_FIELD(scope),NR_ORCH_FIELD(generations)};}
    bool operator==(const SoftOptIn&)const=default;
};
struct RoutingIntent
{
    C::OwnerValueReference source;
    C::Symbol mode=Symbol("Shadow"),tier=Symbol("Basic");
    C::BoundedList<C::Symbol,8> require,forbid,prefer;
    C::BoundedList<SoftOptIn,8> optIns;
    std::optional<C::Symbol> fixedStrategy;
    std::optional<C::Placement> fixedPlacement;
    std::uint32_t promotionFrames=2,warmupFrames=0;
    inline static constexpr std::string_view WireName="ORCH.RoutingIntent";
    static constexpr auto Fields(){using Self=RoutingIntent;return std::tuple{NR_ORCH_FIELD(source),NR_ORCH_FIELD(mode),NR_ORCH_FIELD(tier),
        NR_ORCH_FIELD(require),NR_ORCH_FIELD(forbid),NR_ORCH_FIELD(prefer),NR_ORCH_FIELD(optIns),NR_ORCH_FIELD(fixedStrategy),
        NR_ORCH_FIELD(fixedPlacement),NR_ORCH_FIELD(promotionFrames),NR_ORCH_FIELD(warmupFrames)};}
    bool operator==(const RoutingIntent&)const=default;
};
// Signature metadata carries only stable dependencies/relevant structural generations.
// There is deliberately no ContentRevision, frame counter, resource address or output stamp here.
struct PlanAnchor
{
    C::RecordKey plan;C::ScopeRef scope;C::Symbol strategy;C::ProfileKey profile;
    C::Placement placement=C::Placement::NativeBefore;
    C::MetadataRef<C::StructuralSignature> signature;
    std::optional<C::MetadataRef<C::NativeDeliveryContractV1>> nativeDelivery;
    inline static constexpr std::string_view WireName="ORCH.PlanAnchor";
    static constexpr auto Fields(){using Self=PlanAnchor;return std::tuple{NR_ORCH_FIELD(plan),NR_ORCH_FIELD(scope),NR_ORCH_FIELD(strategy),
        NR_ORCH_FIELD(profile),NR_ORCH_FIELD(placement),NR_ORCH_FIELD(signature),NR_ORCH_FIELD(nativeDelivery)};}
    bool operator==(const PlanAnchor&)const=default;
};
struct OperationalFact
{
    C::OwnerValueReference source;C::Symbol field;C::RecordKey plan;
    C::ContractRef<C::ContractId::C02> context;
    C::MetadataRef<C::GenerationVector> generations;
    C::OptionalFact<bool> value;C::PolicyReason reason;
    inline static constexpr std::string_view WireName="ORCH.OperationalFact";
    static constexpr auto Fields(){using Self=OperationalFact;return std::tuple{NR_ORCH_FIELD(source),NR_ORCH_FIELD(field),NR_ORCH_FIELD(plan),
        NR_ORCH_FIELD(context),NR_ORCH_FIELD(generations),NR_ORCH_FIELD(value),NR_ORCH_FIELD(reason)};}
    bool operator==(const OperationalFact&)const=default;
};
struct DegradationRank
{
    std::uint32_t mandatoryFeatureLoss=0,requiredEvidenceLoss=0,outputAge=0,optionalFeatureLoss=0;
    inline static constexpr std::string_view WireName="ORCH.DegradationRank";
    static constexpr auto Fields(){using Self=DegradationRank;return std::tuple{NR_ORCH_FIELD(mandatoryFeatureLoss),NR_ORCH_FIELD(requiredEvidenceLoss),
        NR_ORCH_FIELD(outputAge),NR_ORCH_FIELD(optionalFeatureLoss)};}
    auto Tuple()const{return std::tie(mandatoryFeatureLoss,requiredEvidenceLoss,outputAge,optionalFeatureLoss);}
    bool operator==(const DegradationRank&)const=default;
};
struct CompletePlan
{
    C::OwnerValueReference offer;PlanAnchor anchor;
    C::ContractRef<C::ContractId::C02> context;
    C::MetadataRef<C::QualificationCertificate> semantic;
    C::MetadataList<OperationalFact,16> facts;
    C::BoundedList<C::Symbol,8> features;
    C::BoundedList<C::PolicyReason,8> softRestrictions;
    DegradationRank degradation;
    C::OptionalFact<std::uint32_t> evidenceRank;
    C::OptionalFact<double> measuredCost;
    C::MetadataList<C::ContractRef<C::ContractId::C01>,64> sourceBundle;
    C::MetadataList<C::ContractRef<C::ContractId::C12>,32> preparations;
    C::OptionalFact<std::optional<C::ContractRef<C::ContractId::C09>>> multipass;
    C::OptionalFact<std::optional<C::RecordReference>> fgSubplan;
    C::BoundaryDescription boundary;
    C::OptionalFact<C::ContentRevision> currentContent;
    inline static constexpr std::string_view WireName="ORCH.CompletePlan";
    static constexpr auto Fields(){using Self=CompletePlan;return std::tuple{NR_ORCH_FIELD(offer),NR_ORCH_FIELD(anchor),NR_ORCH_FIELD(context),
        NR_ORCH_FIELD(semantic),NR_ORCH_FIELD(facts),NR_ORCH_FIELD(features),NR_ORCH_FIELD(softRestrictions),NR_ORCH_FIELD(degradation),
        NR_ORCH_FIELD(evidenceRank),NR_ORCH_FIELD(measuredCost),NR_ORCH_FIELD(sourceBundle),NR_ORCH_FIELD(preparations),
        NR_ORCH_FIELD(multipass),NR_ORCH_FIELD(fgSubplan),NR_ORCH_FIELD(boundary),NR_ORCH_FIELD(currentContent)};}
    bool operator==(const CompletePlan&)const=default;
};
struct IncumbentState
{
    C::OwnerValueReference source;
    std::optional<PlanAnchor> current,promotion;
    C::OptionalFact<bool> structurallyCurrent;
    std::optional<C::BaseRealFrameId> lastCounted;
    std::uint32_t streak=0,policyVersion=PolicyVersion;
    bool wasLocked=false;
    inline static constexpr std::string_view WireName="ORCH.IncumbentState";
    static constexpr auto Fields(){using Self=IncumbentState;return std::tuple{NR_ORCH_FIELD(source),NR_ORCH_FIELD(current),NR_ORCH_FIELD(promotion),
        NR_ORCH_FIELD(structurallyCurrent),NR_ORCH_FIELD(lastCounted),NR_ORCH_FIELD(streak),NR_ORCH_FIELD(policyVersion),NR_ORCH_FIELD(wasLocked)};}
    bool operator==(const IncumbentState&)const=default;
};
struct RoutingInput
{
    std::uint32_t schemaVersion=1,policyVersion=PolicyVersion;
    C::SchemaVersion contractVersion;
    C::ScopeRef scope;RoutingIntent intent;
    C::BoundedList<C::MetadataRef<CompletePlan>,16> offers;
    IncumbentState incumbent;
    C::FrameIdentity frame;
    C::OwnerValueReference frameSource;C::ScopeRef frameScope;
    C::OptionalFact<bool> provenRealFrame,repeatedPresent;
    C::OptionalFact<bool> mandatoryRR,mandatoryFG;
    std::optional<PlanAnchor> legacyEffective;
    inline static constexpr std::string_view WireName="ORCH.RoutingInput";
    static constexpr auto Fields(){using Self=RoutingInput;return std::tuple{NR_ORCH_FIELD(schemaVersion),NR_ORCH_FIELD(policyVersion),NR_ORCH_FIELD(contractVersion),NR_ORCH_FIELD(scope),
        NR_ORCH_FIELD(intent),NR_ORCH_FIELD(offers),NR_ORCH_FIELD(incumbent),NR_ORCH_FIELD(frame),NR_ORCH_FIELD(frameSource),NR_ORCH_FIELD(frameScope),NR_ORCH_FIELD(provenRealFrame),
        NR_ORCH_FIELD(repeatedPresent),NR_ORCH_FIELD(mandatoryRR),NR_ORCH_FIELD(mandatoryFG),NR_ORCH_FIELD(legacyEffective)};}
    C::Error Check()const{return schemaVersion==1&&policyVersion==PolicyVersion&&contractVersion==C::SchemaVersion{}?C::Error::None:C::Error::UnsupportedVersion;}
    bool operator==(const RoutingInput&)const=default;
};
struct CandidateReason
{
    C::RecordKey plan;C::PolicyReason reason;
    inline static constexpr std::string_view WireName="ORCH.CandidateReason";
    static constexpr auto Fields(){using Self=CandidateReason;return std::tuple{NR_ORCH_FIELD(plan),NR_ORCH_FIELD(reason)};}
    bool operator==(const CandidateReason&)const=default;
};
struct ShadowDecision
{
    std::uint32_t schemaVersion=1,policyVersion=PolicyVersion;
    C::SchemaVersion contractVersion;
    bool valid=false;C::Symbol executionEffect=Symbol("None");
    std::optional<PlanAnchor> proposed;
    C::BoundedList<C::RecordKey,16> ranked;
    C::BoundedList<CandidateReason,32> rejected;
    C::BoundedList<C::Symbol,16> rules;
    IncumbentState next;
    C::Symbol lockDisposition=Symbol("Inactive");
    bool alternateSelectionAllowed=true;
    inline static constexpr std::string_view WireName="ORCH.ShadowDecision";
    static constexpr auto Fields(){using Self=ShadowDecision;return std::tuple{NR_ORCH_FIELD(schemaVersion),NR_ORCH_FIELD(policyVersion),NR_ORCH_FIELD(contractVersion),NR_ORCH_FIELD(valid),
        NR_ORCH_FIELD(executionEffect),NR_ORCH_FIELD(proposed),NR_ORCH_FIELD(ranked),NR_ORCH_FIELD(rejected),NR_ORCH_FIELD(rules),
        NR_ORCH_FIELD(next),NR_ORCH_FIELD(lockDisposition),NR_ORCH_FIELD(alternateSelectionAllowed)};}
    C::Error Check()const{return schemaVersion==1&&policyVersion==PolicyVersion&&executionEffect.View()=="None"?C::Error::None:C::Error::UnsupportedVersion;}
    bool operator==(const ShadowDecision&)const=default;
};
#undef NR_ORCH_FIELD
}
