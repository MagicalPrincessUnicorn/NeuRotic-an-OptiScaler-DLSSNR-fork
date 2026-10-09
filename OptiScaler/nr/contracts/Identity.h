#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "Evidence.h"

namespace Neurotic::Contracts
{
enum class IdentityKind : std::uint32_t
{
    SessionId,
    RenderStreamId,
    ViewId,
    EpisodeId,
    BaseRealFrameId,
    RenderId,
    EvaluationId,
    PresentId,
    GeneratedFrameId,
    FinalSealId,
    ObjectIncarnation,
    ResourceIncarnation,
    ResourceViewIncarnation,
    ProviderIncarnation,
    SwapchainGeneration,
    ResourceGeneration,
    RepresentationGeneration,
    RasterGeneration,
    ColorGeneration,
    ExposureEpoch,
    RRGeneration,
    RouteGeneration,
    ModelGeneration,
    HistoryGeneration,
    FgPolicyGeneration,
    HandoffContractGeneration,
    InterceptedSourceTransactionIdV1
};
template<> struct EnumTraits<IdentityKind>
{
    inline static constexpr auto Values = std::array {
        std::pair {IdentityKind::SessionId, std::string_view {"SessionId"}},
        std::pair {IdentityKind::RenderStreamId, std::string_view {"RenderStreamId"}},
        std::pair {IdentityKind::ViewId, std::string_view {"ViewId"}},
        std::pair {IdentityKind::EpisodeId, std::string_view {"EpisodeId"}},
        std::pair {IdentityKind::BaseRealFrameId, std::string_view {"BaseRealFrameId"}},
        std::pair {IdentityKind::RenderId, std::string_view {"RenderId"}},
        std::pair {IdentityKind::EvaluationId, std::string_view {"EvaluationId"}},
        std::pair {IdentityKind::PresentId, std::string_view {"PresentId"}},
        std::pair {IdentityKind::GeneratedFrameId, std::string_view {"GeneratedFrameId"}},
        std::pair {IdentityKind::FinalSealId, std::string_view {"FinalSealId"}},
        std::pair {IdentityKind::ObjectIncarnation, std::string_view {"ObjectIncarnation"}},
        std::pair {IdentityKind::ResourceIncarnation, std::string_view {"ResourceIncarnation"}},
        std::pair {IdentityKind::ResourceViewIncarnation, std::string_view {"ResourceViewIncarnation"}},
        std::pair {IdentityKind::ProviderIncarnation, std::string_view {"ProviderIncarnation"}},
        std::pair {IdentityKind::SwapchainGeneration, std::string_view {"SwapchainGeneration"}},
        std::pair {IdentityKind::ResourceGeneration, std::string_view {"ResourceGeneration"}},
        std::pair {IdentityKind::RepresentationGeneration, std::string_view {"RepresentationGeneration"}},
        std::pair {IdentityKind::RasterGeneration, std::string_view {"RasterGeneration"}},
        std::pair {IdentityKind::ColorGeneration, std::string_view {"ColorGeneration"}},
        std::pair {IdentityKind::ExposureEpoch, std::string_view {"ExposureEpoch"}},
        std::pair {IdentityKind::RRGeneration, std::string_view {"RRGeneration"}},
        std::pair {IdentityKind::RouteGeneration, std::string_view {"RouteGeneration"}},
        std::pair {IdentityKind::ModelGeneration, std::string_view {"ModelGeneration"}},
        std::pair {IdentityKind::HistoryGeneration, std::string_view {"HistoryGeneration"}},
        std::pair {IdentityKind::FgPolicyGeneration, std::string_view {"FgPolicyGeneration"}},
        std::pair {IdentityKind::HandoffContractGeneration, std::string_view {"HandoffContractGeneration"}},
        std::pair {IdentityKind::InterceptedSourceTransactionIdV1, std::string_view {"InterceptedSourceTransactionIdV1"}}
    };
};

inline constexpr bool IsGenerationKind(IdentityKind kind)
{
    switch (kind)
    {
    case IdentityKind::ObjectIncarnation:
    case IdentityKind::ResourceIncarnation:
    case IdentityKind::ResourceViewIncarnation:
    case IdentityKind::ProviderIncarnation:
    case IdentityKind::SwapchainGeneration:
    case IdentityKind::ResourceGeneration:
    case IdentityKind::RepresentationGeneration:
    case IdentityKind::RasterGeneration:
    case IdentityKind::ColorGeneration:
    case IdentityKind::ExposureEpoch:
    case IdentityKind::RRGeneration:
    case IdentityKind::RouteGeneration:
    case IdentityKind::ModelGeneration:
    case IdentityKind::HistoryGeneration:
    case IdentityKind::FgPolicyGeneration:
    case IdentityKind::HandoffContractGeneration:
        return true;
    default:
        return false;
    }
}

struct StableIdentity
{
    IdentityKind kind = IdentityKind::SessionId;
    Symbol nameSpace {};
    Symbol issuer {};
    std::uint64_t value {};
    inline static constexpr std::string_view WireName = "StableIdentity";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("kind", &StableIdentity::kind),
            Field("nameSpace", &StableIdentity::nameSpace),
            Field("issuer", &StableIdentity::issuer),
            Field("value", &StableIdentity::value)
        };
    }
    Error Check() const
    {
        return nameSpace.Empty() || issuer.Empty() ? Error::MissingProvenance : Error::None;
    }
    bool operator==(const StableIdentity&) const = default;
};

template<IdentityKind K> struct ScopedIdentity
{
    Symbol nameSpace;
    Symbol issuer;
    std::uint64_t value = 0;
    inline static constexpr IdentityKind Kind = K;
    StableIdentity Describe() const { return {K, nameSpace, issuer, value}; }
    static constexpr auto Fields()
    {
        return std::tuple {Field("nameSpace", &ScopedIdentity::nameSpace),
                           Field("issuer", &ScopedIdentity::issuer), Field("value", &ScopedIdentity::value)};
    }
    Error Check() const { return Describe().Check(); }
    bool operator==(const ScopedIdentity&) const = default;
};
using InterceptedSourceTransactionIdV1 = ScopedIdentity<IdentityKind::InterceptedSourceTransactionIdV1>;
using SessionId = ScopedIdentity<IdentityKind::SessionId>;
using RenderStreamId = ScopedIdentity<IdentityKind::RenderStreamId>;
using ViewId = ScopedIdentity<IdentityKind::ViewId>;
using EpisodeId = ScopedIdentity<IdentityKind::EpisodeId>;
using BaseRealFrameId = ScopedIdentity<IdentityKind::BaseRealFrameId>;
using RenderId = ScopedIdentity<IdentityKind::RenderId>;
using EvaluationId = ScopedIdentity<IdentityKind::EvaluationId>;
using PresentId = ScopedIdentity<IdentityKind::PresentId>;
using GeneratedFrameId = ScopedIdentity<IdentityKind::GeneratedFrameId>;
using FinalSealId = ScopedIdentity<IdentityKind::FinalSealId>;
using ObjectIncarnation = ScopedIdentity<IdentityKind::ObjectIncarnation>;
using ResourceIncarnation = ScopedIdentity<IdentityKind::ResourceIncarnation>;
using ResourceViewIncarnation = ScopedIdentity<IdentityKind::ResourceViewIncarnation>;
using ProviderIncarnation = ScopedIdentity<IdentityKind::ProviderIncarnation>;
using SwapchainGeneration = ScopedIdentity<IdentityKind::SwapchainGeneration>;
using ResourceGeneration = ScopedIdentity<IdentityKind::ResourceGeneration>;
using RepresentationGeneration = ScopedIdentity<IdentityKind::RepresentationGeneration>;
using RasterGeneration = ScopedIdentity<IdentityKind::RasterGeneration>;
using ColorGeneration = ScopedIdentity<IdentityKind::ColorGeneration>;
using ExposureEpoch = ScopedIdentity<IdentityKind::ExposureEpoch>;
using RRGeneration = ScopedIdentity<IdentityKind::RRGeneration>;
using RouteGeneration = ScopedIdentity<IdentityKind::RouteGeneration>;
using ModelGeneration = ScopedIdentity<IdentityKind::ModelGeneration>;
using HistoryGeneration = ScopedIdentity<IdentityKind::HistoryGeneration>;
using FgPolicyGeneration = ScopedIdentity<IdentityKind::FgPolicyGeneration>;
using HandoffContractGeneration = ScopedIdentity<IdentityKind::HandoffContractGeneration>;

struct GenerationToken
{
    StableIdentity identity {};
    inline static constexpr std::string_view WireName = "GenerationToken";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("identity", &GenerationToken::identity)
        };
    }
    Error Check() const
    {
        return IsGenerationKind(identity.kind) ? Error::None : Error::WrongIdentityKind;
    }
    bool operator==(const GenerationToken&) const = default;
};

class GenerationVector
{
    BoundedList<GenerationToken, 64> entries_;
  public:
    Error Insert(const GenerationToken& token)
    {
        if (token.identity.Check() != Error::None)
            return Error::MissingProvenance;
        if (token.Check() != Error::None)
            return Error::WrongIdentityKind;
        for (const auto& entry : entries_)
            if (entry.identity.kind == token.identity.kind && entry.identity.nameSpace == token.identity.nameSpace &&
                entry.identity.issuer == token.identity.issuer)
                return entry.identity.value == token.identity.value ? Error::DuplicateEntry : Error::ContradictoryEntry;
        if (!entries_.Push(token))
            return Error::LimitExceeded;
        std::sort(entries_.begin(), entries_.end(), [](const GenerationToken& a, const GenerationToken& b) {
            return std::tuple {EnumName(a.identity.kind), a.identity.nameSpace.View(), a.identity.issuer.View()} <
                   std::tuple {EnumName(b.identity.kind), b.identity.nameSpace.View(), b.identity.issuer.View()};
        });
        return Error::None;
    }
    const auto& Entries() const noexcept { return entries_; }
    bool operator==(const GenerationVector&) const = default;
};
inline Error CheckIdentityFact(const OptionalFact<StableIdentity>& fact, IdentityKind expected)
{
    return fact.IsKnown() && fact.KnownPart()->value.kind != expected ? Error::WrongIdentityKind : Error::None;
}
inline Error CheckGenerationFact(const OptionalFact<GenerationToken>& fact, IdentityKind expected)
{
    return fact.IsKnown() && fact.KnownPart()->value.identity.kind != expected ? Error::WrongIdentityKind : Error::None;
}
struct ResourceIdentityToken
{
    OptionalFact<StableIdentity> objectIncarnation {};
    OptionalFact<StableIdentity> resourceIncarnation {};
    OptionalFact<StableIdentity> resourceViewIncarnation {};
    OptionalFact<GenerationToken> resourceGeneration {};
    OptionalFact<GenerationToken> representationGeneration {};
    OptionalFact<ContentRevision> contentRevision {};
    inline static constexpr std::string_view WireName = "ResourceIdentityToken";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("objectIncarnation", &ResourceIdentityToken::objectIncarnation),
            Field("resourceIncarnation", &ResourceIdentityToken::resourceIncarnation),
            Field("resourceViewIncarnation", &ResourceIdentityToken::resourceViewIncarnation),
            Field("resourceGeneration", &ResourceIdentityToken::resourceGeneration),
            Field("representationGeneration", &ResourceIdentityToken::representationGeneration),
            Field("contentRevision", &ResourceIdentityToken::contentRevision)
        };
    }
    Error Check() const
    {
        if (CheckIdentityFact(objectIncarnation, IdentityKind::ObjectIncarnation) != Error::None ||
            CheckIdentityFact(resourceIncarnation, IdentityKind::ResourceIncarnation) != Error::None ||
            CheckIdentityFact(resourceViewIncarnation, IdentityKind::ResourceViewIncarnation) != Error::None ||
            CheckGenerationFact(resourceGeneration, IdentityKind::ResourceGeneration) != Error::None ||
            CheckGenerationFact(representationGeneration, IdentityKind::RepresentationGeneration) != Error::None)
            return Error::WrongIdentityKind;
        return Error::None;
    }
    bool operator==(const ResourceIdentityToken&) const = default;
};

// W03-SPECTRE-AMENDMENT-1: Native source order is not a real-frame ID.
struct NativeSampleIdentityV1
{
    std::uint32_t version=1;
    SessionId session;
    RenderStreamId stream;
    ViewId view;
    ObjectIncarnation feature,ingress;
    std::uint64_t producerOrdinal=0;
    RecordKey callback;
    inline static constexpr std::string_view WireName="NativeSampleIdentityV1";
    static constexpr auto Fields()
    {
        return std::tuple{Field("version",&NativeSampleIdentityV1::version),Field("session",&NativeSampleIdentityV1::session),
            Field("stream",&NativeSampleIdentityV1::stream),Field("view",&NativeSampleIdentityV1::view),
            Field("feature",&NativeSampleIdentityV1::feature),Field("ingress",&NativeSampleIdentityV1::ingress),
            Field("producerOrdinal",&NativeSampleIdentityV1::producerOrdinal),Field("callback",&NativeSampleIdentityV1::callback)};
    }
    Error Check()const
    {
        if(version!=1)return Error::UnsupportedVersion;
        if(session.Check()!=Error::None||stream.Check()!=Error::None||view.Check()!=Error::None||
            feature.Check()!=Error::None||ingress.Check()!=Error::None||callback.Check()!=Error::None)return Error::MissingProvenance;
        return producerOrdinal?Error::None:Error::MissingProvenance;
    }
    // Callback metadata is checked separately, never a uniqueness dimension.
    // Route, evaluation and reset cannot create a second sample.
    bool SameSample(const NativeSampleIdentityV1& other)const noexcept
    {
        return Check()==Error::None&&other.Check()==Error::None&&
            session==other.session&&stream==other.stream&&view==other.view&&feature==other.feature&&
            ingress==other.ingress&&producerOrdinal==other.producerOrdinal;
    }
    bool operator==(const NativeSampleIdentityV1&)const=default;
};

// No clock, pointer, counter or presentation cadence is converted into frame association here.
struct FrameIdentity
{
    OptionalFact<SessionId> sessionId {};
    OptionalFact<RenderStreamId> renderStreamId {};
    OptionalFact<ViewId> viewId {};
    OptionalFact<EpisodeId> episodeId {};
    OptionalFact<BaseRealFrameId> baseRealFrameId {};
    OptionalFact<RenderId> renderId {};
    OptionalFact<EvaluationId> evaluationId {};
    OptionalFact<PresentId> presentId {};
    OptionalFact<GeneratedFrameId> generatedFrameId {};
    OptionalFact<FinalSealId> finalSealId {};
    std::optional<MetadataRef<NativeSampleIdentityV1>> nativeSample;
    BoundedList<EvidenceRef, 16> associationEvidence {};
    inline static constexpr std::string_view WireName = "FrameIdentity";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("sessionId", &FrameIdentity::sessionId),
            Field("renderStreamId", &FrameIdentity::renderStreamId),
            Field("viewId", &FrameIdentity::viewId),
            Field("episodeId", &FrameIdentity::episodeId),
            Field("baseRealFrameId", &FrameIdentity::baseRealFrameId),
            Field("renderId", &FrameIdentity::renderId),
            Field("evaluationId", &FrameIdentity::evaluationId),
            Field("presentId", &FrameIdentity::presentId),
            Field("generatedFrameId", &FrameIdentity::generatedFrameId),
            Field("finalSealId", &FrameIdentity::finalSealId),
            Field("nativeSample", &FrameIdentity::nativeSample),
            Field("associationEvidence", &FrameIdentity::associationEvidence)
        };
    }
    bool operator==(const FrameIdentity&) const = default;
};

} // namespace Neurotic::Contracts
