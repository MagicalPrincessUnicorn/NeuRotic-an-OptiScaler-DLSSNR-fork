#pragma once
#include "FinalRealFrameFinalizer.h"
#include "OwnerPublicationJournal.h"
#include <functional>

namespace Neurotic::Orchestration { class StreamCoordinatorKernel; }
namespace Neurotic::Lifecycle
{
// The existing coordinator authenticates the exact retained strict claim and
// serializes these publications with every other finalizer event.
struct NativeFinalConsumerEvents
{
    std::function<FinalizerEvent()> next;
    C::RecordHeader frameGeneration;
    C::RecordHeader identity;
};
class NativeFinalConsumerAction
{
    friend class Orchestration::StreamCoordinatorKernel;
#ifdef NR_FSR3_CONSUMER_TESTING
    friend struct Fsr3ConsumerTestAccess;
#endif
  protected:
    virtual FinalizationResult Apply(FinalRealFrameFinalizer&,NativeFinalConsumerEvents&)=0;
  public:
    virtual ~NativeFinalConsumerAction()=default;
    virtual const C::FrameIdentity& Frame()const noexcept=0;
};
class NativeFinalConsumerTail
{
  public:
    virtual ~NativeFinalConsumerTail()=default;
    virtual const C::FrameIdentity& Frame()const noexcept=0;
    virtual void Advance()=0;
};
}
