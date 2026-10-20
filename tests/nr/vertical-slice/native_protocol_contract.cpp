#include "../finalizer/finalizer_fixture.h"
#if __has_include(<nr/protocol/NativeTemporalProtocolAdapter.h>)
#include <nr/protocol/NativeTemporalProtocolAdapter.h>
#define NR_NATIVE_PROTOCOL 1
#endif
#if __has_include(<dlssnr/NativeDispatchOutcome.h>)
#include <dlssnr/NativeDispatchOutcome.h>
#define NR_OUTPUT_PASS_OUTCOME 1
#endif
#if __has_include(<dlssnr/NativeOutputPassToken.h>)
#include <dlssnr/NativeOutputPassToken.h>
#define NR_OUTPUT_PASS_TOKEN 1
#endif
using namespace NrTest002;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-protocol",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_NATIVE_PROTOCOL
    namespace P=Neurotic::Protocol;
#ifdef NR_OUTPUT_PASS_TOKEN
    static_assert(!std::is_default_constructible_v<DlssNr::NativeOutputPassToken>);
    static_assert(!std::is_copy_constructible_v<DlssNr::NativeOutputPassToken>);
    static_assert(!std::is_move_constructible_v<DlssNr::NativeOutputPassToken>);
    static_assert(!std::is_constructible_v<DlssNr::NativeOutputPassToken,
        const DlssNr::NativeRendererInvocationBorrow*,const DlssNr::NativeDispatchOutcome&>);
    r.Check("W03.pass-capability-private",true,true,"only real renderer issues output authority");
#else
    r.Check("W03.pass-capability-private",false,true,"missing actual output-pass capability");
#endif
    r.Check("T30.native-default-off",P::SelectNativePath(false,0,true,true)==P::NativePath::Legacy,true,"SelectNativePath");
    r.Check("T30.native-nonnative-inert",P::SelectNativePath(true,2,true,true)==P::NativePath::Legacy,true,"SelectNativePath");
    r.Check("T30.native-matching-placement",P::SelectNativePath(true,0,true,true)==P::NativePath::Candidate,true,"SelectNativePath");
    r.Check("T30.native-post-no-repeat",P::SelectNativePath(true,0,true,false)==P::NativePath::Skip,true,"SelectNativePath");
    auto f=std::make_unique<FinalizerTests::Fixture>();
    auto first=P::AcquireNativePrimary(*f->finalizer,f->request,f->store,f->Event());
    r.Check("T27.native-first-claim",first.has_value(),true,"AcquireNativePrimary");
    r.Check("T27.native-repeated-no-execute",!P::AcquireNativePrimary(*f->finalizer,f->request,f->store,f->Event()),true,"AcquireNativePrimary");
    P::NativeExecutionObserver observer;
    r.Check("T29.native-observer-default-no-rights",!observer.Check(P::NativeCheckPoint::BeforeModel,{}),true,"NativeExecutionObserver");
#ifndef NR_OUTPUT_PASS_OUTCOME
    r.Check("W03.resolve-effect-accounting",false,true,"actual dispatch must retain possible effects");
#else
    using Effect=DlssNr::NativeDispatchEffect;
    // Pointer identities are never dereferenced by the CPU observation type.
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(0x1000);
    auto* target=reinterpret_cast<ID3D12Resource*>(0x2000);
    DlssNr::NativeDispatchOutcome pass(list,target,64,32);
    r.Check("W03.pass-local-identity",pass.CommandList()==list&&pass.Target()==target&&pass.Width()==64&&pass.Height()==32&&pass.Subresource()==0,true,"local pass only");
    P::NativeExecutionObserver refused;
    refused.ObserveOutputPass(pass);
    r.Check("W03.pass-refused-no-dispatch",pass.Effect()==Effect::RefusedBeforeCommands&&!refused.Facts().commandsRecorded,true,"pre-command refusal");
    pass.ObserveStateMutation();
    P::NativeExecutionObserver mutated;
    mutated.ObserveOutputPass(pass);
    r.Check("W03.pass-state-distinct-from-write",mutated.Facts().outputPass->Effect()==Effect::StateMutationPossible&&!mutated.Facts().commandsRecorded,true,"setters alone do not dispatch");
    pass.ObserveDispatchPossible();
    P::NativeExecutionObserver uncertain;
    uncertain.Fail("Native.EarlierFailure");
    uncertain.ObserveOutputPass(pass);
    r.Check("W03.pass-partial-effect-sticky",uncertain.Facts().commandsRecorded&&uncertain.Facts().outputPass->Effect()==Effect::DispatchPossible&&uncertain.Facts().failure==P::Symbol("Native.EarlierFailure"),true,"possible effects survive earlier failure");
    pass.ObserveDispatchRecorded();pass.ObserveStateMutation();pass.ObserveDispatchPossible();
    r.Check("W03.pass-monotonic",pass.Effect()==Effect::DispatchRecorded,true,"later notification cannot erase dispatch");
    P::NativeExecutionObserver written;
    written.ObserveOutputPass(pass);
    r.Check("W03.pass-no-inferred-delivery",written.Facts().commandsRecorded&&written.Facts().composition==C::CompositionStatus::NotAttempted&&!P::S::Established(written.Facts().output),true,"recorded dispatch is not resource publication");
    r.Check("W03.pass-publication-failure",!written.Produced(P::NativeCheckPoint::BeforeComposition,{}),true,"missing Resource owner refuses");
    r.Check("W03.pass-publication-preserves-effects",written.Facts().outputPass==pass&&written.Facts().commandsRecorded,true,"failed publication retains actual operation");
    P::NativeExecutionObserver duplicate;
    duplicate.ObserveOutputPass(pass);duplicate.ObserveOutputPass(pass);
    r.Check("W03.pass-duplicate-refused",duplicate.Facts().failure==P::Symbol("Native.OutputPassRepeated")&&duplicate.Facts().outputPass==pass,true,"one resolve per observer");
    DlssNr::NativeDispatchOutcome other(list,reinterpret_cast<ID3D12Resource*>(0x3000),64,32);
    other.ObserveDispatchRecorded();duplicate.ObserveOutputPass(other);
    r.Check("W03.pass-target-replacement-refused",duplicate.Facts().outputPass==pass&&duplicate.Facts().commandsRecorded,true,"first exact target retained");
    P::NativeExecutionObserver earlier;
    earlier.Recorded();earlier.ObserveOutputPass(DlssNr::NativeDispatchOutcome(list,target,64,32));
    r.Check("W03.pass-refusal-does-not-clear-earlier-work",earlier.Facts().commandsRecorded,true,"encode/model work retained");
    P::NativeExecutionObserver invalid;
    invalid.ObserveOutputPass(DlssNr::NativeDispatchOutcome(nullptr,target,0,32));
    r.Check("W03.pass-invalid-binding-refused",!invalid.Facts().outputPass&&invalid.Facts().failure==P::Symbol("Native.OutputPassInvalid"),true,"exact local binding required");
    auto inconsistent=written.Facts();inconsistent.commandsRecorded=false;
    r.Check("W03.pass-coherence",!P::NativeFactsCoherent(inconsistent),true,"possible dispatch requires effect accounting");
#endif
#else
    r.Check("T30.native-adapter-production",false,true,"AcquireNativePrimary");
#endif
    return r.Finish();});
}
