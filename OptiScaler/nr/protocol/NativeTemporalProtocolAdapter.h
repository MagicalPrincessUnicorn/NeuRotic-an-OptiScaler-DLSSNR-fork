#pragma once
#include "NativeTemporalRuntime.h"
#include <nr/lifecycle/FinalRealFrameFinalizer.h>
namespace Neurotic::Protocol
{
enum class NativePath { Legacy,Candidate,Skip };
inline NativePath SelectNativePath(bool enabled,std::uint32_t route,bool selectedBefore,bool callbackBefore)noexcept
{
    if(!enabled||route!=0)return NativePath::Legacy;
    return selectedBefore==callbackBefore?NativePath::Candidate:NativePath::Skip;
}
template<class Reader>std::optional<Lifecycle::PrimaryNrClaimHandle> AcquireNativePrimary(
    Lifecycle::FinalRealFrameFinalizer& finalizer,const Lifecycle::PrimaryClaimRequest& request,
    const Reader& reader,const Lifecycle::FinalizerEvent& event)
{
    const auto result=finalizer.TryClaimPrimary(request,reader,event);
    return result.accepted&&result.newPrimaryClaim?result.claim:std::nullopt;
}
struct NativeProtocolResult
{
    std::optional<Lifecycle::PrimaryNrClaimHandle> claim;
    std::optional<Lifecycle::SourceBoundClaimHandle> sourceClaim;
    std::optional<C::EvaluationResult> evaluation;
    NativeExecutionFacts facts;
    C::Symbol reason;
    bool submittedToFinalizer=false;
    bool legacyAllowed=false;
};
// Executor owns the exact current renderer call. It receives only this callback's
// bindings and observer and returns owner-proven output/submission milestones.
// Reader/store and Events are authenticated owner facades held for this call.
#ifdef NR_NATIVE_PROTOCOL_FIXTURE_TESTING
// Historical CPU-only fixture entry. Deliberately absent from production; raw
// requests never create coordinator authority or reach the actual model seam.
template<class Store,class Events,class Executor> NativeProtocolResult ExecuteNativeProtocol(
    const RecipeProduct& product,const Lifecycle::PrimaryClaimRequest& request,const C::RecordHeader& resultHeader,
    Lifecycle::FinalRealFrameFinalizer& finalizer,NativeOperationOwner& resourceOwner,
    Store& store,Events& events,Executor& executor,Lifecycle::NativeHistoryState* history=nullptr)
{
    NativeProtocolResult result;
    if(request.recipe!=product.recipe){result.reason=Symbol("Native.RecipeMismatch");return result;}
    const auto map=BuildNativeBindingMap(product,store);
    if(!map){result.reason=Symbol("Native.BindingRejected");result.legacyAllowed=true;return result;}
    result.claim=AcquireNativePrimary(finalizer,request,store,events());
    if(!result.claim){result.reason=Symbol("Native.PrimaryUnavailable");return result;}
    NativeExecutionObserver observer(&resourceOwner,history,product.recipe.evaluation);
    try
    {
        // Executor may not invoke legacy parsing; it consumes the map at the
        // existing insertion point and reports each operation at its owner.
        result.facts=executor(*map,observer);
        const auto& observed=observer.Facts();
        if(result.facts.modelAttempted!=observed.modelAttempted||result.facts.modelSucceeded!=observed.modelSucceeded||
           result.facts.actualResetRequested!=observed.actualResetRequested||
           result.facts.commandsRecorded!=observed.commandsRecorded||result.facts.outputRestoreFailed!=observed.outputRestoreFailed||
           result.facts.commandStateRestoreFailed!=observed.commandStateRestoreFailed||result.facts.outputPass!=observed.outputPass||
           (result.facts.composition!=observed.composition&&
            !(observed.composition==C::CompositionStatus::Recorded&&result.facts.composition==C::CompositionStatus::Accepted))||
           (!observed.failure.Empty()&&result.facts.failure!=observed.failure))
        {result.facts=observed;result.facts.failure=Symbol("Native.MilestoneMismatch");}
        result.evaluation=BuildNativeEvaluationResult(product.recipe,resultHeader,result.facts,store);
        if(!result.evaluation){result.reason=Symbol("Native.ResultPublicationFailed");return result;}
        Lifecycle::FinalCandidateSubmission candidate;candidate.result=*result.evaluation;candidate.generations=request.generations;
        candidate.acceptedFinalCandidate=C::OptionalFact<bool>::FromKnown(result.evaluation->stage==C::OutcomeStage::Produced,
            C::EvidenceRef{resultHeader.record});
        result.submittedToFinalizer=finalizer.SubmitEvaluationResult(*result.claim,candidate,store,events()).accepted;
        if(!result.submittedToFinalizer)result.reason=Symbol("Native.FinalizerRejected");
        if(result.submittedToFinalizer&&result.evaluation->stage==C::OutcomeStage::Produced)
            observer.AwaitFinalConsumption();
        // No legacy retry after acquiring a primary claim. Even pre-model
        // recording may still replay; only the owner can release the claim.
    }
    catch(...)
    {
        result.facts=observer.Facts();result.reason=Symbol("Native.OwnerFailure");
        result.facts.failure=result.reason;
        // Retain the primary and all owner work; never invent cancellation.
    }
    return result;
}
#endif
template<class Store,class Executor> NativeProtocolResult ExecuteNativeProtocol(
    NativeInvocationAdmission& admission,const C::RecordHeader& resultHeader,NativeOperationOwner& resourceOwner,
    Store& store,Executor& executor,Lifecycle::NativeHistoryState* history=nullptr,bool localUndelivered=false)
{
    NativeProtocolResult result;const auto* product=admission.Product();
    if(!product){result.reason=Symbol("Native.AdmissionUnavailable");return result;}
    const auto map=BuildNativeBindingMap(*product,store);
    if(!map){result.reason=Symbol("Native.BindingRejected");return result;}
    if(!admission.AcquirePrimary()){result.reason=Symbol("Native.PrimaryUnavailable");return result;}
    result.claim=admission.Primary();result.sourceClaim=admission.SourceBoundPrimary();NativeExecutionObserver observer(admission,resourceOwner,history);
    try
    {
        result.facts=executor(*map,observer);
        const auto& observed=observer.Facts();
        if(result.facts.modelAttempted!=observed.modelAttempted||result.facts.modelSucceeded!=observed.modelSucceeded||
           result.facts.actualResetRequested!=observed.actualResetRequested||result.facts.commandsRecorded!=observed.commandsRecorded||
           result.facts.outputRestoreFailed!=observed.outputRestoreFailed||
           result.facts.commandStateRestoreFailed!=observed.commandStateRestoreFailed||result.facts.outputPass!=observed.outputPass||
           (result.facts.composition!=observed.composition&&
            !(observed.composition==C::CompositionStatus::Recorded&&result.facts.composition==C::CompositionStatus::Accepted))||
           (!observed.failure.Empty()&&result.facts.failure!=observed.failure))
        {result.facts=observed;result.facts.failure=Symbol("Native.MilestoneMismatch");}
        result.evaluation=BuildNativeEvaluationResult(product->recipe,resultHeader,result.facts,store);
        if(!result.evaluation){result.reason=Symbol("Native.ResultPublicationFailed");return result;}
        if(admission.Delivery()==C::NativeDeliveryKind::HostReturn)
        {
            if(localUndelivered)
            {
                // SP03 records only local work. No post-SR or caller-target
                // publication exists, so history must close unconsumed.
                result.reason=Symbol("Native.LocalUndelivered");
            }
            else
            {
            // C07 describes Native execution. The authenticated outer return
            // owner must still prove the caller-target write before delivery.
            result.reason=Symbol("Native.HostReturnPending");
            if(result.evaluation->stage==C::OutcomeStage::Produced)observer.AwaitHostReturn();
            }
        }
        else if(admission.Delivery()==C::NativeDeliveryKind::FinalConsumerRequired)
        {
            result.submittedToFinalizer=admission.SubmitEvaluation(*result.evaluation);
            if(!result.submittedToFinalizer)result.reason=Symbol("Native.FinalizerRejected");
            if(result.submittedToFinalizer&&result.evaluation->stage==C::OutcomeStage::Produced)observer.AwaitFinalConsumption();
        }
        else result.reason=Symbol("Native.DeliveryUnavailable");
    }
    catch(...){result.facts=observer.Facts();result.reason=Symbol("Native.OwnerFailure");result.facts.failure=result.reason;}
    return result;
}
}
