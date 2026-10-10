#include "pch.h"
#include <runtime/OwnedNgxCreateParameters.h>
#include <runtime/NgxCallGate.h>
#include <runtime/OwnedFeatureReleasePolicy.h>
#include <runtime/RuntimeLifetime.h>
#include <mfg/ExperimentalMfgRuntime.h>
#include "../nr/diagnostics/capability/CapabilityNgxObservation.h"
// NR-FEED-001 BEGIN
#include <inputs/universal_feeder/providers/NgxObservationAdapter.h>
// NR-FEED-001 END
#include <dlssnr/FrameTrace.h>
#include <dlssnr/PreFg.h>
#include <dlssnr/FgLifecycle.h>
#include <nr/semantic/character/CharacterFgActivity.h>
#include <nr/semantic/character/CharacterRuntime.h>
#include <dlssnr/NrGpuSafety.h>
#include <dlssnr/NativeFeatureRegistry.h>
#include <nr/lifecycle/NativeProcessBootstrap.h>
#include <nr/lifecycle/SelectedFsr3NativeLeaseOwner.h>
#include <nr/lifecycle/Fsr3ControlledModule.h>
#include <nr/protocol/NativeFrameBridge.h>
#include <nr/protocol/NativeTypedEvaluation.h>
#include <nr/protocol/NativeCommandStateScope.h>
#include <dlssnr/NativeParameterOverride.h>
#include <dlssnr/NativeTemporalInputs.h>
#include <dlssnr/NativeSharedListInvocation.h>
#include <dlssnr/DlssNr_BasicMultipass.h>
#include <dlssnr/DlssNr_Multipass.h>
#include <dlssnr/NativeIdentity.h>
#include <dlssnr/DlssNr_PresentGuides.h>
#include "Util.h"
#include "Config.h"

#include "NVNGX_DLSS.h"
#include "NVNGX_Parameter.h"
#include "proxies/NVNGX_Proxy.h"
#include "dlssnr/DlssNr.h"
#include "dlssnr/DlssNr_ExposureScan.h"
#include <upscalers/dlss/DLSSFeature_Dx12.h>
#include <shaders/output_scaling/OS_Dx12.h>

#include <upscalers/FeatureProvider_Dx12.h>
#include "upscalers/dlss/DLSSFeature_Dx12.h"

#include <framegen/nvngx/Nvngx_FG.h>
#include "FG/FSR3_Dx12_FG.h"
#include "FG/Upscaler_Inputs_Dx12.h"

#include <imgui/ImGuiNotify.hpp>

#include <hooks/D3D12_Hooks.h>
#include <hooks/Streamline_Hooks.h>

#include <dxgi1_4.h>
#include <shared_mutex>
#include "detours/detours.h"
#include <ankerl/unordered_dense.h>
#include <misc/IdentifyGpu.h>

static ankerl::unordered_dense::map<unsigned int, ContextData<IFeature_Dx12>> Dx12Contexts;
static DlssNr::NativeFeatureRegistry<NVSDK_NGX_Feature> HandleToFeature;

template<class Invocation>
static bool InvokeLegacyNativeNr(ID3D12GraphicsCommandList* list, NVSDK_NGX_Parameter* parameters,
                                 const NrConfigSnapshot<Config>& settings, bool beforeSeam, bool forcedAfter, Invocation invoke)
{
    const bool runBefore = settings.DlssNrRunBeforeSr.value_or_default();
    const bool requestedSeam = beforeSeam ? runBefore : (!runBefore || forcedAfter);
    if (!settings.GetDlssNrRuntimeSnapshot().enabled || settings.DlssNrRoute.value_or_default() != 0 || !requestedSeam)
    {
        // The helper still owns CPU bookkeeping at an unrequested boundary.
        invoke();
        return true;
    }

    using Mask = Neurotic::D3D12::RestoreMask;
    auto mask = Mask::Compute | Mask::Pipeline | Mask::Heaps | Mask::HeapInvalidatedTables;
    if (beforeSeam) mask = mask | Mask::RootInvalidatedTables | Mask::BlendFactor;
    // The nested legacy envelope may replay graphics when explicitly requested.
    // Cover that stage's real mutations rather than weakening the owner check.
    if (Config::Instance()->RestoreGraphicSignature.value_or_default()) mask = mask | Mask::Graphics;
    Neurotic::D3D12::NativeStateCaptureDiagnostic diagnostic;
    void* originalOutput = nullptr;
    DlssNr::NativeIdentity::Resolved<ID3D12GraphicsCommandList> nativeList;
    bool entered = false, invocationCompleted = false;
    bool restoreAttempted = false, restoreReturned = false, stateRestored = false;
    Neurotic::D3D12::NativeRecordingState::RestoreDiagnostic restoreDiagnostic;
    const auto result = DlssNr::InvokeNativeSharedList(
        [&]() -> std::optional<NativeStateRestorePoint> {
            // Creation enrolls the resolved native object. A Streamline caller
            // interface is not a journal key; keep invocation on its existing
            // path while capturing/restoring that same authenticated native list.
            nativeList = DlssNr::NativeIdentity::Resolve<ID3D12GraphicsCommandList>(list);
            if (!nativeList.object)
            {
                diagnostic.reason = "Native.CallerIdentityUnavailable";
                return {};
            }
            auto saved = D3D12Hooks::CapturePostSrState(nativeList.object.Get(), mask, &diagnostic);
            if (!saved) return {};
            if (!parameters || parameters->Get(NVSDK_NGX_Parameter_Output, &originalOutput) !=
                    NVSDK_NGX_Result_Success || !originalOutput)
            {
                diagnostic.reason = "Native.OutputBindingUnavailable";
                return {};
            }
            return saved;
        }, [&] { entered = true; invoke(); invocationCompleted = true; },
        [&](const auto& saved) {
            restoreAttempted = true;
            stateRestored = D3D12Hooks::RestorePostSrState(saved, &restoreDiagnostic);
            restoreReturned = true;
            return stateRestored;
        });
    // A completed envelope proves invocation and replay, not that NR rendered.
    // Keep this small summary available after the detailed 16-refusal budget.
    try
    {
        const bool zeroBasic = DlssNr::BasicMultipass::Active(settings) &&
                               DlssNr::Multipass::RequestedCount(settings) == 0;
        if (!zeroBasic && (beforeSeam ? runBefore : (!runBefore || forcedAfter)))
        {
            static std::mutex reportMutex;
            static bool reported = false, lastEntered = false, lastRunBefore = false, lastForcedAfter = false;
            static auto lastResult = DlssNr::NativeSharedListResult::Skipped;
            static std::string lastReason;
            static ULONGLONG lastMs = 0;
            std::lock_guard lock(reportMutex);
            const bool skipped = result == DlssNr::NativeSharedListResult::Skipped;
            const char* reason = skipped ? diagnostic.reason : "";
            const auto now = GetTickCount64();
            if (!reported || entered != lastEntered || runBefore != lastRunBefore || forcedAfter != lastForcedAfter ||
                result != lastResult || lastReason != reason || now - lastMs >= 1000)
            {
                reported = true; lastEntered = entered; lastRunBefore = runBefore; lastForcedAfter = forcedAfter;
                lastResult = result; lastReason = reason; lastMs = now;
                const char* outcome = skipped ? "Skipped" : result == DlssNr::NativeSharedListResult::Restored
                    ? "Restored-invoke-and-replay" : "Failed-invoke-or-replay";
                if (skipped)
                    LOG_INFO("NR Native invocation: seam={} runBefore={} forcedAfter={} outcome={} entered={} "
                             "reason={} parameter={} known={} required={}",
                             beforeSeam ? "Before" : "After", runBefore, forcedAfter, outcome, entered,
                             diagnostic.reason, diagnostic.parameter, diagnostic.known, diagnostic.required);
                else
                    LOG_INFO("NR Native invocation: seam={} runBefore={} forcedAfter={} outcome={} entered={}",
                             beforeSeam ? "Before" : "After", runBefore, forcedAfter, outcome, entered);
            }
        }
    }
    catch (...) {} // Diagnostics cannot alter SR continuation or replay results.
    if (result == DlssNr::NativeSharedListResult::Skipped)
    {
        static std::atomic<unsigned> refused {0};
        if (++refused <= 16)
        {
            // Read-only, bounded evidence distinguishes an unseen raw wrapper
            // from missing Reset history or a genuine native state rejection.
            // Diagnostic failure must never prevent required SR continuation.
            try
            {
                const auto raw = D3D12Hooks::ObserveNativeRecording(list);
                const auto recorded = D3D12Hooks::ObserveNativeRecording(nativeList.object.Get());
                const auto hooks = D3D12Hooks::DiagnoseNativeRecording(nativeList.object.Get());
                LOG_WARN("NR Native caller-state unavailable; custom pass skipped before mutation: "
                         "list={:p} native={:p} layers={} resolve={:X} reason={} "
                         "rawEntry={} rawActive={} nativeEntry={} active={} coverage={} incarnation={} generation={} "
                         "installs={}/{} globalInstalls={}/{} resets={}/{} enrolled={} closed={} "
                         "hookFlags={} historicalFirstRefusal={} historicalFirstRefusalSequence={} historicalSlot={} stage={} "
                         "firstResult={:X} resetResult={:X} parameter={} known={} required={} trace_count={} trace_total={} "
                         "capturedEntry={} capturedRawActive={} capturedTainted={} capturedHistoryKnown={} capturedHistory={} "
                         "capturedIncarnation={} capturedGeneration={} currentFirstInvalidation={} currentSlot={} currentSequence={} currentWork={}",
                         static_cast<void*>(list), static_cast<void*>(nativeList.object.Get()), nativeList.layers,
                         static_cast<unsigned>(nativeList.result), diagnostic.reason,
                         raw.trackingBeganBeforeRecording, raw.active, recorded.trackingBeganBeforeRecording,
                         recorded.active, recorded.completeCoverage, recorded.incarnation, recorded.hookGeneration,
                         hooks.installSuccesses, hooks.installAttempts, hooks.globalInstallSuccesses,
                         hooks.globalInstallAttempts, hooks.resetSuccesses, hooks.resetCalls,
                         hooks.enrollmentSuccesses, hooks.closeCalls, hooks.flags,
                         static_cast<unsigned>(hooks.firstRefusal), hooks.firstRefusalSequence,
                         hooks.failedSlot, hooks.enrollmentStage, static_cast<unsigned>(hooks.firstResult),
                         static_cast<unsigned>(hooks.lastResetResult), diagnostic.parameter,
                         diagnostic.known, diagnostic.required, diagnostic.traceCount, diagnostic.traceTotal,
                         diagnostic.recordingPresent, diagnostic.rawActive, diagnostic.tainted,
                         diagnostic.historyKnown, diagnostic.historyComplete, diagnostic.incarnation, diagnostic.hookGeneration,
                         diagnostic.firstInvalidation.reason, diagnostic.firstInvalidation.slot,
                         diagnostic.firstInvalidation.sequence, diagnostic.firstInvalidation.workOrdinal);
                // The legacy path uses the same already-captured bounded tail
                // as protocol diagnostics. It never changes capture authority.
                for (UINT i = 0; i < std::min<UINT>(diagnostic.traceCount, UINT(diagnostic.trace.size())); ++i)
                {
                    const auto& entry = diagnostic.trace[i];
                    LOG_WARN("NR Native caller-state trace: list={:p} native={:p} observedIncarnation={} "
                             "capturedIncarnation={} seq={} work={} op={} a={} b={} c={}",
                             static_cast<void*>(list), static_cast<void*>(nativeList.object.Get()),
                             recorded.incarnation, diagnostic.incarnation, entry.sequence, entry.workOrdinal, entry.operation,
                             entry.a, entry.b, entry.c);
                }
            }
            catch (...)
            {
                LOG_WARN("NR Native caller-state unavailable; custom pass skipped before mutation: "
                         "list={:p} reason={} recording diagnostics unavailable", static_cast<void*>(list), diagnostic.reason);
            }
        }
        return true;
    }
    if (result == DlssNr::NativeSharedListResult::Restored)
    {
        static std::atomic<unsigned> restored {0};
        if (++restored <= 4)
            LOG_DEBUG("NR Native caller-state restored before continuation: list={:p}", static_cast<void*>(list));
        return true;
    }

    // Bounded evidence only; do not weaken the failed-restoration continuation rule.
    try
    {
        static std::atomic<unsigned> failures {0};
        if (++failures <= 16)
        {
            const auto hooks = D3D12Hooks::DiagnoseNativeRecording(nativeList.object.Get());
            const auto& d = restoreDiagnostic;
            LOG_ERROR("NR Native failure detail: seam={} list={:p} native={:p} invocationCompleted={} "
                      "restoreAttempted={} restoreReturned={} stateRestored={} reason={} "
                      "active={} tainted={} incarnation={}/{} computeMutation={}/{} graphicsMutation={}/{} "
                      "unrestorableOrdinal={}/{} work={}/{} firstRefusal={} firstRefusalSequence={} slot={} "
                      "trace_count={} trace_total={}",
                      beforeSeam ? "Before" : "After", static_cast<void*>(list), static_cast<void*>(nativeList.object.Get()),
                      invocationCompleted, restoreAttempted, restoreReturned, stateRestored, d.reason,
                      d.active, d.tainted, d.savedIncarnation, d.currentIncarnation,
                      d.savedComputeMutation, d.currentComputeMutation, d.savedGraphicsMutation, d.currentGraphicsMutation,
                      d.savedUnrestorableOrdinal, d.currentUnrestorableOrdinal, d.savedWorkOrdinal, d.currentWorkOrdinal,
                      static_cast<unsigned>(hooks.firstRefusal), hooks.firstRefusalSequence, hooks.failedSlot,
                      d.tail.traceCount, d.tail.traceTotal);
            LOG_ERROR("NR Native non-root witness: native={:p} incarnation={} slot={} seq={} ordinal={} work={} "
                      "a={} b={} c={} scope=last-nonroot priorStateKnown=false",
                      static_cast<void*>(nativeList.object.Get()), d.currentIncarnation, d.lastUnrestorableSlot,
                      d.lastUnrestorableSequence, d.lastUnrestorableOrdinal, d.lastUnrestorableWorkOrdinal,
                      d.lastUnrestorableA, d.lastUnrestorableB, d.lastUnrestorableC);
            for (UINT i = 0; i < std::min<UINT>(d.tail.traceCount, UINT(d.tail.trace.size())); ++i)
            {
                const auto& entry = d.tail.trace[i];
                LOG_ERROR("NR Native restore trace: native={:p} seq={} work={} op={} a={} b={} c={}",
                          static_cast<void*>(nativeList.object.Get()), entry.sequence, entry.workOrdinal,
                          entry.operation, entry.a, entry.b, entry.c);
            }
        }
    }
    catch (...) {} // Logging cannot alter restoration or cleanup.
    // No original SR call or success publication may follow a failed replay.
    // Close temporary legacy output/Reset parameters even on invocation failure.
    try
    {
        if (parameters && originalOutput) parameters->Set(NVSDK_NGX_Parameter_Output, originalOutput);
        DlssNr::RestoreAfterUpscale(parameters);
    }
    catch (...) {}
    LOG_ERROR("NR Native caller-state restoration or invocation failed; continuation refused: list={:p}",
              static_cast<void*>(list));
    return false;
}

namespace Neurotic::Lifecycle
{
Orchestration::InitResult NativeProcessBootstrap::PrepareRuntime(Callback& callback,const NrConfigSnapshot<Config>& settings)
{
    if(!settings.DlssNrRunBeforeSr.value_or_default()&&!callback.RequiredSrInput())
    {callback.runtimeReason_=Protocol::Symbol("Native.PostSrOutputPending");return {};}
    return Materialize(callback,settings,[&]{
        const bool before=settings.DlssNrRunBeforeSr.value_or_default();
        const auto color=callback.publication_?callback.publication_->DeclaredColor(!before):std::nullopt;
        if(!color||!Context::Established(color->domain))return std::unique_ptr<DlssNr::NativeRendererPreparation>{};
        const auto domain=color->domain.KnownPart()->value;
        if(domain!=C::ColorDomain::SceneLinear&&domain!=C::ColorDomain::EncodedDisplay)
            return std::unique_ptr<DlssNr::NativeRendererPreparation>{};
        return DlssNr::NativeRendererPreparation::TryAcquire(callback.call_->CommandList(),
            callback.call_->Resource(before?"Color":"Output"),settings,before,domain==C::ColorDomain::SceneLinear);});
}
Protocol::NativeProtocolResult NativeProcessBootstrap::RunNativeBefore(
    Callback& callback,void* nativeParameters,const NrConfigSnapshot<Config>& settings)
{
    Protocol::NativeProtocolResult result;
    const bool before=settings.DlssNrRunBeforeSr.value_or_default();
    if(!before&&!callback.RequiredSrInput())
    {result.reason=Protocol::Symbol("Native.PostSrOutputPending");return result;}
    if(callback.invocationAttempted_||!callback.Current()||!callback.currentPreparation_)
    {result.reason=Protocol::Symbol("Native.InitializationUnavailable");return result;}
    callback.invocationAttempted_=true;
    if(callback.currentPreparation_->request.placement!=(before?C::Placement::NativeBefore:C::Placement::NativeAfter))
    {result.reason=Protocol::Symbol("Native.PostSrOutputPending");callback.rendererBorrow_.reset();return result;}
    auto borrow=callback.rendererBorrow_;
    // Preparation authenticated the loaded matched forwarder contract and
    // installed this exact provider owner. A no-argument catalog lookup cannot
    // describe the retained renderer generation.
    const auto* preparedResources=borrow?borrow->Inspect():nullptr;
    if(!preparedResources||!preparedResources->providerUses||
       !preparedResources->providerUses->Contract().Supported())
    {result.reason=Protocol::Symbol("ProviderLifetimeContractUnavailable");callback.runtimeReason_=result.reason;callback.rendererBorrow_.reset();return result;}
    if(!borrow||!nativeParameters||!borrow->Owns(callback.call_->CommandList(),callback.call_->Resource(before?"Color":"Output")))
    {result.reason=Protocol::Symbol("Native.RecordingBorrowUnavailable");callback.rendererBorrow_.reset();return result;}
    std::shared_ptr<NativeInvocationOwner> owner;
    struct End{std::function<void()> close;~End(){close();}} end{[&]{
        if(owner&&callback.invocation_!=owner)owner->CloseCallback();callback.rendererBorrow_.reset();}};
    try
    {
        NativeInvocationOwner::Seeds seeds;C::RecordHeader resultHeader;
        std::shared_ptr<NativeSessionLifetime> root;NativeOwnerSet* owners=nullptr;
        {
            std::lock_guard lock(state_->mutex);
            if(state_->closed||!state_->lifetime||!state_->Owner())
            {result.reason=Protocol::Symbol("Native.ScopeUnavailable");return result;}
            owners=state_->Owner();root=state_->lifetime;
            seeds.consumer=owners->ProtocolJournal().Event().evidence.record;
            seeds.recording=owners->ProtocolJournal().Event().evidence.record;
            seeds.reservation=owners->ProtocolJournal().Event().evidence.record;
            for(auto& key:seeds.providerRegistrations)
                key=owners->RuntimeJournal().Event().evidence.record;
            seeds.history=owners->HistoryJournal().Event().evidence.record;
            const auto issued=owners->Handoff()->ContractChanged(owners->FinalizerJournal().Event());
            if(issued.status!=IdentityStatus::Ok||!Context::Established(issued.value))
            {result.reason=Protocol::Symbol("Native.HandoffUnavailable");return result;}
            seeds.handoff=issued.value.KnownPart()->value;
            resultHeader=owners->StrategyJournal().Header(C::ContractId::C07,callback.scope_.scope);
        }
        owner=std::shared_ptr<NativeInvocationOwner>(new NativeInvocationOwner(
            callback.currentPreparation_,borrow,callback.call_,*state_->resources,*owners,
            callback.sample_,callback.scope_.scope,seeds,
            [&callback]{return callback.pin_&&callback.pin_->Current();}));
        owner->retainedSourceState_=callback.state_;
        auto admission=Protocol::NativeInvocationIngress::Admit(root,owner);
        if(!admission)
        {result.reason=owner->PreparationReason();return result;}
        const auto* product=admission->Product();
        const auto target=callback.CallerOutputTarget();
        if(!product||!target||target->region.x||target->region.y||
           !Context::Established(target->view.raster.active)||
           target->view.raster.active.KnownPart()->value.width!=target->region.width||
           target->view.raster.active.KnownPart()->value.height!=target->region.height||
           !owner->ReserveReturn(resultHeader,target->view))
        {result.reason=Protocol::Symbol("Native.ReturnReservationUnavailable");return result;}
        callback.invocation_=owner;
        {std::lock_guard lock(state_->mutex);
         if(state_->latestReturn.sequence==callback.sample_.producerOrdinal)state_->latestReturnOwner=owner;}
        auto executor=[&](const Protocol::NativeBindingMap& map,Protocol::NativeExecutionObserver& observer){
            if(!product){observer.Fail("Native.AdmissionUnavailable");return observer.Facts();}
            const auto frame=Protocol::BuildNativeFrameInfo(map,*product,*owner->Store());
            if(!frame){observer.Fail("Native.FrameBindingUnavailable");return observer.Facts();}
            Protocol::NativeTypedEvaluation typed;typed.frame=*frame;
            typed.colour=callback.call_->Resource(before?"Color":"Output");typed.target=typed.colour;
            typed.depth=callback.call_->Resource("Depth");typed.motion=callback.call_->Resource("MotionVectors");
            unsigned quality=0;if(callback.call_->Get("PerfQualityValue",&quality)==0)
                typed.hostQuality=static_cast<int>(quality);
            typed.observer=&observer;typed.rendererBorrow=borrow.get();
            if(before)DlssNr::EvaluateNativeProtocolBefore(callback.call_->CommandList(),
                static_cast<NVSDK_NGX_Parameter*>(nativeParameters),nullptr,settings,typed,true,*admission);
            else DlssNr::EvaluateNativeProtocolTyped(callback.call_->CommandList(),typed,nullptr,settings,*admission);
            return observer.Facts();
        };
        result=Protocol::ExecuteNativeProtocol(*admission,resultHeader,*owner,*owner->Store(),
            executor,owner->History(),false);
        owner->ObserveReturnExecution(result);
        if(owner->snapshot_&&owner->snapshot_->SourceBoundRequest())
        {
            std::shared_ptr<ControlledFg> selected;
            {std::lock_guard lock(state_->mutex);selected=state_->controlledFg;}
            // The admitted callback keeps FinishControlledWriters excluded.
            // Drop the state lock before acquiring the selection lock so a
            // simultaneous observation cannot race publication or invert locks.
            if(selected)
            {
                std::lock_guard selectionLock(selected->mutex);
                if(!selected->invocation)
                {
                    selected->invocation=owner;selected->transaction=callback.transactionObservation_;
                    selected->claim=admission->SourceBoundPrimary();
                    if(!selected->claim)selected->observation.failed=1;
                }
            }
        }
        callback.runtimeReason_=result.reason;
        return result;
    }
    catch(...)
    {result.reason=Protocol::Symbol("Native.InvocationOwnerFailure");callback.runtimeReason_=result.reason;return result;}
}
void NativeProcessBootstrap::FinishNativeReturn(Callback& callback,std::uint32_t hostResult,bool succeeded)noexcept
{
    // Last destructor in this function: keep the coordinator's retaining slot
    // alive until all local shared references have dropped without destruction.
    struct TailPin
    {
        std::atomic<bool>* active=nullptr;
        ~TailPin(){if(active)active->store(false);}
    } tail;
    auto owner=callback.invocation_;
    if(!owner||!owner->return_){callback.Drop(succeeded);return;}
    owner->returnTailActive_=true;tail.active=&owner->returnTailActive_;
    // Capture actual SR consumption and Resource publication before dropping
    // call-scoped resources. No result code substitutes for the selected join.
    const bool current=callback.Current();
    const auto sr=callback.srReturn_;
    auto pin=callback.pin_; // keeps the real CPU return tail admitted
    auto state=callback.state_;
    const bool cpu=callback.nativeCpuRestored_,commands=callback.nativeCommandRestored_;
    owner->CloseCallback(true);
    callback.Drop(succeeded);
    // Match source closure's State -> Resource order. Drop's bookkeeping must
    // finish before either lock is retained across the publication tail.
    std::lock_guard stateLock(state->mutex);
    auto resourceLock=owner->resources_->LockNativeAction();
    std::optional<C::ResourceView> output;
    output=owner->resources_->CurrentRegisteredView(owner->call_->Resource("Output"));
    const auto finish=[&]{return owner->providerOwner_->WithHistory(owner->recordingUse_,[&](NativeHistoryState&){
        const auto rejected=[&]{owner->return_->FinishRejected();return false;};
        if(sr&&!owner->return_->ObserveSr(sr->result,sr->succeeded,owner->consumedSrInput_))return rejected();
        if(!current||state->closed||!succeeded||!sr||!sr->succeeded||!sr->callerBindingMatches||sr->result!=hostResult)
        {owner->return_->Reject("Native.ReturnOuterFailure");return rejected();}
        const auto returned=owner->snapshot_->product.recipe.placement==C::Placement::NativeAfter?
            owner->publishedOutput_:owner->returnedSrOutput_;
        if(output&&returned&&!owner->return_->ObserveOutput(*returned,*output,*owner->store_))return rejected();
        if(!owner->return_->ObserveRestoration(cpu,commands)||!owner->return_->Prepare(*owner->store_))return rejected();
        return owner->return_->Commit();
    });};
    // After cleanup, only current-source/history/Resource synchronization and
    // preallocated CPU publication remain. The callback pin ends at this CPU
    // boundary; the existing State is retained separately for downstream tails.
    if(!pin||!pin->WithCurrent(finish))
        owner->providerOwner_->WithHistory(owner->recordingUse_,[&](NativeHistoryState&){
            owner->return_->Reject("Native.ReturnSourceRevoked");owner->return_->FinishRejected();return false;});
}
}
namespace DlssNr
{
// Sole process source attachment for this actual registry. Retained until
// aggregate downstream retirement, never reconstructed by a callback facade.
class NativeDx12Source
{
    inline static std::mutex mutex_;
    inline static Neurotic::Lifecycle::NativeProcessBootstrap* host_ = nullptr;
    inline static bool globallyClosed_ = false;
  public:
    static unsigned ControlledBegin(std::uintptr_t);
    static unsigned ControlledEnroll(const NativeControlledFgEnrollmentV1*);
    static unsigned ControlledFinish(NativeControlledFgObservationV1*);
    static FfxNrStatusV1 ControlledConsumer(FfxNrOwnedOutputHandleV1,FfxNrAlgorithmHandleV1,const FfxNrDispatchTicketV1*,void**);
    static void ControlledReturned(void*,std::int32_t);
    static FfxNrStatusV1 ControlledSubmit(const FfxNrDispatchTicketV1*,void**);
    static void ControlledSubmitted(void*,const FfxNrSubmitResultV1*);
    static unsigned ControlledObserve(NativeControlledFgObservationV1*,bool);
    // Scoped binding of the already produced Native scratch to the selected
    // existing SR call. No copy, Resource revision or extra owner is introduced.
    class SrInputScope
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host_=nullptr;
        Neurotic::Lifecycle::NativeProcessBootstrap::Callback* callback_=nullptr;
        std::unique_ptr<NativeParameterOverride<NVSDK_NGX_Parameter,NVSDK_NGX_Result>> binding_;
        ID3D12Resource* input_=nullptr;
        bool reading_=false,closed_=false,ready_=true;
      public:
        SrInputScope(Neurotic::Lifecycle::NativeProcessBootstrap::Callback* callback,NVSDK_NGX_Parameter* parameters):callback_(callback)
        {
            {std::lock_guard lock(mutex_);host_=NativeDx12Source::host_;}
            if(!host_||!callback_)return;
            input_=host_->BeforeSrInput(*callback_);if(!input_)return;
            void* original=nullptr;
            if(!callback_->OriginalCall()||callback_->OriginalCall()->Get("Color",&original)!=0)
            {ready_=false;return;}
            binding_=std::make_unique<NativeParameterOverride<NVSDK_NGX_Parameter,NVSDK_NGX_Result>>(
                parameters,"Color",original,NVSDK_NGX_Result_Success);
            ready_=binding_->Bind(input_);
            if(ready_)ready_=reading_=host_->TransitionSrInput(*callback_,input_,true);
        }
        SrInputScope(const SrInputScope&)=delete;
        ~SrInputScope(){Close();}
        bool Ready()const noexcept{return ready_;}
        bool Close()noexcept
        {
            if(closed_)return ready_;closed_=true;
            if(!input_)return ready_;
            const bool state=!reading_||host_->TransitionSrInput(*callback_,input_,false);
            const bool binding=binding_&&binding_->Restore();
            ready_=ready_&&state&&binding;host_->ObserveSrInputRestoration(*callback_,ready_);return ready_;
        }
    };
    static auto BeginSourceScope(){return Neurotic::Lifecycle::NativeSourceTransactionScope{};}
    static void BindSourceScope(Neurotic::Lifecycle::NativeSourceTransactionScope& scope,
        const Neurotic::Lifecycle::NativeProcessBootstrap::Callback& callback)
    {if(scope.depth_<=8)scope.observation_=callback.TransactionObservation();}
    static auto Capture(NativeFeatureRegistry<NVSDK_NGX_Feature>::CallbackPin&& pin,
                        ID3D12GraphicsCommandList* list, const NVSDK_NGX_Parameter* parameters)
    {
        // The process retains this root permanently. Copy its address under the
        // root lock, then release that lock before parameter/COM owner calls.
        // A reentrant shutdown can revoke the source during BindCall; its final
        // current-source check then refuses the captured inputs.
        Neurotic::Lifecycle::NativeProcessBootstrap* host = nullptr;
        {
            std::lock_guard lock(mutex_);
            if(globallyClosed_)return std::optional<Neurotic::Lifecycle::NativeProcessBootstrap::Callback>{};
            if (!host_) host_ = new Neurotic::Lifecycle::NativeProcessBootstrap;
            host = host_;
        }
        auto callback = host->Capture(HandleToFeature, std::move(pin));
        if (callback) host->BindCall(*callback, list, parameters, NVSDK_NGX_Result_Success,
            D3D12Hooks::ObserveNativeRecording);
        return callback;
    }
    static NVSDK_NGX_Result RecordSceneEvaluate(ID3D12GraphicsCommandList* list,
        const NVSDK_NGX_Handle* handle,NVSDK_NGX_Parameter* parameters,
        ID3D12Resource* disocclusion,unsigned frame,unsigned inverted)noexcept
    try
    {
        static_assert(static_cast<unsigned>(NVSDK_NGX_PerfQuality_Value_DLAA)==5);
        if(!list||!handle||!parameters||!disocclusion||frame>3||inverted>1)return NVSDK_NGX_Result_FAIL_InvalidParameter;
        const auto settings=TryNrConfigSnapshot(*Config::Instance());
        if(!settings||!settings->DlssNrEnabled.value_or_default()||
           !settings->DlssNrNativeProtocol.value_or_default()||settings->DlssNrRoute.value_or_default()!=0||
           settings->DlssNrRunBeforeSr.value_or_default())return NVSDK_NGX_Result_FAIL_InvalidParameter;
        const auto snapshot=HandleToFeature.Read(handle->Id);
        if(!snapshot||snapshot.feature!=NVSDK_NGX_Feature_SuperSampling)return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        if(!Neurotic::Lifecycle::NativeProcessBootstrap::ControlledCreation(snapshot.originalCreation,inverted))
            return NVSDK_NGX_Result_FAIL_InvalidParameter;
        auto pin=HandleToFeature.Pin(handle->Id,snapshot);
        if(!pin||!pin->Current())return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        Neurotic::Lifecycle::NativeControlledSceneProducer::Images images{};
        constexpr const char* names[]={"Color","Depth","MotionVectors"};
        for(unsigned i=0;i<3;++i)
        {
            void* raw=nullptr;
            if(parameters->Get(names[i],&raw)!=NVSDK_NGX_Result_Success||!raw)return NVSDK_NGX_Result_FAIL_InvalidParameter;
            images[i]=static_cast<ID3D12Resource*>(raw);
        }
        images[3]=disocclusion;
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {
            std::lock_guard lock(mutex_);
            if(globallyClosed_)return NVSDK_NGX_Result_FAIL_PlatformError;
            if(!host_)host_=new Neurotic::Lifecycle::NativeProcessBootstrap;
            host=host_;
        }
        auto producer=host->RecordControlledScene(list,images,snapshot.generation,frame,inverted!=0,
            D3D12Hooks::ObserveNativeRecording,D3D12Hooks::RegisterNativeRootLayout);
        if(!producer||!pin->Current())return NVSDK_NGX_Result_Fail;
        Neurotic::Lifecycle::NativeControlledSceneProducer::Scope scope(producer,handle,parameters);
        // Actual evaluation consumes the producer span immediately. No public
        // setter can label unrelated resources or arbitrary game recordings.
        return NVSDK_NGX_D3D12_EvaluateFeature(list,handle,parameters,nullptr);
    }
    catch(...){return NVSDK_NGX_Result_Fail;}
    static auto RunBefore(Neurotic::Lifecycle::NativeProcessBootstrap::Callback& callback,
        NVSDK_NGX_Parameter* parameters,const NrConfigSnapshot<Config>& settings)
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {std::lock_guard lock(mutex_);host=host_;}
        namespace P=Neurotic::Protocol;
        P::NativeProtocolResult result;
        if(!host)return result;
        std::optional<NativeStateRestorePoint> captured;
        auto captureReason=P::Symbol("Native.CommandStateUnavailable");
        try
        {
            const auto* call=callback.OriginalCall();
            // The selected NR route uses compute, PSO and descriptor heaps.
            // Graphics state may be undefined on a compute-only caller list;
            // the observer must reject any mutation of that omitted stage.
            using Mask=Neurotic::D3D12::RestoreMask;
            Neurotic::D3D12::NativeStateCaptureDiagnostic diagnostic;
            if(call)captured=D3D12Hooks::CapturePostSrState(call->CommandList(),Mask::Compute|Mask::Pipeline|Mask::Heaps|Mask::HeapInvalidatedTables,&diagnostic);
            if(!captured)
            {
                auto reason=std::string(diagnostic.reason);
                if(diagnostic.parameter!=UINT_MAX)
                    reason+=".p"+std::to_string(diagnostic.parameter)+".known"+std::to_string(diagnostic.known)+
                        "of"+std::to_string(diagnostic.required);
                captureReason=P::Symbol(reason);
                LOG_WARN("NR state capture refused: list={:p} reason={} trace_count={} trace_total={}",
                    call ? static_cast<void*>(call->CommandList()) : nullptr, reason,
                    diagnostic.traceCount, diagnostic.traceTotal);
                for(UINT i=0;i<diagnostic.traceCount;++i)
                {
                    const auto& entry=diagnostic.trace[i];
                    LOG_WARN("NR state trace: seq={} work={} op={} a={} b={} c={}",
                        entry.sequence,entry.workOrdinal,entry.operation,entry.a,entry.b,entry.c);
                }
            }
        }
        catch(...){result.reason=P::Symbol("Native.CommandStateCaptureFailed");return host->RecordNativeResult(callback,result);}
        if(!captured)return host->RecordCommandStateUnavailable(callback,D3D12Hooks::ObserveNativeRecording(
            callback.OriginalCall()?callback.OriginalCall()->CommandList():nullptr),captureReason);
        P::NativeCommandStateScope restore([&]{return D3D12Hooks::RestorePostSrState(*captured);},
            [&]()noexcept{P::NativeMarkCommandStateRestoreFailed(result.facts);});
        try
        {
            // Creation/preparation can itself record work. Its state belongs
            // inside the same envelope as the actual evaluation.
            host->PrepareRuntime(callback,settings);
            if(!callback.Current())result.reason=P::Symbol("Native.ScopeUnavailable");
            else result=host->RunNativeBefore(callback,parameters,settings);
        }
        catch(...){result.reason=P::Symbol("Native.InvocationOwnerFailure");}
        // Explicit before the result is copied; destructor covers exceptions.
        const bool restored=restore.Restore();
        if(!restored&&result.reason.Empty())result.reason=result.facts.failure;
        host->ObserveNativeRestoration(callback,result,restored,D3D12Hooks::ObserveNativeRecording(
            callback.OriginalCall()?callback.OriginalCall()->CommandList():nullptr));
        return host->RecordNativeResult(callback,result);
    }
    static void FinishReturn(Neurotic::Lifecycle::NativeProcessBootstrap::Callback& callback,
        NVSDK_NGX_Result result)noexcept
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {std::lock_guard lock(mutex_);host=host_;}
        if(host)host->FinishNativeReturn(callback,static_cast<std::uint32_t>(result),
            result==NVSDK_NGX_Result_Success);
    }
    static bool ObserveSrReturn(Neurotic::Lifecycle::NativeProcessBootstrap::Callback& callback,
        NVSDK_NGX_Parameter* parameters,NVSDK_NGX_Result result)noexcept
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {std::lock_guard lock(mutex_);host=host_;}
        return host&&host->ObserveSrReturn(callback,parameters,result,NVSDK_NGX_Result_Success);
    }
    static auto BeginOpaqueSr(Neurotic::Lifecycle::NativeProcessBootstrap::Callback& callback,
        const NVSDK_NGX_Handle* feature,NVSDK_NGX_Parameter* parameters)noexcept
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {std::lock_guard lock(mutex_);host=host_;}
        return host?host->BeginOpaqueSr(callback,feature,parameters,NVSDK_NGX_Result_Success,
            D3D12Hooks::ObserveNativeRecording(callback.OriginalCall()?callback.OriginalCall()->CommandList():nullptr)):
            std::shared_ptr<Neurotic::Lifecycle::NativeProcessBootstrap::OpaqueSrOperation>{};
    }
    static void SealOpaqueSr(Neurotic::Lifecycle::NativeProcessBootstrap::Callback& callback,
        Neurotic::Lifecycle::NativeProcessBootstrap::OpaqueSrOperation& operation,
        const NVSDK_NGX_Handle* feature,NVSDK_NGX_Parameter* parameters,NVSDK_NGX_Result result)noexcept
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {std::lock_guard lock(mutex_);host=host_;}
        if(host)host->SealOpaqueSr(callback,operation,feature,parameters,result,NVSDK_NGX_Result_Success,
            D3D12Hooks::ObserveNativeRecording(callback.OriginalCall()?callback.OriginalCall()->CommandList():nullptr));
    }
    static void Close()noexcept
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {std::lock_guard lock(mutex_);globallyClosed_=true;host=host_;}
        if(host)host->CloseSource();
    }
    static NativeTeardownObservationV1 PrepareTeardown()noexcept
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {std::lock_guard lock(mutex_);host=host_;}
        auto result=host?host->PrepareTeardown():NativeTeardownObservationV1{};
        // Keep the same immutable epoch across preflight and the foreign
        // attempt. A concurrently published scope cannot inherit this result.
        if(host)host->AttemptRendererTeardown(result,[]{return DlssNr::Shutdown();});
        return result;
    }
    static NativeTeardownObservationV1 CompleteTeardown()noexcept
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {std::lock_guard lock(mutex_);host=host_;}
        return host?host->CompleteTeardown(DlssNr::ObserveNativeRendererRetirement()):NativeTeardownObservationV1{};
    }
    static bool RecreateScope()noexcept
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {std::lock_guard lock(mutex_);host=host_;}
        return host&&host->RecreateScope([host](std::shared_ptr<Neurotic::Lifecycle::NativeProcessBootstrap> fresh){
            // Readers that already copied the old pointer keep a closed,
            // immutable scope. Retain each bounded epoch until process exit;
            // publishing a pointer never reopens the previous owner.
            static auto* retained=new std::array<std::shared_ptr<Neurotic::Lifecycle::NativeProcessBootstrap>,16>;
            std::lock_guard lock(mutex_);
            if(globallyClosed_||host_!=host||!fresh)return false;
            for(auto& entry:*retained)if(!entry)
            {entry=std::move(fresh);host_=entry.get();return true;}
            return false;
        });
    }
    static unsigned QueryReturn(std::uint64_t after,std::uint64_t handle,std::uint64_t list,
        std::uint64_t output,NativeHostReturnObservationV1& result)noexcept
    {
        Neurotic::Lifecycle::NativeProcessBootstrap* host=nullptr;
        {std::lock_guard lock(mutex_);host=host_;}
        return host?host->QueryNativeReturn(after,handle,list,output,result):0u;
    }
};
}
// Explicit controlled teardown. Stops source ingress, sweeps genuine invocation
#include "NativeControlledFgIntegration.inl"
// tails, then uses the existing checked renderer shutdown. No module is unloaded.
extern "C" __declspec(dllexport) unsigned __cdecl OptiScaler_W03_PrepareTeardownV1(
    DlssNr::NativeTeardownObservationV1* output,std::uint32_t size) noexcept
try
{
    if(!output||size!=sizeof(*output))return 0;
    *output=DlssNr::NativeDx12Source::PrepareTeardown();
    return 1;
}
catch(...){return 0;}
extern "C" __declspec(dllexport) unsigned __cdecl OptiScaler_W03_CompleteTeardownV1(
    DlssNr::NativeTeardownObservationV1* output,std::uint32_t size) noexcept
try
{
    if(!output||size!=sizeof(*output))return 0;
    *output=DlssNr::NativeDx12Source::CompleteTeardown();
    return 1;
}
catch(...){return 0;}
extern "C" __declspec(dllexport) unsigned __cdecl OptiScaler_W03_QueryRendererShutdownV1(
    DlssNr::NativeRendererShutdownObservationV1* output,std::uint32_t size) noexcept
{
    if(!output||size!=sizeof(*output))return 0;
    *output=DlssNr::ObserveNativeRendererShutdown();return 1;
}
// Read-only identity of the existing proxy's selected module. No initialization,
// load, ownership transfer, fallback selection or provider action is performed.
extern "C" __declspec(dllexport) HMODULE __cdecl OptiScaler_W03_QueryCoreModuleV1() noexcept
{
    return NVNGXProxy::NVNGXModule();
}
extern "C" __declspec(dllexport) NVSDK_NGX_Result __cdecl OptiScaler_W03_RecordSceneEvaluateV1(
    ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,NVSDK_NGX_Parameter* parameters,
    ID3D12Resource* disocclusion,unsigned frame,unsigned inverted)noexcept
{
    return DlssNr::NativeDx12Source::RecordSceneEvaluate(list,handle,parameters,disocclusion,frame,inverted);
}
extern "C" __declspec(dllexport) unsigned __cdecl OptiScaler_W03_QueryNativeReturnV1(
    std::uint64_t afterSequence,std::uint64_t expectedHandle,std::uint64_t expectedCommandList,
    std::uint64_t expectedOutput,DlssNr::NativeHostReturnObservationV1* output,std::uint32_t outputSize)noexcept
{
    if(!output||outputSize!=sizeof(*output))return 0;
    *output={};
    return DlssNr::NativeDx12Source::QueryReturn(afterSequence,expectedHandle,expectedCommandList,expectedOutput,*output);
}
extern "C" __declspec(dllexport) unsigned __cdecl OptiScaler_W03_QueryRecordingV1(
    std::uint64_t commandList,DlssNr::NativeRecordingObservationV1* output,std::uint32_t outputSize)noexcept
{
    if(!output||outputSize!=sizeof(*output)||!commandList)return 0;
    *output={};
    try
    {
        const auto observed=D3D12Hooks::ObserveNativeRecording(
            reinterpret_cast<ID3D12GraphicsCommandList*>(static_cast<std::uintptr_t>(commandList)));
        output->commandList=commandList;
        output->incarnation=observed.incarnation;
        output->workOrdinal=observed.workOrdinal;
        output->hookGeneration=observed.hookGeneration;
        output->flags=(observed.active?1u:0u)|(observed.completeCoverage?2u:0u)|
            (observed.trackingBeganBeforeRecording?4u:0u)|(observed.missingRequiredHistory?8u:0u);
        return 1;
    }
    catch(...){return 0;}
}
extern "C" __declspec(dllexport) unsigned __cdecl OptiScaler_W03_QueryRecordingDiagnosticV1(
    std::uint64_t commandList,DlssNr::NativeRecordingDiagnosticV1* output,std::uint32_t outputSize)noexcept
{
    if(!output||outputSize!=sizeof(*output)||!commandList)return 0;
    *output={};
    try
    {
        *output=D3D12Hooks::DiagnoseNativeRecording(
            reinterpret_cast<ID3D12GraphicsCommandList*>(static_cast<std::uintptr_t>(commandList)));
        return 1;
    }
    catch(...){return 0;}
}
static std::mutex ngxObservationMutex;
static std::mutex fgObservationMutex;
static ID3D12Device* D3D12Device = nullptr;

static const char* NgxFeatureName(NVSDK_NGX_Feature feature)
{
    switch (feature)
    {
    case NVSDK_NGX_Feature_SuperSampling:
        return "Super Resolution";
    case NVSDK_NGX_Feature_InPainting:
        return "InPainting";
    case NVSDK_NGX_Feature_ImageSuperResolution:
        return "Image Super Resolution";
    case NVSDK_NGX_Feature_SlowMotion:
        return "Slow Motion";
    case NVSDK_NGX_Feature_VideoSuperResolution:
        return "Video Super Resolution";
    case NVSDK_NGX_Feature_ImageSignalProcessing:
        return "Image Signal Processing";
    case NVSDK_NGX_Feature_DeepResolve:
        return "Deep Resolve";
    case NVSDK_NGX_Feature_FrameGeneration:
        return "Frame Generation";
    case NVSDK_NGX_Feature_DeepDVC:
        return "DeepDVC";
    case NVSDK_NGX_Feature_RayReconstruction:
        return "Ray Reconstruction";
    default:
        return "Unknown/Reserved";
    }
}

static void LogNgxCreateTrace(NVSDK_NGX_Feature feature, const char* route, NVSDK_NGX_Result result,
                              const NVSDK_NGX_Handle* handle)
{
    LOG_INFO("DLSS-NR Test 0.9 trace: Create feature {} (id {}) via {}; result 0x{:X}, handle {}",
             NgxFeatureName(feature), static_cast<int>(feature), route, static_cast<uint32_t>(result),
             handle != nullptr ? handle->Id : 0);
}

struct NgxEvaluationTraceObservation
{
    bool tracked = false;
    NVSDK_NGX_Feature feature = NVSDK_NGX_Feature_Reserved0;
};

static std::unordered_map<unsigned int, NgxEvaluationTraceObservation> NgxEvaluationTraceObservations;

static void LogNgxEvaluationTrace(unsigned int handleId, bool tracked, NVSDK_NGX_Feature feature)
{
    const NgxEvaluationTraceObservation observed { tracked, feature };
    std::lock_guard lock(ngxObservationMutex);
    const auto it = NgxEvaluationTraceObservations.find(handleId);
    if (it != NgxEvaluationTraceObservations.end() && it->second.tracked == observed.tracked &&
        it->second.feature == observed.feature)
        return;

    NgxEvaluationTraceObservations[handleId] = observed;
    const char* route = handleId < DLSS_MOD_ID_OFFSET ? "native NGX handle" : "OptiScaler/provider handle";
    LOG_INFO("DLSS-NR Test 0.9 trace: Evaluate handle {} feature {} (id {}), {}, {}", handleId,
             tracked ? NgxFeatureName(feature) : "untracked", tracked ? static_cast<int>(feature) : -1,
             tracked ? "mapped at creation" : "no mapped creation", route);
}

static bool IsNrPipelineFeature(NVSDK_NGX_Feature feature)
{
    return feature == NVSDK_NGX_Feature_SuperSampling ||
           feature == NVSDK_NGX_Feature_RayReconstruction;
}

// Log pipeline identity only when it changes. This is intentionally handle-scoped: a secondary
// viewport must not overwrite the main viewport's evidence merely because it evaluated later.
struct NrPipelineObservation
{
    NVSDK_NGX_Feature feature = (NVSDK_NGX_Feature) 0;
    bool preSr = false;
    unsigned int inputWidth = 0;
    unsigned int inputHeight = 0;
    unsigned int outputWidth = 0;
    unsigned int outputHeight = 0;
};

static std::unordered_map<unsigned int, NrPipelineObservation> NrPipelineObservations;

static void LogNrPipelineObservation(unsigned int handleId, NVSDK_NGX_Feature feature,
                                     NVSDK_NGX_Parameter* params, bool performanceMode)
{
    if (!IsNrPipelineFeature(feature) || params == nullptr)
        return;

    void* color = nullptr;
    void* output = nullptr;
    params->Get(NVSDK_NGX_Parameter_Color, &color);
    params->Get(NVSDK_NGX_Parameter_Output, &output);

    NrPipelineObservation observed {};
    observed.feature = feature;
    observed.preSr = feature == NVSDK_NGX_Feature_SuperSampling && performanceMode;

    if (color != nullptr)
    {
        const auto desc = static_cast<ID3D12Resource*>(color)->GetDesc();
        observed.inputWidth = static_cast<unsigned int>(desc.Width);
        observed.inputHeight = desc.Height;
    }
    if (output != nullptr)
    {
        const auto desc = static_cast<ID3D12Resource*>(output)->GetDesc();
        observed.outputWidth = static_cast<unsigned int>(desc.Width);
        observed.outputHeight = desc.Height;
    }

    std::lock_guard lock(ngxObservationMutex);
    const auto it = NrPipelineObservations.find(handleId);
    if (it != NrPipelineObservations.end() && it->second.feature == observed.feature &&
        it->second.preSr == observed.preSr && it->second.inputWidth == observed.inputWidth &&
        it->second.inputHeight == observed.inputHeight && it->second.outputWidth == observed.outputWidth &&
        it->second.outputHeight == observed.outputHeight)
        return;

    NrPipelineObservations[handleId] = observed;
    const bool rr = feature == NVSDK_NGX_Feature_RayReconstruction;
    LOG_INFO("DLSS-NR: handle {} feature {} input {}x{} output {}x{}; placement {}",
             handleId, rr ? "Ray Reconstruction" : "Super Resolution", observed.inputWidth,
             observed.inputHeight, observed.outputWidth, observed.outputHeight,
             observed.preSr ? "NR -> SR" : (rr ? "native mode-aware RR -> NR" : "SR -> NR"));
}

static int evalCounter = 0;
static bool shutdown = false;
static thread_local bool _skipInit = false;
static wchar_t const** paths;

class ScopedInitDx12
{
  private:
    bool previousState;

  public:
    ScopedInitDx12()
    {
        previousState = _skipInit;
        _skipInit = true;
    }

    ~ScopedInitDx12() { _skipInit = previousState; }
};

static void UpdateInitPaths(NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    State::Instance().NVNGX_FeatureInfo_Paths.clear();

    if (InFeatureInfo != nullptr)
    {
        auto exePath = Util::ExePath().remove_filename();

        std::optional<std::filesystem::path> nvngxDlssPath = std::nullopt;
        std::optional<std::filesystem::path> nvngxDlssDPath = std::nullopt;
        std::optional<std::filesystem::path> nvngxDlssGPath = std::nullopt;

        // Check DLSS path
        if (State::Instance().NVNGX_DLSS_Path.has_value())
        {
            nvngxDlssPath = std::filesystem::path(State::Instance().NVNGX_DLSS_Path.value());
        }
        else
        {
            auto path = Util::FindFilePath(exePath, "nvngx_dlss.dll");

            if (path.has_value())
                nvngxDlssPath = path.value();
        }

        // Check DLSS-D path
        if (State::Instance().NVNGX_DLSSD_Path.has_value())
        {
            nvngxDlssDPath = std::filesystem::path(State::Instance().NVNGX_DLSSD_Path.value());
        }
        else
        {
            auto path = Util::FindFilePath(exePath, "nvngx_dlssd.dll");

            if (path.has_value())
                nvngxDlssDPath = path.value();
        }

        // Check DLSS-G path
        if (State::Instance().NVNGX_DLSSG_Path.has_value())
        {
            nvngxDlssGPath = std::filesystem::path(State::Instance().NVNGX_DLSSG_Path.value());
        }
        else
        {
            auto path = Util::FindFilePath(exePath, "nvngx_dlssg.dll");

            if (path.has_value())
                nvngxDlssGPath = path.value();
        }

        // Override locations
        if (Config::Instance()->DLSSFeaturePath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(Config::Instance()->DLSSFeaturePath.value());

        // If DLSS path is overriden
        if (Config::Instance()->NVNGX_DLSS_Library.has_value() && nvngxDlssPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssPath.value().parent_path().wstring());

        // OptiDll Path
        State::Instance().NVNGX_FeatureInfo_Paths.push_back(Config::Instance()->MainDllPath.value());

        // Original paths from NVNGX
        for (size_t i = 0; i < InFeatureInfo->PathListInfo.Length; i++)
        {
            const wchar_t* path = InFeatureInfo->PathListInfo.Path[i];
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(std::wstring(path));
        }

        // Exe path
        State::Instance().NVNGX_FeatureInfo_Paths.push_back(exePath.wstring());

        // If DLSS path is not overriden
        if (!Config::Instance()->NVNGX_DLSS_Library.has_value() && nvngxDlssPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssPath.value().parent_path().wstring());

        // Add found locations
        if (nvngxDlssDPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssDPath.value().parent_path().wstring());

        if (nvngxDlssGPath.has_value())
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(nvngxDlssGPath.value().parent_path().wstring());

        // Build pointer array
        paths = new const wchar_t*[State::Instance().NVNGX_FeatureInfo_Paths.size()];
        for (size_t i = 0; i < State::Instance().NVNGX_FeatureInfo_Paths.size(); ++i)
        {
            paths[i] = State::Instance().NVNGX_FeatureInfo_Paths[i].c_str();
            LOG_DEBUG("Feature Path [{}]: {}", i, wstring_to_string(State::Instance().NVNGX_FeatureInfo_Paths[i]));
        }

        InFeatureInfo->PathListInfo.Path = paths;
        InFeatureInfo->PathListInfo.Length = (int) State::Instance().NVNGX_FeatureInfo_Paths.size();
    }
}

#pragma region DLSS Init Calls

static NVSDK_NGX_Result InitDx12ExtendedCore(unsigned long long InApplicationId,
                                                        const wchar_t* InApplicationDataPath, ID3D12Device* InDevice,
                                                        NVSDK_NGX_Version InSDKVersion,
                                                        const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    LOG_FUNC();

    NVSDK_NGX_FeatureCommonInfo localFeatureInfo = {};

    if (InFeatureInfo != nullptr)
        std::memcpy(&localFeatureInfo, InFeatureInfo, sizeof(NVSDK_NGX_FeatureCommonInfo));

    if (!_skipInit)
        UpdateInitPaths(&localFeatureInfo);

    State::Instance().NVNGX_ApplicationId = InApplicationId;
    State::Instance().NVNGX_ApplicationDataPath = std::wstring(InApplicationDataPath);
    State::Instance().NVNGX_Version = InSDKVersion;
    State::Instance().NVNGX_FeatureInfo = &localFeatureInfo;
    State::Instance().NVNGX_Version = InSDKVersion;

    if (Config::Instance()->DLSSEnabled.value_or_default() && !_skipInit)
    {
        if (Config::Instance()->UseGenericAppIdWithDlss.value_or_default())
            InApplicationId = app_id_override;

        if (NVNGXProxy::NVNGXModule() == nullptr)
            NVNGXProxy::InitNVNGX();

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D12_Init_Ext() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D12_Init_Ext");

            auto result = NVNGXProxy::D3D12_Init_Ext()(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion,
                                                       &localFeatureInfo);
            LOG_INFO("calling NVNGXProxy::D3D12_Init_Ext result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx12Inited(true);
        }
        else
        {
            LOG_WARN("NVNGXProxy::NVNGXModule or NVNGXProxy::D3D12_Init_Ext is nullptr!");
        }
    }

    if (InFeatureInfo != nullptr && InSDKVersion > 0x0000013)
        State::Instance().NVNGX_Logger = InFeatureInfo->LoggingInfo;

    if (State::Instance().nvngxDx12Inited && InDevice == D3D12Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None)
    {
        Nvngx_FG::D3D12_Init_Ext(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);
    }

    LOG_INFO("AppId: {0}", InApplicationId);
    LOG_INFO("SDK: {0:x}", (unsigned int) InSDKVersion);
    LOG_INFO(L"InApplicationDataPath {0}", std::wstring(InApplicationDataPath));

    D3D12Device = InDevice;
    State::Instance().currentD3D12Device = InDevice;
    D3D12Hooks::HookDevice(InDevice);

    State::Instance().nvngxDx12Inited = true;

    UpscalerInputsDx12::Init(InDevice);
    DlssNr::NotifyDeviceInit(InDevice);

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_Ext(unsigned long long InApplicationId,
    const wchar_t* InApplicationDataPath, ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    return InitDx12ExtendedCore(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
}
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init(unsigned long long InApplicationId,
                                                    const wchar_t* InApplicationDataPath, ID3D12Device* InDevice,
                                                    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                                    NVSDK_NGX_Version InSDKVersion)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    LOG_FUNC();

    NVSDK_NGX_FeatureCommonInfo localFeatureInfo = {};

    if (InFeatureInfo != nullptr)
        std::memcpy(&localFeatureInfo, InFeatureInfo, sizeof(NVSDK_NGX_FeatureCommonInfo));

    if (!_skipInit)
        UpdateInitPaths(&localFeatureInfo);

    if (Config::Instance()->DLSSEnabled.value_or_default() && !_skipInit)
    {
        if (Config::Instance()->UseGenericAppIdWithDlss.value_or_default())
            InApplicationId = app_id_override;

        if (NVNGXProxy::NVNGXModule() == nullptr)
            NVNGXProxy::InitNVNGX();

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D12_Init() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D12_Init");

            auto result = NVNGXProxy::D3D12_Init()(InApplicationId, InApplicationDataPath, InDevice, &localFeatureInfo,
                                                   InSDKVersion);

            LOG_INFO("calling NVNGXProxy::D3D12_Init result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx12Inited(true);
        }
    }

    if (State::Instance().nvngxDx12Inited && InDevice == D3D12Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    // if (State::Instance().activeFgInput == FGInput::NvngxFG)
    //{
    //     Nvngx_FG::D3D12_Init(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    // }

    ScopedInitDx12 scopedInit {};
    auto result =
        InitDx12ExtendedCore(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);

    LOG_DEBUG("was called NVSDK_NGX_D3D12_Init_Ext");
    return result;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_ProjectID(const char* InProjectId,
                                                              NVSDK_NGX_EngineType InEngineType,
                                                              const char* InEngineVersion,
                                                              const wchar_t* InApplicationDataPath,
                                                              ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                                              const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    LOG_FUNC();

    NVSDK_NGX_FeatureCommonInfo localFeatureInfo = {};

    if (InFeatureInfo != nullptr)
        std::memcpy(&localFeatureInfo, InFeatureInfo, sizeof(NVSDK_NGX_FeatureCommonInfo));

    if (!_skipInit)
        UpdateInitPaths(&localFeatureInfo);

    if (Config::Instance()->DLSSEnabled.value_or_default() && !_skipInit)
    {
        if (Config::Instance()->UseGenericAppIdWithDlss.value_or_default())
            InProjectId = project_id_override;

        if (NVNGXProxy::NVNGXModule() == nullptr)
            NVNGXProxy::InitNVNGX();

        if (NVNGXProxy::NVNGXModule() != nullptr && NVNGXProxy::D3D12_Init_ProjectID() != nullptr)
        {
            LOG_INFO("calling NVNGXProxy::D3D12_Init_ProjectID");

            auto result =
                NVNGXProxy::D3D12_Init_ProjectID()(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath,
                                                   InDevice, InSDKVersion, &localFeatureInfo);

            LOG_INFO("calling NVNGXProxy::D3D12_Init_ProjectID result: {0:X}", (UINT) result);

            if (result == NVSDK_NGX_Result_Success)
                NVNGXProxy::SetDx12Inited(true);
        }
    }

    LOG_INFO("InProjectId: {0}", InProjectId);
    LOG_INFO("InEngineType: {0}", (int) InEngineType);
    LOG_INFO("InEngineVersion: {0}", InEngineVersion);

    State::Instance().NVNGX_ProjectId = std::string(InProjectId);
    State::Instance().NVNGX_Engine = InEngineType;
    State::Instance().NVNGX_EngineVersion = std::string(InEngineVersion);

    if (State::Instance().nvngxDx12Inited && InDevice == D3D12Device)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    ScopedInitDx12 scopedInit {};
    auto result = InitDx12ExtendedCore(0x1337, InApplicationDataPath, InDevice, InSDKVersion, &localFeatureInfo);
    return result;
}

// Not sure about this one, original nvngx does not export this method
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Init_with_ProjectID(
    const char* InProjectId, NVSDK_NGX_EngineType InEngineType, const char* InEngineVersion,
    const wchar_t* InApplicationDataPath, ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
    NVSDK_NGX_Version InSDKVersion)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    LOG_FUNC();

    LOG_INFO("InProjectId: {0}", InProjectId);
    LOG_INFO("InEngineType: {0}", (int) InEngineType);
    LOG_INFO("InEngineVersion: {0}", InEngineVersion);

    State::Instance().NVNGX_ProjectId = std::string(InProjectId);
    State::Instance().NVNGX_Engine = InEngineType;
    State::Instance().NVNGX_EngineVersion = std::string(InEngineVersion);

    if (State::Instance().nvngxDx12Inited)
    {
        LOG_WARN("NVNGX already inited");
        return NVSDK_NGX_Result_Success;
    }

    auto result = InitDx12ExtendedCore(0x1337, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);

    return result;
}

#pragma endregion

#pragma region DLSS Shutdown Calls

// Fixed process-lifetime retention slots; no allocation can fail after logical
// release is published. All access is under the existing native NGX call lease.
using OwnedDx12ReleaseGuard = DlssNr::NativeFeatureRegistry<NVSDK_NGX_Feature>::ReleaseGuard;
struct DeferredOwnedDx12Release
{
    unsigned int handle = 0;
    bool failed = false;
    std::optional<OwnedDx12ReleaseGuard> guard;
};
static std::array<DeferredOwnedDx12Release,16> DeferredOwnedDx12Releases;
static DeferredOwnedDx12Release* AvailableOwnedDx12Release()
{
    for(auto& slot:DeferredOwnedDx12Releases)if(!slot.guard)return &slot;
    return nullptr;
}
static bool HasDeferredOwnedDx12Release()
{
    return std::any_of(DeferredOwnedDx12Releases.begin(),DeferredOwnedDx12Releases.end(),
                       [](const auto& slot){return slot.guard.has_value();});
}
static void PollDeferredOwnedDx12Releases()
{
    for(auto& slot:DeferredOwnedDx12Releases)
    {
        if(!slot.guard||slot.failed)continue;
        const auto it=Dx12Contexts.find(slot.handle);
        if(it==Dx12Contexts.end()||!it->second.ownedReleaseDeferred||!it->second.feature)
        {slot.failed=true;continue;} // preserve the closed generation on inconsistent ownership
        auto& context=it->second;
        if(!context.feature->CanRetire())
        {
            if(!context.feature->CanDeferRetirement())
            {
                slot.failed=true;context.ownedReleaseUnresolved=true;
                LOG_ERROR("Deferred owned SR/RR tracking became unavailable; closed generation retained, restart required");
            }
            continue;
        }
        NVSDK_NGX_Result result=NVSDK_NGX_Result_Fail;
        try{result=context.feature->ReleaseProvider();}catch(...){slot.failed=true;}
        if(result!=NVSDK_NGX_Result_Success||!slot.guard->Complete(true))
        {
            slot.failed=true;context.ownedReleaseUnresolved=true;
            LOG_ERROR("Deferred owned SR/RR physical release failed; generation retained, restart required");
            continue; // never reopen admission or retry an entered opaque provider
        }
        const auto handle=slot.handle;
        Dx12Contexts.erase(it);
        slot.guard.reset();slot.handle=0;
        {std::lock_guard lock(ngxObservationMutex);
         NrPipelineObservations.erase(handle);NgxEvaluationTraceObservations.erase(handle);}
        if(!HandleToFeature.Has(NVSDK_NGX_Feature_RayReconstruction))
            DlssNr::SetNativeRayReconstructionActive(false);
        // No currentFeature, menu, exposure or FG cleanup: those globals may
        // already belong to a newer generation.
        LOG_INFO("Deferred owned SR/RR physical retirement completed, HandleId: {}",handle);
    }
}

static NVSDK_NGX_Result ShutdownDx12Core()
{
    shutdown = true;

    for (const auto& [id, context] : Dx12Contexts)
        if (context.feature && !context.feature->CanRetire()) { shutdown = false; return NVSDK_NGX_Result_FAIL_PlatformError; }
    for (const auto& [id, context] : Dx12Contexts)
    {
        const auto result = context.feature ? context.feature->ReleaseProvider() : NVSDK_NGX_Result_Success;
        if (result != NVSDK_NGX_Result_Success) { shutdown = false; return result; }
    }
    State::Instance().currentFeature = nullptr;
    Dx12Contexts.clear();
    // NR owns driver features, capability parameters, and device-bound scratch resources.
    // Release them while the native NGX core is still live so a later initialization cannot reuse a
    // stale generation.
    if (!DlssNr::Shutdown())
    {
        shutdown = false;
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
    if(!IFeature_Dx12::TryRetireSharedMenu())
    {shutdown=false;return NVSDK_NGX_Result_FAIL_PlatformError;}
    State::Instance().nvngxDx12Inited = false;

    ID3D12Device* shutdownDevice = D3D12Device;
    D3D12Device = nullptr;

    State::Instance().currentFeature = nullptr;

    // Unhooking and cleaning stuff causing issues during shutdown.
    // Disabled for now to check if it cause any issues
    // UnhookAll();
    DLSSFeatureDx12::Shutdown(shutdownDevice);

    // Added `&& !State::Instance().isShuttingDown` hack for crash on exit
    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::IsDx12Inited() &&
        !State::Instance().isShuttingDown)
    {
        // Owned DLSS and native passthrough initialized this same proxy core.
        // Select one shutdown variant and preserve the device for its fallback.
        const auto result = NVNGXProxy::D3D12_Shutdown() != nullptr ? NVNGXProxy::D3D12_Shutdown()() :
            NVNGXProxy::D3D12_Shutdown1() != nullptr ? NVNGXProxy::D3D12_Shutdown1()(shutdownDevice) :
            NVSDK_NGX_Result_FAIL_PlatformError;
        if (result != NVSDK_NGX_Result_Success)
        {
            shutdown = false;
            return result;
        }
        NVNGXProxy::SetDx12Inited(false);
    }

    // Unhooking and cleaning stuff causing issues during shutdown.
    // Disabled for now to check if it cause any issues
    // HooksDx::UnHook();

    // Disabled to prevent crash
    if (State::Instance().currentFG != nullptr && State::Instance().activeFgInput == FGInput::Upscaler)
    {
        if (State::Instance().isShuttingDown)
            State::Instance().currentFG->Shutdown();
        else
            State::Instance().currentFG->DestroyFGContext();

        State::Instance().clearCapturedHudlesses = true;
    }

    shutdown = false;

    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None)
    {
        Nvngx_FG::D3D12_Shutdown();
    }

    State::Instance().nvngxDx12Inited = false;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Shutdown(void)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    PollDeferredOwnedDx12Releases();
    if(HasDeferredOwnedDx12Release())return NVSDK_NGX_Result_FAIL_PlatformError;
    DlssNr::NativeDx12Source::Close();
    auto sourceShutdown = HandleToFeature.BeginShutdown();
    if (!sourceShutdown) return NVSDK_NGX_Result_FAIL_PlatformError;
    const auto result = ShutdownDx12Core();
    sourceShutdown->Complete();
    return result;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_Shutdown1(ID3D12Device* InDevice)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    PollDeferredOwnedDx12Releases();
    if(HasDeferredOwnedDx12Release())return NVSDK_NGX_Result_FAIL_PlatformError;
    DlssNr::NativeDx12Source::Close();
    auto sourceShutdown = HandleToFeature.BeginShutdown();
    if (!sourceShutdown) return NVSDK_NGX_Result_FAIL_PlatformError;
    shutdown = true;
    for (const auto& [id, context] : Dx12Contexts)
        if (context.feature && !context.feature->CanRetire()) { shutdown = false; sourceShutdown->Complete(); return NVSDK_NGX_Result_FAIL_PlatformError; }
    for (const auto& [id, context] : Dx12Contexts)
    {
        const auto result = context.feature ? context.feature->ReleaseProvider() : NVSDK_NGX_Result_Success;
        if (result != NVSDK_NGX_Result_Success) { shutdown = false; sourceShutdown->Complete(); return result; }
    }
    State::Instance().currentFeature = nullptr;
    Dx12Contexts.clear();
    // Shutdown1 must release NR before either native NGX shutdown variant is invoked.
    if (!DlssNr::Shutdown())
    {
        shutdown = false;
        sourceShutdown->Complete();
        return NVSDK_NGX_Result_FAIL_PlatformError;
    }
    if(!IFeature_Dx12::TryRetireSharedMenu())
    {shutdown=false;sourceShutdown->Complete();return NVSDK_NGX_Result_FAIL_PlatformError;}
    State::Instance().nvngxDx12Inited = false;

    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None)
    {
        Nvngx_FG::D3D12_Shutdown1(InDevice);
    }

    // Added `&& !State::Instance().isShuttingDown` hack for crash on exit
    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::IsDx12Inited() &&
        NVNGXProxy::D3D12_Shutdown1() != nullptr && !State::Instance().isShuttingDown)
    {
        auto result = NVNGXProxy::D3D12_Shutdown1()(InDevice);
        if (result != NVSDK_NGX_Result_Success)
        {
            shutdown = false;
            sourceShutdown->Complete();
            return result;
        }
        NVNGXProxy::SetDx12Inited(false);
    }

    const auto result = ShutdownDx12Core();
    sourceShutdown->Complete();
    return result;
}

// Controlled host requires an actual core shutdown call. The general public
// wrapper also supports no-native-core configurations and cannot attest that.
extern "C" __declspec(dllexport) std::uint32_t __cdecl OptiScaler_W03_ShutdownCheckedV1() noexcept
try
{
    if(!Config::Instance()->DLSSEnabled.value_or_default()||!NVNGXProxy::IsDx12Inited()||
       !NVNGXProxy::D3D12_Shutdown()||State::Instance().isShuttingDown||
       State::Instance().activeFgNvngx!=FGNvngxReplacement::None||State::Instance().currentFG)
        return static_cast<std::uint32_t>(NVSDK_NGX_Result_FAIL_PlatformError);
    // A failed/throwing native shutdown may have changed provider state. Never
    // re-enter that operation through the controlled API, including reentrancy.
    static std::atomic<bool> attempted{false};
    if(attempted.exchange(true))return static_cast<std::uint32_t>(NVSDK_NGX_Result_FAIL_PlatformError);
    return static_cast<std::uint32_t>(NVSDK_NGX_D3D12_Shutdown());
}
catch(...){return static_cast<std::uint32_t>(NVSDK_NGX_Result_FAIL_PlatformError);}

extern "C" __declspec(dllexport) unsigned __cdecl OptiScaler_W03_RecreateScopeV1() noexcept
try
{
    if(!D3D12Device||!NVNGXProxy::IsDx12Inited()||State::Instance().isShuttingDown||
       State::Instance().currentFG||State::Instance().activeFgNvngx!=FGNvngxReplacement::None||
       !DlssNr::NativeDx12Source::RecreateScope())return 0;
    // The old scope and attempt latch remain retired. This initializes only
    // the new renderer generation after authoritative prior retirement.
    DlssNr::NotifyDeviceInit(D3D12Device);return 1;
}
catch(...){return 0;}

#pragma endregion

#pragma region DLSS Parameter Calls

/**
 * @brief [Deprecated NGX API] Superceeded by NVSDK_NGX_AllocateParameters and NVSDK_NGX_GetCapabilityParameters.
 *
 * Retrieves a common NVSDK parameter map for providing params to the SDK. The lifetime of this
 * map is NOT managed by the application. It is expected to be managed internally by the SDK.
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetParameters(NVSDK_NGX_Parameter** OutParameters)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    LOG_FUNC();

    if (OutParameters == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    // If DLSS is enabled and the real DLSS module is loaded, get native NGX table
    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::D3D12_GetParameters() != nullptr)
    {
        LOG_INFO("Calling NVNGXProxy::D3D12_GetParameters");
        auto result = NVNGXProxy::D3D12_GetParameters()(OutParameters);
        LOG_INFO("Calling NVNGXProxy::D3D12_GetParameters result: {0:X}, ptr: {1:X}", (UINT) result,
                 (UINT64) *OutParameters);

        // Copy OptiScaler config to real NGX param table
        if (result == NVSDK_NGX_Result_Success)
        {
            InitNGXParameters(*OutParameters, API::DX12);
            SetNGXParamAllocType(*(*OutParameters), NGX_AllocTypes::NVPersistent);
            return NVSDK_NGX_Result_Success;
        }
    }

    // Get custom parameters if using custom backend
    static NVNGX_Parameters oldParams = NVNGX_Parameters(API::DX12, true);
    *OutParameters = &oldParams;
    InitNGXParameters(*OutParameters, API::DX12);

    LOG_DEBUG("Returning custom Opti parameters");

    return NVSDK_NGX_Result_Success;
}

/**
 * @brief Allocates a new NVSDK parameter map pre-populated with NGX capabilities and information about available
 * features. The output parameter map may also be used in the same ways as a parameter map allocated with
 * AllocateParameters(). The lifetime of this map is managed by the calling application with DestroyParameters().
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetCapabilityParameters(NVSDK_NGX_Parameter** OutParameters)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    LOG_FUNC();

    if (OutParameters == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    // Get native DLSS params if DLSS is enabled and the module is loaded
    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::IsDx12Inited() && NVNGXProxy::D3D12_GetCapabilityParameters() != nullptr)
    {
        // Streamline caches this result during plugin startup. Publish the
        // selected Ada unlock before NGX computes the capability map.
        StreamlineHooks::prepareNativeMfgCapabilities();
        if (Neurotic::Mfg::Experimental::GameFgScope::Current()) StreamlineHooks::prepareExperimentalMfgCapabilities();
        LOG_INFO("Calling NVNGXProxy::D3D12_GetCapabilityParameters");
        auto result = NVNGXProxy::D3D12_GetCapabilityParameters()(OutParameters);
        LOG_INFO("Calling NVNGXProxy::D3D12_GetCapabilityParameters result: {0:X}, ptr: {1:X}", (UINT) result,
                 (UINT64) *OutParameters);

        if (result == NVSDK_NGX_Result_Success)
        {
            // Init external NGX table with current configuration and mark as dynamic+external
            InitNGXParameters(*OutParameters, API::DX12);
            SetNGXParamAllocType(*(*OutParameters), NGX_AllocTypes::NVDynamic);
            return NVSDK_NGX_Result_Success;
        }
    }

    // Get custom parameters if using custom backend
    auto& params = *(new NVNGX_Parameters(API::DX12, false));
    InitNGXParameters(&params, API::DX12);
    *OutParameters = &params;

    LOG_DEBUG("Returning custom Opti parameters");

    return NVSDK_NGX_Result_Success;
}

/**
 * @brief Allocates a new parameter map used to provide parameters needed by the DLSS API. The lifetime of this map
 * is managed by the calling application with DestroyParameters().
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_AllocateParameters(NVSDK_NGX_Parameter** OutParameters)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    LOG_FUNC();

    if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule() != nullptr &&
        NVNGXProxy::D3D12_AllocateParameters() != nullptr)
    {
        LOG_INFO("Calling NVNGXProxy::D3D12_AllocateParameters");
        auto result = NVNGXProxy::D3D12_AllocateParameters()(OutParameters);
        LOG_INFO("Calling NVNGXProxy::D3D12_AllocateParameters result: {0:X}, ptr: {1:X}", (UINT) result,
                 (UINT64) *OutParameters);

        if (result == NVSDK_NGX_Result_Success)
        {
            SetNGXParamAllocType(*(*OutParameters), NGX_AllocTypes::NVDynamic);
            return result;
        }
    }

    auto* params = new NVNGX_Parameters(API::DX12, false);
    *OutParameters = params;

    return NVSDK_NGX_Result_Success;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* InParameters)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    LOG_FUNC();

    if (InParameters == nullptr)
        return NVSDK_NGX_Result_Fail;

    InitNGXParameters(InParameters, API::DX12);

    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None)
    {
        Nvngx_FG::D3D12_PopulateParameters_Impl(InParameters);
    }

    return NVSDK_NGX_Result_Success;
}

/**
 * @brief Destroys a given input parameter map created with AllocateParameters or GetCapabilityParameters.
 Must not be called on maps returned by GetParameters(). Unsupported tables will not be freed.
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_DestroyParameters(NVSDK_NGX_Parameter* InParameters)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    LOG_FUNC();

    if (InParameters == nullptr)
        return NVSDK_NGX_Result_Fail;

    const bool isUsingDlss = Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::NVNGXModule();
    const bool success = TryDestroyNGXParameters(InParameters, NVNGXProxy::D3D12_DestroyParameters());

    if (isUsingDlss)
        UpscalerInputsDx12::Reset();

    return success ? NVSDK_NGX_Result_Success : NVSDK_NGX_Result_Fail;
}

#pragma endregion

#pragma region DLSS Feature Calls

static Upscaler GetUpscalerBackend()
{
    Upscaler upscaler = Upscaler::XeSS; // Default

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    if (NVNGXProxy::IsDx12Inited() && primaryGpu.dlssCapable)
        upscaler = Upscaler::DLSS;

    if (primaryGpu.fsr4Support != FSR4Support::None)
        upscaler = Upscaler::FFX;

    if (Config::Instance()->Dx12Upscaler.has_value())
        upscaler = Config::Instance()->Dx12Upscaler.value();

    return upscaler;
}

static bool EnsureD3D12Device(ID3D12GraphicsCommandList* cmdList)
{
    if (D3D12Device)
        return true;

    LOG_DEBUG("Get D3D12 device from InCmdList!");

    if (FAILED(cmdList->GetDevice(IID_PPV_ARGS(&D3D12Device))) || !D3D12Device)
    {
        LOG_ERROR("Can't get Dx12Device from InCmdList!");
        return false;
    }

    return true;
}

static NVSDK_NGX_Result TryEvaluateOptiFeature(ID3D12GraphicsCommandList* InCmdList,
                                               const NVSDK_NGX_Handle* InFeatureHandle,
                                               NVSDK_NGX_Parameter* InParameters,
                                               PFN_NVSDK_NGX_ProgressCallback InCallback,
                                               DlssNr::NativeTemporalInputs::OutputEvaluation& outputEvaluation);

static NVSDK_NGX_Result TryCreateOptiFeature(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                             NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle)
{
    PollDeferredOwnedDx12Releases();
    if(!Neurotic::Runtime::CanCreateOwnedFeature(Dx12Contexts,*OutHandle))
    {
        LOG_ERROR("Owned SR/RR creation refused: unresolved prior release or live caller handle");
        return NVSDK_NGX_Result_Fail;
    }
    State& state = State::Instance();
    const Config& cfg = *Config::Instance();

    state.api = DX12;

    const uint32_t handleId = IFeature::GetNextHandleId();
    LOG_INFO("Creating OptiScaler feature, HandleId: {}", handleId);

    if (!EnsureD3D12Device(InCmdList))
    {
        LOG_ERROR("Failed to acquire D3D12 device");
        return NVSDK_NGX_Result_Fail;
    }

    // Determine backend name
    Upscaler upscalerBackend;
    if (InFeatureID == NVSDK_NGX_Feature_SuperSampling)
    {
        upscalerBackend = GetUpscalerBackend();
        LOG_INFO("Creating {} upscaler feature", UpscalerDisplayName(upscalerBackend));
    }
    else
    {
        upscalerBackend = Upscaler::DLSSD;
        LOG_INFO("Creating DLSSD (Ray Reconstruction) feature");
    }

    // Root signature restoration setup
    const bool restoreCompute = cfg.RestoreComputeSignature.value_or_default();
    const bool restoreGraphics = cfg.RestoreGraphicSignature.value_or_default();
    const bool shouldRestoreSigs = restoreCompute || restoreGraphics;

    // To avoid capturing the upscaler creation
    D3D12Hooks::SetRootSignatureTracking(false);

    if (shouldRestoreSigs)
        D3D12Hooks::HookToCommandListLate(InCmdList);

    // Create context entry
    Dx12Contexts[handleId] = {};
    Dx12Contexts[handleId].ownedCreateParams = Neurotic::Runtime::SnapshotCreateParameters(*InParameters);

    // Retrieve feature implementation
    if (!FeatureProvider_Dx12::GetFeature(upscalerBackend, handleId, InParameters, &Dx12Contexts[handleId].feature))
    {
        LOG_ERROR("Failed to retrieve feature implementation for '{}'", UpscalerDisplayName(upscalerBackend));

        D3D12Hooks::SetRootSignatureTracking(true);

        Dx12Contexts.erase(handleId);
        return NVSDK_NGX_Result_Fail;
    }

    // Assign handle
    if (*OutHandle == nullptr)
        *OutHandle = new NVSDK_NGX_Handle { handleId };
    else
        (*OutHandle)->Id = handleId;

    state.autoExposure.reset();

    IFeature_Dx12* feature = Dx12Contexts[handleId].feature.get();

    // RR already owns both denoising and mode-aware reconstruction. Initialize it from the game's
    // untouched parameter block; creating a second DLSS feature here would upscale an already
    // quality-reduced intermediate instead of allowing RR to reconstruct directly to Output.
    const bool featureInitialized = feature->Init(D3D12Device, InCmdList, InParameters);

    // Initialize feature
    if (featureInitialized)
    {
        state.currentFeature = feature;
        evalCounter = 0;
        UpscalerInputsDx12::Reset();
    }
    else
    {
        const bool rayReconstruction = upscalerBackend == Upscaler::DLSSD;
        LOG_ERROR("Feature '{}' initialization failed; scheduling {}", UpscalerDisplayName(upscalerBackend),
                  rayReconstruction ? "RR recreation" : "FSR 2.1.2 fallback");
        state.newBackend = rayReconstruction ? Upscaler::DLSSD : Upscaler::FSR21;
        state.changeBackend[handleId] = true;
    }

    // Restore root signatures
    if (shouldRestoreSigs)
        D3D12Hooks::RestoreRoot(InCmdList);

    D3D12Hooks::SetRootSignatureTracking(true);

    if (state.activeFgInput == FGInput::Upscaler)
        state.fgChanged = true;

    return NVSDK_NGX_Result_Success;
}

/**
 * @brief Instantiates a new feature based on the given unique feature ID and param table and
 * provides a handle used to reference the feature elsewhere in the API. Currently supports
 * various TSR and Frame Generation algorithms, including a special case for DLSS-RR passthrough.
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                             NVSDK_NGX_Feature InFeatureID,
                                                             NVSDK_NGX_Parameter* InParameters,
                                                             NVSDK_NGX_Handle** OutHandle)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    // NR-FEED-001 BEGIN
    Neurotic::Feed::NgxCreationSnapshot feedCreation(InParameters, NVSDK_NGX_Result_Success);
    // NR-FEED-001 END
    LOG_FUNC();

    if (!InCmdList)
    {
        LOG_ERROR("InCmdList is null");
        return NVSDK_NGX_Result_Fail;
    }

    if (!OutHandle)
    {
        LOG_ERROR("OutHandle is null");
        return NVSDK_NGX_Result_Fail;
    }

    const State& state = State::Instance();
    const Config& cfg = *Config::Instance();

    Neurotic::Mfg::Experimental::GameFgScope ngxGameScope(InFeatureID == NVSDK_NGX_Feature_FrameGeneration &&
        Neurotic::Mfg::Experimental::GameFgScope::Current());
    if (InFeatureID == NVSDK_NGX_Feature_FrameGeneration) {
        Neurotic::Mfg::Experimental::BeforeCreate();
        if (!Neurotic::Mfg::Experimental::AllowFeatureCall()) return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    }

    // DLSSG replacements passthrough
    const auto nativeCreation = (cfg.DlssNrNativeProtocol.value_or_default() || IsNrPipelineFeature(InFeatureID))
        ? DlssNr::NativeNgxCreationParameters::Capture(InParameters, NVSDK_NGX_Result_Success)
        : DlssNr::NativeNgxCreationParameters{};
    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None && Nvngx_FG::isDx12Available() &&
        InFeatureID == NVSDK_NGX_Feature_FrameGeneration)
    {
        LOG_INFO("Passthrough to DLSSG Replacement's CreateFeature for FrameGeneration");

        NVSDK_NGX_Result res = Nvngx_FG::D3D12_CreateFeature(InCmdList, InFeatureID, InParameters, OutHandle);

        if (res == NVSDK_NGX_Result_Success && *OutHandle)
        {
            LOG_INFO("Created modded DLSSG feature with HandleId: {}", (*OutHandle)->Id);
            HandleToFeature.Set((*OutHandle)->Id, InFeatureID, nativeCreation);
            // NR-FEED-001 BEGIN
            if (Neurotic::Feed::Observing()) feedCreation.Publish({"NGX", Neurotic::Contracts::GraphicsApi::D3D12, "create",
                HandleToFeature.Read((*OutHandle)->Id).generation}, *OutHandle, InFeatureID);
            // NR-FEED-001 END
        }

        LogNgxCreateTrace(InFeatureID, "DLSSG replacement", res, *OutHandle);
        return res;
    }

    // The explicit Native SR route uses the genuine provider handle, which
    // reaches the existing selected native evaluation/HostReturn path. Other
    // SR/RR choices retain their existing replacement creation route.
    const bool selectedNativeSr=DlssNr::UseNativeSrPassthrough(cfg.DlssNrNativeProtocol.value_or_default(),
        cfg.GetDlssNrRuntimeSnapshot().enabled,cfg.DlssNrRoute.value_or_default(),
        InFeatureID==NVSDK_NGX_Feature_SuperSampling,
        cfg.Dx12Upscaler.has_value()&&cfg.Dx12Upscaler.value()==Upscaler::DLSS);
    // Every SR/RR route can switch from private Present to shared Native NR.
    // Enroll before creation returns; only a genuine later Reset grants coverage.
    if (IsNrPipelineFeature(InFeatureID) || (InFeatureID == NVSDK_NGX_Feature_SuperSampling &&
        Neurotic::Semantic::Character::CharacterEarlyCaptureEnabled && Neurotic::Semantic::Character::CharacterWorkerRequested()))
    {
        // Our configured legacy hooks must exist before Native pins their routes
        // for replacement SR/RR as well as Native SR. Preserve explicit settings;
        // never rebaseline foreign changes or authenticate this in-progress list.
        if (cfg.RestoreComputeSignature.value_or_default() || cfg.RestoreGraphicSignature.value_or_default())
            D3D12Hooks::HookToCommandListLate(InCmdList);
        const auto nativeList = DlssNr::NativeIdentity::Resolve<ID3D12GraphicsCommandList>(InCmdList);
        if (nativeList.object) D3D12Hooks::InstallNativeRecordingHooks(nativeList.object.Get());
    }
    if (selectedNativeSr||(InFeatureID != NVSDK_NGX_Feature_SuperSampling && InFeatureID != NVSDK_NGX_Feature_RayReconstruction))
    {
        if (cfg.DLSSEnabled.value_or_default() && NVNGXProxy::InitDx12(D3D12Device) &&
            NVNGXProxy::D3D12_CreateFeature() != nullptr)
        {
            LOG_INFO("Passthrough to native NGX CreateFeature for feature {}", (int) InFeatureID);

            const bool diagnosticFg = InFeatureID == NVSDK_NGX_Feature_FrameGeneration;
            const auto operation = diagnosticFg ? DlssNr::FgLifecycle::Begin("fg-create-begin") : 0;
            NVSDK_NGX_Result res = NVNGXProxy::D3D12_CreateFeature()(InCmdList, InFeatureID, InParameters, OutHandle);
            if (diagnosticFg)
                DlssNr::FgLifecycle::Created(operation,
                    res == NVSDK_NGX_Result_Success && OutHandle && *OutHandle ? (*OutHandle)->Id : 0,
                    static_cast<uint32_t>(res), res == NVSDK_NGX_Result_Success, InCmdList);
            if (diagnosticFg && res == NVSDK_NGX_Result_Success && OutHandle && *OutHandle)
                DlssNr::PreFg::PublishNativeFgCreated((*OutHandle)->Id);

            if (res == NVSDK_NGX_Result_Success && *OutHandle)
            {
                LOG_INFO("Native CreateFeature success, HandleId: {}", (*OutHandle)->Id);
                HandleToFeature.Set((*OutHandle)->Id, InFeatureID, nativeCreation);
                // NR-FEED-001 BEGIN
                if (Neurotic::Feed::Observing()) feedCreation.Publish({"NGX", Neurotic::Contracts::GraphicsApi::D3D12, "create",
                    HandleToFeature.Read((*OutHandle)->Id).generation}, *OutHandle, InFeatureID);
                // NR-FEED-001 END
            }
            else
            {
                LOG_INFO("Native CreateFeature failed: 0x{:X}", (uint32_t) res);
            }

            LogNgxCreateTrace(InFeatureID, "native NGX passthrough", res, *OutHandle);
            return res;
        }

        if (InFeatureID == NVSDK_NGX_Feature_FrameGeneration)
            DlssNr::FgLifecycle::Created(0, 0, static_cast<uint32_t>(NVSDK_NGX_Result_FAIL_FeatureNotSupported), false, InCmdList);
        LOG_WARN("Native DLSS passthrough not available for feature {}", (int) InFeatureID);
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    }

    // OptiScaler internal handling (SuperSampling or RayReconstruction)
    auto tryResult = TryCreateOptiFeature(InCmdList, InFeatureID, InParameters, OutHandle);

    if (tryResult == NVSDK_NGX_Result_Success)
    {
        if (*OutHandle) HandleToFeature.Set((*OutHandle)->Id, InFeatureID, nativeCreation);
        // NR-FEED-001 BEGIN
        if (Neurotic::Feed::Observing() && *OutHandle) feedCreation.Publish({"NGX", Neurotic::Contracts::GraphicsApi::D3D12, "create",
            HandleToFeature.Read((*OutHandle)->Id).generation}, *OutHandle, InFeatureID);
        // NR-FEED-001 END
        if (InFeatureID == NVSDK_NGX_Feature_RayReconstruction)
        {
            LOG_INFO("DLSS-NR: native mode-aware RR feature created; active reconstruction follows evaluation");
        }
    }

    LogNgxCreateTrace(InFeatureID, "OptiScaler pipeline", tryResult, *OutHandle);
    return tryResult;
}

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle* InHandle)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    LOG_FUNC();

    if (!InHandle)
        return NVSDK_NGX_Result_Success;

    auto handleId = InHandle->Id;
    const auto featureSnapshot = HandleToFeature.Read(handleId);
    const auto owned=Dx12Contexts.find(handleId);
    if(owned!=Dx12Contexts.end()&&owned->second.ownedReleaseDeferred)
        return NVSDK_NGX_Result_Success; // logical release is idempotent; the retained owner stays closed
    std::optional<DlssNr::NativeFeatureRegistry<NVSDK_NGX_Feature>::ReleaseGuard> sourceRelease;
    if (featureSnapshot)
    {
        sourceRelease = HandleToFeature.BeginRelease(handleId, featureSnapshot);
        if (!sourceRelease)
        {
            if(owned!=Dx12Contexts.end())owned->second.ownedReleaseUnresolved=true;
            LOG_ERROR("Owned SR/RR release refused: source callback admission is still occupied or unavailable");
            return NVSDK_NGX_Result_Fail;
        }
    }
    if(owned!=Dx12Contexts.end()&&owned->second.feature&&!owned->second.feature->CanRetire())
    {
        auto& context=owned->second;
        auto* slot=AvailableOwnedDx12Release();
        const bool known=sourceRelease&&sourceRelease->CanRetainPending()&&context.feature->CanDeferRetirement();
        const bool independentFg=State::Instance().currentFG==nullptr||State::Instance().activeFgInput!=FGInput::Upscaler;
        if(!known||!independentFg||!slot)
        {
            context.ownedReleaseUnresolved=true; // every refused explicit release blocks fresh allocation
            if(sourceRelease)sourceRelease->DeferBeforeProvider();
            LOG_ERROR("Owned SR/RR release refused before provider: known={}, independentFG={}, capacity={}",known,independentFg,slot!=nullptr);
            return NVSDK_NGX_Result_Fail;
        }
        slot->handle=handleId;slot->failed=false;slot->guard.emplace(std::move(*sourceRelease));
        context.ownedReleaseDeferred=true;context.ownedReleaseUnresolved=false;
        if(State::Instance().currentFeature==context.feature.get())State::Instance().currentFeature=nullptr;
        LOG_INFO("Owned SR/RR logical release accepted with tracked pending recording, HandleId: {}",handleId);
        return NVSDK_NGX_Result_Success; // no global FG/exposure cleanup on this retained generation
    }
    if(owned!=Dx12Contexts.end())owned->second.ownedReleaseUnresolved=true;

    // Before any feature's resources are freed, drop the exposure scan's references to whatever it
    // captured. The scan AddRef's candidates and never released them; a Streamline/DLSS-D resource it
    // pinned would otherwise be used after its heap is freed here -- the Cyberpunk device-removal.
    // Capture is gated on NR being enabled (not the scan source), so this drops whatever was captured
    // whenever NR is on; a no-op only when NR is off.
    DlssNr::ExposureScan::ReleaseTrackedResources();

    const NVSDK_NGX_Feature releasedFeature = featureSnapshot.feature;
    const auto diagnosticInstance = DlssNr::FgLifecycle::Find(handleId);
    const auto nativeFgInstance = DlssNr::PreFg::NativeFgInstance(handleId);
    const bool diagnosticFg = releasedFeature == NVSDK_NGX_Feature_FrameGeneration || diagnosticInstance != 0;
    const auto finishRelease = [&](NVSDK_NGX_Result result) {
        if (sourceRelease ? sourceRelease->Complete(result == NVSDK_NGX_Result_Success) :
            HandleToFeature.Released(handleId, featureSnapshot, result == NVSDK_NGX_Result_Success))
        {
            {
                std::lock_guard lock(ngxObservationMutex);
                NrPipelineObservations.erase(handleId);
                NgxEvaluationTraceObservations.erase(handleId);
            }
            if (releasedFeature == NVSDK_NGX_Feature_RayReconstruction &&
                !HandleToFeature.Has(NVSDK_NGX_Feature_RayReconstruction))
                DlssNr::SetNativeRayReconstructionActive(false);
        }
        return result;
    };

    // A replacement can retain the same dimensions and preset. Treat release of either feature
    // that owns the NR seam as a continuity break; unrelated NGX features must not perturb NR.
    if (handleId < DLSS_MOD_ID_OFFSET && IsNrPipelineFeature(releasedFeature))
        DlssNr::NotifyUpscalerRelease();

    // Clean up framegen
    if (State::Instance().currentFG != nullptr && State::Instance().activeFgInput == FGInput::Upscaler)
    {
        State::Instance().fgChanged = true;
        State::Instance().currentFG->DestroyFGContext();
        State::Instance().clearCapturedHudlesses = true;
        UpscalerInputsDx12::Reset();
    }

    if (!shutdown)
        LOG_INFO("releasing feature with id {0}", handleId);

    // OptiScaler handles start after this offset. If it's outside this range, it doesn't belong to OptiScaler.
    if (handleId < DLSS_MOD_ID_OFFSET)
    {
        if (Config::Instance()->DLSSEnabled.value_or_default() && NVNGXProxy::D3D12_ReleaseFeature() != nullptr)
        {
            if (!shutdown)
                LOG_INFO("calling D3D12_ReleaseFeature for ({0})", handleId);

            // Clean up real DLSS feature
            const auto operation = diagnosticFg ? DlssNr::FgLifecycle::Begin("fg-release-begin", handleId) : 0;
            auto result = NVNGXProxy::D3D12_ReleaseFeature()(InHandle);
            if (diagnosticFg)
                DlssNr::FgLifecycle::Released(operation, handleId, diagnosticInstance,
                    static_cast<uint32_t>(result), result == NVSDK_NGX_Result_Success);
            if (releasedFeature == NVSDK_NGX_Feature_FrameGeneration &&
                result == NVSDK_NGX_Result_Success)
            {
                DlssNr::PreFg::PublishNativeFgReleased(handleId, nativeFgInstance);
                Neurotic::Semantic::Character::NativeFgWork().Released(handleId,nativeFgInstance);
            }

            if (!shutdown)
                LOG_INFO("D3D12_ReleaseFeature result for ({0}): {1:X}", handleId, (UINT) result);

            return finishRelease(result);
        }
        else
        {
            if (!shutdown)
                LOG_INFO("D3D12_ReleaseFeature not available for ({0})", handleId);
            if (diagnosticFg)
                DlssNr::FgLifecycle::Released(0, handleId, diagnosticInstance,
                    static_cast<uint32_t>(NVSDK_NGX_Result_FAIL_FeatureNotFound), false);

            return finishRelease(NVSDK_NGX_Result_FAIL_FeatureNotFound);
        }
    }
    // Clean up OptiScaler feature with framegen
    else if (State::Instance().activeFgNvngx != FGNvngxReplacement::None && handleId >= NVNGX_PROVIDER_ID_OFFSET)
    {
        LOG_INFO("D3D12_ReleaseFeature modded DLSSG with HandleId: {0}", handleId);
        return finishRelease(Nvngx_FG::D3D12_ReleaseFeature(InHandle));
    }

    // Remove feature from context map
    if (auto it = Dx12Contexts.find(handleId); it != Dx12Contexts.end())
    {
        auto& entry = it->second;

        if (auto* deviceContext = entry.feature.get())
        {
            if (!deviceContext->CanRetire())
            {
                if(sourceRelease)sourceRelease->DeferBeforeProvider();
                return NVSDK_NGX_Result_Fail;
            }
            const auto providerRelease = deviceContext->ReleaseProvider();
            if (providerRelease != NVSDK_NGX_Result_Success) return finishRelease(providerRelease);
            // Clear global reference if it matches
            if (deviceContext == State::Instance().currentFeature)
                State::Instance().currentFeature = nullptr;

            // Erase from map (smart pointer reset is implicit on erase)
            Dx12Contexts.erase(it);
        }
        else Dx12Contexts.erase(it);
    }
    else
    {
        // Fallback Error Handling
        if (!shutdown)
            LOG_ERROR("can't release feature with id {0}!", handleId);
    }

    return finishRelease(NVSDK_NGX_Result_Success);
}

/**
 * @brief Used by the client application to check for feature support.
 * @param Adapter Device the feature is for.
 * @param FeatureDiscoveryInfo Specifies the feature being queried.
 * @param OutSupported Used to indicate whether a feature is supported and its requirements.
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetFeatureRequirements(
    IDXGIAdapter* Adapter, const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
    NVSDK_NGX_FeatureRequirement* OutSupported)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    LOG_DEBUG("for ({0})", (int) FeatureDiscoveryInfo->FeatureID);

    const bool isUpscaling = FeatureDiscoveryInfo->FeatureID == NVSDK_NGX_Feature_SuperSampling;
    const bool isFG = FeatureDiscoveryInfo->FeatureID == NVSDK_NGX_Feature_FrameGeneration;
    const bool dlssgAdjacent = Nvngx_FG::isDx12Available() || State::Instance().activeFgInput == FGInput::DLSSG;

    if (isUpscaling || (isFG && dlssgAdjacent))
    {
        if (OutSupported == nullptr)
        {
            static auto tmp = NVSDK_NGX_FeatureRequirement();
            OutSupported = &tmp;
        }

        OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_Supported;
        OutSupported->MinHWArchitecture = 0;

        // Some old windows 10 os version
        strcpy_s(OutSupported->MinOSVersion, "10.0.10240.16384");
        DlssNr::Capability::CaptureRequirementsResult(DlssNr::Capability::GraphicsPath::D3D12, static_cast<uint64_t>(FeatureDiscoveryInfo->FeatureID), NVSDK_NGX_Result_Success, NVSDK_NGX_Result_Success, OutSupported, DlssNr::Capability::RequirementsOrigin::Effective);
        return NVSDK_NGX_Result_Success;
    }

    if (Config::Instance()->DLSSEnabled.value_or_default() && IdentifyGpu::getPrimaryGpu().dlssCapable &&
        NVNGXProxy::NVNGXModule() == nullptr)
    {
        NVNGXProxy::InitNVNGX();
    }

    if (Config::Instance()->DLSSEnabled.value_or_default() && IdentifyGpu::getPrimaryGpu().dlssCapable &&
        NVNGXProxy::D3D12_GetFeatureRequirements() != nullptr)
    {
        LOG_DEBUG("D3D12_GetFeatureRequirements for ({0})", (int) FeatureDiscoveryInfo->FeatureID);
        if (isFG && Neurotic::Mfg::Experimental::GameFgScope::Current()) StreamlineHooks::prepareExperimentalMfgCapabilities();
        DXGI_ADAPTER_DESC experimentalAdapter{};
        const bool boundExperimentalAdapter = Adapter && SUCCEEDED(Adapter->GetDesc(&experimentalAdapter));
        if (isFG && !Neurotic::Mfg::Experimental::AllowFeatureCall()) return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
        Neurotic::Mfg::Experimental::ArchitectureScope experimentalScope(isFG && boundExperimentalAdapter &&
            Neurotic::Mfg::Experimental::GameFgScope::Current(),
            experimentalAdapter.AdapterLuid);
        auto result = NVNGXProxy::D3D12_GetFeatureRequirements()(Adapter, FeatureDiscoveryInfo, OutSupported);
        LOG_DEBUG("D3D12_GetFeatureRequirements result for ({0}): {1:X}", (int) FeatureDiscoveryInfo->FeatureID,
                  (UINT) result);

        DlssNr::Capability::CaptureRequirementsResult(DlssNr::Capability::GraphicsPath::D3D12, static_cast<uint64_t>(FeatureDiscoveryInfo->FeatureID), result, NVSDK_NGX_Result_Success, OutSupported, DlssNr::Capability::RequirementsOrigin::Raw);
        if (isFG && Adapter && OutSupported) {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(Adapter->GetDesc(&desc))) {
                uint32_t flags = static_cast<uint32_t>(OutSupported->FeatureSupported);
                uint32_t arch = OutSupported->MinHWArchitecture;
                if (Neurotic::Mfg::Experimental::RelaxRequirements(static_cast<uint32_t>(result),
                    static_cast<uint32_t>(FeatureDiscoveryInfo->FeatureID), desc.AdapterLuid, flags, arch)) {
                    OutSupported->FeatureSupported = static_cast<decltype(OutSupported->FeatureSupported)>(flags);
                    OutSupported->MinHWArchitecture = arch;
                }
            }
        }
        return result;
    }
    else
    {
        LOG_DEBUG("D3D12_GetFeatureRequirements not available for ({0})", (int) FeatureDiscoveryInfo->FeatureID);
    }

    OutSupported->FeatureSupported = NVSDK_NGX_FeatureSupportResult_AdapterUnsupported;
    DlssNr::Capability::CaptureRequirementsResult(DlssNr::Capability::GraphicsPath::D3D12, static_cast<uint64_t>(FeatureDiscoveryInfo->FeatureID), NVSDK_NGX_Result_FAIL_FeatureNotSupported, NVSDK_NGX_Result_Success, OutSupported, DlssNr::Capability::RequirementsOrigin::Effective);
    return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
}

static NVSDK_NGX_Result TryEvaluateOptiFeature(ID3D12GraphicsCommandList* InCmdList,
                                               const NVSDK_NGX_Handle* InFeatureHandle,
                                               NVSDK_NGX_Parameter* InParameters,
                                               PFN_NVSDK_NGX_ProgressCallback InCallback,
                                               DlssNr::NativeTemporalInputs::OutputEvaluation& outputEvaluation)
{
    State& state = State::Instance();
    const Config& cfg = *Config::Instance();
    const uint32_t handleId = InFeatureHandle->Id;

    auto ctxIt = Dx12Contexts.find(handleId);

    if (ctxIt == Dx12Contexts.end())
    {
        LOG_WARN("No context found for handle {}", handleId);
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    ContextData<IFeature_Dx12>& ctxData = ctxIt->second;
    IFeature_Dx12* feature = ctxData.feature.get();

    if (feature == nullptr) // Prevent source api name flicker when dlssg is active
        state.setInputApiName = state.currentInputApiName;

    const auto targetApiName =
        !state.setInputApiName.has_value() ? ApiUpscalerInput::DLSS_DX12 : state.setInputApiName.value();

    if (state.currentInputApiName != targetApiName)
        state.currentInputApiName = targetApiName;

    state.setInputApiName.reset();
    evalCounter++;

    // Skip evaluation for the first N frames if configured
    if (cfg.SkipFirstFrames.has_value() && evalCounter < cfg.SkipFirstFrames.value())
        return NVSDK_NGX_Result_Fail;

    // Root signature restoration setup
    const bool restoreCompute = cfg.RestoreComputeSignature.value_or_default();
    const bool restoreGraphics = cfg.RestoreGraphicSignature.value_or_default();
    const bool shouldRestoreSigs = restoreCompute || restoreGraphics;

    if (shouldRestoreSigs)
    {
        D3D12Hooks::HookToCommandListLate(InCmdList);

        if (!D3D12Hooks::CanRestoreRootSignature(InCmdList))
        {
            LOG_DEBUG("Skipping upscaling because can't restore root signature");
            return NVSDK_NGX_Result_Fail;
        }
    }

    if (InCallback)
        LOG_INFO("Progress callback provided but unused in synchronous OptiScaler path");

    // Resolution change detection (only for upscalers that may require recreation)
    if (feature != nullptr)
    {
        const bool isFFX =
            feature->GetUpscalerType() == Upscaler::FFX || feature->GetUpscalerType() == Upscaler::FFX_on12;
        const bool isFSR31OrLater = isFFX && feature->Version() >= feature_version { 3, 1, 0 };

        // FSR 3.1 supports upscaleSize that doesn't need reinit to change output resolution
        if (!isFSR31OrLater && feature->UpdateOutputResolution(InParameters))
            state.changeBackend[handleId] = true;
    }

    // To avoid capturing potential upscaler change (creation) and then upscaling itself
    D3D12Hooks::SetRootSignatureTracking(false);

    // Backend change or recreation requested
    if (state.changeBackend[handleId])
    {
        UpscalerInputsDx12::Reset();

        auto successfulPhase = FeatureProvider_Dx12::ChangeFeature(state.newBackend, D3D12Device, InCmdList, handleId,
                                                                   InParameters, &ctxData);
        feature = ctxData.feature.get();

        evalCounter = 0;

        if (ctxData.changeBackendCounter != 0 || !successfulPhase)
        {
            D3D12Hooks::SetRootSignatureTracking(true);
            return NVSDK_NGX_Result_Fail;
        }
    }

    // Fallback to FSR 2.1.2 if feature failed to initialize and user didn't explicitly request it
    if (feature && feature->GetUpscalerType() != Upscaler::DLSSD && !feature->IsInited() && cfg.Dx12Upscaler.value_or_default() != Upscaler::FSR21)
    {
        LOG_WARN("Feature '{}' failed to initialize. Falling back to FSR 2.1.2", feature->Name());
        ImGui::InsertNotification({ ImGuiToastType::Warning, 10000, "Falling back to FSR 2.1.2" });

        state.newBackend = Upscaler::FSR21;
        state.changeBackend[handleId] = true;

        D3D12Hooks::SetRootSignatureTracking(true);

        return NVSDK_NGX_Result_Fail;
    }

    if (!feature) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    state.currentFeature = feature;

    // Prepare upscaling inputs
    UpscalerInputsDx12::UpscaleStart(InCmdList, InParameters, feature);
    FSR3FG::SetUpscalerInputs(InCmdList, InParameters, feature);

    // Evaluate the feature
    bool evalSuccess = false;
    {
        // Resource tracking
        UpscalerInputsDx12::UpscaleEnd(InCmdList, InParameters, feature);

        ScopedSkipHeapCapture skip {};
        evalSuccess = outputEvaluation.Invoke(feature->GetUpscalerType() == Upscaler::DLSSD &&
            feature->Api() == API::DX12 && !feature->IsWithDx12(),
            [&] { return feature->Evaluate(InCmdList, InParameters); });
    }

    if (!evalSuccess)
    {
        LOG_ERROR("Feature evaluation failed for '{}'", feature->Name());
        ImGui::InsertNotification({ ImGuiToastType::Error, 10000, "Upscaler failed to run!" });
    }

    // Restore root signatures
    if (shouldRestoreSigs)
        D3D12Hooks::RestoreRoot(InCmdList);

    D3D12Hooks::SetRootSignatureTracking(true);

    return evalSuccess ? NVSDK_NGX_Result_Success : NVSDK_NGX_Result_Fail;
}

/**
 * @brief Per-frame feature execution. Runs a feature (upscaler, framegen, etc.) on a given command list using a
 * preexisting feature instance referenced by a unique handle.
 */
NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_EvaluateFeature(ID3D12GraphicsCommandList* InCmdList,
                                                               const NVSDK_NGX_Handle* InFeatureHandle,
                                                               NVSDK_NGX_Parameter* InParameters,
                                                               PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    PollDeferredOwnedDx12Releases();
    if(InFeatureHandle)
        if(const auto old=Dx12Contexts.find(InFeatureHandle->Id);old!=Dx12Contexts.end()&&old->second.ownedReleaseDeferred)
            return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    auto sourceTransactionScope=DlssNr::NativeDx12Source::BeginSourceScope();
    if (!InFeatureHandle)
    {
        LOG_DEBUG("InFeatureHandle is null");
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    if (!InCmdList || !InParameters)
    {
        LOG_ERROR("NGX evaluation requires a command list and parameter block");
        return NVSDK_NGX_Result_Fail;
    }

    const uint32_t handleId = InFeatureHandle->Id;
    LOG_DEBUG("EvaluateFeature - Handle: {}, CmdList: {:p}", handleId, (void*) InCmdList);

    const State& state = State::Instance();
    const Config& cfg = *Config::Instance();

    const auto featureSnapshot = HandleToFeature.Read(handleId);
    if (!featureSnapshot)
    {
        LOG_WARN("EvaluateFeature received untracked handle {}; NR is not attached", handleId);
    }
    const NVSDK_NGX_Feature feature = featureSnapshot.feature;
    Neurotic::Mfg::Experimental::GameFgScope ngxGameScope(feature == NVSDK_NGX_Feature_FrameGeneration &&
        Neurotic::Mfg::Experimental::GameFgScope::Current());
    if (feature == NVSDK_NGX_Feature_FrameGeneration && !Neurotic::Mfg::Experimental::AllowFeatureCall())
        return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
    // NR-FEED-001 BEGIN
    Neurotic::Feed::Callback feedObservation({"NGX", Neurotic::Contracts::GraphicsApi::D3D12,
        "evaluate", featureSnapshot ? std::optional<std::uint64_t>(featureSnapshot.generation) : std::nullopt}, InFeatureHandle);
    Neurotic::Feed::ObserveNgxEvaluation(feedObservation, InParameters, NVSDK_NGX_Result_Success);
    feedObservation.Value("feature", feature);
    // NR-FEED-001 END
    const bool isNrPipelineFeature = IsNrPipelineFeature(feature);
    const bool isSuperResolution = feature == NVSDK_NGX_Feature_SuperSampling;
    const bool isRayReconstruction = feature == NVSDK_NGX_Feature_RayReconstruction;
    // Observe the original temporal color before any NR override or upscaler.
    // Optional capture must not infer resource state from the swapchain HDR mode.
    if (Neurotic::Semantic::Character::CharacterEarlyCaptureEnabled && isSuperResolution && featureSnapshot && InParameters && InCmdList &&
        Neurotic::Semantic::Character::CharacterWorkerRequested() && !cfg.ColorResourceBarrier.has_value())
    {
        const auto nativeList = DlssNr::NativeIdentity::Resolve<ID3D12GraphicsCommandList>(InCmdList);
        if (nativeList.object)
        {
            // One bounded enrollment attempt per native vtable route. Existing
            // in-progress recordings remain unknown until a genuine host Reset.
            // Do not install full command observation while the Inspector is off.
            static std::mutex enrollmentMutex;
            static std::array<void*,8> attemptedRoutes{};
            auto* route = *reinterpret_cast<void**>(nativeList.object.Get());
            bool install = false;
            { std::unique_lock lock(enrollmentMutex,std::try_to_lock);
                if (lock && std::find(attemptedRoutes.begin(),attemptedRoutes.end(),route)==attemptedRoutes.end())
                    if (auto free=std::find(attemptedRoutes.begin(),attemptedRoutes.end(),nullptr); free!=attemptedRoutes.end())
                    { *free=route;install=true; }
            }
            if (install) D3D12Hooks::InstallNativeRecordingHooks(nativeList.object.Get());
        }
        const auto metadata = DlssNr::NativeTemporalInputs::FromCreation(
            featureSnapshot.originalCreation, handleId, featureSnapshot.generation);
        void* color = nullptr;
        unsigned width = 0, height = 0, x = 0, y = 0;
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &width);
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &height);
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, &x);
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, &y);
        if (!width) width = featureSnapshot.originalCreation.Value("Width").value_or(0);
        if (!height) height = featureSnapshot.originalCreation.Value("Height").value_or(0);
        if (metadata && !x && !y && width && height &&
            InParameters->Get(NVSDK_NGX_Parameter_Color, &color) == NVSDK_NGX_Result_Success && color)
        {
            const auto source = DlssNr::NativeIdentity::Resolve<ID3D12Resource>(static_cast<IUnknown*>(color));
            const bool linear = (metadata->flags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR) != 0;
            float preExposure = 1.f;
            InParameters->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &preExposure);
            if (source.object && std::isfinite(preExposure) && preExposure > 1e-6f)
            {
                const auto desc = source.object->GetDesc();
                // Cropped/padded inputs require a separately proved coordinate mapping.
                if (desc.Width == width && desc.Height == height &&
                    !Neurotic::Semantic::Character::CharacterBeforeUpscale(InCmdList, source.object.Get(),
                        width, height, metadata->outputWidth, metadata->outputHeight, linear, preExposure,
                        featureSnapshot.generation))
                    return NVSDK_NGX_Result_FAIL_PlatformError;
            }
        }
    }
    // Cyberpunk retains its RR handle after switching RR off and begins evaluating a separate
    // Super Resolution handle. Track the feature that owns this frame's reconstruction seam;
    // handle creation alone otherwise leaves Present's RR compatibility gate permanently stale.
    if (isNrPipelineFeature)
        DlssNr::SetNativeRayReconstructionActive(isRayReconstruction);
    // NR-DIAG-001 BEGIN: owner-published scalar handoff; no handle/pointer retention or queries.
    const auto m0Publisher = DlssNr::FrameTrace::WithM0Publisher([&]() noexcept {
        using SourceSnapshot = Neurotic::Diagnostics::M0::SourceSnapshot;
        using OwnerDomain = Neurotic::Contracts::OwnerDomain;
        auto source = SourceSnapshot::OwnerPublication(OwnerDomain::Provider,
            "Alpha.NVNGX.Evaluate", "NVNGX_DLSS_Dx12", 1, "NVSDK_NGX_D3D12_EvaluateFeature");
        source.Add("alpha.featureTracked", static_cast<bool>(featureSnapshot));
        source.Add("alpha.featureCode", static_cast<std::uint64_t>(feature));
        source.Add("alpha.nrPipelineFeature", isNrPipelineFeature);
        source.Add("alpha.superResolution", isSuperResolution);
        source.Add("alpha.rayReconstruction", isRayReconstruction);
        source.Add("alpha.activeFgOutput", static_cast<std::uint64_t>(state.activeFgOutput));
        source.Add("alpha.nativeRegistryGeneration", featureSnapshot.generation);
        return source;
    });
    (void) m0Publisher;
    // NR-DIAG-001 END
    const auto traceEvaluation = DlssNr::FrameTrace::Event("ngx-evaluate-enter",
        "handle={} feature={} list={:p} fgOutput={}", handleId, static_cast<unsigned int>(feature),
        static_cast<void*>(InCmdList), static_cast<unsigned int>(state.activeFgOutput));
    DlssNr::FrameTrace::Context traceContext(DlssNr::FrameTrace::nativeObservation, traceEvaluation);
    LogNgxEvaluationTrace(handleId, static_cast<bool>(featureSnapshot), feature);
    LogNrPipelineObservation(handleId, feature, InParameters, cfg.DlssNrRunBeforeSr.value_or_default());
    static size_t evalWithoutFG = 0;
    const bool fgCreated = HandleToFeature.Has(NVSDK_NGX_Feature_FrameGeneration);

    static std::optional<float> lastDlssgCameraNear {};
    static std::optional<float> lastDlssgCameraFar {};
    void* fgBackbuffer = nullptr;
    NVSDK_NGX_Result fgBackbufferResult = NVSDK_NGX_Result_FAIL_InvalidParameter;
    unsigned int fgGenerated = 0, fgIndex = 0;
    bool characterFgWork=false;

    if (feature == NVSDK_NGX_Feature_FrameGeneration)
    {
        if (InParameters)
            fgBackbufferResult = InParameters->Get("DLSSG.Backbuffer", &fgBackbuffer);
        if (DlssNr::FrameTrace::Armed() && InParameters)
        {
            void* traceHudless = nullptr;
            const auto hudlessResult = InParameters->Get("DLSSG.HUDLess", &traceHudless);
            const auto* preFg = DlssNr::PreFg::forwardingFrame;
            NR_FRAME_TRACE("ngx-fg-input", "provider=nvngx handle={} list={:p} backbuffer={:p} "
                "hudless={:p} backbufferResult={} hudlessResult={} realSequence={} providerToken={} "
                "nrSubmitted={} association={} handleInstance={} generation={} claimGeneration={} providerGeneration={}", handleId,
                static_cast<void*>(InCmdList), fgBackbuffer, traceHudless,
                static_cast<unsigned int>(fgBackbufferResult), static_cast<unsigned int>(hudlessResult),
                preFg ? preFg->sequence : 0, preFg ? preFg->key : 0, preFg && preFg->outputSubmitted,
                preFg ? "present-call-scope" : "unknown", DlssNr::FgLifecycle::Find(handleId),
                DlssNr::FgLifecycle::Read().generation, preFg ? preFg->diagnosticClaim.generation : 0,
                preFg ? preFg->providerGeneration : 0);
        }
        int frameCount = 0;
        InParameters->Get("DLSSG.MultiFrameCount", &frameCount);
        const bool haveCount = InParameters->Get("DLSSG.MultiFrameCount", &fgGenerated) == NVSDK_NGX_Result_Success;
        characterFgWork=!haveCount||fgGenerated>0;
        const bool haveIndex = InParameters->Get("DLSSG.MultiFrameIndex", &fgIndex) == NVSDK_NGX_Result_Success;
        if (!haveCount) fgGenerated = 0;
        // The existing single-evaluation path needs no child discriminator.
        // MFG always requires the actual provider index; never infer it by age.
        if (!haveIndex) fgIndex = fgGenerated == 1 ? 1 : 0;
        DlssNr::PreFg::ObserveGeneratedCount(handleId, fgGenerated, fgIndex);
        if (!haveCount || !fgGenerated || fgGenerated > 5 || !fgIndex || fgIndex > fgGenerated)
        {
            static std::atomic<unsigned int> missingObservations {0};
            if (++missingObservations <= 4)
                LOG_INFO("NR Present MFG observation unavailable: countKnown={} indexKnown={} generated={} index={}; "
                         "fixed 2x-6x admission requires valid provider parameters",
                         haveCount, haveIndex, fgGenerated, fgIndex);
        }
        float dlssgCameraNear = 0.0f;
        float dlssgCameraFar = 0.0f;
        const bool haveNear = InParameters->Get("DLSSG.CameraNear", &dlssgCameraNear) == NVSDK_NGX_Result_Success;
        const bool haveFar = InParameters->Get("DLSSG.CameraFar", &dlssgCameraFar) == NVSDK_NGX_Result_Success;
        {
            std::lock_guard lock(fgObservationMutex);
            evalWithoutFG = 0;
            if (haveNear) lastDlssgCameraNear = dlssgCameraNear;
            if (haveFar) lastDlssgCameraFar = dlssgCameraFar;
        }
        State::Instance().dlssgDetectedInterpolationCount = frameCount;
        ReflexHooks::setDlssgFrameCount(frameCount);
    }
    else if (fgCreated)
    {
        bool reportDisabled = false;
        {
            std::lock_guard lock(fgObservationMutex);
            if (evalWithoutFG < 6) reportDisabled = ++evalWithoutFG == 6;
        }
        if (reportDisabled)
        {
            // Report FG as disabled
            State::Instance().dlssgDetectedInterpolationCount = 0;
            ReflexHooks::setDlssgFrameCount(0);
        }
    }

    // One NR request/tuning snapshot covers both sides of this upscaler call. A mode edit
    // during native SR must not schedule NR once before SR and again after it.
    const auto nrSettings = isNrPipelineFeature ? TryNrConfigSnapshot(cfg)
                                               : std::optional<NrConfigSnapshot<Config>>{};
    std::optional<DlssNr::NativeFeatureRegistry<NVSDK_NGX_Feature>::CallbackPin> sourcePin;
    std::optional<Neurotic::Lifecycle::NativeProcessBootstrap::Callback> nativeSource;
    std::shared_ptr<const DlssNr::NativeNgxCallCapture> alternateOriginal;
    DlssNr::NativeTemporalSourceObservation alternateSource;
    const auto rejectLegacyNr = [&]() {
        const auto frame = DlssNr::PreFg::CurrentFrameIdentity(InCmdList);
        DlssNr::PresentGuides::Instance().RejectNative("Native caller-state restoration failed; no current guides",
            frame.key, frame.providerGeneration);
        if (sourcePin && alternateOriginal)
            HandleToFeature.CompleteTemporalSource(*sourcePin, alternateSource, false);
        if (nativeSource)
            DlssNr::NativeDx12Source::FinishReturn(*nativeSource, NVSDK_NGX_Result_FAIL_PlatformError);
        return NVSDK_NGX_Result_FAIL_PlatformError;
    };
    const bool alternateEnabled=nrSettings&&nrSettings->DlssNrAlternateFrame.value_or_default()&&
        nrSettings->GetDlssNrRuntimeSnapshot().enabled;
    // A feature protected by an earlier invocation remains protected when the
    // setting changes. Keep its real CPU admission pinned through this call;
    // the uncaptured effect below remains unknown lifetime coverage.
    if ((!nrSettings || !nrSettings->DlssNrNativeProtocol.value_or_default()) &&
        (HandleToFeature.Protected(handleId) || (isSuperResolution && alternateEnabled)))
    {
        sourcePin = HandleToFeature.Pin(handleId, featureSnapshot);
        if (!sourcePin) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        if(isSuperResolution && alternateEnabled) {
            alternateOriginal=HandleToFeature.CaptureTemporalInputs(*sourcePin,InCmdList,InParameters,NVSDK_NGX_Result_Success);
            alternateSource=HandleToFeature.ObserveTemporalSource(*sourcePin,
                alternateOriginal?alternateOriginal->SourceFrame():nullptr,alternateOriginal);
        }
    }
    if (nrSettings && nrSettings->DlssNrNativeProtocol.value_or_default())
    {
        sourcePin = HandleToFeature.Pin(handleId, featureSnapshot);
        if (!sourcePin) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        try { nativeSource = DlssNr::NativeDx12Source::Capture(std::move(*sourcePin), InCmdList, InParameters); }
        catch (...) { return NVSDK_NGX_Result_Fail; }
        if (!nativeSource || !nativeSource->Current()) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
        if(isSuperResolution)DlssNr::NativeDx12Source::BindSourceScope(sourceTransactionScope,*nativeSource);
        if (isSuperResolution && nrSettings->DlssNrRunBeforeSr.value_or_default())
        {
            // Initial owner/model preparation only. No candidate evaluation or
            // host delivery is authorized by this route materialization.
            const auto nativeBefore = DlssNr::NativeDx12Source::RunBefore(*nativeSource,InParameters,*nrSettings);
            if (!nativeSource->Current()) return NVSDK_NGX_Result_FAIL_FeatureNotFound;
            // A failed restoration may leave the caller's parameter bound to
            // Native scratch. Neither SR path may consume that uncertain binding.
            if (!Neurotic::Protocol::NativeMayContinueOuterEvaluation(nativeBefore.facts))
                return NVSDK_NGX_Result_FAIL_PlatformError;
        }
    }

    // Native DLSS passthrough
    if (handleId < DLSS_MOD_ID_OFFSET)
    {
        if (cfg.DLSSEnabled.value_or_default() && NVNGXProxy::D3D12_EvaluateFeature() != nullptr)
        {
            LOG_DEBUG("Passthrough to native DLSS EvaluateFeature for handle {}", handleId);

            if (isSuperResolution && nrSettings && !nrSettings->DlssNrNativeProtocol.value_or_default())
            {
                if (!InvokeLegacyNativeNr(InCmdList, InParameters, *nrSettings, true, false, [&] {
                    DlssNr::EvaluateBeforeUpscale(InCmdList, InParameters, nullptr, &*nrSettings, true);
                })) return rejectLegacyNr();
            }

            std::shared_ptr<DlssNr::GpuSafety::ExternalExecutionStatus> consumerObservation;
            DlssNr::PreFg::CompletionClaim fgCompletion;
            if (feature == NVSDK_NGX_Feature_FrameGeneration)
            {
                consumerObservation = DlssNr::PreFg::ObserveConsumer(handleId, InCmdList);
                ID3D12Resource* nativeBackbuffer = nullptr;
                if (fgBackbufferResult == NVSDK_NGX_Result_Success && fgBackbuffer)
                {
                    const auto resolved = DlssNr::NativeIdentity::Resolve<ID3D12Resource>(
                        static_cast<IUnknown*>(fgBackbuffer));
                    nativeBackbuffer = resolved.object.Get();
                }
                if (!nativeBackbuffer && DlssNr::PreFg::PendingCompletions())
                {
                    if (consumerObservation) consumerObservation->failed = true;
                    DlssNr::PreFg::RevokeReadiness();
                    NR_FRAME_TRACE("nr-fg-handoff-refused", "reason=missing-native-backbuffer handle={} list={:p}",
                        handleId, static_cast<void*>(InCmdList));
                    return NVSDK_NGX_Result_FAIL_InvalidParameter;
                }
                const auto provider = DlssNr::PreFg::Provider();
                fgCompletion = DlssNr::PreFg::ClaimCompletion(nativeBackbuffer, provider.generation, handleId,
                                                              fgGenerated, fgIndex);
                const auto& completion = fgCompletion;
                bool handoffFailed = completion.result == DlssNr::PreFg::CompletionClaimResult::Refused;
                if (completion.result == DlssNr::PreFg::CompletionClaimResult::Ready &&
                    !DlssNr::GpuSafety::BindExternalWait(InCmdList, completion.dependency.fence.Get(),
                        completion.dependency.value, completion.dependency.token,
                        completion.dependency.sequence, completion.dependency.status))
                {
                    if (completion.dependency.status) completion.dependency.status->Fail();
                    DlssNr::PreFg::RevokeReadiness();
                    DlssNr::PreFg::RejectCompletion(nativeBackbuffer, provider.generation, handleId,
                        completion.dependency.token, completion.dependency.sequence);
                    handoffFailed = true;
                }
                if (handoffFailed)
                {
                    if (consumerObservation) consumerObservation->failed = true;
                    static std::atomic<unsigned int> refusedHandoffs {0};
                    if (++refusedHandoffs <= 16)
                        LOG_WARN("NR Present FG handoff refused: reason={} provider={} nativeHandle={} generated={} index={}",
                                 completion.result == DlssNr::PreFg::CompletionClaimResult::Refused ?
                                 completion.reason : "wait-bind-failed", provider.generation, handleId, fgGenerated, fgIndex);
                    NR_FRAME_TRACE("nr-fg-handoff-refused", "reason={} handle={} list={:p} backbuffer={:p} "
                        "token={} sequence={} generated={} index={}", completion.result == DlssNr::PreFg::CompletionClaimResult::Refused ?
                        "identity-or-input-group-mismatch" : "wait-bind-failed", handleId, static_cast<void*>(InCmdList),
                        static_cast<void*>(nativeBackbuffer), completion.dependency.token,
                        completion.dependency.sequence, fgGenerated, fgIndex);
                    return NVSDK_NGX_Result_FAIL_PlatformError;
                }
            }

            DlssNr::NativeDx12Source::SrInputScope srInput(nativeSource&&isSuperResolution?&*nativeSource:nullptr,InParameters);
            if(!srInput.Ready())
            {
                srInput.Close();
                if(nativeSource)DlssNr::NativeDx12Source::FinishReturn(*nativeSource,NVSDK_NGX_Result_FAIL_PlatformError);
                return NVSDK_NGX_Result_FAIL_PlatformError;
            }
            auto opaqueSr=(nativeSource&&isSuperResolution&&nrSettings)?
                DlssNr::NativeDx12Source::BeginOpaqueSr(*nativeSource,InFeatureHandle,InParameters):nullptr;
            // Account for the actual opaque entry through the original feature
            // owner before any foreign effect. A missing capture permanently
            // excludes a later claim of complete selected-output coverage.
            if(featureSnapshot&&(nativeSource||sourcePin||isSuperResolution))
            {
                if(nativeSource)
                {
                    if(!nativeSource->MarkOpaqueEntry())
                    {
                        srInput.Close();
                        DlssNr::NativeDx12Source::FinishReturn(*nativeSource,NVSDK_NGX_Result_FAIL_PlatformError);
                        return NVSDK_NGX_Result_FAIL_PlatformError;
                    }
                    if(!isSuperResolution||!opaqueSr||!opaqueSr->lifetimeRegistration)
                        HandleToFeature.MarkUntrackedEvaluation(handleId,featureSnapshot);
                }
                else
                {
                    if(sourcePin&&!sourcePin->MarkOpaqueEntry())
                    {
                        srInput.Close();
                        return NVSDK_NGX_Result_FAIL_PlatformError;
                    }
                    HandleToFeature.MarkUntrackedEvaluation(handleId,featureSnapshot);
                }
            }
            if(opaqueSr&&opaqueSr->receipt)opaqueSr->entered=opaqueSr->receipt->MarkEntered();
            auto characterFgToken=Neurotic::Semantic::Character::NativeFgWorkTracker::Token{};
            if(feature==NVSDK_NGX_Feature_FrameGeneration&&characterFgWork){
                characterFgToken=Neurotic::Semantic::Character::NativeFgWork().Begin(handleId,DlssNr::PreFg::NativeFgInstance(handleId));
                if(Neurotic::Semantic::Character::CharacterWorkerRequested())Neurotic::Semantic::Character::CharacterNativeFgStarted();
            }
            NVSDK_NGX_Result result =
                NVNGXProxy::D3D12_EvaluateFeature()(InCmdList, InFeatureHandle, InParameters, InCallback);
            if(characterFgToken.pending)Neurotic::Semantic::Character::NativeFgWork().Finish(characterFgToken,
                result==NVSDK_NGX_Result_Success,DlssNr::PreFg::NativeFgInstance(handleId),Neurotic::Semantic::Character::CharacterActivityNow());
            if (nativeSource && isSuperResolution && nrSettings)
            {
                DlssNr::NativeDx12Source::ObserveSrReturn(*nativeSource,InParameters,result);
                if(opaqueSr)DlssNr::NativeDx12Source::SealOpaqueSr(*nativeSource,*opaqueSr,
                    InFeatureHandle,InParameters,result);
                if(!srInput.Close())result=NVSDK_NGX_Result_FAIL_PlatformError;
                if(result==NVSDK_NGX_Result_Success&&!nrSettings->DlssNrRunBeforeSr.value_or_default())
                {
                    const auto nativeAfter=DlssNr::NativeDx12Source::RunBefore(*nativeSource,InParameters,*nrSettings);
                    if(!Neurotic::Protocol::NativeMayContinueOuterEvaluation(nativeAfter.facts))
                        result=NVSDK_NGX_Result_FAIL_PlatformError;
                }
            }
            if (consumerObservation)
            {
                if (result == NVSDK_NGX_Result_Success) consumerObservation->evaluated = true;
                else consumerObservation->failed = true;
            }
            if (feature == NVSDK_NGX_Feature_FrameGeneration && result != NVSDK_NGX_Result_Success)
            {
                DlssNr::PreFg::RevokeReadiness();
                if (fgCompletion.dependency.status) fgCompletion.dependency.status->Fail();
            }
            if (fgCompletion.result == DlssNr::PreFg::CompletionClaimResult::Ready &&
                fgGenerated >= 1 && fgGenerated <= 5)
            {
                static std::atomic<unsigned int> reports[5] {};
                const auto& dependency = fgCompletion.dependency;
                if (++reports[fgGenerated - 1] <= 12 || dependency.sequence % 600 == 0)
                    LOG_INFO("NR Present MFG handoff: generated={} index={} token={} sequence={} reservation={} "
                             "probe={} retiring={} result=0x{:X} bound={} applied={}; output cadence/release unmeasured",
                             fgGenerated, fgIndex, dependency.token, dependency.sequence, dependency.reservation,
                             dependency.kind == DlssNr::PreFg::CompletionKind::Probe, dependency.retiring,
                             static_cast<unsigned int>(result),
                             dependency.status->bound.load(), dependency.status->applied.load());
            }
            NR_FRAME_TRACE("ngx-native-return", "handle={} feature={} result={} list={:p}", handleId,
                static_cast<unsigned int>(feature), static_cast<unsigned int>(result), static_cast<void*>(InCmdList));

            DlssNr::RestoreAfterUpscale(InParameters);

            LOG_DEBUG("Native DLSS EvaluateFeature result: 0x{:X}", (uint32_t) result);

            // Neural Rendering runs over what the upscaler just wrote, on the same list, so frame
            // generation interpolates from enhanced frames and the model still costs one run per
            // rendered frame. The feature check is the point: frame generation is handed depth and
            // motion vectors too, and its handle can reach here because the branch above does not
            // return, so filtering on the parameter block alone would run the model twice a frame.
            if (result != NVSDK_NGX_Result_Success && isNrPipelineFeature)
            {
                const auto frame = DlssNr::PreFg::CurrentFrameIdentity(InCmdList);
                DlssNr::PresentGuides::Instance().RejectNative("Native SR/RR evaluation failed; no current guides",
                    frame.key, frame.providerGeneration);
            }
            if (result == NVSDK_NGX_Result_Success && isNrPipelineFeature && nrSettings &&
                !nrSettings->DlssNrNativeProtocol.value_or_default())
            {
                auto nativeInputs = DlssNr::NativeTemporalInputs::FromCreation(
                    featureSnapshot.originalCreation, handleId, featureSnapshot.generation);
                if(nativeInputs) {nativeInputs->alternateOriginal=alternateOriginal;nativeInputs->alternateSource=alternateSource;}
                if (!InvokeLegacyNativeNr(InCmdList, InParameters, *nrSettings, false, isRayReconstruction, [&] {
                    DlssNr::EvaluateAfterUpscale(InCmdList, InParameters, nullptr, isRayReconstruction, &*nrSettings,
                                                nativeInputs ? &*nativeInputs : nullptr);
                })) return rejectLegacyNr();
            }

            if(sourcePin&&alternateOriginal)HandleToFeature.CompleteTemporalSource(*sourcePin,alternateSource,
                result==NVSDK_NGX_Result_Success);
            // Close fallible observations and scoped resources before the sole
            // local-delivery commit. The next observable action is API return.
            opaqueSr.reset();consumerObservation.reset();
            if(nativeSource)
            {
                DlssNr::NativeDx12Source::FinishReturn(*nativeSource,result);
            }
            return result;
        }

        LOG_DEBUG("Native DLSS EvaluateFeature not available for handle {}", handleId);
        return NVSDK_NGX_Result_FAIL_FeatureNotFound;
    }

    // DLSSG replacements passthrough
    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None && handleId >= NVNGX_PROVIDER_ID_OFFSET)
    {
        LOG_DEBUG("Passthrough to DLSSG Replacement's EvaluateFeature for handle {}", handleId);
        return Nvngx_FG::D3D12_EvaluateFeature(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    std::optional<float> cameraNear, cameraFar;
    {
        std::lock_guard lock(fgObservationMutex);
        cameraNear = lastDlssgCameraNear;
        cameraFar = lastDlssgCameraFar;
    }
    if (cameraNear) InParameters->Set("DLSSG.CameraNear", *cameraNear);
    if (cameraFar) InParameters->Set("DLSSG.CameraFar", *cameraFar);

    // OptiScaler internal handling
    const auto nrContext = Dx12Contexts.find(handleId);
    auto* nrFeature = nrContext != Dx12Contexts.end() ? nrContext->second.feature.get() : nullptr;
    const bool authoritativeNativePreSr = nrFeature && DlssNr::NativeTemporalInputs::Authoritative(
        isSuperResolution, nrFeature->GetUpscalerType() == Upscaler::DLSS,
        nrFeature->Api() == API::DX12, nrContext->second.feature->IsWithDx12());
    NR_FRAME_TRACE("nr-native-authority", "handle={} generation={} authoritative={}",
        handleId, featureSnapshot.generation, authoritativeNativePreSr);
    std::optional<DlssNr::NativeTemporalInputs::Metadata> nativeInputs;
    if (authoritativeNativePreSr)
        nativeInputs = {static_cast<unsigned>(nrFeature->GetFeatureFlags()),
                        nrFeature->DisplayWidth(), nrFeature->DisplayHeight(), handleId, featureSnapshot.generation};
    if (isSuperResolution && nrSettings && !nrSettings->DlssNrNativeProtocol.value_or_default())
    {
        if (!InvokeLegacyNativeNr(InCmdList, InParameters, *nrSettings, true, false, [&] {
            DlssNr::EvaluateBeforeUpscale(InCmdList, InParameters, nullptr, &*nrSettings, authoritativeNativePreSr,
                                         nativeInputs ? &*nativeInputs : nullptr);
        })) return rejectLegacyNr();
    }

    DlssNr::NativeTemporalInputs::OutputEvaluation outputEvaluation;
    if(nativeInputs) {nativeInputs->alternateOriginal=alternateOriginal;nativeInputs->alternateSource=alternateSource;}
    const NVSDK_NGX_Result optiResult =
        TryEvaluateOptiFeature(InCmdList, InFeatureHandle, InParameters, InCallback, outputEvaluation);
    // Resolve RR metadata only for the backend which actually evaluated this
    // call, after any recreation/fallback. This never authorizes a pre-RR write.
    if (isRayReconstruction)
        nativeInputs = outputEvaluation.RayReconstructed()
            ? DlssNr::NativeTemporalInputs::FromCreation(
                featureSnapshot.originalCreation, handleId, featureSnapshot.generation)
            : std::nullopt;
    NR_FRAME_TRACE("ngx-replacement-return", "handle={} feature={} result={} list={:p}", handleId,
        static_cast<unsigned int>(feature), static_cast<unsigned int>(optiResult), static_cast<void*>(InCmdList));

    DlssNr::RestoreAfterUpscale(InParameters);

    if ((optiResult != NVSDK_NGX_Result_Success || !outputEvaluation.Succeeded()) && isNrPipelineFeature)
    {
        const auto frame = DlssNr::PreFg::CurrentFrameIdentity(InCmdList);
        DlssNr::PresentGuides::Instance().RejectNative("Selected SR/RR did not evaluate successfully; no current guides",
            frame.key, frame.providerGeneration);
    }

    // Same pass, for OptiScaler's own upscalers rather than native DLSS.
    if (optiResult == NVSDK_NGX_Result_Success && outputEvaluation.Succeeded() && isNrPipelineFeature && nrSettings &&
        !nrSettings->DlssNrNativeProtocol.value_or_default())
    {
        if (!InvokeLegacyNativeNr(InCmdList, InParameters, *nrSettings, false, isRayReconstruction, [&] {
            DlssNr::EvaluateAfterUpscale(InCmdList, InParameters, nullptr, isRayReconstruction, &*nrSettings,
                                        nativeInputs ? &*nativeInputs : nullptr);
        })) return rejectLegacyNr();
    }

    if(sourcePin&&alternateOriginal)HandleToFeature.CompleteTemporalSource(*sourcePin,alternateSource,
        optiResult==NVSDK_NGX_Result_Success&&outputEvaluation.Succeeded());
    if(nativeSource)DlssNr::NativeDx12Source::FinishReturn(*nativeSource,optiResult);
    return optiResult;
}

#pragma endregion

#pragma region DLSS Buffer Size Call

NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetScratchBufferSize(NVSDK_NGX_Feature InFeatureId,
                                                                    const NVSDK_NGX_Parameter* InParameters,
                                                                    size_t* OutSizeInBytes)
{
    if (Neurotic::Runtime::BootstrapUnavailable()) return NVSDK_NGX_Result_FAIL_PlatformError;
    Neurotic::Runtime::NgxCallLease vendorCall;
    if (!vendorCall) return NVSDK_NGX_Result_FAIL_PlatformError;
    if (OutSizeInBytes == nullptr)
        return NVSDK_NGX_Result_FAIL_InvalidParameter;

    if (State::Instance().activeFgNvngx != FGNvngxReplacement::None && InFeatureId == NVSDK_NGX_Feature_FrameGeneration)
    {
        return Nvngx_FG::D3D12_GetScratchBufferSize(InFeatureId, InParameters, OutSizeInBytes);
    }

    LOG_WARN("-> 52428800");
    *OutSizeInBytes = 52428800;
    return NVSDK_NGX_Result_Success;
}

#pragma endregion
