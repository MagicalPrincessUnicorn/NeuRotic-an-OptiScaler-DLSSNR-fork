#include <menu/Localization.h>
#include <optional>
#include "PresentFgCopies.h"
#pragma once

// Included once, by Streamline_Hooks.cpp. Observe only the game's interposer;
// OptiScaler's replacement providers retain their separate presentation owner.
#include "PreFg.h"
#include "NativeFgTrace.h"
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
#include <objbase.h>
#pragma comment(lib, "ole32.lib")

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
static std::atomic<sl::RenderAPI> observedApi {sl::RenderAPI::eCount};
// Device-owned scalar metadata deduplicates repeats without retaining a device
// or caching an address that could be reused after ordinary device recovery.
inline constexpr GUID deviceCookieKey =
    {0x194e5d60, 0xba26, 0x488e, {0xb1,0x7f,0xcb,0x1d,0xdf,0x67,0x3a,0x81}};
static GUID observedDeviceCookie {};
inline void Initialized(sl::RenderAPI api)
{
    std::lock_guard lock(hookMutex);
    observedDeviceCookie = {};
    observedApi = api;
    ResetProvider();
}
// Accepted typed API proof is independent of optional identity deduplication.
// The caller holds hookMutex through the companion hook API update.
inline bool AcceptedD3D12Device(HMODULE interposer, IUnknown* identity, HRESULT& cookieResult)
{
    std::lock_guard lock(hookMutex);
    const auto api = observedApi.load();
    if (!interposer || module != interposer ||
        (api != sl::RenderAPI::eCount && api != sl::RenderAPI::eD3D12)) return false;
    GUID cookie {};
    cookieResult = E_NOINTERFACE;
    if (identity)
    {
        ComPtr<ID3D12Object> metadata;
        cookieResult = identity->QueryInterface(IID_PPV_ARGS(&metadata));
        if (SUCCEEDED(cookieResult) && metadata)
        {
            UINT bytes = sizeof(cookie);
            cookieResult = metadata->GetPrivateData(deviceCookieKey, &bytes, &cookie);
            if (cookieResult == DXGI_ERROR_NOT_FOUND)
            {
                cookieResult = CoCreateGuid(&cookie);
                if (SUCCEEDED(cookieResult))
                    cookieResult = metadata->SetPrivateData(deviceCookieKey, sizeof(cookie), &cookie);
            }
            else if (SUCCEEDED(cookieResult) &&
                     (bytes != sizeof(cookie) || IsEqualGUID(cookie, GUID {})))
                cookieResult = E_UNEXPECTED;
        }
    }
    if (SUCCEEDED(cookieResult) && IsEqualGUID(cookie, GUID {})) cookieResult = E_UNEXPECTED;
    if (SUCCEEDED(cookieResult) && api == sl::RenderAPI::eD3D12 &&
        IsEqualGUID(observedDeviceCookie, cookie)) return true;
    // Metadata failure is not an API admission gate. Without reliable dedupe,
    // invalidate stale options/readiness instead of borrowing old-device state.
    observedDeviceCookie = SUCCEEDED(cookieResult) ? cookie : GUID {};
    observedApi = sl::RenderAPI::eD3D12;
    ResetProvider();
    return true;
}

struct AcceptedTags
{
    uint64_t outputEpoch = OutputIdentity::Current();
    bool inputs = false;
    UiPlanes::Batch uiPlanes {};
    TaggedOutput output {};
    // Scalar decoding attribution, outside TaggedOutput equality/admission.
    const char* refusal = nullptr;
    uint32_t diagnosticIndex = 0;
    size_t tagVersion = 0;
    uint32_t lifecycle = 0;
    bool diagnosticKnown = false, lifecycleKnown = false, resolveAttempted = false;
    HRESULT resolveResult = S_OK;
};
inline AcceptedTags ReadAcceptedTags(const sl::ResourceTag* tags, uint32_t count, sl::RenderAPI entryApi)
{
    AcceptedTags accepted;
    // Vulkan native handles are not COM objects. Decode only the actual API
    // selected by a successful initialization of this interposer.
    if (entryApi != sl::RenderAPI::eD3D12 || !tags) return accepted;
    accepted.uiPlanes = UiPlanes::Capture(tags, count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& tag = tags[i];
        if (tag.type == sl::kBufferTypeBackbuffer)
        {
            TaggedOutput output;
            output.declared = true;
            output.outputEpoch = accepted.outputEpoch;
            output.left = tag.extent.left; output.top = tag.extent.top;
            output.width = tag.extent.width; output.height = tag.extent.height;
            const auto tagVersion = tag.structVersion;
            output.unresolved = tagVersion != sl::kStructVersion1;
            const char* refusal = output.unresolved ? "tag-version" : nullptr;
            bool lifecycleKnown = false, resolveAttempted = false;
            unsigned lifecycle = 0;
            HRESULT resolveResult = S_OK;
            // DLSS-G permits extent-only backbuffer tags with a null Resource*
            // and default lifecycle. The actual resource still comes from this
            // Present's swapchain. Only resource-bearing tags need lifetime proof.
            if (!output.unresolved && tag.resource)
            {
                // This tag identifies output; no resource contents are read or
                // retained. Only transient native/QI/private-data references exist.
                lifecycle = static_cast<unsigned>(tag.lifecycle);
                lifecycleKnown = true;
                output.unresolved = !tag.resource->native;
                if (output.unresolved) refusal = "missing-native-pointer";
                if (!output.unresolved)
                {
                    const auto native = NativeIdentity::Resolve<ID3D12Resource>(
                        reinterpret_cast<IUnknown*>(tag.resource->native));
                    resolveAttempted = true; resolveResult = native.result;
                    output.unresolved = !native.object;
                    if (output.unresolved) refusal = "native-interface";
                    else
                    {
                        const auto observed = OutputIdentity::Observe(native.object.Get(), true);
                        output.resource = observed.canonical;
                        output.identity = observed.identity;
                        output.unresolved = FAILED(observed.result) || !observed.identity.HasToken();
                        resolveResult = observed.result;
                        refusal = observed.refusal;
                        // Preserve the already established SDK lifecycle1 route
                        // if optional metadata service is unavailable. A corrupt,
                        // dead/replaced or foreign cookie is not this fallback.
                        if (output.unresolved && observed.optionalUnavailable && observed.canonical &&
                            tag.lifecycle == sl::ResourceLifecycle::eValidUntilPresent)
                        { output.untilPresentPromise = true; output.unresolved = false; refusal = nullptr; }
                    }
                }
            }
            if (accepted.output.declared)
            {
                const auto& previous = accepted.output;
                // Conflict concerns the supplied declaration, not whether optional
                // metadata recovered between two otherwise identical observations.
                const bool sameDeclaration = !previous.unresolved && !output.unresolved &&
                    previous.resource == output.resource && previous.outputEpoch == output.outputEpoch &&
                    previous.left == output.left && previous.top == output.top &&
                    previous.width == output.width && previous.height == output.height;
                // Two actual tokens still distinguish replacement/ABA at one address.
                const bool differentInstance = previous.identity.HasToken() && output.identity.HasToken() &&
                    previous.identity != output.identity;
                if (!sameDeclaration || differentInstance) output.ambiguous = true;
                else if (previous.identity.HasToken() && !output.identity.HasToken())
                {
                    output.identity = previous.identity;
                    output.untilPresentPromise = false;
                }
            }
            output.ambiguous |= accepted.output.ambiguous;
            accepted.output = output;
            if (!accepted.diagnosticKnown || (!accepted.refusal && (refusal || output.ambiguous)))
            {
                accepted.diagnosticKnown = true; accepted.diagnosticIndex = i;
                accepted.tagVersion = tagVersion; accepted.lifecycle = lifecycle;
                accepted.lifecycleKnown = lifecycleKnown; accepted.resolveAttempted = resolveAttempted;
                accepted.resolveResult = resolveResult;
                accepted.refusal = refusal ? refusal : output.ambiguous ? "conflicting-tags" : nullptr;
            }
#if defined(NR_FRAME_TRACE)
            NR_FRAME_TRACE("nr-api","api=fg-backbuffer-tag version={} lifecycle={} descriptor={:p} "
                "resolved={:X} unresolved={} ambiguous={} left={} top={} width={} height={}",
                tag.structVersion,static_cast<unsigned>(tag.lifecycle),static_cast<void*>(tag.resource),
                output.resource,output.unresolved,output.ambiguous,
                output.left,output.top,output.width,output.height);
#endif
        }
        else if (tag.resource && tag.resource->native &&
            (tag.type == sl::kBufferTypeDepth || tag.type == sl::kBufferTypeMotionVectors ||
             tag.type == sl::kBufferTypeHUDLessColor)) accepted.inputs = true;
    }
    return accepted;
}

// This is decoding evidence, not a successful frame/output or resource-lifetime promise.
inline void TraceAcceptedTags(const char* api, const AcceptedTags& accepted, uint32_t frame,
    uint32_t viewport, uint64_t providerGeneration, sl::RenderAPI entryApi, sl::Result originalResult) noexcept
{
    if (loggerClosed.load(std::memory_order_relaxed) || !spdlog::should_log(spdlog::level::trace)) return;
    static std::atomic<unsigned> acceptedReceipts {0}, refusedReceipts {0};
    const bool refused = accepted.refusal || accepted.output.unresolved || accepted.output.ambiguous || originalResult != sl::Result::eOk;
    auto& receipts = refused ? refusedReceipts : acceptedReceipts;
    const unsigned limit = refused ? 56u : 8u;
    auto next = receipts.load(std::memory_order_relaxed);
    while (next < limit && !receipts.compare_exchange_weak(next, next + 1, std::memory_order_relaxed)) {}
    if (next >= limit) return;
    const DWORD originalError = GetLastError();
    try
    {
        LOG_TRACE("NR FG tag decode: api={} result={} entryApi={} providerGeneration={} frame={} "
            "viewport={} declared={} unresolved={} ambiguous={} firstDecodeReason={} diagnosticTagIndex={} diagnosticTagVersion={} "
            "lifecycleKnown={} lifecycle={} resolveAttempted={} resolveResult={:X} finalTaggedResource={:X} "
            "outputEpoch={} epochCurrent={} identityToken={} untilPresentPromise={} "
            "left={} top={} width={} height={} inputs={} observationOnly=true",
            api, static_cast<unsigned>(originalResult), static_cast<unsigned>(entryApi), providerGeneration,
            frame, viewport, accepted.output.declared, accepted.output.unresolved, accepted.output.ambiguous,
            accepted.refusal ? accepted.refusal : accepted.diagnosticKnown ? "accepted" : "no-backbuffer-tag",
            accepted.diagnosticIndex, accepted.tagVersion, accepted.lifecycleKnown, accepted.lifecycle,
            accepted.resolveAttempted, static_cast<unsigned>(accepted.resolveResult), accepted.output.resource,
            accepted.outputEpoch, OutputIdentity::IsCurrent(accepted.outputEpoch), accepted.output.identity.HasToken(),
            accepted.output.untilPresentPromise, accepted.output.left, accepted.output.top,
            accepted.output.width, accepted.output.height, accepted.inputs);
    }
    catch (...) {} // Diagnostics cannot change the original callback's result.
    SetLastError(originalError);
}

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

// Scalar policy context from current application settings and observed provider.
// No SL query or retained foreign object is introduced here. Actual Present
// integration still must arm Request only at an eligible shared-owner boundary.
struct FullImageContext
{
    FinalColor::Scope scope;
    bool wanted = false;
};
inline FullImageContext FullImageContextFor(uint32_t viewport)
{
    const auto provider = Provider();
    const auto runtime = Config::Instance()->GetDlssNrRuntimeSnapshot();
    const auto route = Config::Instance()->DlssNrRoute.value_or_default();
    const auto key = ViewportKey(viewport);
    return {{provider.generation, OutputIdentity::Current(), key, runtime.resumeGeneration, route},
        observedApi.load() == sl::RenderAPI::eD3D12 && runtime.enabled && (route == 1 || route == 2) &&
        provider.known && provider.enabled && provider.supported && !provider.ambiguous && provider.viewportKey == key};
}
// Call-local adapter for both actual tag APIs. Even on a local allocation
// failure, the original API runs once with its original arguments/result, and
// its true in-flight lifetime stays enrolled through scalar fallback storage.
struct FullImageTagCall
{
    // Some STL implementations allocate during TagCopy construction.
    // Keep that construction inside the same guarded recovery boundary.
    std::optional<FinalColor::TagCopy> copy;
    FinalColor::Controller::TagTicket ticket;
    std::optional<FinalColor::Controller::FallbackTicket> fallback;
    const sl::ResourceTag* actual = nullptr;
    bool finished = false, prepared = false;
    uint32_t viewport = 0;
    void Uncertain()
    {
        fallback.emplace(State().finalColor.BeginFallback());
        if (ticket.call) State().finalColor.EndTag(ticket, false);
        ticket = {};
    }
    FullImageTagCall(FullImageContext context, uint64_t key, bool legacy,
        const sl::ResourceTag* tags, uint32_t count, sl::RenderAPI api) : actual(tags)
    {
        if (api != sl::RenderAPI::eD3D12) return;
        viewport = static_cast<uint32_t>(context.scope.viewport - 1);
        try
        {
            copy.emplace();
            if (!context.wanted && State().finalColor.Observing(context.scope.viewport))
                State().finalColor.Request(context.scope, key, false);
            const bool prepare = context.wanted || State().finalColor.Observing(context.scope.viewport);
            prepared = prepare && copy->Prepare(tags, count);
            const bool interpretable = !prepare || prepared;
            ticket = State().finalColor.BeginTag(context.scope, key, legacy, interpretable);
            if (ticket.disposition == FinalColor::Disposition::NullOptional)
            {
                // Activation may race the cold-path decision. The already
                // enrolled filtered ticket covers this late local preparation.
                if (!prepare) prepared = copy->Prepare(tags, count);
                if (!prepared) { Uncertain(); return; }
                if (count) actual = copy->Data();
            }
        }
        catch (const std::bad_alloc&) { Uncertain(); }
        catch (const std::length_error&) { Uncertain(); }
    }
    void Finish(bool success)
    {
        if (finished) return;
        finished = true;
        if (ticket.call)
        {
            const auto current = FullImageContextFor(viewport);
            State().finalColor.EndTag(ticket, success, copy ? copy->OriginalDeclarations() : FinalColor::OptionalDeclarations {},
                prepared && copy && copy->DeclarationsKnown(), &current.scope);
        }
        if (fallback) State().finalColor.EndFallback(*fallback);
    }
    ~FullImageTagCall() { Finish(false); }
};
inline sl::Result Tags(const sl::FrameToken& frame, const sl::ViewportHandle& viewport,
    const sl::ResourceTag* tags, uint32_t count, sl::CommandBuffer* list)
{
    const auto entryApi = observedApi.load();
    const auto receiptProvider = Provider();
    const auto providerGeneration = receiptProvider.generation;
    const uint32_t viewportId = static_cast<uint32_t>(viewport);
    const uint32_t frameId = static_cast<uint32_t>(frame);
    const auto policyContext = entryApi == sl::RenderAPI::eD3D12 ? FullImageContextFor(viewportId) : FullImageContext {};
    FullImageTagCall policy(policyContext, uint64_t(frameId) + 1, false, tags, count, entryApi);
    const auto* actualTags = policy.actual;
    // Actual accepted metadata describes the descriptors forwarded upstream.
    const auto accepted = ReadAcceptedTags(actualTags, count, entryApi);
    const auto receipt = NativeFgTrace::Begin(actualTags,count,entryApi,
        {providerGeneration,accepted.outputEpoch,uint64_t(frameId)+1,ViewportKey(viewportId),0},
        false,static_cast<void*>(list),receiptProvider.enabled);
    NR_FRAME_TRACE("nr-api", "api=tags phase=enter frame={} viewport={} count={} list={:p}",
        frameId, viewportId, count, static_cast<void*>(list));
    const auto result = setTags(frame, viewport, actualTags, count, list);
    TraceAcceptedTags("frame-tags", accepted, frameId, viewportId, providerGeneration, entryApi, result);
    NR_FRAME_TRACE("nr-api", "api=tags phase=return frame={} viewport={} result={}",
        frameId, viewportId, static_cast<unsigned int>(result));
    if (result == sl::Result::eOk)
    {
        ObserveUiTags(uint64_t(frameId) + 1, viewportId, providerGeneration,
            accepted.outputEpoch, accepted.uiPlanes, false);
        if (accepted.inputs || accepted.output.declared)
            ObserveTags(frameId, viewportId, providerGeneration,
                accepted.output.declared ? &accepted.output : nullptr, accepted.inputs, accepted.outputEpoch);
    }
    if (receipt.id) NativeFgTrace::Returned(receipt,result,Provider().generation,OutputIdentity::Current());
    policy.Finish(result == sl::Result::eOk);
    return result;
}
struct FullImageConstantsCall
{
    FullImageContext entry;
    FinalColor::Controller::ConstantsTicket ticket;
    std::optional<FinalColor::Controller::FallbackTicket> fallback;
    bool tracked = false, finished = false;
    FullImageConstantsCall(FullImageContext context, uint64_t key, sl::RenderAPI api) : entry(context)
    {
        tracked = api == sl::RenderAPI::eD3D12;
        if (!tracked) return;
        try { ticket = State().finalColor.BeginConstants(entry.scope, key, entry.wanted); }
        catch (const std::bad_alloc&) { fallback.emplace(State().finalColor.BeginFallback()); }
        catch (const std::length_error&) { fallback.emplace(State().finalColor.BeginFallback()); }
    }
    void Finish(bool success, bool reset, const FinalColor::Scope& current)
    {
        if (finished) return;
        finished = true;
        if (tracked) State().finalColor.EndConstants(ticket, success, reset, current);
        if (fallback) State().finalColor.EndFallback(*fallback);
    }
    ~FullImageConstantsCall() { Finish(false, false, entry.scope); }
};
inline sl::Result Constants(const sl::Constants& values, const sl::FrameToken& frame,
    const sl::ViewportHandle& viewport)
{
    const auto entryApi = observedApi.load();
    const auto providerGeneration = Provider().generation;
    const auto outputEpoch = OutputIdentity::Current();
    const auto entry = entryApi == sl::RenderAPI::eD3D12 ?
        FullImageContextFor(static_cast<uint32_t>(viewport)) : FullImageContext {};
    FullImageConstantsCall policy(entry, uint64_t(static_cast<uint32_t>(frame)) + 1, entryApi);
    sl::Constants local;
    const bool copied = policy.ticket.forceReset && FinalColor::CopyResetConstants(values, local);
    const auto& actual = copied ? local : values;
    NR_FRAME_TRACE("nr-api", "api=constants phase=enter frame={} viewport={} resetInjected={}",
        static_cast<uint32_t>(frame), static_cast<uint32_t>(viewport), copied);
    const auto result = setConstants(actual, frame, viewport);
    NR_FRAME_TRACE("nr-api", "api=constants phase=return frame={} viewport={} result={}",
        static_cast<uint32_t>(frame), static_cast<uint32_t>(viewport), static_cast<unsigned int>(result));
    if (result == sl::Result::eOk && entryApi == sl::RenderAPI::eD3D12)
        ObserveConstants(static_cast<uint32_t>(frame), static_cast<uint32_t>(viewport), providerGeneration, outputEpoch);
    const auto current = entryApi == sl::RenderAPI::eD3D12 ?
        FullImageContextFor(static_cast<uint32_t>(viewport)).scope : entry.scope;
    policy.Finish(result == sl::Result::eOk, FinalColor::ActualConstantsReset(actual), current);
    return result;
}
inline sl::Result LegacyTags(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags,
                            uint32_t count, sl::CommandBuffer* list)
{
    const auto entryApi = observedApi.load();
    const auto receiptProvider = Provider();
    const auto providerGeneration = receiptProvider.generation;
    const uint32_t viewportId = static_cast<uint32_t>(viewport);
    const auto policyContext = entryApi == sl::RenderAPI::eD3D12 ? FullImageContextFor(viewportId) : FullImageContext {};
    const uint64_t entryFrame = entryApi == sl::RenderAPI::eD3D12 ?
        LegacyUiFrameKey(viewportId, providerGeneration, policyContext.scope.epoch) : 0;
    FullImageTagCall policy(policyContext, entryFrame, true, tags, count, entryApi);
    const auto* actualTags = policy.actual;
    const auto accepted = ReadAcceptedTags(actualTags, count, entryApi);
    const auto receipt = NativeFgTrace::Begin(actualTags,count,entryApi,
        {providerGeneration,accepted.outputEpoch,0,ViewportKey(viewportId),0},
        true,static_cast<void*>(list),receiptProvider.enabled);
    NR_FRAME_TRACE("nr-api", "api=legacy-tags phase=enter viewport={} count={} list={:p}",
        viewportId, count, static_cast<void*>(list));
    const auto result = setLegacyTags(viewport, actualTags, count, list);
    TraceAcceptedTags("legacy-tags", accepted, 0, viewportId, providerGeneration, entryApi, result);
    NR_FRAME_TRACE("nr-api", "api=legacy-tags phase=return viewport={} result={}",
        viewportId, static_cast<unsigned int>(result));
    if (result == sl::Result::eOk)
    {
        ObserveUiTags(entryFrame, viewportId, providerGeneration,
            accepted.outputEpoch, accepted.uiPlanes, true);
        if (accepted.inputs || accepted.output.declared)
            ObserveLegacyTags(viewportId, providerGeneration,
                accepted.output.declared ? &accepted.output : nullptr, accepted.inputs, accepted.outputEpoch);
    }
    if (receipt.id) NativeFgTrace::Returned(receipt,result,Provider().generation,OutputIdentity::Current());
    policy.Finish(result == sl::Result::eOk);
    return result;
}
inline bool CurrentFullImageFrame(const Frame& frame, const FullImageContext& context, bool late)
{
    const auto provider = Provider();
    const auto native = NativeFg();
    return frame.valid && frame.key && context.wanted && provider.known && provider.enabled && provider.supported &&
        ::State::Instance().activeFgOutput == FGOutput::NoFG &&
        provider.generation == frame.providerGeneration && frame.viewportKey &&
        provider.viewportKey == frame.viewportKey && native.active == 1 &&
        native.generation == frame.nativeFgGeneration && native.instance == frame.nativeFgInstance &&
        (!late || !frame.readiness || frame.readinessIdentity.invalidation == State().readinessEpoch.load());
}
inline bool CommitFullFrameInputs(Frame& frame)
{
    if (!frame.viewportKey) return false;
    const auto viewport = static_cast<uint32_t>(frame.viewportKey - 1);
    const auto context = FullImageContextFor(viewport);
    if (!CurrentFullImageFrame(frame, context, false)) return false;
    // The actual token is used only inside this invocation. Resolve it before
    // activating the interval, without issuing duplicate Constants/GetState.
    const uint32_t id = static_cast<uint32_t>(frame.key - 1);
    sl::FrameToken* token = nullptr;
    if (frame.legacyTags ? !setLegacyTags : (!getToken || !setTags ||
        getToken(token, &id) != sl::Result::eOk || !token || static_cast<uint32_t>(*token) != id)) return false;
    FinalColor::Controller::ClearTicket clear;
    try
    {
        frame.finalColorScope = context.scope;
        frame.finalColor = State().finalColor.Request(context.scope, frame.key, context.wanted);
        clear = State().finalColor.BeginClear(frame.finalColor, context.scope);
    }
    catch (const std::bad_alloc&)
    {
        // No mutating SL call began; withhold only this NR operation.
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
    if (!clear.allowed) return false;
    struct ClearReturn
    {
        FinalColor::Controller::ClearTicket ticket;
        FinalColor::Scope scope;
        ~ClearReturn() { State().finalColor.EndClear(ticket, false, scope); }
    } clearReturn {clear, context.scope};
    sl::ResourceTag fullFrame[] = {
        {nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent},
        {nullptr, sl::kBufferTypeUIAlpha, sl::ResourceLifecycle::eValidUntilPresent}};
    if (!frame.legacyTags && static_cast<uint32_t>(*token) != id) return false;
    const auto result = frame.legacyTags ? setLegacyTags(sl::ViewportHandle(viewport), fullFrame, 3, nullptr) :
        setTags(*token, sl::ViewportHandle(viewport), fullFrame, 3, nullptr);
    const auto current = FullImageContextFor(viewport);
    const bool tokenMatches = frame.legacyTags || static_cast<uint32_t>(*token) == id;
    State().finalColor.EndClear(clear, result == sl::Result::eOk && tokenMatches, current.scope, frame.legacyTags);
    // Only the actual successful clear under the still-current requested
    // policy can authorize model work. Reset or a desired toggle is not enough.
    const auto accepted = State().finalColor.View(frame.finalColor);
    return current.wanted && current.scope == context.scope && accepted.accepted && !accepted.unsafe;
}
inline bool PrepareFullFrame(const Frame& frame)
{
    if (!frame.viewportKey || !frame.finalColor) return false;
    const auto current = FullImageContextFor(static_cast<uint32_t>(frame.viewportKey - 1));
    if (!CurrentFullImageFrame(frame, current, true) || !(current.scope == frame.finalColorScope)) return false;
    try
    {
        const auto publication = State().finalColor.BeginPublication(frame.finalColor, current.scope);
        if (!publication.allowed) return false;
        frame.finalColorPublication = publication;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    return frame.finalColorPublication.allowed;
}

inline HRESULT Dispatch(IDXGISwapChain* chain, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters,
                        bool usePresent1)
{
    NativeFgTrace::PresentScope receiptPresent({});
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
    if (State().swapchains == 1)
        Neurotic::FrameCadence::GameFrames().Observe(reinterpret_cast<uintptr_t>(owner.Get()),
            Provider().generation, Util::MillisecondsNow());
    else Neurotic::FrameCadence::GameFrames().Invalidate();
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
        const auto provider = Provider();
        if (provider.viewportKey)
        {
            const auto context = FullImageContextFor(static_cast<uint32_t>(provider.viewportKey - 1));
            try { State().finalColor.Request(context.scope, 0, false); }
            catch (const std::bad_alloc&)
            {
                // Runtime Off independently stops model/publication. Retry only
                // the scalar exit bookkeeping on a later genuine callback.
            }
            catch (const std::length_error&) {}
        }
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
                State().uiPlanes.Reset();
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
    owner->ObserveProvider(fg, provider.generation, start);
    auto frame = Claim(fg);
    if (State().swapchains != 1) owner->startup.Reset();
    if (frame.providerGeneration != provider.generation)
    { if (frame.valid) frame.refusal = "FG provider changed during Present frame selection"; frame.valid = false; }
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
        const bool consumerReady = ConsumerReady(frame);
        Admission::Snapshot admission {provider.known, provider.enabled, provider.supported, provider.ambiguous,
            ::State::Instance().activeFgOutput == FGOutput::NoFG, provider.observedGenerated, nativeFg.active,
            nativeFg.instance, provider.generation, nativeFg.generation, provider.viewportKey,
            provider.rawOptions, consumerReady, frame.valid};
        if (!consumerReady)
        {
            if (frame.valid) frame.refusal = "FG execution path not ready; NR paused while original game frames continue";
            frame.valid = false;
            owner->startup.Reset();
        }
        const auto providerRefusal = Admission::ProviderGate(admission);
        if (providerRefusal != Admission::Reason::None)
        {
            if (frame.valid) frame.refusal = Admission::Describe(providerRefusal);
            frame.valid = false;
            owner->startup.Reset();
        }
        admission.frameValid = frame.valid;
        if (owner->admissionTrace.Changed(admission, start))
            LOG_INFO("NR Present FG admission: gate={} frameRefusal={} providerKnown={} providerEnabled={} "
                "providerSupported={} ambiguous={} providerGeneration={} viewport={} rawKnown={} rawAbi={} "
                "rawMode={} rawGenerated={} rawQueue={} rawUi={} rawFlags={} effectiveD3DQueueSupported={} "
                "observedGenerated={} nativeOutput={} nativeActive={} nativeGeneration={} nativeInstance={} "
                "consumerReady={} frameValid={} token={} sequence={} queue={:p}",
                Admission::Describe(providerRefusal), frame.valid ? "none" : (frame.refusal ? frame.refusal : "unspecified"),
                admission.providerKnown, admission.providerEnabled, admission.providerSupported, admission.ambiguous,
                admission.providerGeneration, admission.viewport, admission.raw.known, admission.raw.abi,
                admission.raw.mode, admission.raw.generated, admission.raw.queue, admission.raw.ui, admission.raw.flags,
                Admission::ClassifyD3D12(admission.raw).queueSupported, admission.observedGenerated, admission.nativeOutput,
                admission.nativeActive, admission.nativeGeneration, admission.nativeInstance, admission.consumerReady,
                admission.frameValid, frame.key, frame.sequence, static_cast<void*>(owner->queue.Get()));
        if (frame.valid)
        {
            ComPtr<IDXGISwapChain3> chain3;
            ComPtr<ID3D12Resource> backbuffer;
            const HRESULT chainResult=chain->QueryInterface(IID_PPV_ARGS(&chain3));
            NR_FRAME_TRACE("nr-api","api=fg-backbuffer stage=swapchain-interface chain={:p} result={:X} "
                "frame={} viewport={} provider={}",static_cast<void*>(chain),static_cast<unsigned>(chainResult),
                frame.key,frame.viewportKey,frame.providerGeneration);
            if (FAILED(chainResult))
            {
                frame.valid = false;
                frame.refusal = "FG backbuffer: swapchain interface unavailable";
                owner->startup.Reset();
            }
            if (frame.valid)
            {
                const UINT bufferIndex=chain3->GetCurrentBackBufferIndex();
                const HRESULT bufferResult=chain3->GetBuffer(bufferIndex,IID_PPV_ARGS(&backbuffer));
                NR_FRAME_TRACE("nr-api","api=fg-backbuffer stage=get-buffer chain={:p} index={} result={:X} "
                    "buffer={:p} frame={} viewport={} provider={}",static_cast<void*>(chain),bufferIndex,
                    static_cast<unsigned>(bufferResult),static_cast<void*>(backbuffer.Get()),
                    frame.key,frame.viewportKey,frame.providerGeneration);
                if(FAILED(bufferResult)) {
                    frame.valid=false;frame.refusal="FG backbuffer: GetBuffer failed";
                    owner->startup.Reset();
                }
            }
            if (frame.valid)
            {
                const auto nativeBuffer = NativeIdentity::Resolve<ID3D12Resource>(backbuffer.Get());
                frame.outputResource = nativeBuffer.object.Get();
                const auto description = backbuffer->GetDesc();
                if (!frame.outputResource || !frame.taggedOutput.Matches(frame.outputResource, description.Width, description.Height))
                {
                    frame.valid = false;
                    const auto& tag=frame.taggedOutput;
                    frame.refusal = !frame.outputResource ? "FG backbuffer: native resource interface unavailable" :
                        tag.unresolved ? "FG backbuffer: tagged resource unresolved" :
                        tag.ambiguous ? "FG backbuffer: conflicting tags" :
                        tag.resource ?
                            "FG backbuffer: tagged resource differs from Present" : "FG backbuffer: tag region out of bounds";
                    NR_FRAME_TRACE("nr-api","api=fg-backbuffer stage=match reason={} resolveResult={:X} actual={:p} "
                        "tagged={:X} declared={} unresolved={} ambiguous={} left={} top={} width={} height={} "
                        "bufferWidth={} bufferHeight={} frame={} viewport={} provider={}",frame.refusal,
                        static_cast<unsigned>(nativeBuffer.result),static_cast<void*>(frame.outputResource),tag.resource,
                        tag.declared,tag.unresolved,tag.ambiguous,tag.left,tag.top,tag.width,tag.height,
                        description.Width,description.Height,frame.key,frame.viewportKey,frame.providerGeneration);
                    owner->startup.Reset();
                }
            }
        }
        // Establish policy before any model/readiness/guide/allocation bypass.
        // An NR miss then forwards original F under accepted full-image FG;
        // it must not silently reinstate a split UI history.
        if (frame.valid && !CommitFullFrameInputs(frame))
        {
            frame.valid = false;
            frame.refusal = Neurotic::UiLiteral("ingame.nr-final-color.policy-pending",
                "Waiting for FG to accept the full image. NR is paused.");
        }
        if (frame.valid) frame.prepareInputs = &PrepareFullFrame;
    }
    // FG off uses the existing admission contract. Native mode still performs no Present model work.
    auto identity = EvaluatePresentImageOnly(chain, owner->queue.Get(), flags, parameters, fg ? &frame : nullptr);
    if (frame.finalColorPublication.call)
    {
        const auto current = FullImageContextFor(static_cast<uint32_t>(frame.viewportKey - 1));
        State().finalColor.EndPublication(frame.finalColorPublication, identity.copybackSubmitted, current.scope);
    }
    const bool policyUnsafe = frame.finalColor && State().finalColor.View(frame.finalColor).unsafe;
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
    frame.outputSubmitted = identity.completedOutput && (!fg || completionPublished) && !policyUnsafe;
    // A metadata-only refusal forwards the untouched image. Completed packets
    // from older Presents must not turn that refusal into a duplicate FG claim.
    // The one pending probe retains its packet until its exact acknowledgement.
    if (!frame.outputSubmitted && !owner->startup.Pending()) RetireFrameCompletions(frame.sequence);
    const bool unsafeHandoff = fg && identity.copybackSubmitted && (!completionPublished || policyUnsafe);
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
    receiptPresent.SetContext(NativeFgTrace::FrameContext(frame));
    // Copyback may already be on the GPU even if its signal/publication failed.
    // Do not enter a provider Present with an unorderable modified input.
    const HRESULT result = unsafeHandoff ? E_FAIL : [&]() -> HRESULT {
        NativeFgTrace::ConsumerScope receiptForward(NativeFgTrace::FrameContext(frame));
        return forward();
    }();
    if (frame.finalColor)
    {
        const auto current = FullImageContextFor(static_cast<uint32_t>(frame.viewportKey - 1));
        State().finalColor.Handoff(frame.finalColor, !unsafeHandoff, result == S_OK, current.scope);
    }
    receiptPresent.End(result,!unsafeHandoff,frame.valid,frame.outputSubmitted,frame.refusal,chain,owner->queue.Get());
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
    State().uiPlanes.Reset();
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
    if (owner) { owner->startup.Reset();
        Neurotic::FrameCadence::GameFrames().Invalidate(reinterpret_cast<uintptr_t>(owner.Get())); }
    OutputIdentity::ResizeScope outputResize;
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
    if (owner) { owner->startup.Reset();
        Neurotic::FrameCadence::GameFrames().Invalidate(reinterpret_cast<uintptr_t>(owner.Get())); }
    OutputIdentity::ResizeScope outputResize;
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
        observedDeviceCookie = {};
        observedApi = sl::RenderAPI::eCount;
        ResetProvider();
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
