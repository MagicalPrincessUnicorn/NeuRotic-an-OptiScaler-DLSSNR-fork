#pragma once
#include "ProfileRegistry.h"
#include <nr/contracts/C02_Context.h>
#include <nr/contracts/C05_Routing.h>
#include <nr/context/RepresentationLineage.h>

namespace Neurotic::Protocol
{
#define NR_PROTOCOL_FIELD(member) C::Field(#member,&Self::member)
// Immutable projection of the existing CFG-owned render snapshot. No parsing or live Config read.
struct SettingsSnapshot
{
    C::OwnerValueReference source;
    C::BoundedList<C::SemanticClaim,16> controls;
    inline static constexpr std::string_view WireName="RENDER.SettingsSnapshot";
    static constexpr auto Fields(){using Self=SettingsSnapshot;return std::tuple{NR_PROTOCOL_FIELD(source),NR_PROTOCOL_FIELD(controls)};}
    bool operator==(const SettingsSnapshot&)const=default;
};
// Current-use description published by the C12 owner. It conveys no resource rights.
// Plan/source content and all lineage are explicit; protocol does not discover alternatives.
struct RepresentationUse
{
    C::OwnerValueReference source;
    C::ContractRef<C::ContractId::C02> context;
    Purpose purpose=Purpose::ColorModelInput;
    C::Symbol kind;
    C::MetadataRef<C::RepresentationPlan> plan;
    C::MetadataRef<C::ResourceView> currentSource;
    // Exact C01 selected by C12. A native invocation must revalidate this
    // publication at use; the reference grants neither content nor GPU rights.
    std::optional<C::ContractRef<C::ContractId::C01>> sourceCandidate;
    C::MetadataList<C::TransformStep,16> applied,required,consumerDeferred;
    // C12 owner attests the exact consumer representation, not merely equal dimensions.
    // Only this version's legacy binding ABI is accepted. Live authentication is external.
    C::Symbol consumerContract;
    C::OptionalFact<C::MetadataRef<C::PreparedView>> prepared;
    C::OptionalFact<C::ContentRevision> currentContent;
    std::optional<C::MetadataRef<C::NativeSampleIdentityV1>> nativeSample;
    inline static constexpr std::string_view WireName="RENDER.RepresentationUse";
    static constexpr auto Fields(){using Self=RepresentationUse;return std::tuple{NR_PROTOCOL_FIELD(source),NR_PROTOCOL_FIELD(context),
        NR_PROTOCOL_FIELD(purpose),NR_PROTOCOL_FIELD(kind),NR_PROTOCOL_FIELD(plan),NR_PROTOCOL_FIELD(currentSource),NR_PROTOCOL_FIELD(applied),NR_PROTOCOL_FIELD(required),
        NR_PROTOCOL_FIELD(consumerDeferred),NR_PROTOCOL_FIELD(consumerContract),NR_PROTOCOL_FIELD(prepared),NR_PROTOCOL_FIELD(currentContent),
        NR_PROTOCOL_FIELD(nativeSample),NR_PROTOCOL_FIELD(sourceCandidate)};}
    bool operator==(const RepresentationUse&)const=default;
};
struct Binding
{
    Purpose purpose=Purpose::ColorModelInput;
    C::Symbol key,mode,effectiveSource,rule,stage=Symbol("Evaluate");
    C::OptionalFact<C::ScalarValue> raw,effective;
    std::optional<C::ContractRef<C::ContractId::C12>> representation;
    inline static constexpr std::string_view WireName="RENDER.Binding";
    static constexpr auto Fields(){using Self=Binding;return std::tuple{NR_PROTOCOL_FIELD(purpose),NR_PROTOCOL_FIELD(key),NR_PROTOCOL_FIELD(mode),
        NR_PROTOCOL_FIELD(effectiveSource),NR_PROTOCOL_FIELD(rule),NR_PROTOCOL_FIELD(stage),NR_PROTOCOL_FIELD(raw),NR_PROTOCOL_FIELD(effective),NR_PROTOCOL_FIELD(representation)};}
    bool operator==(const Binding&)const=default;
};
struct BindingPlan
{
    std::uint32_t schemaVersion=1;
    C::RecordKey recipe;C::EvaluationId evaluation;
    C::BoundedList<Binding,64> bindings;
    bool modelEvaluation=false,historyAdvanceRequested=false;
    inline static constexpr std::string_view WireName="RENDER.BindingPlan";
    static constexpr auto Fields(){using Self=BindingPlan;return std::tuple{NR_PROTOCOL_FIELD(schemaVersion),NR_PROTOCOL_FIELD(recipe),
        NR_PROTOCOL_FIELD(evaluation),NR_PROTOCOL_FIELD(bindings),NR_PROTOCOL_FIELD(modelEvaluation),NR_PROTOCOL_FIELD(historyAdvanceRequested)};}
    C::Error Check()const{return schemaVersion==1?C::Error::None:C::Error::UnsupportedVersion;}
    bool operator==(const BindingPlan&)const=default;
};
struct DependencyDeclaration
{
    C::ProfileKey profile;C::Symbol strategy;C::Placement placement=C::Placement::NativeAfter;
    C::OptionalFact<C::ProviderIncarnation> provider;
    C::OwnerValueReference settings;
    C::BoundedList<C::RecordKey,8> structuralPlans;
    C::MetadataRef<C::HistoryKey> history;
    inline static constexpr std::string_view WireName="RENDER.DependencyDeclaration";
    static constexpr auto Fields(){using Self=DependencyDeclaration;return std::tuple{NR_PROTOCOL_FIELD(profile),NR_PROTOCOL_FIELD(strategy),
        NR_PROTOCOL_FIELD(placement),NR_PROTOCOL_FIELD(provider),NR_PROTOCOL_FIELD(settings),NR_PROTOCOL_FIELD(structuralPlans),NR_PROTOCOL_FIELD(history)};}
    bool operator==(const DependencyDeclaration&)const=default;
};
// Nonexecuting profile-owner input; it has no executable C06 or committed plan.
struct NativeProfilePreparationRequest
{
    Family family=Family::LegacyCompatibleNativeDlssNr;std::uint32_t profileVersion=1;
    C::ProfileKey requestedProfile;C::GraphicsApi api=C::GraphicsApi::D3D12;C::Placement placement=C::Placement::NativeAfter;
    C::Symbol strategy=Symbol("NativeTemporal"),bypassReason;
    RuntimeContract runtime;
    C::RecordHeader profileHeader{C::ContractId::C04};
    C::EvaluationId evaluation;
    C::MetadataRef<C::CanonicalFrameContext> context;C::ContractRef<C::ContractId::C02> currentContext;
    C::MetadataRef<C::QualificationCertificate> semantic;
    C::MetadataRef<SettingsSnapshot> settings;
    C::BoundedList<RepresentationUse,8> representations;
    C::BoundedList<Purpose,16> requestedPurposes;
    C::MetadataRef<C::HistoryKey> history;
    C::OptionalFact<C::ContractRef<C::ContractId::C08>> resetProjection;
};
struct RecipeRequest:NativeProfilePreparationRequest
{
    C::RecordHeader recipeHeader{C::ContractId::C06};
    C::ContractRef<C::ContractId::C05> externalPlan;
    // Exact retained metadata only; current-route admission remains with ORCH.
    std::optional<C::MetadataRef<C::PlanCommit>> nativePlan;
};
// Adapter values accompany, rather than redefine, frozen C06. All members are derived
// together from the exact immutable inputs; decoded fixtures confer no live authority.
struct RecipeProduct
{
    C::NrExecutionRecipe recipe;
    C::QualificationCertificate profileCertificate;
    std::optional<C::ContractRef<C::ContractId::C04>> semanticCertificate;
    C::MetadataRef<C::CanonicalFrameContext> inputContext;
    std::optional<C::MetadataRef<C::QualificationCertificate>> inputSemantic;
    C::MetadataRef<SettingsSnapshot> inputSettings;
    C::BoundedList<RepresentationUse,8> inputRepresentations;
    std::optional<RuntimeContract> runtime;
    BindingPlan bindingPlan;
    DependencyDeclaration dependencies;
    C::Symbol disposition,bypassReason;
    inline static constexpr std::string_view WireName="RENDER.RecipeProduct";
    static constexpr auto Fields(){using Self=RecipeProduct;return std::tuple{NR_PROTOCOL_FIELD(recipe),NR_PROTOCOL_FIELD(profileCertificate),
        NR_PROTOCOL_FIELD(semanticCertificate),NR_PROTOCOL_FIELD(inputContext),NR_PROTOCOL_FIELD(inputSemantic),NR_PROTOCOL_FIELD(inputSettings),
        NR_PROTOCOL_FIELD(inputRepresentations),NR_PROTOCOL_FIELD(runtime),NR_PROTOCOL_FIELD(bindingPlan),NR_PROTOCOL_FIELD(dependencies),NR_PROTOCOL_FIELD(disposition),NR_PROTOCOL_FIELD(bypassReason)};}
    bool operator==(const RecipeProduct&)const=default;
};
struct RecipeBuildResult
{
    std::optional<C::ImmutableRecord<RecipeProduct>> product;
    C::Symbol reason;
};
#undef NR_PROTOCOL_FIELD
}
