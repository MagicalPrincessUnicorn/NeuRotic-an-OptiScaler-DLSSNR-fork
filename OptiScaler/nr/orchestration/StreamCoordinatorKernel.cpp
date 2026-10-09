#include "StreamCoordinatorKernel.h"
#include <nr/lifecycle/NativeSourceBoundConsumerAction.h>
#include <nr/lifecycle/NativeSessionLifetime.h>
#include <nr/protocol/NativeRouteAdmission.h>
#include <nr/lifecycle/NativeFinalConsumerAction.h>
namespace Neurotic::Orchestration
{
struct StreamCoordinatorKernel::Impl
{
    Lifecycle::NativeSessionLifetime& root;
    mutable std::mutex mutex;
    std::mutex finalizerEvents;
    CoordinatorSnapshot state;
    bool budgetConsumed=false,preparing=false,closingPass=false,finalizerStopped=false;
    std::shared_ptr<const MaterializationRecord> record;
    std::unique_ptr<PreparedInitialNative> unresolvedPreparation;
    struct Operation
    {
        bool used=false,handle=false,preparing=false,claimPending=false,consumptionPending=false,consumed=false,interrupted=false;
        std::uint64_t serial=0;
        Lifecycle::NativeExecutionLedger::Ticket attempt;
        std::array<unsigned,5> actions{};
        std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot;
        std::shared_ptr<Protocol::NativeInvocationOwnerPort> owner;
        std::optional<Lifecycle::PrimaryNrClaimHandle> claim;
        std::optional<Lifecycle::FinalizerRetirementAcknowledgment> finalizerRetirement;
        std::shared_ptr<Lifecycle::NativeFinalConsumerTail> finalConsumer;
        std::optional<Lifecycle::SourceBoundClaimHandle> sourceClaim;
        std::optional<Lifecycle::FinalCandidateSubmission> sourceCandidate;
        std::optional<Lifecycle::SourceBoundTerminalReceipt> sourceTerminal;
        std::shared_ptr<Lifecycle::NativeSourceBoundConsumerTail> sourceConsumer;
        bool sourceHistoryClosed=false;
    };
    std::array<Operation,16> operations;
    std::size_t pendingAdmissions=0;bool sweeping=false,pollingFinalConsumers=false;
    Operation* Find(std::size_t index,std::uint64_t serial)noexcept
    {return index<operations.size()&&operations[index].used&&operations[index].serial==serial?&operations[index]:nullptr;}
#ifdef NR_ORCH_INIT_TESTING
    TestFault fault=TestFault::None;
#endif
    explicit Impl(Lifecycle::NativeSessionLifetime& owner):root(owner){}
};
StreamCoordinatorKernel::StreamCoordinatorKernel(Lifecycle::NativeSessionLifetime& root):impl_(std::make_unique<Impl>(root)){}
StreamCoordinatorKernel::~StreamCoordinatorKernel()=default;
#ifdef NR_ORCH_INIT_TESTING
void StreamCoordinatorKernel::SetTestingFault(TestFault fault)
{std::lock_guard lock(impl_->mutex);impl_->fault=fault;}
#endif
CoordinatorSnapshot StreamCoordinatorKernel::Inspect()const
{std::lock_guard lock(impl_->mutex);return impl_->state;}
InitResult StreamCoordinatorKernel::Read()
{
    auto& p=*impl_;std::unique_lock lock(p.mutex,std::try_to_lock);
    if(!lock.owns_lock())return {InitStatus::Busy,{}};
    if(p.state.phase!=RoutePhase::Active)return {InitStatus::Refused,{}};
    return {InitStatus::ExistingReadOnly,CurrentRouteView(p.root.shared_from_this(),p.record)};
}
InitResult StreamCoordinatorKernel::Activate()
{
    auto& p=*impl_;
    std::shared_ptr<const MaterializationRecord> active;
    {
        std::unique_lock lock(p.mutex,std::try_to_lock);
        if(!lock.owns_lock()||p.state.phase==RoutePhase::Preparing)return {InitStatus::Busy,{}};
        if(p.state.phase==RoutePhase::Active)active=p.record;
        else
        {
            if(p.state.phase!=RoutePhase::Empty||p.budgetConsumed)return {InitStatus::Terminal,{}};
            p.state.phase=RoutePhase::Preparing;p.preparing=true;
        }
    }
    if(active)
    {
        const bool same=p.root.port_->MatchesActive(*active);
        std::lock_guard lock(p.mutex);
        if(!same||p.state.phase!=RoutePhase::Active||p.record!=active)return {InitStatus::Refused,{}};
        return {InitStatus::ExistingReadOnly,CurrentRouteView(p.root.shared_from_this(),active)};
    }
    std::unique_ptr<PreparedInitialNative> prepared;
    InitStatus result=InitStatus::Refused;
    try
    {
        if(!p.root.port_->Enabled())result=InitStatus::Disabled;
        else prepared=p.root.port_->Prepare(p.root.enrollment_);
        if(prepared&&prepared->record_)
        {
            // Allocate/prepare header and event before the irreversible leaf.
            auto& record=*prepared->record_;
            record.plan.header=p.root.owners_->RouteJournal().Header(C::ContractId::C05,p.root.enrollment_.scope);
            const auto event=p.root.owners_->RouteJournal().Event();
            const auto committed=C::OptionalFact<bool>::FromKnown(true,event.evidence);
            auto lifetime=p.root.shared_from_this(); // may throw only BEFORE issue
            std::lock_guard lock(p.mutex);
            if(p.state.phase==RoutePhase::Preparing&&!p.budgetConsumed)
            {
                Lifecycle::Outcome<C::RouteGeneration> issued;
#ifdef NR_ORCH_INIT_TESTING
                if(p.fault==TestFault::IssuerBusy)issued.status=Lifecycle::IdentityStatus::Busy;
                else
#endif
                {
                    // Sole canonical materializer call site. Issuer is a bounded
                    // noncallback leaf; no finalizer/model/owner callback here.
                    issued=p.root.owners_->Route()->CommittedChange(event);
                }
                if(issued.status==Lifecycle::IdentityStatus::Busy)result=InitStatus::Busy;
                else
                {
                    // Even an indeterminate non-Busy outcome consumes INIT.
                    p.budgetConsumed=true;
                    if(issued.status==Lifecycle::IdentityStatus::Ok&&issued.value.IsKnown())
                    {
                        ++p.state.successfulIssues;
                        record.plan.routeGeneration=issued.value;record.plan.committed=committed;
                        p.record=prepared->record_; // retained even for an issued tombstone
#ifdef NR_ORCH_INIT_TESTING
                        if(p.fault==TestFault::AfterIssue)p.state.phase=RoutePhase::Quarantined;
                        else
#endif
                        {
                            p.record=prepared->record_;prepared->consumed_=true;
                            ++p.state.publications;p.state.phase=RoutePhase::Active;
                            result=InitStatus::Activated;
                        }
                    }
                    else p.state.phase=RoutePhase::Quarantined;
                }
            }
        }
    }
    catch(...){result=InitStatus::Refused;}
    // Cancellation/activation and destruction run outside mutation lock.
    Retirement cleanup=Retirement::Retired;
    if(prepared){if(prepared->consumed_)prepared->Activated();else cleanup=prepared->Cancel();}
    if(cleanup!=Retirement::Retired)
    {
        std::lock_guard lock(p.mutex);
        p.unresolvedPreparation=std::move(prepared);
    }
    prepared.reset(); // only a proven-retired or activated token can die here
    std::lock_guard lock(p.mutex);p.preparing=false;
    if(cleanup!=Retirement::Retired)p.state.phase=RoutePhase::Quarantined;
    else if(p.state.phase==RoutePhase::Preparing)p.state.phase=RoutePhase::Empty;
    if(result==InitStatus::Activated&&p.state.phase==RoutePhase::Active)
        return {result,CurrentRouteView(p.root.shared_from_this(),p.record)};
    return {result==InitStatus::Activated?InitStatus::Terminal:result,{}};
}
void StreamCoordinatorKernel::Revoke(RevocationCause)
{
    auto& p=*impl_;std::lock_guard lock(p.mutex);
    if(p.state.phase==RoutePhase::Closed||p.state.phase==RoutePhase::Quarantined)return;
    p.budgetConsumed=true;p.state.phase=RoutePhase::Closing;
}
ClosureResult StreamCoordinatorKernel::Close()
{
    auto& p=*impl_;Revoke(RevocationCause::SessionClose);
    SweepInvocations();
    {
        std::lock_guard lock(p.mutex);
        if(p.state.phase==RoutePhase::Closed)return {true,p.finalizerStopped};
        if(p.preparing||p.closingPass||p.pendingAdmissions||p.sweeping||p.pollingFinalConsumers)return {false,p.finalizerStopped};
        for(const auto& op:p.operations)if(op.used){p.state.phase=RoutePhase::Quarantined;return {false,p.finalizerStopped};}
        p.closingPass=true;
    }
    // closingPass serializes retry of the retained token. No owner callback or
    // preparation destruction occurs under the slot lock.
    if(p.unresolvedPreparation)
    {
        const auto cleanup=p.unresolvedPreparation->Cancel();
        if(cleanup!=Retirement::Retired)
        {
            std::lock_guard lock(p.mutex);p.closingPass=false;p.state.phase=RoutePhase::Quarantined;
            return {false,p.finalizerStopped};
        }
        std::unique_ptr<PreparedInitialNative> retired;
        {std::lock_guard lock(p.mutex);retired=std::move(p.unresolvedPreparation);}
        retired.reset();
    }
    const auto external=p.root.port_->ObserveRetirement();
    // No global finalizer stop until claim tails AND every owner are retired.
    // This deliberately conservative ordering never strands an unsealed claim.
    bool closed=false,stopped=false;
    if(external.AllRetired())
    {
        auto* finalizer=p.root.owners_->Finalizer();
        stopped=finalizer&&finalizer->StopAdmissions();
        if(stopped)closed=p.root.owners_->CloseDrained();
    }
    std::lock_guard lock(p.mutex);p.closingPass=false;
    p.finalizerStopped=p.finalizerStopped||stopped;
    p.state.phase=closed?RoutePhase::Closed:RoutePhase::Quarantined;
    return {closed,p.finalizerStopped};
}

void StreamCoordinatorKernel::SweepInvocations()
{
    auto& p=*impl_;std::array<Impl::Operation,16> candidates,retired;
    // Poll every retained tail on ordinary active admissions as well as close.
    // Failed FSR enrollment has no feature-local entry to drive this work.
    // Keep sweeping false during owner callbacks so genuine acknowledgments can
    // join, and bound reentrant/parallel polling with a separate owner flag.
    std::array<std::shared_ptr<Lifecycle::NativeFinalConsumerTail>,16> tails;
    std::array<std::shared_ptr<Lifecycle::NativeSourceBoundConsumerTail>,16> sourceTails;
    {
        std::lock_guard lock(p.mutex);if(p.sweeping||p.pollingFinalConsumers)return;
        p.pollingFinalConsumers=true;
        for(std::size_t i=0;i<p.operations.size();++i)tails[i]=p.operations[i].finalConsumer;
        for(std::size_t i=0;i<p.operations.size();++i)sourceTails[i]=p.operations[i].sourceConsumer;
    }
    {
        struct PollEnd
        {
            Impl& p;
            ~PollEnd(){std::lock_guard lock(p.mutex);p.pollingFinalConsumers=false;}
        } end{p};
        for(const auto& tail:tails)if(tail)tail->Advance();
        for(const auto& tail:sourceTails)if(tail)tail->Advance();
    }
    {
        std::lock_guard lock(p.mutex);if(p.sweeping)return;p.sweeping=true;
        for(std::size_t i=0;i<p.operations.size();++i)
            if(p.operations[i].used&&!p.operations[i].handle&&!p.operations[i].preparing&&!p.operations[i].claimPending&&
               !p.operations[i].consumptionPending)candidates[i]=p.operations[i];
    }
    for(std::size_t i=0;i<candidates.size();++i)
    {
        auto& candidate=candidates[i];if(!candidate.used)continue;
        if(candidate.claim&&!candidate.finalizerRetirement)continue;
        if(candidate.sourceClaim&&!candidate.sourceTerminal)continue;
        // Only a genuine exact finalizer acknowledgment closes the claim tail.
        // The invocation owner still independently checks recording, provider,
        // callback, history and Resource-action terminality before retirement.
        const auto disposition=candidate.snapshot?candidate.owner->Retire(*candidate.snapshot,{}):candidate.owner->CancelPreparation();
        if(disposition!=Retirement::Retired)continue;
        std::lock_guard lock(p.mutex);auto* op=p.Find(i,candidate.serial);
        if(!op||op->handle||op->preparing||op->claimPending)continue;
        if(candidate.attempt.revision&&!p.root.execution_.Retire(candidate.attempt))
        {p.state.phase=RoutePhase::Quarantined;continue;}
        retired[i]=std::move(*op);*op={};op->serial=candidate.serial;
    }
    {std::lock_guard lock(p.mutex);p.sweeping=false;}
    // Owner and metadata releases happen here, outside the mutation lock.
}
std::optional<Protocol::NativeInvocationAdmission> StreamCoordinatorKernel::AdmitInvocation(
    std::shared_ptr<Protocol::NativeInvocationOwnerPort> owner)
{
    if(!owner)return {};
    auto& p=*impl_;SweepInvocations();std::shared_ptr<const MaterializationRecord> record;
    std::size_t index=0;std::uint64_t serial=0;
    {
        std::unique_lock lock(p.mutex,std::try_to_lock);if(!lock.owns_lock()||p.state.phase!=RoutePhase::Active)return {};
        for(;index<p.operations.size();++index)
        {
            auto& op=p.operations[index];if(op.used||op.serial==(std::numeric_limits<std::uint64_t>::max)())continue;
            serial=++op.serial;op.used=op.preparing=true;op.owner=owner;break;
        }
        if(!serial)return {}; // capacity refusal precedes owner preparation
        record=p.record;++p.pendingAdmissions;
    }
    std::optional<C::NativeSampleIdentityV1> source;
    try{source=owner->SourceSample(*record);}catch(...){source.reset();}
    bool reserved=false;
    {
        std::lock_guard lock(p.mutex);
        auto* op=p.Find(index,serial);
        if(op&&p.state.phase==RoutePhase::Active&&p.record==record&&source&&
           source->Check()==C::Error::None&&source->session==record->initialSample.session&&
           source->stream==record->initialSample.stream&&source->view==record->initialSample.view&&
           source->feature==record->initialSample.feature&&source->ingress==record->initialSample.ingress&&
           source->producerOrdinal>=record->initialSample.producerOrdinal)
        {
            const auto attempt=p.root.execution_.Reserve(*source);
            if(attempt){op->attempt=*attempt;reserved=true;}
        }
        if(!reserved){if(op)op->preparing=false;--p.pendingAdmissions;}
    }
    if(!reserved)return {};
    std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot;
    std::unique_ptr<Protocol::NativeOwnerActionGuard> guard;
    try
    {
        snapshot=owner->Prepare(*record);
        if(snapshot&&snapshot->claim_&&Protocol::NativeInvocationMatches(*snapshot,*record))
            guard=owner->Acquire(*snapshot,Protocol::NativeCheckPoint::BeforeRecord);
    }
    catch(...){guard.reset();}
    const bool checked=guard&&snapshot&&snapshot->Sample()&&
        snapshot->Sample()->SameSample(*source)&&snapshot->Sample()->callback==source->callback&&
        Protocol::SameNativeInvocation(*snapshot,guard->Current());
    std::lock_guard lock(p.mutex);--p.pendingAdmissions;
    auto* op=p.Find(index,serial);if(!op)return {};
    op->preparing=false;
    if(!checked||p.state.phase!=RoutePhase::Active||p.record!=record)return {};
    op->snapshot=std::move(snapshot);op->handle=true;
    return Protocol::NativeInvocationAdmission(p.root.shared_from_this(),index,serial);
}
void StreamCoordinatorKernel::DropInvocation(std::size_t slot,std::uint64_t serial)noexcept
{auto& p=*impl_;std::lock_guard lock(p.mutex);if(auto* op=p.Find(slot,serial))op->handle=false;}
const Protocol::RecipeProduct* StreamCoordinatorKernel::InvocationProduct(std::size_t slot,std::uint64_t serial)const noexcept
{auto& p=*impl_;std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);return op&&op->handle?&op->snapshot->product:nullptr;}
std::optional<Lifecycle::PrimaryNrClaimHandle> StreamCoordinatorKernel::InvocationClaim(std::size_t slot,std::uint64_t serial)const
{auto& p=*impl_;std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);return op&&op->handle?op->claim:std::nullopt;}
C::NativeDeliveryKind StreamCoordinatorKernel::InvocationDelivery(std::size_t slot,std::uint64_t serial)const noexcept
{auto& p=*impl_;std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
 return op&&op->handle?op->snapshot->Delivery():C::NativeDeliveryKind::Invalid;}
std::optional<C::NativeSampleIdentityV1> StreamCoordinatorKernel::InvocationSample(std::size_t slot,std::uint64_t serial)const
{auto& p=*impl_;std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
 return op&&op->handle?op->snapshot->Sample():std::nullopt;}
std::unique_ptr<Protocol::NativeOwnerActionGuard> StreamCoordinatorKernel::InvocationAction(
    std::size_t slot,std::uint64_t serial,Protocol::NativeCheckPoint point)
{
    auto& p=*impl_;std::shared_ptr<Protocol::NativeInvocationOwnerPort> owner;
    std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot;
    {
        std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
        if(!op||!op->handle||!p.root.execution_.Authorized(op->attempt))return {};
        const bool continuation=point==Protocol::NativeCheckPoint::BeforeComposition||point==Protocol::NativeCheckPoint::BeforeCopy||
            point==Protocol::NativeCheckPoint::BeforeSubmission;
        if(p.state.phase!=RoutePhase::Active&&!(p.root.execution_.ModelCalled(op->attempt)&&continuation))return {};
        if(point==Protocol::NativeCheckPoint::BeforeModel&&p.root.execution_.ModelStarted(op->attempt))return {};
        owner=op->owner;snapshot=op->snapshot;
    }
    auto guard=owner->Acquire(*snapshot,point);
    if(!guard||!Protocol::SameNativeInvocation(*snapshot,guard->Current()))return {};
    {
        std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
        const auto checkpoint=static_cast<std::size_t>(point);
        // The inspected Native renderer has two preparation checks, one model
        // call, at most two composition checks and three scratch/copy checks.
        constexpr std::array<unsigned,5> limits{2,1,2,3,1};
        const bool continuation=point==Protocol::NativeCheckPoint::BeforeComposition||point==Protocol::NativeCheckPoint::BeforeCopy||
            point==Protocol::NativeCheckPoint::BeforeSubmission;
        if(!op||!op->handle||!p.root.execution_.Authorized(op->attempt)||checkpoint>=limits.size()||
            op->actions[checkpoint]>=limits[checkpoint]||
            (p.state.phase!=RoutePhase::Active&&!(p.root.execution_.ModelCalled(op->attempt)&&continuation)))return {};
        ++op->actions[checkpoint];
    }
    return guard;
}
bool StreamCoordinatorKernel::ClaimInvocation(std::size_t slot,std::uint64_t serial)
{
    auto& p=*impl_;std::shared_ptr<Protocol::NativeInvocationOwnerPort> owner;
    std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot;
    Lifecycle::PrimaryClaimRequest request;
    C::NativeDeliveryKind delivery=C::NativeDeliveryKind::Invalid;
    {
        std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
        if(!op||!op->handle||p.state.phase!=RoutePhase::Active||
           !p.root.execution_.BeginClaim(op->attempt))return false;
        op->claimPending=true;owner=op->owner;snapshot=op->snapshot;delivery=snapshot->Delivery();
        request.recipe=snapshot->product.recipe;request.plan=p.record->plan;request.generations=snapshot->generations;
        request.handoff=snapshot->handoff;request.observation=Lifecycle::RenderObservation::NewGameContent;
    }
    Lifecycle::FinalizationResult result;
    Lifecycle::SourceBoundResult sourceResult;
    bool nativeAuthorized=false;
    try
    {
        auto guard=owner->Acquire(*snapshot,Protocol::NativeCheckPoint::BeforeRecord);
        if(guard&&Protocol::SameNativeInvocation(*snapshot,guard->Current()))
        {
            if(delivery==C::NativeDeliveryKind::HostReturn)
            {
                std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
                if(op&&p.state.phase==RoutePhase::Active)
                    nativeAuthorized=p.root.execution_.Authorize(op->attempt);
            }
            else if(delivery==C::NativeDeliveryKind::FinalConsumerRequired)
            {
                bool current=false;
                {std::lock_guard lock(p.mutex);current=p.state.phase==RoutePhase::Active;}
                if(current)
                {
                    std::lock_guard eventLock(p.finalizerEvents);
                    const Lifecycle::FinalizerEvent event{p.root.owners_->FinalizerJournal().Event(),request.generations};
                    if(snapshot->sourceRequest_&&snapshot->sourceClaim_)
                        sourceResult=snapshot->sourceClaim_(*p.root.owners_->Finalizer(),*snapshot->sourceRequest_,event);
                    else result=snapshot->claim_(*p.root.owners_->Finalizer(),request,event);
                }
            }
        }
    }
    catch(...)
    {
        // An indeterminate claim remains pending; closure cannot infer absence.
        std::lock_guard lock(p.mutex);p.state.phase=RoutePhase::Quarantined;return false;
    }
    std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);if(!op)return false;
    op->claimPending=false;
    if(result.accepted&&result.newPrimaryClaim&&result.claim)
    {
        op->claim=result.claim;
        nativeAuthorized=p.root.execution_.Authorize(op->attempt);
    }
    if(sourceResult.accepted&&sourceResult.newPrimaryClaim&&sourceResult.claim)
    {
        op->sourceClaim=sourceResult.claim;
        nativeAuthorized=p.root.execution_.Authorize(op->attempt);
    }
    return nativeAuthorized&&p.state.phase==RoutePhase::Active;
}
bool StreamCoordinatorKernel::StartInvocationModel(std::size_t slot,std::uint64_t serial,const Protocol::NativeOwnerActionGuard& guard)
{
    // Inspect owner snapshot outside the slot lock; no virtual calls inside it.
    const auto& current=guard.Current();auto& p=*impl_;std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
    if(!op||!op->handle||p.state.phase!=RoutePhase::Active||
        !Protocol::SameNativeInvocation(*op->snapshot,current))return false;
    return p.root.execution_.StartModel(op->attempt);
}
bool StreamCoordinatorKernel::InvocationModelCalled(std::size_t slot,std::uint64_t serial)
{
    auto& p=*impl_;std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
    if(!op||!op->handle)return false;
    return p.root.execution_.MarkModelCalled(op->attempt); // winning start survives revoke
}
void StreamCoordinatorKernel::InvocationModelReturned(std::size_t slot,std::uint64_t serial)
{
    auto& p=*impl_;std::lock_guard lock(p.mutex);
    if(auto* op=p.Find(slot,serial))p.root.execution_.MarkModelReturned(op->attempt);
}
bool StreamCoordinatorKernel::SubmitInvocation(std::size_t slot,std::uint64_t serial,const C::EvaluationResult& result)
{
    auto& p=*impl_;std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot;
    std::optional<Lifecycle::PrimaryNrClaimHandle> claim;
    {
        std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);if(!op||!op->handle||(!op->claim&&!op->sourceClaim))return false;
        if(p.root.execution_.ModelCalled(op->attempt)&&
           (result.stage==C::OutcomeStage::NotAttempted||result.stage==C::OutcomeStage::Bypassed))return false;
        snapshot=op->snapshot;claim=op->claim;
        if(op->sourceClaim)
        {
            Lifecycle::FinalCandidateSubmission candidate;candidate.result=result;candidate.generations=snapshot->generations;
            candidate.acceptedFinalCandidate=C::OptionalFact<bool>::FromKnown(result.stage==C::OutcomeStage::Produced,C::EvidenceRef{result.header.record});
            if(op->sourceCandidate)return *op->sourceCandidate==candidate;
            op->sourceCandidate=candidate;return true;
        }
    }
    Lifecycle::FinalCandidateSubmission candidate;candidate.result=result;candidate.generations=snapshot->generations;
    candidate.acceptedFinalCandidate=C::OptionalFact<bool>::FromKnown(result.stage==C::OutcomeStage::Produced,C::EvidenceRef{result.header.record});
    std::lock_guard eventLock(p.finalizerEvents);
    const Lifecycle::FinalizerEvent event{p.root.owners_->FinalizerJournal().Event(),snapshot->generations};
    return snapshot->submit_&&snapshot->submit_(*p.root.owners_->Finalizer(),*claim,candidate,event).accepted;
}
bool StreamCoordinatorKernel::SubmitCommittedStage(const Lifecycle::PrimaryNrClaimHandle& claim)
{
    auto& p=*impl_;std::shared_ptr<Protocol::NativeInvocationOwnerPort> owner;
    {
        std::lock_guard lock(p.mutex);
        for(const auto& op:p.operations)if(op.used&&op.claim&&*op.claim==claim&&!op.finalizerRetirement)
        {owner=op.owner;break;}
    }
    if(!owner)return false;
    const auto stage=owner->CommittedStage();return stage&&SubmitStageDelivery(*stage);
}
bool StreamCoordinatorKernel::SubmitStageDelivery(const C::NativeStageDeliveryV1& stage)
{
    auto& p=*impl_;std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot;
    std::optional<Lifecycle::PrimaryNrClaimHandle> claim;
    std::optional<Lifecycle::SourceBoundClaimHandle> sourceClaim;
    std::optional<Lifecycle::FinalCandidateSubmission> sourceCandidate;
    {
        std::lock_guard lock(p.mutex);
        for(const auto& op:p.operations)
        {
            if(!op.used||op.preparing||op.claimPending||!op.snapshot||(!op.claim&&!op.sourceClaim)||op.finalizerRetirement||op.sourceTerminal)continue;
            const auto& s=*op.snapshot;
            if(s.Delivery()!=C::NativeDeliveryKind::FinalConsumerRequired||!s.product.recipe.nativeSample||
               stage.sample!=*s.product.recipe.nativeSample||stage.evaluation!=s.product.recipe.evaluation||
               stage.admissionEpoch!=s.reservation||stage.recipe.record!=s.product.recipe.header.record||
               stage.recipe.revision!=s.product.recipe.header.revision)continue;
            if(snapshot)return false;
            snapshot=op.snapshot;claim=op.claim;
            sourceClaim=op.sourceClaim;sourceCandidate=op.sourceCandidate;
        }
    }
    if(!snapshot||(!claim&&!sourceClaim))return false;
    std::lock_guard eventLock(p.finalizerEvents);
    const Lifecycle::FinalizerEvent event{p.root.owners_->FinalizerJournal().Event(),snapshot->generations};
    if(sourceClaim)return sourceCandidate&&snapshot->sourceStage_&&
        snapshot->sourceStage_(*p.root.owners_->Finalizer(),*sourceClaim,*sourceCandidate,stage,event).accepted;
    return snapshot->stage_&&snapshot->stage_(*p.root.owners_->Finalizer(),*claim,stage,event).accepted;
}
bool StreamCoordinatorKernel::AcknowledgeFinalizerRetirement(const Lifecycle::FinalizerRetirementAcknowledgment& acknowledgment)
{
    auto& p=*impl_;std::lock_guard lock(p.mutex);
    for(auto& op:p.operations)
        if(op.used&&op.claim&&acknowledgment.Matches(*p.root.owners_->Finalizer(),*op.claim))
        {op.finalizerRetirement=acknowledgment;return true;}
    return false;
}
bool StreamCoordinatorKernel::AcknowledgeFinalConsumption(const Lifecycle::FinalizerConsumptionAcknowledgment& acknowledgment)
{
    auto& p=*impl_;std::shared_ptr<Protocol::NativeInvocationOwnerPort> owner;
    std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot;
    std::optional<Lifecycle::PrimaryNrClaimHandle> claim;std::size_t slot=0;std::uint64_t serial=0;
    {
        std::lock_guard lock(p.mutex);
        if(p.sweeping)return false;
        for(;slot<p.operations.size();++slot)
        {
            auto& op=p.operations[slot];
            if(!op.used||!op.claim||!op.snapshot||op.consumptionPending||op.consumed||op.interrupted||op.finalizerRetirement||
               !acknowledgment.Matches(*p.root.owners_->Finalizer(),*op.claim,op.snapshot->product.recipe.evaluation))continue;
            op.consumptionPending=true;owner=op.owner;snapshot=op.snapshot;claim=op.claim;serial=op.serial;break;
        }
    }
    if(!snapshot||!owner||!claim)return false;
    const bool accepted=owner->FinalConsumed(*snapshot,*claim,acknowledgment);
    std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
    if(!op)return false;
    op->consumptionPending=false;op->consumed=accepted;return accepted;
}
bool StreamCoordinatorKernel::AcknowledgeFinalInterruption(const Lifecycle::FinalizerInterruptionAcknowledgment& acknowledgment)
{
    auto& p=*impl_;std::shared_ptr<Protocol::NativeInvocationOwnerPort> owner;
    std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot;
    std::optional<Lifecycle::PrimaryNrClaimHandle> claim;std::size_t slot=0;std::uint64_t serial=0;
    {
        std::lock_guard lock(p.mutex);
        if(p.sweeping)return false;
        for(;slot<p.operations.size();++slot)
        {
            auto& op=p.operations[slot];
            if(!op.used||!op.claim||!op.snapshot||op.consumptionPending||op.consumed||op.interrupted||op.finalizerRetirement||
               !acknowledgment.Matches(*p.root.owners_->Finalizer(),*op.claim,op.snapshot->product.recipe.evaluation))continue;
            op.consumptionPending=true;owner=op.owner;snapshot=op.snapshot;claim=op.claim;serial=op.serial;break;
        }
    }
    if(!snapshot||!owner||!claim)return false;
    const bool accepted=owner->FinalInterrupted(*snapshot,*claim,acknowledgment);
    std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
    if(!op)return false;
    op->consumptionPending=false;op->interrupted=accepted;return accepted;
}
bool StreamCoordinatorKernel::FinalConsumerLogicalClosed(const Lifecycle::PrimaryNrClaimHandle& claim)const
{
    auto& p=*impl_;std::lock_guard lock(p.mutex);
    for(const auto& op:p.operations)if(op.used&&op.claim&&*op.claim==claim)return op.consumed||op.interrupted;
    return false;
}
bool StreamCoordinatorKernel::RetainFinalConsumer(const Lifecycle::PrimaryNrClaimHandle& claim,
    std::shared_ptr<Lifecycle::NativeFinalConsumerTail> tail)
{
    if(!tail)return false;
    auto& p=*impl_;std::lock_guard lock(p.mutex);
    for(auto& op:p.operations)
        if(op.used&&op.claim&&*op.claim==claim&&op.snapshot&&!op.finalizerRetirement&&
           op.snapshot->Delivery()==C::NativeDeliveryKind::FinalConsumerRequired&&tail->Frame()==op.snapshot->frame)
        {
            if(op.finalConsumer)return op.finalConsumer==tail;
            op.finalConsumer=std::move(tail);return true;
        }
    return false;
}
std::optional<Lifecycle::SourceBoundClaimHandle> StreamCoordinatorKernel::InvocationSourceBoundClaim(std::size_t slot,std::uint64_t serial)const
{
    auto& p=*impl_;std::lock_guard lock(p.mutex);const auto* op=p.Find(slot,serial);
    return op?op->sourceClaim:std::nullopt;
}
bool StreamCoordinatorKernel::RetainSourceBoundConsumer(const Lifecycle::SourceBoundClaimHandle& claim,
    std::shared_ptr<Lifecycle::NativeSourceBoundConsumerTail> tail)
{
    if(!tail)return false;auto& p=*impl_;std::lock_guard lock(p.mutex);
    for(auto& op:p.operations)if(op.used&&op.sourceClaim&&*op.sourceClaim==claim&&op.snapshot&&
        op.snapshot->sourceTransaction_&&!op.sourceTerminal&&tail->Transaction()==*op.snapshot->sourceTransaction_)
    {
        if(op.sourceConsumer)return op.sourceConsumer==tail;
        op.sourceConsumer=std::move(tail);return true;
    }
    return false;
}
Lifecycle::SourceBoundResult StreamCoordinatorKernel::ApplySourceBoundConsumer(
    const Lifecycle::SourceBoundClaimHandle& claim,Lifecycle::NativeSourceBoundConsumerAction& action)
{
    auto& p=*impl_;std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot;
    std::shared_ptr<Protocol::NativeInvocationOwnerPort> owner;std::size_t slot=0;std::uint64_t serial=0;
    {
        std::lock_guard lock(p.mutex);
        for(;slot<p.operations.size();++slot)
        {
            const auto& op=p.operations[slot];
            if(op.used&&op.sourceClaim&&*op.sourceClaim==claim&&op.snapshot&&op.snapshot->sourceTransaction_&&
               !op.sourceTerminal&&action.Transaction()==*op.snapshot->sourceTransaction_)
            {snapshot=op.snapshot;owner=op.owner;serial=op.serial;break;}
        }
    }
    if(!snapshot||!owner)return {};
    Lifecycle::SourceBoundResult result;
    {
        std::lock_guard lock(p.finalizerEvents);
        const C::ScopeRef scope=snapshot->product.recipe.header.scope;
        Lifecycle::NativeFinalConsumerEvents events{
            [&]{return Lifecycle::FinalizerEvent{p.root.owners_->FinalizerJournal().Event(),snapshot->generations};},
            p.root.owners_->FrameGenerationJournal().Header(C::ContractId::C13,scope),
            p.root.owners_->FinalConsumerIdentityJournal().Header(C::ContractId::C11,scope)};
        result=action.Apply(*p.root.owners_->Finalizer(),events);
    }
    if(result.logical)
    {
        bool needsAck=false;
        {
            std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
            if(op&&!op->consumptionPending&&!op->consumed&&!op->interrupted)
            {op->consumptionPending=true;needsAck=true;}
        }
        if(needsAck)
        {
            bool historyClosed=false;
            {std::lock_guard lock(p.mutex);const auto* op=p.Find(slot,serial);historyClosed=op&&op->sourceHistoryClosed;}
            if(!historyClosed)historyClosed=owner->SourceBoundLogicalClosed(*snapshot,claim,*result.logical);
            bool accepted=false;
            if(historyClosed)
            {
                std::lock_guard lock(p.finalizerEvents);
                accepted=p.root.owners_->Finalizer()->AcknowledgeSourceBoundLogicalClosed(claim,
                    {p.root.owners_->FinalizerJournal().Event(),snapshot->generations});
            }
            std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);
            if(op){op->consumptionPending=false;op->sourceHistoryClosed=historyClosed;
                if(accepted){op->consumed=result.logical->Consumed();op->interrupted=!op->consumed;}}
        }
    }
    if(result.terminal)
    {
        std::lock_guard lock(p.mutex);auto* op=p.Find(slot,serial);if(op)op->sourceTerminal=result.terminal;
    }
    return result;
}
Lifecycle::FinalizationResult StreamCoordinatorKernel::ApplyFinalConsumer(
    const Lifecycle::PrimaryNrClaimHandle& claim,Lifecycle::NativeFinalConsumerAction& action)
{
    auto& p=*impl_;std::shared_ptr<const Protocol::NativeInvocationSnapshot> snapshot;
    {
        std::lock_guard lock(p.mutex);
        for(const auto& op:p.operations)
            if(op.used&&op.claim&&*op.claim==claim&&op.snapshot&&!op.finalizerRetirement&&
               op.snapshot->Delivery()==C::NativeDeliveryKind::FinalConsumerRequired&&action.Frame()==op.snapshot->frame)
            {snapshot=op.snapshot;break;}
    }
    if(!snapshot)return {};
    Lifecycle::FinalizationResult result;
    {
        std::lock_guard lock(p.finalizerEvents);
        const C::ScopeRef scope{p.root.owners_->FrameGenerationJournal().Binding().subject};
        Lifecycle::NativeFinalConsumerEvents events{
            [&]{return Lifecycle::FinalizerEvent{p.root.owners_->FinalizerJournal().Event(),snapshot->generations};},
            p.root.owners_->FrameGenerationJournal().Header(C::ContractId::C13,scope),
            p.root.owners_->FinalConsumerIdentityJournal().Header(C::ContractId::C11,scope)};
        result=action.Apply(*p.root.owners_->Finalizer(),events);
    }
    if(result.consumption)AcknowledgeFinalConsumption(*result.consumption);
    if(result.interruption)AcknowledgeFinalInterruption(*result.interruption);
    if(result.retirement)AcknowledgeFinalizerRetirement(*result.retirement);
    return result;
}
}
