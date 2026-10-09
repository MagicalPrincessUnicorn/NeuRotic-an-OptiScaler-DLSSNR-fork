#pragma once
#include "NativeFinalConsumerAction.h"
namespace Neurotic::Lifecycle
{
// Sibling of canonical Strict. Only the coordinator applies an action after
// matching the retained invocation's authentic source transaction.
class NativeSourceBoundConsumerAction
{
    friend class Orchestration::StreamCoordinatorKernel;
#ifdef NR_SOURCE_BOUND_CONSUMER_TESTING
    friend struct SourceBoundPublicationTestAccess;
#endif
  protected:
    virtual SourceBoundResult Apply(FinalRealFrameFinalizer&,NativeFinalConsumerEvents&)=0;
    template<class Reader>static SourceBoundResult Authorize(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,
        const FinalBoundaryObservation& b,const FinalFrameRuntimeProof& p,const Reader& r,const FinalizerEvent& e)
    {return f.AuthorizeSourceBoundConsumer(h,b,p,r,e);}
    template<class Reader>static SourceBoundResult AuthorizeWithAncestry(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,
        const FinalBoundaryObservation& b,const FinalInputAncestry& ancestry,const FinalFrameRuntimeProof& p,
        const Reader& r,const FinalizerEvent& e)
    {return f.AuthorizeSourceBoundConsumerWithAncestryV1(h,b,ancestry,p,r,e);}
    static bool Current(FinalRealFrameFinalizer& f,const SourceBoundConsumerAuthorization& a,const FinalFrameRuntimeProof& p)
    {return f.SourceBoundAuthorizationCurrent(a,p);}
    static SourceBoundResult Dispatch(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,
        const SourceBoundDispatchOutcome& o,const FinalizerEvent& e){return f.ObserveSourceBoundDispatch(h,o,e);}
    static SourceBoundResult Submission(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,
        const SourceBoundSubmissionOutcome& o,const FinalizerEvent& e){return f.ObserveSourceBoundSubmission(h,o,e);}
    static SourceBoundResult Release(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,
        const LiveConsumptionLease& l,const OwnerFacts& o,const FinalizerEvent& e){return f.ObserveSourceBoundProviderRelease(h,l,o,e);}
    static SourceBoundResult ResourceRetirement(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,
        const LiveConsumptionLease& l,const OwnerFacts& o,const FinalizerEvent& e){return f.ObserveSourceBoundResourceRetirement(h,l,o,e);}
    static SourceBoundResult Retire(FinalRealFrameFinalizer& f,const SourceBoundClaimHandle& h,
        const C::RecordHeader& owner,const SourceTransactionExclusion& transaction,
        const C::OptionalFact<bool>& excluded,const FinalizerEvent& e){return f.RetireSourceBound(h,owner,transaction,excluded,e);}
  public:
    virtual ~NativeSourceBoundConsumerAction()=default;
    virtual const C::InterceptedSourceTransactionV1& Transaction()const noexcept=0;
};
class NativeSourceBoundConsumerTail
{
  public:
    virtual ~NativeSourceBoundConsumerTail()=default;
    virtual const C::InterceptedSourceTransactionV1& Transaction()const noexcept=0;
    virtual void Advance()=0;
};
}
