#pragma once

// Included once, by Streamline_Hooks.cpp. Observe only the game's interposer;
// OptiScaler's replacement providers retain their separate presentation owner.
#include "PreFg.h"
#include "DredDiagnostics.h"
#include "DlssNr_Present.h"
#include "DlssNr_PresentGuides.h"
#include "DlssNrFeature_Dx12.h"
#include "FrameTrace.h"
#include <nr/semantic/character/CharacterRuntime.h>
#include <menu/input/input_system.h>
#include <sl.h>
#include <sl_dlss_g.h>
#include <detours/detours.h>
#include <map>

namespace DlssNr::PreFg::Streamline
{
using Microsoft::WRL::ComPtr;
using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
using CreateFactory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
using CreateChain = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateHwnd = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Resize1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT,
    const UINT*, IUnknown* const*);
static HMODULE module = nullptr;
static std::recursive_mutex hookMutex;
static std::map<const void*, void*> targets;
static CreateFactory factory0 = nullptr, factory1 = nullptr;
static CreateFactory2 factory2 = nullptr;
static CreateChain createChain = nullptr;
static CreateHwnd createHwnd = nullptr;
static PresentFn present = nullptr;
static Present1Fn present1 = nullptr;
static ResizeFn resize = nullptr;
static Resize1Fn resize1 = nullptr;
static decltype(&slUpgradeInterface) upgrade = nullptr;
static decltype(&slGetNewFrameToken) getToken = nullptr;
static decltype(&slSetTagForFrame) setTags = nullptr;
static decltype(&slSetTag) setLegacyTags = nullptr;
static decltype(&slSetConstants) setConstants = nullptr;
static thread_local bool forwardingPresent = false;

inline void TraceLedger(const char* operation, uint32_t frame, uint32_t viewport,
                        Ledger::Snapshot before, Ledger::Snapshot after) noexcept
{
    NR_FRAME_TRACE("nr-ledger", "operation={} frame={} viewport={} beforeConstants={} beforeTags={} "
        "beforeConsumed={} constants={} tags={} consumed={} claims={} foreignConstants={} foreignTags={} legacy={}",
        operation, frame, viewport, before.constants, before.tags, before.consumed,
        after.constants, after.tags, after.consumed, after.sequence,
        after.foreignConstants, after.foreignTags, after.legacy);
}

inline bool FromInterposer(void* function)
{
    HMODULE owner = nullptr;
    return function && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(function), &owner) && owner == module;
}
template<class F> bool Attach(F& original, void* target, F hook)
{
    if (original) return targets[&original] == target;
    if (!FromInterposer(target)) return false;
    original = reinterpret_cast<F>(target);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    const LONG attached = DetourAttach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(hook));
    if (attached != NO_ERROR) { DetourTransactionAbort(); original = nullptr; return false; }
    if (DetourTransactionCommit() != NO_ERROR) { original = nullptr; return false; }
    targets[&original] = target;
    return true;
}

inline sl::Result Tags(const sl::FrameToken& frame, const sl::ViewportHandle& viewport,
    const sl::ResourceTag* tags, uint32_t count, sl::CommandBuffer* list)
{
    NR_FRAME_TRACE("nr-api", "api=tags phase=enter frame={} viewport={} count={} list={:p}",
        static_cast<uint32_t>(frame), static_cast<uint32_t>(viewport), count, static_cast<void*>(list));
    const auto result = setTags(frame, viewport, tags, count, list);
    NR_FRAME_TRACE("nr-api", "api=tags phase=return frame={} viewport={} result={}",
        static_cast<uint32_t>(frame), static_cast<uint32_t>(viewport), static_cast<unsigned int>(result));
    if (result == sl::Result::eOk && tags)
        for (uint32_t i = 0; i < count; ++i)
            if (tags[i].resource && tags[i].resource->native &&
                (tags[i].type == sl::kBufferTypeDepth || tags[i].type == sl::kBufferTypeMotionVectors ||
                 tags[i].type == sl::kBufferTypeHUDLessColor))
            { ObserveTags(static_cast<uint32_t>(frame), static_cast<uint32_t>(viewport)); break; }
    return result;
}
inline sl::Result Constants(const sl::Constants& values, const sl::FrameToken& frame,
    const sl::ViewportHandle& viewport)
{
    NR_FRAME_TRACE("nr-api", "api=constants phase=enter frame={} viewport={}",
        static_cast<uint32_t>(frame), static_cast<uint32_t>(viewport));
    const auto result = setConstants(values, frame, viewport);
    NR_FRAME_TRACE("nr-api", "api=constants phase=return frame={} viewport={} result={}",
        static_cast<uint32_t>(frame), static_cast<uint32_t>(viewport), static_cast<unsigned int>(result));
    if (result == sl::Result::eOk) ObserveConstants(static_cast<uint32_t>(frame), static_cast<uint32_t>(viewport));
    return result;
}
inline sl::Result LegacyTags(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags,
                            uint32_t count, sl::CommandBuffer* list)
{
    NR_FRAME_TRACE("nr-api", "api=legacy-tags phase=enter viewport={} count={} list={:p}",
        static_cast<uint32_t>(viewport), count, static_cast<void*>(list));
    const auto result = setLegacyTags(viewport, tags, count, list);
    NR_FRAME_TRACE("nr-api", "api=legacy-tags phase=return viewport={} result={}",
        static_cast<uint32_t>(viewport), static_cast<unsigned int>(result));
    if (result == sl::Result::eOk && tags)
        for (uint32_t i = 0; i < count; ++i)
            if (tags[i].resource && tags[i].resource->native &&
                (tags[i].type == sl::kBufferTypeDepth || tags[i].type == sl::kBufferTypeMotionVectors ||
                 tags[i].type == sl::kBufferTypeHUDLessColor))
            { ObserveLegacyTags(static_cast<uint32_t>(viewport)); break; }
    return result;
}
inline bool PrepareFullFrame(const Frame& frame)
{
    const auto provider = Provider();
    const auto native = NativeFg();
    if (!provider.known || !provider.enabled || !provider.supported ||
        provider.generation != frame.providerGeneration || native.active != 1 ||
        native.generation != frame.nativeFgGeneration || native.instance != frame.nativeFgInstance ||
        (frame.readiness && frame.readinessIdentity.invalidation != State().readinessEpoch.load())) return false;
    sl::ResourceTag fullFrame[] = {
        {nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr, sl::kBufferTypeUIAlpha, sl::ResourceLifecycle::eValidUntilPresent}};
    if (frame.legacyTags)
        return setLegacyTags && setLegacyTags(sl::ViewportHandle(0), fullFrame, 3, nullptr) == sl::Result::eOk;
    const uint32_t id = static_cast<uint32_t>(frame.key - 1);
    sl::FrameToken* token = nullptr;
    return getToken && setTags && getToken(token, &id) == sl::Result::eOk && token &&
        static_cast<uint32_t>(*token) == id &&
        setTags(*token, sl::ViewportHandle(0), fullFrame, 3, nullptr) == sl::Result::eOk;
}

inline HRESULT Dispatch(IDXGISwapChain* chain, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters,
                        bool usePresent1)
{
    const auto forward = [&]() -> HRESULT {
        return usePresent1 ? present1(static_cast<IDXGISwapChain1*>(chain), sync, flags, parameters) :
                             present(chain, sync, flags);
    };
    auto owner = GetOwner(chain);
    if (!owner || forwardingPresent || (flags & DXGI_PRESENT_TEST) != 0 || ::State::Instance().isShuttingDown)
        return forward();
    std::unique_lock ownerLock(owner->presentationMutex, std::try_to_lock);
    if (!ownerLock.owns_lock())
    {
        RevokeReadiness();
        ++State().rejected;
        NR_FRAME_TRACE("nr-before-fg-refused", "reason=concurrent-application-present");
        return forward();
    }
    struct ForwardScope { ForwardScope() { forwardingPresent = true; } ~ForwardScope() { forwardingPresent = false; } } scope;
    auto* config = Config::Instance();
    // Observe the application's real image before the original interposer Present
    // generates any outputs. Keep this independent of the selected NR method.
    if (Neurotic::Semantic::Character::CharacterWorkerRequested())
    {
        ComPtr<IDXGISwapChain3> inspectorChain;
        if (owner->queue && State().swapchains == 1 &&
            SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&inspectorChain))))
        {
            const auto inspectorProvider = Provider();
            Neurotic::Semantic::Character::CharacterPresent(inspectorChain.Get(), owner->queue.Get(),
                inspectorProvider.known && inspectorProvider.enabled, OptiInput::IsFocused(),
                Neurotic::Semantic::Character::ReadSettings(*config), true,
                reinterpret_cast<std::uintptr_t>(owner.Get()), true, PresentFrame());
        }
        else Neurotic::Semantic::Character::CharacterSourceInvalidated();
    }
    const auto runtime = config->GetDlssNrRuntimeSnapshot();
    const unsigned int route = config->DlssNrRoute.value_or_default();
    const bool requested = runtime.enabled && (route == 1 || route == 2);
    if (!requested)
    {
        // Native/Off forwards without Present admission, buffer queries or probes.
        // Retire the previous Present policy once; its GPU dependencies keep their
        // own lifetime until completion even after the visible policy is disabled.
        if (owner->presentPolicyActive)
        {
            StopConsumerObservation();
            owner->startup.Reset();
            CloseCompletionAdmission();
            PresentGuides::Instance().Enable(false);
            {
                std::lock_guard lock(State().mutex);
                State().ledger.Reset();
            }
            owner->presentPolicyActive = false;
            owner->previousPresentMs = 0.0;
            // Run the inherited exit cleanup once to disable guide observation,
            // invalidate the old Present history and clear its UI telemetry.
            const double exitStart = Util::MillisecondsNow();
            auto identity = EvaluatePresentImageOnly(chain, owner->queue.Get(), flags, parameters, nullptr);
            const double beforeForward = Util::MillisecondsNow();
            const HRESULT result = forward();
            const double end = Util::MillisecondsNow();
            ReportPresentCallTiming({identity, 0.0, beforeForward - exitStart,
                end - exitStart, end - beforeForward, result, false, 0});
            return result;
        }
        if (AdvisorSampling::ObserveNativeCadence(runtime.enabled, route, State().swapchains))
        {
            // Advisor-only native cadence. No Present NR admission, resource queries,
            // probes or FG input changes; ordinary Native/Off retains its fast exit.
            const double start = Util::MillisecondsNow();
            const double interval = owner->previousPresentMs ? start - owner->previousPresentMs : 0.0;
            owner->previousPresentMs = start;
            const auto provider = Provider();
            PresentCallIdentity identity {};
            identity.pacing.route = PresentPacing::Route::NativeTemporal;
            identity.advisorConfigurationGeneration = AdvisorSampling::ConfigurationGeneration.load();
            const HRESULT result = forward();
            const double end = Util::MillisecondsNow();
            ReportPresentCallTiming({identity, interval, 0.0, end - start, end - start, result,
                provider.known && !provider.enabled && State().swapchains == 1, provider.generation});
            return result;
        }
        owner->previousPresentMs = 0.0;
        return forward();
    }
    owner->presentPolicyActive = true;
    const double start = Util::MillisecondsNow();
    const auto previousReadiness = owner->startup.State();
    const double interval = owner->previousPresentMs != 0.0 ? start - owner->previousPresentMs : 0.0;
    owner->previousPresentMs = start;
    const auto provider = Provider();
    const bool fg = provider.known ? provider.enabled : ::State::Instance().dlssgLastSetMode != sl::DLSSGMode::eOff;
    State().observeConsumer = fg;
    if (owner->lastFg != fg || owner->providerGeneration != provider.generation)
    {
        std::lock_guard lock(State().mutex);
        State().ledger.Reset();
        owner->lastFg = fg;
        owner->providerGeneration = provider.generation;
        owner->startup.Reset();
        owner->providerChangedMs = start;
    }
    auto frame = Claim(fg);
    if (State().swapchains != 1) owner->startup.Reset();
    frame.providerGeneration = provider.generation;
    frame.diagnosticClaim = FgLifecycle::Read();
    const auto nativeFg = NativeFg();
    frame.nativeFgGeneration = nativeFg.generation;
    frame.nativeFgInstance = nativeFg.instance;
    NR_FRAME_TRACE("fg-frame-claim", "generation={} instance={} providerGeneration={} token={} sequence={} "
        "swapchain={:p} queue={:p}", frame.diagnosticClaim.generation, frame.diagnosticClaim.instance,
        provider.generation, frame.key, frame.sequence, static_cast<void*>(chain), static_cast<void*>(owner->queue.Get()));
    if (!fg || route != owner->startupRoute || runtime.resumeGeneration != owner->startupResume)
        owner->startup.Reset();
    owner->startupRoute = route;
    owner->startupResume = runtime.resumeGeneration;
    if (fg)
    {
        frame.readiness = &owner->startup;
        frame.allowOutput = false; // decided against exact resources inside evaluation
    }
    if (fg)
    {
        if (!ConsumerReady(frame))
        {
            frame.valid = false;
            frame.refusal = "FG execution path not ready; NR paused while original game frames continue";
            owner->startup.Reset();
        }
        if (!provider.known || !provider.supported || ::State::Instance().activeFgOutput != FGOutput::NoFG ||
            ::State::Instance().dlssgLastSetMode != sl::DLSSGMode::eOn ||
            provider.observedGenerated < 1 || provider.observedGenerated > 5 ||
            nativeFg.active != 1 || !nativeFg.instance)
        {
            frame.valid = false;
            frame.refusal = "Present FG needs an observed fixed 2x-6x native provider and supported queue/UI settings";
            owner->startup.Reset();
        }
        if (frame.valid)
        {
            ComPtr<IDXGISwapChain3> chain3;
            ComPtr<ID3D12Resource> backbuffer;
            if (FAILED(chain->QueryInterface(IID_PPV_ARGS(&chain3))) ||
                FAILED(chain3->GetBuffer(chain3->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&backbuffer))))
            {
                frame.valid = false;
                frame.refusal = "Could not identify the native FG backbuffer for this Present";
                owner->startup.Reset();
            }
            else
            {
                const auto nativeBuffer = NativeIdentity::Resolve<ID3D12Resource>(backbuffer.Get());
                frame.outputResource = nativeBuffer.object.Get();
                if (!frame.outputResource)
                {
                    frame.valid = false;
                    frame.refusal = "Could not identify the native FG backbuffer for this Present";
                    owner->startup.Reset();
                }
            }
        }
        // Called only after successful NR model recording, before any backbuffer
        // copyback. Refused/missing-guide frames retain the game's original FG tags.
        if (frame.valid) frame.prepareInputs = &PrepareFullFrame;
    }
    // FG off uses the existing admission contract. Native mode still performs no Present model work.
    auto identity = EvaluatePresentImageOnly(chain, owner->queue.Get(), flags, parameters, fg ? &frame : nullptr);
    if (frame.completionReservation && !identity.copybackSubmitted && !identity.probeSubmitted)
        CancelCompletion(frame.completionReservation);
    bool completionPublished = false;
    if ((identity.completedOutput || identity.probeSubmitted) && frame.completionReservation)
    {
        const auto nativeOutput = NativeIdentity::Resolve<ID3D12Resource>(identity.outputResource);
        completionPublished = nativeOutput.object.Get() == frame.outputResource &&
            CommitCompletion(frame.completionReservation, frame.outputResource,
                             identity.completionFence.Get(), identity.completionValue,
                             identity.probeSubmitted ? CompletionKind::Probe : CompletionKind::Output);
        if (identity.probeSubmitted)
        {
            ++owner->probes;
            owner->probeSubmittedMs = Util::MillisecondsNow();
            if (!completionPublished || !owner->startup.Submit(frame.readinessIdentity,
                    identity.completionFence.Get(), identity.completionValue, frame.sequence,
                    CompletionStatus(frame.completionReservation)))
            { owner->startup.Reset(); RevokeReadiness(); }
        }
    }
    frame.outputSubmitted = identity.completedOutput && (!fg || completionPublished);
    // A metadata-only refusal forwards the untouched image. Completed packets
    // from older Presents must not turn that refusal into a duplicate FG claim.
    // The one pending probe retains its packet until its exact acknowledgement.
    if (!frame.outputSubmitted && !owner->startup.Pending()) RetireFrameCompletions(frame.sequence);
    const bool unsafeHandoff = fg && identity.copybackSubmitted && !completionPublished;
    if (unsafeHandoff)
        NR_FRAME_TRACE("nr-fg-handoff-refused", "reason=completion-publish-failed token={} sequence={} "
            "resource={:p} identityResource={:p} fence={:p} value={}", frame.key, frame.sequence,
            static_cast<void*>(frame.outputResource), static_cast<void*>(identity.outputResource),
            static_cast<void*>(identity.completionFence.Get()), identity.completionValue);
    if (identity.completedOutput)
        FgLifecycle::Output(frame.diagnosticClaim, provider.generation, frame.key, chain, owner->queue.Get());
    const double beforeProvider = Util::MillisecondsNow();
    if (frame.outputSubmitted) ++State().submitted;
    else if (requested) ++State().rejected;
    NR_FRAME_TRACE("nr-before-fg-forward", "provider=game-streamline realSequence={} token={} valid={} "
        "outputSubmitted={} swapchain={:p} queue={:p} policy=full-frame-hud-included",
        frame.sequence, frame.key, frame.valid, frame.outputSubmitted,
        static_cast<void*>(chain), static_cast<void*>(owner->queue.Get()));
    // The NR adapter submitted on the application creation queue. Native DLSSG
    // claims the exact-backbuffer completion fence and binds it to its provider
    // command list before that list reaches the provider-owned queue.
    ForwardFrame providerScope(frame);
    // Copyback may already be on the GPU even if its signal/publication failed.
    // Do not enter a provider Present with an unorderable modified input.
    const HRESULT result = unsafeHandoff ? E_FAIL : forward();
    if (FAILED(result) || unsafeHandoff)
    {
        owner->startup.Reset();
        ResetCompletions();
    }
    FgLifecycle::Present(frame.diagnosticClaim, provider.generation, frame.key, chain, owner->queue.Get(),
        frame.outputSubmitted, result);
    if (DredDiagnostics::Enabled() && FAILED(result) && owner->queue)
    {
        ComPtr<ID3D12Device> faultDevice;
        if (SUCCEEDED(owner->queue->GetDevice(IID_PPV_ARGS(&faultDevice))))
            DredDiagnostics::Collect(faultDevice.Get(), faultDevice->GetDeviceRemovedReason());
    }
    if (fg && requested)
    {
        ComPtr<ID3D12Device> readinessDevice;
        if (!owner->queue || FAILED(owner->queue->GetDevice(IID_PPV_ARGS(&readinessDevice))) ||
            readinessDevice->GetDeviceRemovedReason() != S_OK)
        { owner->startup.Reset(); ResetCompletions(); }
        owner->startup.Presented(frame.sequence, SUCCEEDED(result));
        owner->startup.CheckEpoch(State().readinessEpoch.load());
        if (previousReadiness != owner->startup.State())
        {
            const auto transition = ++owner->readinessTransitions;
            if (transition <= 16 || transition % 120 == 0)
                LOG_INFO("NR readiness: phase={} reason={} token={} sequence={} probes={} transitions={} "
                    "providerAgeMs={:.3f} probeAgeMs={:.3f} output={}",
                    static_cast<unsigned int>(owner->startup.State()), owner->startup.Reason(),
                    frame.key, frame.sequence, owner->probes, transition,
                    start - owner->providerChangedMs, Util::MillisecondsNow() - owner->probeSubmittedMs,
                    frame.outputSubmitted);
        }
        NR_FRAME_TRACE("nr-startup-admission", "token={} modelPrepared={} outputAllowed={} outputSubmitted={} "
            "probeSubmitted={} phase={} epoch={}", frame.key, identity.modelPrepared, frame.allowOutput,
            frame.outputSubmitted, identity.probeSubmitted, static_cast<unsigned int>(owner->startup.State()),
            State().readinessEpoch.load());
    }
    const double end = Util::MillisecondsNow();
    ReportPresentCallTiming({identity, interval, beforeProvider - start, end - start, end - beforeProvider, result,
        provider.known && (!fg || frame.valid), provider.generation});
    if (frame.sequence <= 4 || frame.sequence % 120 == 0)
        LOG_INFO("NR pre-FG: real={} submitted={} bypassedOutputs={} rejected={} token={} source={} result={:X}",
            State().realCalls.load(), State().submitted.load(), State().bypassed.load(), State().rejected.load(),
            frame.key, frame.outputSubmitted ? "NR output" : "original", static_cast<unsigned int>(result));
    return result;
}
inline HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* chain, UINT sync, UINT flags)
{ return Dispatch(chain, sync, flags, nullptr, false); }
inline HRESULT STDMETHODCALLTYPE Present1(IDXGISwapChain1* chain, UINT sync, UINT flags,
                                         const DXGI_PRESENT_PARAMETERS* parameters)
{ return Dispatch(chain, sync, flags, parameters, true); }
inline void Invalidate()
{
    std::lock_guard lock(State().mutex);
    RevokeReadiness();
    State().ledger.Reset();
    State().completions.Reset();
}
inline HRESULT STDMETHODCALLTYPE Resize(IDXGISwapChain* chain, UINT count, UINT width, UINT height,
                                        DXGI_FORMAT format, UINT flags)
{
    NR_FG_EVENT("resize-begin", "swapchain={:p} count={} width={} height={} api=ResizeBuffers",
        static_cast<void*>(chain), count, width, height);
    auto owner = GetOwner(chain);
    std::unique_lock<std::recursive_mutex> ownerLock;
    if (owner) ownerLock = std::unique_lock(owner->presentationMutex);
    if (!Neurotic::Semantic::Character::CharacterBeforeResize()) return DXGI_ERROR_WAS_STILL_DRAWING;
    if (owner) owner->startup.Reset();
    Invalidate();
    PresentGuides::Instance().Enable(false);
    ReportPresentUnavailable(PresentApi::D3D12, "Present target changed");
    const auto result = resize(chain, count, width, height, format, flags);
    NR_FG_EVENT("resize-end", "swapchain={:p} result={} api=ResizeBuffers", static_cast<void*>(chain),
        static_cast<uint32_t>(result));
    return result;
}
inline HRESULT STDMETHODCALLTYPE Resize1(IDXGISwapChain3* chain, UINT count, UINT width, UINT height,
    DXGI_FORMAT format, UINT flags, const UINT* masks, IUnknown* const* queues)
{
    NR_FG_EVENT("resize-begin", "swapchain={:p} count={} width={} height={} queueArray={:p} api=ResizeBuffers1",
        static_cast<void*>(chain), count, width, height, static_cast<const void*>(queues));
    auto owner = GetOwner(chain);
    std::unique_lock<std::recursive_mutex> ownerLock;
    if (owner) ownerLock = std::unique_lock(owner->presentationMutex);
    if (!Neurotic::Semantic::Character::CharacterBeforeResize()) return DXGI_ERROR_WAS_STILL_DRAWING;
    if (owner) owner->startup.Reset();
    Invalidate();
    PresentGuides::Instance().Enable(false);
    ReportPresentUnavailable(PresentApi::D3D12, "Present target changed");
    const HRESULT result = resize1(chain, count, width, height, format, flags, masks, queues);
    NR_FG_EVENT("resize-end", "swapchain={:p} result={} api=ResizeBuffers1", static_cast<void*>(chain),
        static_cast<uint32_t>(result));
    // Per-buffer queue changes need a new adapter contract. Stop NR; never retain a guessed queue.
    if (SUCCEEDED(result) && queues && owner) owner->queue.Reset();
    return result;
}
inline void HookChain(IDXGISwapChain* chain, IUnknown* device)
{
    if (!chain) return;
    std::lock_guard lock(hookMutex);
    ComPtr<ID3D12CommandQueue> queue;
    if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&queue)))) return;
    auto** table = *reinterpret_cast<void***>(chain);
    if (!Attach(present, table[8], &Present)) return;
    ComPtr<IDXGISwapChain3> extended;
    if (FAILED(chain->QueryInterface(IID_PPV_ARGS(&extended)))) return;
    auto** table3 = *reinterpret_cast<void***>(extended.Get());
    if (!Attach(present1, table3[22], &Present1) || !Attach(resize, table[13], &Resize)) return;
    ComPtr<IDXGISwapChain4> fourth;
    if (SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&fourth))))
    {
        auto** table4 = *reinterpret_cast<void***>(fourth.Get());
        if (!Attach(resize1, table4[39], &Resize1)) return;
    }
    if (Register(chain, queue.Get()))
    {
        NR_FG_EVENT("swapchain-register", "swapchain={:p} creationQueue={:p} nativeQueue={:p}",
            static_cast<void*>(chain), static_cast<void*>(queue.Get()), static_cast<void*>(GetOwner(chain)->queue.Get()));
        LOG_INFO("NR pre-FG: registered Streamline application Present, swapchain={:p} creationQueue={:p} nativeQueue={:p}",
            static_cast<void*>(chain), static_cast<void*>(queue.Get()),
            static_cast<void*>(GetOwner(chain)->queue.Get()));
    }
}
inline HRESULT STDMETHODCALLTYPE Chain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
                                      IDXGISwapChain** chain)
{
    const HRESULT result = createChain(factory, device, desc, chain);
    if (SUCCEEDED(result) && chain) HookChain(*chain, device);
    return result;
}
inline HRESULT STDMETHODCALLTYPE Hwnd(IDXGIFactory2* factory, IUnknown* device, HWND window,
    const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
    IDXGIOutput* output, IDXGISwapChain1** chain)
{
    const HRESULT result = createHwnd(factory, device, window, desc, fullscreen, output, chain);
    if (SUCCEEDED(result) && chain) HookChain(*chain, device);
    return result;
}
inline void HookFactory(IUnknown* factory)
{
    if (!factory) return;
    std::lock_guard lock(hookMutex);
    ComPtr<IDXGIFactory2> extended;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&extended)))) return;
    auto** table = *reinterpret_cast<void***>(extended.Get());
    Attach(createChain, table[10], &Chain);
    Attach(createHwnd, table[15], &Hwnd);
}
inline HRESULT WINAPI Factory0(REFIID id, void** out)
{
    const HRESULT result = factory0(id, out);
    if (SUCCEEDED(result) && out) HookFactory(static_cast<IUnknown*>(*out));
    return result;
}
inline HRESULT WINAPI Factory1(REFIID id, void** out)
{
    const HRESULT result = factory1(id, out);
    if (SUCCEEDED(result) && out) HookFactory(static_cast<IUnknown*>(*out));
    return result;
}
inline HRESULT WINAPI Factory2(UINT flags, REFIID id, void** out)
{
    const HRESULT result = factory2(flags, id, out);
    if (SUCCEEDED(result) && out) HookFactory(static_cast<IUnknown*>(*out));
    return result;
}
inline sl::Result Upgrade(void** object)
{
    const auto result = upgrade(object);
    if (result == sl::Result::eOk && object && *object)
    {
        auto* unknown = static_cast<IUnknown*>(*object);
        HookFactory(unknown);
        ComPtr<IDXGISwapChain> chain;
        if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&chain))))
        {
            ComPtr<ID3D12CommandQueue> queue;
            UINT bytes = sizeof(ID3D12CommandQueue*);
            if (SUCCEEDED(chain->GetPrivateData(creationQueueKey, &bytes, queue.GetAddressOf())))
                HookChain(chain.Get(), queue.Get());
        }
    }
    return result;
}
inline void Uninstall()
{
    std::lock_guard lock(hookMutex);
    if (!module) return;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    auto detach = [](auto& original, auto hook) {
        if (original) DetourDetach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(hook));
    };
    detach(factory0, &Factory0); detach(factory1, &Factory1); detach(factory2, &Factory2);
    detach(createChain, &Chain); detach(createHwnd, &Hwnd);
    detach(present, &Present); detach(present1, &Present1);
    detach(resize, &Resize); detach(resize1, &Resize1);
    detach(upgrade, &Upgrade); detach(setTags, &Tags); detach(setLegacyTags, &LegacyTags); detach(setConstants, &Constants);
    if (DetourTransactionCommit() == NO_ERROR)
    {
        ledgerObserver.store(nullptr, std::memory_order_relaxed);
        factory0 = factory1 = nullptr; factory2 = nullptr;
        createChain = nullptr; createHwnd = nullptr;
        present = nullptr; present1 = nullptr; resize = nullptr; resize1 = nullptr;
        upgrade = nullptr; setTags = nullptr; setLegacyTags = nullptr; setConstants = nullptr; getToken = nullptr;
        module = nullptr; targets.clear(); Invalidate();
        std::lock_guard stateLock(State().mutex);
        State().provider = {};
    }
    else LOG_ERROR("NR pre-FG: could not detach Streamline adapter");
}
inline void Install(HMODULE interposer)
{
    std::lock_guard lock(hookMutex);
    if (module) return;
    module = interposer;
    if (FrameTrace::AssociationRequested()) ledgerObserver.store(&TraceLedger, std::memory_order_relaxed);
    getToken = reinterpret_cast<decltype(getToken)>(KernelBaseProxy::GetProcAddress_()(module, "slGetNewFrameToken"));
    auto address = [](const char* name) { return reinterpret_cast<void*>(KernelBaseProxy::GetProcAddress_()(module, name)); };
    // Attach after the existing OptiScaler hooks: each trampoline preserves them.
    Attach(setTags, address("slSetTagForFrame"), &Tags);
    Attach(setLegacyTags, address("slSetTag"), &LegacyTags);
    Attach(setConstants, address("slSetConstants"), &Constants);
    Attach(upgrade, address("slUpgradeInterface"), &Upgrade);
    Attach(factory0, address("CreateDXGIFactory"), &Factory0);
    Attach(factory1, address("CreateDXGIFactory1"), &Factory1);
    Attach(factory2, address("CreateDXGIFactory2"), &Factory2);
    LOG_INFO("NR pre-FG: Streamline adapter installed (frame tags={}, constants={}, upgrade={}, factories={}/{}/{})",
        setTags != nullptr, setConstants != nullptr, upgrade != nullptr,
        factory0 != nullptr, factory1 != nullptr, factory2 != nullptr);
}
}
