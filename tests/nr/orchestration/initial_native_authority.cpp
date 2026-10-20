// Compile without NR_ORCH_INIT_TESTING. Every refusal has a compiling control.
#include <nr/lifecycle/NativeSessionLifetime.h>
#include <nr/protocol/NativeTemporalProtocolAdapter.h>
#include "../context/ContextFixtureStore.h"
namespace O=Neurotic::Orchestration;
namespace L=Neurotic::Lifecycle;
namespace P=Neurotic::Protocol;
namespace C=Neurotic::Contracts;
// Irreversible publication tail uses only fixed fact/shared-pointer assignments.
// A future allocating contract field must fail these compiling controls.
static_assert(std::is_nothrow_copy_assignable_v<C::OptionalFact<C::RouteGeneration>>);
static_assert(std::is_nothrow_copy_assignable_v<C::OptionalFact<bool>>);
static_assert(std::is_nothrow_copy_assignable_v<std::shared_ptr<const O::MaterializationRecord>>);
static_assert(std::is_nothrow_move_constructible_v<O::CurrentRouteView>);
struct Executor{P::NativeExecutionFacts operator()(const P::NativeBindingMap&,P::NativeExecutionObserver& observer){return observer.Facts();}};
void Probe(O::InitialFixedNativeCoordinator& coordinator,L::NativeSessionLifetime& root,L::NativeOwnerSet& owners,
    P::NativeInvocationAdmission& admission,const C::PlanCommit& plan,const P::RecipeProduct& product,
    const L::PrimaryClaimRequest& request,const C::RecordHeader& header,L::FinalRealFrameFinalizer& finalizer,
    P::NativeOperationOwner& owner,NrContextTests::Store& store,std::shared_ptr<O::InitialNativeOwnerPort> port)
{
    (void)coordinator;(void)root;(void)owners;(void)admission;(void)plan;(void)product;
    (void)request;(void)header;(void)finalizer;(void)owner;(void)store;(void)port;
#ifdef NR_AUTH_CONTROL
    auto evidence=plan;auto view=coordinator.TryReadCurrent();(void)evidence;(void)view;
    static_assert(!std::is_copy_constructible_v<P::NativeInvocationAdmission>);
#if NR_AUTH_CASE == 5
    Executor executor;P::ExecuteNativeProtocol(admission,header,owner,store,executor);
#endif
#else
#if NR_AUTH_CASE == 1
    O::CurrentRouteView forged(plan);
#elif NR_AUTH_CASE == 2
    owners.Route();
#elif NR_AUTH_CASE == 3
    coordinator.Recommit();
#elif NR_AUTH_CASE == 4
    auto copied=admission;
#elif NR_AUTH_CASE == 5
    Executor executor;auto events=[] {return L::FinalizerEvent{};};
    P::ExecuteNativeProtocol(product,request,header,finalizer,owner,store,events,executor);
#elif NR_AUTH_CASE == 6
    coordinator.MintFinalSeal();
#elif NR_AUTH_CASE == 7
    root.CloseDrained(true);
#elif NR_AUTH_CASE == 8
    O::InitialNativeTestAccess::Open(port);
#else
#error Select a numbered authority case
#endif
#endif
}
