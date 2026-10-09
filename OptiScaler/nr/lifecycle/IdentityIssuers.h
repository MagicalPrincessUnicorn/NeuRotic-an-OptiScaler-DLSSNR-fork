#pragma once
// NR-LIFE-001 candidate. Identity bookkeeping only; never GPU lifetime management.
#include "IdentityPrimitives.h"
#include "NativeEvaluationAssociation.h"
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string_view>
#include <system_error>

namespace Neurotic::Lifecycle
{
// This is an observed disposition, not a C14 identity kind or a classifier from timing.
enum class RenderObservation { Unknown, NewGameContent, ExistingGameContent, GeneratedOutput, RepeatedContent };
struct RenderEvidence
{
    OwnerEvent event {};
    RenderObservation observation = RenderObservation::Unknown;
};
class HostIdentityDomain;
namespace Detail
{
// Non-owning implementation handles. Never serializable, and never survive their host's lifetime.
// Only HostIdentityDomain constructs the public owner facades.
struct ChannelHandle { HostIdentityDomain* host; std::size_t slot; std::uint64_t session; };
enum class ChannelRole
{
    Topology, Episode, Object, Render, Evaluation, Present, Resource, Provider, Swapchain, Representation, Raster, Color, Rr, Route, Model, History, FgPolicy, Handoff,
    Finalizer
};
}

class TopologyIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit TopologyIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::RenderStreamId> Stream(const OwnerEvent& event) const;
    Outcome<C::ViewId> View(const OwnerEvent& event) const;
};

class EpisodeIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit EpisodeIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::EpisodeId> Episode(const OwnerEvent& event) const;
};

class ObjectIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit ObjectIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::ObjectIncarnation> Incarnation(const OwnerEvent& event) const;
};

class RenderIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit RenderIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::RenderId> Render(const OwnerEvent& event) const;
    Outcome<C::BaseRealFrameId> NewBase(const RenderEvidence& evidence) const;
    // Names an enrolled source-use observation, never an original game frame.
    Outcome<C::InterceptedSourceTransactionIdV1> SourceTransaction(const OwnerEvent& event) const;
};

class EvaluationIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit EvaluationIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::EvaluationId> Evaluation(const OwnerEvent& event) const;
    std::optional<NativeEvaluationAssociation> NativeEvaluation(const C::NativeSampleIdentityV1& sample,
        C::EpisodeId episode,const OwnerEvent& event)const;
};

class PresentIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit PresentIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::PresentId> Present(const OwnerEvent& event) const;
};

class ResourceIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit ResourceIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::ObjectIncarnation> Object(const OwnerEvent& event) const;
    Outcome<C::ResourceIncarnation> Resource(const OwnerEvent& event) const;
    Outcome<C::ResourceViewIncarnation> View(const OwnerEvent& event) const;
    Outcome<C::ResourceGeneration> Structure(const OwnerEvent& event) const;
    Outcome<C::ContentRevision> ContentWrite(const OwnerEvent& event,const C::ResourceIncarnation& resource) const;
};

class ProviderIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit ProviderIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::ProviderIncarnation> Incarnation(const OwnerEvent& event) const;
};

class SwapchainIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit SwapchainIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::ObjectIncarnation> Object(const OwnerEvent& event) const;
    Outcome<C::SwapchainGeneration> Recreated(const OwnerEvent& event) const;
};

class RepresentationIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit RepresentationIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::RepresentationGeneration> Structure(const OwnerEvent& event) const;
};

class RasterIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit RasterIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::RasterGeneration> Changed(const OwnerEvent& event) const;
};

class ColorIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit ColorIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::ColorGeneration> Changed(const OwnerEvent& event) const;
    Outcome<C::ExposureEpoch> ExposureDiscontinuity(const OwnerEvent& event) const;
};

class RrIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit RrIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::RRGeneration> TopologyChanged(const OwnerEvent& event) const;
};

class RouteIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit RouteIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::RouteGeneration> CommittedChange(const OwnerEvent& event) const;
};

class ModelIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit ModelIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::ModelGeneration> Changed(const OwnerEvent& event) const;
};

class HistoryIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit HistoryIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::HistoryGeneration> AppliedReset(const OwnerEvent& event) const;
};

class FgPolicyIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit FgPolicyIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::FgPolicyGeneration> PolicyChange(const OwnerEvent& event) const;
};

class HandoffIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit HandoffIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::HandoffContractGeneration> ContractChanged(const OwnerEvent& event) const;
};

// FinalSealId belongs only to the finalizer. Handoff also admits the FG owner,
// so it must never expose this capability. The host outlives all facade copies.
class FinalizerIssuer
{
    friend class HostIdentityDomain;
    Detail::ChannelHandle handle_;
    explicit FinalizerIssuer(Detail::ChannelHandle handle) : handle_(handle) {}
  public:
    Outcome<C::FinalSealId> FinalSeal(const OwnerEvent& event) const;
};

// One host-owned instance per explicitly unique bootstrap namespace. The host must own this object,
// serialize its destruction, and give each subsystem only its designated facade. No process-global
// singleton or diagnostic fallback is installed. Constructor/binding do not authenticate a caller.
// NamespaceCapacity and the root-name bound are candidate engineering limits, not G1 ABI limits.
class HostIdentityDomain
{
    friend class TopologyIssuer;
    friend class EpisodeIssuer;
    friend class ObjectIssuer;
    friend class RenderIssuer;
    friend class EvaluationIssuer;
    friend class PresentIssuer;
    friend class ResourceIssuer;
    friend class ProviderIssuer;
    friend class SwapchainIssuer;
    friend class RepresentationIssuer;
    friend class RasterIssuer;
    friend class ColorIssuer;
    friend class RrIssuer;
    friend class RouteIssuer;
    friend class ModelIssuer;
    friend class HistoryIssuer;
    friend class FgPolicyIssuer;
    friend class HandoffIssuer;
    friend class FinalizerIssuer;

  public:
    static constexpr std::size_t NamespaceCapacity=64;
  private:
    static constexpr std::size_t KindCount=C::EnumTraits<C::IdentityKind>::Values.size();
    struct Counter
    {
        std::uint64_t value=0;
        std::uint64_t lastEvidence=0;
        bool hasEvidence=false;
    };
    struct Entry
    {
        OwnerBinding binding {};
        C::Symbol nameSpace {};
        Detail::ChannelRole role=Detail::ChannelRole::Topology;
        std::uint64_t session=0;
        std::array<Counter,KindCount> ids {};
        Counter contents {};
        std::uint64_t latestEvidence=0;
        bool hasEvidence=false;
    };
    C::Symbol root_ {};
    std::uint64_t ceiling_;
    std::atomic_flag entered_=ATOMIC_FLAG_INIT;
    std::array<Entry,NamespaceCapacity> entries_ {};
    std::size_t used_=0;
    Counter sessions_ {};
    std::optional<OwnerBinding> sessionBinding_;
    C::SessionId active_ {};
    C::EvidenceRef sessionEvidence_ {};
    bool open_=false;
    class TryGuard
    {
        std::atomic_flag& flag_;
        bool acquired_;
      public:
        explicit TryGuard(std::atomic_flag& flag) noexcept : flag_(flag),acquired_(!flag_.test_and_set(std::memory_order_acquire)) {}
        ~TryGuard() { if (acquired_) flag_.clear(std::memory_order_release); }
        bool Acquired() const noexcept { return acquired_; }
        TryGuard(const TryGuard&)=delete;
        TryGuard& operator=(const TryGuard&)=delete;
    };
    bool RootValid() const noexcept { return !root_.Empty() && root_.View().size()<=48 && ceiling_!=0; }
    bool Namespace(C::Symbol& out,std::uint64_t session,std::size_t slot) const
    {
        std::array<char,96> b {};
        auto p=b.data();const auto end=b.data()+b.size();
        for (char ch : root_.View()) *p++=ch;
        *p++='.';*p++='s';
        auto s=std::to_chars(p,end,session);if (s.ec!=std::errc{}) return false;p=s.ptr;
        if (end-p<2) return false;*p++='.';*p++='n';
        auto n=std::to_chars(p,end,slot);if (n.ec!=std::errc{}) return false;
        return out.Assign(std::string_view(b.data(),static_cast<std::size_t>(n.ptr-b.data())));
    }
    bool SessionNamespace(C::Symbol& out) const
    {
        std::array<char,64> b {};std::size_t size=0;
        for (char ch : root_.View()) b[size++]=ch;
        for (char ch : std::string_view(".sessions")) b[size++]=ch;
        return out.Assign(std::string_view(b.data(),size));
    }
    // Registration declares owner evidence record values as a monotonic event sequence in this
    // subject's evidenceNamespace. One event may establish different kinds; the same kind cannot
    // be issued twice for it. This is NOT an assumption about arbitrary RecordKeys or game counters.
    IdentityStatus Next(const Counter& counter,const OwnerEvent& event,std::uint64_t& next) const
    {
        if (counter.hasEvidence && event.evidence.record.value<=counter.lastEvidence) return IdentityStatus::StaleEvidence;
        return CheckedSuccessor(counter.value,ceiling_,next);
    }
    static void Commit(Counter& counter,const OwnerEvent& event,std::uint64_t next) noexcept
    {
        counter.value=next;counter.lastEvidence=event.evidence.record.value;counter.hasEvidence=true;
    }
    Entry* Select(const Detail::ChannelHandle& h,Detail::ChannelRole role) noexcept
    {
        if (h.host!=this || !open_ || h.session!=active_.value || h.slot>=used_) return nullptr;
        auto& e=entries_[h.slot];
        return e.session==h.session && e.role==role ? &e : nullptr;
    }
    template<C::IdentityKind K> static constexpr std::size_t KindIndex() noexcept
    {
        for (std::size_t i=0;i<KindCount;++i) if (C::EnumTraits<C::IdentityKind>::Values[i].first==K) return i;
        return KindCount;
    }
    template<C::IdentityKind K>
    Outcome<C::ScopedIdentity<K>> Issue(const Detail::ChannelHandle& handle,Detail::ChannelRole role,const OwnerEvent& event)
    {return IssueInSession<K>(handle,role,event,nullptr);}
    template<C::IdentityKind K>
    Outcome<C::ScopedIdentity<K>> IssueInSession(const Detail::ChannelHandle& handle,Detail::ChannelRole role,const OwnerEvent& event,
        const C::SessionId* requiredSession)
    {
        TryGuard guard(entered_);
        if (!guard.Acquired()) return Refuse<C::ScopedIdentity<K>>(IdentityStatus::Busy);
        auto* e=Select(handle,role);
        if (!e) return Refuse<C::ScopedIdentity<K>>(IdentityStatus::Closed);
        if(requiredSession&&*requiredSession!=active_)return Refuse<C::ScopedIdentity<K>>(IdentityStatus::InvalidScope);
        const C::ScopeRef scope{e->binding.subject};
        auto status=CheckOwnerEvent(e->binding,event);
        if (status!=IdentityStatus::Ok) return Refuse<C::ScopedIdentity<K>>(status,scope);
        if (e->hasEvidence && event.evidence.record.value<e->latestEvidence)
            return Refuse<C::ScopedIdentity<K>>(IdentityStatus::StaleEvidence,scope);
        constexpr auto index=KindIndex<K>();
        static_assert(index<KindCount);
        auto& counter=e->ids[index];std::uint64_t next=0;
        status=Next(counter,event,next);
        if (status!=IdentityStatus::Ok) return Refuse<C::ScopedIdentity<K>>(status,scope);
        C::ScopedIdentity<K> id{e->nameSpace,e->binding.publisher.issuer,next};
        Commit(counter,event,next);
        e->latestEvidence=event.evidence.record.value;e->hasEvidence=true;
        return Established(id,event.evidence);
    }
    Outcome<C::ContentRevision> WriteContents(const Detail::ChannelHandle& handle,const OwnerEvent& event,
                                              const C::ResourceIncarnation& resource)
    {
        TryGuard guard(entered_);
        if (!guard.Acquired()) return Refuse<C::ContentRevision>(IdentityStatus::Busy);
        auto* e=Select(handle,Detail::ChannelRole::Resource);
        if (!e) return Refuse<C::ContentRevision>(IdentityStatus::Closed);
        const C::ScopeRef scope{e->binding.subject};
        auto status=CheckOwnerEvent(e->binding,event);
        if (status!=IdentityStatus::Ok) return Refuse<C::ContentRevision>(status,scope);
        if (e->hasEvidence && event.evidence.record.value<e->latestEvidence)
            return Refuse<C::ContentRevision>(IdentityStatus::StaleEvidence,scope);
        const auto& backing=e->ids[KindIndex<C::IdentityKind::ResourceIncarnation>()];
        if (!backing.hasEvidence) return Refuse<C::ContentRevision>(IdentityStatus::Unknown,scope);
        const C::ResourceIncarnation current{e->nameSpace,e->binding.publisher.issuer,backing.value};
        if (resource.Check()!=C::Error::None) return Refuse<C::ContentRevision>(IdentityStatus::InvalidScope,scope);
        if (resource!=current) return Refuse<C::ContentRevision>(IdentityStatus::Different,scope);
        std::uint64_t next=0;status=Next(e->contents,event,next);
        if (status!=IdentityStatus::Ok) return Refuse<C::ContentRevision>(status,scope);
        Commit(e->contents,event,next);
        e->latestEvidence=event.evidence.record.value;e->hasEvidence=true;
        // The revision is scoped by the required resource argument. Consumers must keep that pair.
        // No resource/representation/route/history generation is read or changed here.
        return Established(C::ContentRevision{next},event.evidence);
    }
    template<class... Owners>
    std::optional<Detail::ChannelHandle> Bind(const OwnerBinding& binding,Detail::ChannelRole role,Owners... allowed)
    {
        TryGuard guard(entered_);
        if (!guard.Acquired() || !open_ || !RootValid() || !ValidBinding(binding) ||
            !((binding.owner==allowed)||...)) return std::nullopt;
        // Reject re-registration of the same subject in this role even when the publisher changes.
        // A new operational owner must receive an explicitly new subject, not reset this issuer.
        for (std::size_t i=0;i<used_;++i)
            if (entries_[i].session==active_.value && entries_[i].role==role &&
                entries_[i].binding.subject==binding.subject) return std::nullopt;
        if (used_==NamespaceCapacity) return std::nullopt;
        C::Symbol ns;
        if (!Namespace(ns,active_.value,used_)) return std::nullopt;
        auto& e=entries_[used_];e.binding=binding;e.role=role;e.session=active_.value;e.nameSpace=ns;
        const Detail::ChannelHandle handle{this,used_,active_.value};
        ++used_;
        return handle;
    }
  public:
    explicit HostIdentityDomain(const C::Symbol& uniqueProcessNamespace,
        std::uint64_t issuanceCeiling=(std::numeric_limits<std::uint64_t>::max)())
        : root_(uniqueProcessNamespace),ceiling_(issuanceCeiling) {}
    HostIdentityDomain(const HostIdentityDomain&)=delete;
    HostIdentityDomain& operator=(const HostIdentityDomain&)=delete;
    HostIdentityDomain(HostIdentityDomain&&)=delete;
    HostIdentityDomain& operator=(HostIdentityDomain&&)=delete;
    Outcome<C::SessionId> OpenSession(const OwnerEvent& event)
    {
        TryGuard guard(entered_);
        if (!guard.Acquired()) return Refuse<C::SessionId>(IdentityStatus::Busy);
        if (open_) return Refuse<C::SessionId>(IdentityStatus::AlreadyOpen);
        if (!RootValid()) return Refuse<C::SessionId>(IdentityStatus::InvalidScope);
        if (event.owner!=C::OwnerDomain::Session) return Refuse<C::SessionId>(IdentityStatus::WrongOwner);
        OwnerBinding binding{C::OwnerDomain::Session,event.publisher,event.subject,event.evidence.record.nameSpace};
        if (sessionBinding_) binding=*sessionBinding_;
        auto status=CheckOwnerEvent(binding,event);
        if (status!=IdentityStatus::Ok) return Refuse<C::SessionId>(status);
        std::uint64_t next=0;status=Next(sessions_,event,next);
        if (status!=IdentityStatus::Ok) return Refuse<C::SessionId>(status,{event.subject});
        C::Symbol ns;if (!SessionNamespace(ns)) return Refuse<C::SessionId>(IdentityStatus::InvalidScope);
        active_={ns,event.publisher.issuer,next};sessionEvidence_=event.evidence;
        Commit(sessions_,event,next);sessionBinding_=binding;open_=true;
        return Established(active_,event.evidence);
    }
    Outcome<C::SessionId> CurrentSession()
    {
        TryGuard guard(entered_);
        if (!guard.Acquired()) return Refuse<C::SessionId>(IdentityStatus::Busy);
        return open_ ? Established(active_,sessionEvidence_) : Refuse<C::SessionId>(IdentityStatus::Closed);
    }
    IdentityStatus CloseSession(const C::SessionId& expected)
    {
        TryGuard guard(entered_);
        if (!guard.Acquired()) return IdentityStatus::Busy;
        if (!open_) return IdentityStatus::Closed;
        if (expected!=active_) return IdentityStatus::Different;
        open_=false; // Invalidates descriptors only. No resource is released, reset, or waited on.
        return IdentityStatus::Ok;
    }
    std::optional<TopologyIssuer> BindTopology(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Topology,C::OwnerDomain::IdentityRegistry,C::OwnerDomain::Topology);
        if (!handle) return std::nullopt;
        return TopologyIssuer(*handle);
    }
    std::optional<EpisodeIssuer> BindEpisode(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Episode,C::OwnerDomain::IdentityRegistry,C::OwnerDomain::Context);
        if (!handle) return std::nullopt;
        return EpisodeIssuer(*handle);
    }
    std::optional<ObjectIssuer> BindObject(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Object,C::OwnerDomain::IdentityRegistry);
        if (!handle) return std::nullopt;
        return ObjectIssuer(*handle);
    }
    std::optional<RenderIssuer> BindRender(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Render,C::OwnerDomain::IdentityRegistry);
        if (!handle) return std::nullopt;
        return RenderIssuer(*handle);
    }
    std::optional<EvaluationIssuer> BindEvaluation(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Evaluation,C::OwnerDomain::Strategy,C::OwnerDomain::Multipass);
        if (!handle) return std::nullopt;
        return EvaluationIssuer(*handle);
    }
    std::optional<PresentIssuer> BindPresent(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Present,C::OwnerDomain::Presentation);
        if (!handle) return std::nullopt;
        return PresentIssuer(*handle);
    }
    std::optional<ResourceIssuer> BindResource(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Resource,C::OwnerDomain::Resource);
        if (!handle) return std::nullopt;
        return ResourceIssuer(*handle);
    }
    std::optional<ProviderIssuer> BindProvider(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Provider,C::OwnerDomain::Provider);
        if (!handle) return std::nullopt;
        return ProviderIssuer(*handle);
    }
    std::optional<SwapchainIssuer> BindSwapchain(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Swapchain,C::OwnerDomain::Presentation);
        if (!handle) return std::nullopt;
        return SwapchainIssuer(*handle);
    }
    std::optional<RepresentationIssuer> BindRepresentation(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Representation,C::OwnerDomain::Context,C::OwnerDomain::Resource);
        if (!handle) return std::nullopt;
        return RepresentationIssuer(*handle);
    }
    std::optional<RasterIssuer> BindRaster(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Raster,C::OwnerDomain::Context);
        if (!handle) return std::nullopt;
        return RasterIssuer(*handle);
    }
    std::optional<ColorIssuer> BindColor(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Color,C::OwnerDomain::ColorContinuity);
        if (!handle) return std::nullopt;
        return ColorIssuer(*handle);
    }
    std::optional<RrIssuer> BindRr(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Rr,C::OwnerDomain::RayReconstruction);
        if (!handle) return std::nullopt;
        return RrIssuer(*handle);
    }
    std::optional<RouteIssuer> BindRoute(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Route,C::OwnerDomain::StreamCoordinator);
        if (!handle) return std::nullopt;
        return RouteIssuer(*handle);
    }
    std::optional<ModelIssuer> BindModel(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Model,C::OwnerDomain::Strategy,C::OwnerDomain::Multipass);
        if (!handle) return std::nullopt;
        return ModelIssuer(*handle);
    }
    std::optional<HistoryIssuer> BindHistory(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::History,C::OwnerDomain::History);
        if (!handle) return std::nullopt;
        return HistoryIssuer(*handle);
    }
    std::optional<FgPolicyIssuer> BindFgPolicy(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::FgPolicy,C::OwnerDomain::FrameGeneration);
        if (!handle) return std::nullopt;
        return FgPolicyIssuer(*handle);
    }
    std::optional<HandoffIssuer> BindHandoff(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Handoff,C::OwnerDomain::Finalizer,C::OwnerDomain::FrameGeneration);
        if (!handle) return std::nullopt;
        return HandoffIssuer(*handle);
    }
    std::optional<FinalizerIssuer> BindFinalizer(const OwnerBinding& binding)
    {
        const auto handle=Bind(binding,Detail::ChannelRole::Finalizer,C::OwnerDomain::Finalizer);
        if (!handle) return std::nullopt;
        return FinalizerIssuer(*handle);
    }
};

inline Outcome<C::RenderStreamId> TopologyIssuer::Stream(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::RenderStreamId>(handle_,Detail::ChannelRole::Topology,event);
}

inline Outcome<C::ViewId> TopologyIssuer::View(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ViewId>(handle_,Detail::ChannelRole::Topology,event);
}

inline Outcome<C::EpisodeId> EpisodeIssuer::Episode(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::EpisodeId>(handle_,Detail::ChannelRole::Episode,event);
}

inline Outcome<C::ObjectIncarnation> ObjectIssuer::Incarnation(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ObjectIncarnation>(handle_,Detail::ChannelRole::Object,event);
}

inline Outcome<C::RenderId> RenderIssuer::Render(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::RenderId>(handle_,Detail::ChannelRole::Render,event);
}

inline Outcome<C::EvaluationId> EvaluationIssuer::Evaluation(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::EvaluationId>(handle_,Detail::ChannelRole::Evaluation,event);
}

inline std::optional<NativeEvaluationAssociation> EvaluationIssuer::NativeEvaluation(
    const C::NativeSampleIdentityV1& sample,C::EpisodeId episode,const OwnerEvent& event)const
{
    if(sample.Check()!=C::Error::None||!episode.value||episode.Check()!=C::Error::None)return {};
    const auto issued=handle_.host->IssueInSession<C::IdentityKind::EvaluationId>(handle_,Detail::ChannelRole::Evaluation,event,&sample.session);
    if(issued.status!=IdentityStatus::Ok||!issued.value.IsKnown())return {};
    return NativeEvaluationAssociation(sample,episode,issued.value.KnownPart()->value);
}

inline Outcome<C::PresentId> PresentIssuer::Present(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::PresentId>(handle_,Detail::ChannelRole::Present,event);
}

inline Outcome<C::ObjectIncarnation> ResourceIssuer::Object(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ObjectIncarnation>(handle_,Detail::ChannelRole::Resource,event);
}

inline Outcome<C::ResourceIncarnation> ResourceIssuer::Resource(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ResourceIncarnation>(handle_,Detail::ChannelRole::Resource,event);
}

inline Outcome<C::ResourceViewIncarnation> ResourceIssuer::View(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ResourceViewIncarnation>(handle_,Detail::ChannelRole::Resource,event);
}

inline Outcome<C::ResourceGeneration> ResourceIssuer::Structure(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ResourceGeneration>(handle_,Detail::ChannelRole::Resource,event);
}

inline Outcome<C::ProviderIncarnation> ProviderIssuer::Incarnation(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ProviderIncarnation>(handle_,Detail::ChannelRole::Provider,event);
}

inline Outcome<C::ObjectIncarnation> SwapchainIssuer::Object(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ObjectIncarnation>(handle_,Detail::ChannelRole::Swapchain,event);
}

inline Outcome<C::SwapchainGeneration> SwapchainIssuer::Recreated(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::SwapchainGeneration>(handle_,Detail::ChannelRole::Swapchain,event);
}

inline Outcome<C::RepresentationGeneration> RepresentationIssuer::Structure(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::RepresentationGeneration>(handle_,Detail::ChannelRole::Representation,event);
}

inline Outcome<C::RasterGeneration> RasterIssuer::Changed(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::RasterGeneration>(handle_,Detail::ChannelRole::Raster,event);
}

inline Outcome<C::ColorGeneration> ColorIssuer::Changed(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ColorGeneration>(handle_,Detail::ChannelRole::Color,event);
}

inline Outcome<C::ExposureEpoch> ColorIssuer::ExposureDiscontinuity(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ExposureEpoch>(handle_,Detail::ChannelRole::Color,event);
}

inline Outcome<C::RRGeneration> RrIssuer::TopologyChanged(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::RRGeneration>(handle_,Detail::ChannelRole::Rr,event);
}

inline Outcome<C::RouteGeneration> RouteIssuer::CommittedChange(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::RouteGeneration>(handle_,Detail::ChannelRole::Route,event);
}

inline Outcome<C::ModelGeneration> ModelIssuer::Changed(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::ModelGeneration>(handle_,Detail::ChannelRole::Model,event);
}

inline Outcome<C::HistoryGeneration> HistoryIssuer::AppliedReset(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::HistoryGeneration>(handle_,Detail::ChannelRole::History,event);
}

inline Outcome<C::FgPolicyGeneration> FgPolicyIssuer::PolicyChange(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::FgPolicyGeneration>(handle_,Detail::ChannelRole::FgPolicy,event);
}

inline Outcome<C::HandoffContractGeneration> HandoffIssuer::ContractChanged(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::HandoffContractGeneration>(handle_,Detail::ChannelRole::Handoff,event);
}

inline Outcome<C::FinalSealId> FinalizerIssuer::FinalSeal(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::FinalSealId>(handle_,Detail::ChannelRole::Finalizer,event);
}

inline Outcome<C::InterceptedSourceTransactionIdV1> RenderIssuer::SourceTransaction(const OwnerEvent& event) const
{
    return handle_.host->Issue<C::IdentityKind::InterceptedSourceTransactionIdV1>(handle_,Detail::ChannelRole::Render,event);
}

inline Outcome<C::BaseRealFrameId> RenderIssuer::NewBase(const RenderEvidence& evidence) const
{
    switch (evidence.observation)
    {
    case RenderObservation::Unknown:
        return Refuse<C::BaseRealFrameId>(IdentityStatus::Unknown,{evidence.event.subject});
    case RenderObservation::ExistingGameContent:
    case RenderObservation::GeneratedOutput:
    case RenderObservation::RepeatedContent:
        return Refuse<C::BaseRealFrameId>(IdentityStatus::NotNewReal,{evidence.event.subject});
    case RenderObservation::NewGameContent:
        return handle_.host->Issue<C::IdentityKind::BaseRealFrameId>(handle_,Detail::ChannelRole::Render,evidence.event);
    }
    return Refuse<C::BaseRealFrameId>(IdentityStatus::InvalidRule,{evidence.event.subject});
}
inline Outcome<C::ContentRevision> ResourceIssuer::ContentWrite(const OwnerEvent& event,
                                                               const C::ResourceIncarnation& resource) const
{
    return handle_.host->WriteContents(handle_,event,resource);
}
} // namespace Neurotic::Lifecycle
