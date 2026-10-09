#include <pch.h>
// NR-FEED-001 BEGIN
#include <inputs/universal_feeder/providers/PresentGuideObservationAdapter.h>
// NR-FEED-001 END
#include "DlssNr_Dx11.h"
#include "DlssNr_Dx11Transport.h"
#include "NrNativeDx11OutputContract.h"
#include "DlssNrFeature_Dx12.h"
#include "DlssNr_PresentGuides.h"
#include "DlssNr_PresentInputPolicy.h"
#include <with_dx12/with_dx12.h>
#include <Config.h>
#include <array>
#include <atomic>
#include <map>
#include <mutex>

namespace DlssNr::NativeDx11
{
using Microsoft::WRL::ComPtr;
namespace
{
struct Chain
{
    ComPtr<ID3D11Device> device;
    ComPtr<IUnknown> identity;
};
struct Chains
{
    std::mutex mutex;
    std::map<IDXGISwapChain*, Chain> live; // borrowed while the wrapper owns the real swapchain
};
Chains& Registry() { static auto* value = new Chains; return *value; }
ComPtr<IDXGISwapChain3> UniqueChain(ID3D11Device* device)
{
    auto& registry = Registry();
    std::lock_guard lock(registry.mutex);
    IDXGISwapChain* found = nullptr;
    for (auto& [chain, entry] : registry.live)
        if (Dx11Transport::SameObject(device, entry.device.Get()))
        {
            if (found) return {};
            found = chain;
        }
    ComPtr<IDXGISwapChain3> result;
    if (found) found->QueryInterface(IID_PPV_ARGS(&result));
    return result;
}
struct Slot
{
    Dx11Transport::Texture depth, motion;
    Dx11Transport::Output output;
    ComPtr<ID3D11Texture2D> originalDepth, originalMotion;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    UINT64 ready = 0, completed = 0, retired = 0;
    bool reserved = false;
};
struct Runtime
{
    std::mutex mutex;
    ComPtr<ID3D11Device5> device11;
    ComPtr<ID3D11DeviceContext4> context11;
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D11Fence> ready11;
    ComPtr<ID3D11Fence> completed11;
    ComPtr<ID3D12Fence> ready12, completed12;
    Dx11Transport::Converter converter;
    // Present normally keeps three submissions pending. Four slots preserve that
    // pipeline while bounding worst-case 4K guide storage.
    std::array<Slot, Dx11Transport::SlotCount> slots;
    UINT64 nextReady = 0, nextCompleted = 0;
    bool failed = false, nrChecked = false, nrAvailable = false;

    bool Initialize(ID3D11DeviceContext* context, std::string& reason)
    {
        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        if (failed) { reason = "DX11 transport completion lost; restart required"; return false; }
        if (device11)
        {
            if (!Dx11Transport::SameObject(device.Get(), device11.Get()) ||
                !Dx11Transport::SameObject(context, context11.Get()) ||
                WithDx12::GetD3D12Device() != device12.Get() ||
                WithDx12::GetD3D12CommandQueue() != queue.Get())
            { reason = "DX11 transport device/queue changed; restart required"; return false; }
            return true;
        }
        ComPtr<ID3D11Device5> d11;
        ComPtr<ID3D11DeviceContext4> c11;
        if (FAILED(device.As(&d11)) || FAILED(context->QueryInterface(IID_PPV_ARGS(&c11))) ||
            !WithDx12::PrepareD3D12ForD3D11(device.Get()))
        { reason = "DX11 shared-fence device/context unavailable"; return false; }
        ComPtr<ID3D12Device> d12 = WithDx12::GetD3D12Device();
        ComPtr<ID3D12CommandQueue> q = WithDx12::GetD3D12CommandQueue();
        ComPtr<ID3D12Device> queueDevice;
        if (!d12 || !q || q->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
            FAILED(q->GetDevice(IID_PPV_ARGS(&queueDevice))) || !Dx11Transport::SameObject(d12.Get(), queueDevice.Get()) ||
            !Dx11Transport::SameAdapter(d11.Get(), d12.Get()))
        { reason = "DX11 and NR devices do not share the same adapter/direct queue"; return false; }
        ComPtr<ID3D11Fence> f11;
        ComPtr<ID3D12Fence> f12, done;
        ComPtr<ID3D11Fence> done11;
        HANDLE handle = nullptr;
        HRESULT hr = d11->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&f11));
        if (SUCCEEDED(hr)) hr = f11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle);
        if (SUCCEEDED(hr)) hr = d12->OpenSharedHandle(handle, IID_PPV_ARGS(&f12));
        if (handle) CloseHandle(handle);
        if (SUCCEEDED(hr)) hr = d12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&done));
        handle = nullptr;
        if (SUCCEEDED(hr)) hr = d12->CreateSharedHandle(done.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
        if (SUCCEEDED(hr)) hr = d11->OpenSharedFence(handle, IID_PPV_ARGS(&done11));
        if (handle) CloseHandle(handle);
        if (FAILED(hr) || !converter.Initialize(d11.Get(), reason))
        { if (reason.empty()) reason = "DX11 producer fence creation failed"; return false; }
        device11 = d11; context11 = c11; device12 = d12; queue = q;
        ready11 = f11; ready12 = f12; completed11 = done11; completed12 = done;
        LOG_INFO("NR native DX11: same-adapter guide transport initialized; native DLSS remains DX11");
        return true;
    }
    bool Available(Slot& slot)
    {
        if (slot.reserved || failed) return false;
        const auto ready = ready11->GetCompletedValue(), completed = completed12->GetCompletedValue();
        if (ready == UINT64_MAX || completed == UINT64_MAX) { failed = true; return false; }
        return ready >= std::max(slot.ready, slot.retired) && completed >= slot.completed;
    }
};
// Hooks and submitted GPU work can outlive an NGX feature. Bounded transport allocations
// have process lifetime; no static destructor frees resources with unknown completion.
Runtime& Transport() { static auto* runtime = new Runtime; return *runtime; }
std::atomic<UINT64> nextFeature {0};
ComPtr<ID3D11Texture2D> Texture(NVSDK_NGX_Parameter* p, const char* name)
{
    ID3D11Resource* resource = nullptr;
    ComPtr<ID3D11Texture2D> texture;
    if (p->Get(name, &resource) == NVSDK_NGX_Result_Success && resource)
        resource->QueryInterface(IID_PPV_ARGS(&texture));
    return texture;
}
}
struct Feature::State
{
    UINT64 id = ++nextFeature, evaluation = 0, submissions = 0;
    unsigned int flags = 0, width = 0, height = 0, outWidth = 0, outHeight = 0;
    int quality = 0;
    bool created = false, copyGuides = false, nativePostSr = false, nativePreSr = false;
    bool guideContractReported = false;
    bool privateColorActive = false, privateColorUsed = false, preSrDelivered = false;
    bool colorXChanged = false, colorYChanged = false;
    int pending = -1;
    UINT backbuffer = 0;
    unsigned int colorX = 0, colorY = 0;
    UINT64 generation = 0;
    DlssNrFrameInfo frame;
    std::optional<NrConfigSnapshot<Config>> settings;
    ComPtr<IUnknown> identity;
    // Snapshot these references/values for this evaluation; no borrowed parameter block.
    ComPtr<ID3D11Texture2D> color, output, exposure;
    std::string lastReason;
    void Reject(const std::string& reason)
    {
        PresentGuides::Instance().RejectNative("DX11: " + reason);
        if (lastReason != reason) LOG_INFO("NR native DX11: {}", reason);
        lastReason = reason;
    }
};
void RegisterSwapchain(IDXGISwapChain* swapchain)
{
    Chain entry;
    if (!swapchain || FAILED(swapchain->GetDevice(IID_PPV_ARGS(&entry.device)))) return;
    entry.identity = PresentGuides::Identity(swapchain);
    auto& registry = Registry();
    std::lock_guard lock(registry.mutex);
    registry.live.emplace(swapchain, std::move(entry));
    PresentGuides::Instance().Invalidate();
}
void UnregisterSwapchain(IDXGISwapChain* swapchain)
{
    auto& registry = Registry();
    std::lock_guard lock(registry.mutex);
    if (registry.live.erase(swapchain)) PresentGuides::Instance().Invalidate();
}
void ResizeSwapchain(IDXGISwapChain* swapchain)
{
    auto& registry = Registry();
    std::lock_guard lock(registry.mutex);
    if (registry.live.contains(swapchain)) PresentGuides::Instance().Invalidate();
}
bool QualifiedPresentInputs(IDXGISwapChain* swapchain)
{
    if (!swapchain) return false;
    DXGI_SWAP_CHAIN_DESC desc {};
    if (FAILED(swapchain->GetDesc(&desc))) return false;
    auto identity = PresentGuides::Identity(swapchain);
    auto& runtime = Transport(); std::lock_guard lock(runtime.mutex);
    return PresentGuides::Instance().CurrentQualified(runtime.queue.Get(), identity.Get(),
        desc.BufferDesc.Width, desc.BufferDesc.Height);
}
Feature::Feature() : state(std::make_unique<State>()) {}
Feature::~Feature()
{
    if (state->created) PresentGuides::Instance().Invalidate();
    if (state->pending >= 0)
    {
        auto& runtime = Transport(); std::lock_guard lock(runtime.mutex);
        runtime.slots[state->pending].reserved = false;
    }
}
void Feature::Created(NVSDK_NGX_Parameter* p)
{
    state->guideContractReported = false;
    state->created = p &&
        p->Get(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, &state->flags) == NVSDK_NGX_Result_Success &&
        p->Get(NVSDK_NGX_Parameter_Width, &state->width) == NVSDK_NGX_Result_Success &&
        p->Get(NVSDK_NGX_Parameter_Height, &state->height) == NVSDK_NGX_Result_Success &&
        p->Get(NVSDK_NGX_Parameter_OutWidth, &state->outWidth) == NVSDK_NGX_Result_Success &&
        p->Get(NVSDK_NGX_Parameter_OutHeight, &state->outHeight) == NVSDK_NGX_Result_Success &&
        p->Get(NVSDK_NGX_Parameter_PerfQualityValue, &state->quality) == NVSDK_NGX_Result_Success;
    PresentGuides::Instance().Invalidate();
    LOG_INFO("NR native DX11 feature: id={} createValid={} flags={} render={}x{} output={}x{} quality={}",
        state->id, state->created, state->flags, state->width, state->height,
        state->outWidth, state->outHeight, state->quality);
}
void Feature::Prepare(ID3D11DeviceContext* context, NVSDK_NGX_Parameter* p)
{
    auto& s = *state;
    ++s.evaluation;
    s.copyGuides = false;
    s.nativePostSr = false;
    s.nativePreSr = false;
    s.privateColorActive = false;
    s.privateColorUsed = false;
    s.preSrDelivered = false;
    s.colorXChanged = false;
    s.colorYChanged = false;
    s.settings.reset();
    const auto* config = Config::Instance();
    const auto settings = TryNrConfigSnapshot(*config);
    if (!settings) return;
    const auto route = settings->DlssNrRoute.value_or_default();
    const auto presentPolicy = PresentInput::Selected(*settings);
    const bool presentObserve = route != 0 &&
        (presentPolicy != PresentInput::Policy::ImageOnly ||
         PresentResolution::Selected(*settings).mode == PresentResolution::FollowNative);
    const bool nativePostSr = route == 0 && !settings->DlssNrRunBeforeSr.value_or_default();
    const bool nativePreSr = route == 0 && settings->DlssNrRunBeforeSr.value_or_default();
    const auto runtimeSettings = settings->GetDlssNrRuntimeSnapshot();
    PresentGuides::Instance().Enable(runtimeSettings.enabled && presentObserve,
        PresentResolution::CaptureKey(*settings));
    if (!runtimeSettings.enabled) return;
    if (!nativePostSr && !nativePreSr && !presentObserve) return;
    if (!s.created || !context || !p || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
    { s.Reject("native feature contract or immediate context unavailable"); return; }
    s.copyGuides = nativePostSr || nativePreSr ||
        (presentObserve && presentPolicy != PresentInput::Policy::ImageOnly);
    s.nativePostSr = nativePostSr;
    s.nativePreSr = nativePreSr;
    s.settings = *settings;
    if (s.pending >= 0) { s.Reject("overlapping native feature evaluations"); return; }
    if (s.copyGuides && (s.flags & NVSDK_NGX_DLSS_Feature_Flags_MVJittered))
    { s.Reject("jittered motion-vector convention is not validated"); return; }
    Dx11Transport::ContextLock contextLock(context);
    auto& runtime = Transport();
    std::lock_guard lock(runtime.mutex);
    std::string reason;
    if (!runtime.Initialize(context, reason)) { s.Reject(reason); return; }
    if ((s.nativePostSr || s.nativePreSr) && !runtime.nrChecked)
    {
        runtime.nrChecked = true;
        runtime.nrAvailable = DirectD3D12Available(runtime.device12.Get());
    }
    if ((s.nativePostSr || s.nativePreSr) && !runtime.nrAvailable)
    { s.Reject("native DX11 model is unavailable on the private D3D12 device"); return; }
    auto chain = UniqueChain(runtime.device11.Get());
    if (!chain) { s.Reject("no unique registered native DX11 swapchain"); return; }
    s.identity = PresentGuides::Identity(chain.Get());
    s.backbuffer = chain->GetCurrentBackBufferIndex();
    s.generation = PresentGuides::Instance().Inspect().generation;
    s.color = Texture(p, NVSDK_NGX_Parameter_Color);
    s.output = Texture(p, NVSDK_NGX_Parameter_Output);
    s.exposure = Texture(p, NVSDK_NGX_Parameter_ExposureTexture);
    auto depth = Texture(p, NVSDK_NGX_Parameter_Depth);
    auto motion = Texture(p, NVSDK_NGX_Parameter_MotionVectors);
    D3D11_TEXTURE2D_DESC outputDesc {}, colorDesc {}, dd {}, md {};
    DXGI_SWAP_CHAIN_DESC1 chainDesc {};
    if (!s.output || !s.color || FAILED(chain->GetDesc1(&chainDesc)))
    { s.Reject("native color/output texture or swapchain description unavailable"); return; }
    s.output->GetDesc(&outputDesc); s.color->GetDesc(&colorDesc);
    for (auto* resource : {s.color.Get(), s.output.Get(), depth.Get(), motion.Get(), s.exposure.Get()})
    {
        if (!resource) continue;
        ComPtr<ID3D11Device> owner; resource->GetDevice(&owner);
        if (!Dx11Transport::SameObject(owner.Get(), runtime.device11.Get()))
        { s.Reject("native input resource belongs to another device"); return; }
    }
    unsigned int outputX = 0, outputY = 0, colorX = 0, colorY = 0, reset = 0;
    p->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X, &outputX);
    p->Get(NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y, &outputY);
    const bool haveColorX = p->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, &colorX) ==
        NVSDK_NGX_Result_Success;
    const bool haveColorY = p->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, &colorY) ==
        NVSDK_NGX_Result_Success;
    s.colorX = colorX;
    s.colorY = colorY;
    const bool outputBackingMatches = s.nativePreSr
        ? outputDesc.Width >= s.outWidth && outputDesc.Height >= s.outHeight
        : outputDesc.Width == s.outWidth && outputDesc.Height == s.outHeight;
    const auto outputContract = ValidateOutputContract(s.nativePostSr || s.nativePreSr,
        outputX, outputY, outputBackingMatches, Dx11Transport::SupportedShape(outputDesc),
        s.outWidth, s.outHeight, chainDesc.Width, chainDesc.Height);
    if (outputContract == OutputContractResult::PartialOrUnsupported)
    { s.Reject("native output texture/subrect is partial or unsupported"); return; }
    if (outputContract == OutputContractResult::PresentTargetMismatch)
    { s.Reject("native output does not match the full Present target: declared=" +
        std::to_string(s.outWidth) + "x" + std::to_string(s.outHeight) + " backing=" +
        std::to_string(outputDesc.Width) + "x" + std::to_string(outputDesc.Height) + " render=" +
        std::to_string(s.width) + "x" + std::to_string(s.height) + " present=" +
        std::to_string(chainDesc.Width) + "x" + std::to_string(chainDesc.Height)); return; }
    s.frame = {};
    s.frame.RenderSubrectWidth = s.width; s.frame.RenderSubrectHeight = s.height;
    p->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &s.frame.RenderSubrectWidth);
    p->Get(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &s.frame.RenderSubrectHeight);
    p->Get(NVSDK_NGX_Parameter_Reset, &reset);
    s.frame.Reset = reset != 0 || s.evaluation == 1;
    s.frame.DepthInverted = (s.flags & NVSDK_NGX_DLSS_Feature_Flags_DepthInverted) != 0;
    s.frame.ColourIsLinearHdr = (s.flags & NVSDK_NGX_DLSS_Feature_Flags_IsHDR) != 0;
    p->Get(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, &s.frame.PreExposure);
    p->Get(NVSDK_NGX_Parameter_MV_Scale_X, &s.frame.MvScaleX);
    p->Get(NVSDK_NGX_Parameter_MV_Scale_Y, &s.frame.MvScaleY);
    const bool jitter = p->Get(NVSDK_NGX_Parameter_Jitter_Offset_X, &s.frame.JitterX) == NVSDK_NGX_Result_Success &&
        p->Get(NVSDK_NGX_Parameter_Jitter_Offset_Y, &s.frame.JitterY) == NVSDK_NGX_Result_Success;
    p->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, &s.frame.DepthSubrectX);
    p->Get(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, &s.frame.DepthSubrectY);
    p->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, &s.frame.MotionSubrectX);
    p->Get(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, &s.frame.MotionSubrectY);
    const auto rw = s.frame.RenderSubrectWidth, rh = s.frame.RenderSubrectHeight;
    s.frame.DepthSubrectWidth = rw;
    s.frame.DepthSubrectHeight = rh;
    const bool lowResolutionMotion = (s.flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) != 0;
    s.frame.MotionSubrectWidth = lowResolutionMotion ? rw : s.outWidth;
    s.frame.MotionSubrectHeight = lowResolutionMotion ? rh : s.outHeight;
    // NR-FEED-001 BEGIN
    Neurotic::Feed::Callback feedEffective({"Native.D3D11", Neurotic::Contracts::GraphicsApi::D3D11,
        "legacy-effective", {}, Neurotic::Contracts::SourceClass::HostObserved}, this);
    Neurotic::Feed::ObservePresentGuide(feedEffective, s.frame, depth.Get(), motion.Get());
    feedEffective.Value("host.firstEvaluationReset", s.evaluation == 1);
    // NR-FEED-001 END
    if (!rw || !rh || rw > s.outWidth || rh > s.outHeight || colorX > colorDesc.Width || colorY > colorDesc.Height ||
        rw > colorDesc.Width - colorX || rh > colorDesc.Height - colorY)
    { s.Reject("render subrect exceeds native color/output dimensions"); return; }
    if (s.nativePreSr && !Dx11Transport::SupportedShape(colorDesc))
    { s.Reject("native Pre-SR color shape unsupported; original color preserved"); return; }
    if (s.copyGuides && (!jitter || !std::isfinite(s.frame.MvScaleX) || !std::isfinite(s.frame.MvScaleY) ||
        !std::isfinite(s.frame.JitterX) || !std::isfinite(s.frame.JitterY)))
    { s.Reject("jitter/motion-scale metadata missing or non-finite"); return; }
    if (s.copyGuides && (!depth || !motion)) { s.Reject("native depth or motion texture missing"); return; }
    if (depth) depth->GetDesc(&dd);
    if (motion) motion->GetDesc(&md);
    if (s.copyGuides && (!Dx11Transport::SupportedShape(dd) || !Dx11Transport::SupportedShape(md) ||
        s.frame.DepthSubrectX > dd.Width || s.frame.DepthSubrectY > dd.Height ||
        rw > dd.Width - s.frame.DepthSubrectX || rh > dd.Height - s.frame.DepthSubrectY ||
        s.frame.MotionSubrectX > md.Width || s.frame.MotionSubrectY > md.Height ||
        s.frame.MotionSubrectWidth > md.Width - s.frame.MotionSubrectX ||
        s.frame.MotionSubrectHeight > md.Height - s.frame.MotionSubrectY))
    { s.Reject("native guide shape/subrect unsupported"); return; }
    if (s.copyGuides && !s.guideContractReported)
    {
        LOG_INFO("NR native DX11 guides: depth={}x{} at {},{} motion={}x{} at {},{} ({})",
            s.frame.DepthSubrectWidth, s.frame.DepthSubrectHeight,
            s.frame.DepthSubrectX, s.frame.DepthSubrectY,
            s.frame.MotionSubrectWidth, s.frame.MotionSubrectHeight,
            s.frame.MotionSubrectX, s.frame.MotionSubrectY,
            lowResolutionMotion ? "render-resolution" : "output-resolution");
        s.guideContractReported = true;
    }
    D3D11_TEXTURE2D_DESC imageDesc = outputDesc;
    if (s.nativePreSr)
    {
        imageDesc = colorDesc;
        imageDesc.Width = rw;
        imageDesc.Height = rh;
        imageDesc.MipLevels = 1;
        imageDesc.ArraySize = 1;
        imageDesc.SampleDesc.Count = 1;
        imageDesc.SampleDesc.Quality = 0;
        imageDesc.Usage = D3D11_USAGE_DEFAULT;
        imageDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        imageDesc.CPUAccessFlags = 0;
        imageDesc.MiscFlags = 0;
    }
    int selected = -1;
    UINT64 resident = 0, outputResident = 0;
    for (unsigned int i = 0; i < runtime.slots.size(); ++i)
    {
        auto& slot = runtime.slots[i];
        resident += slot.depth.bytes + slot.motion.bytes;
        outputResident += slot.output.bytes;
        if (runtime.Available(slot) && selected < 0) selected = static_cast<int>(i);
    }
    if (selected < 0) { s.Reject("private guide transport slots are still in flight"); return; }
    auto& slot = runtime.slots[selected];
    const UINT64 requested = Dx11Transport::Texture::RequiredBytes(dd, true) +
        Dx11Transport::Texture::RequiredBytes(md, false);
    const UINT64 requestedOutput = Dx11Transport::Output::RequiredBytes(imageDesc);
    // Four full 4K R32 depth/RG32 motion slots need just over 1 GiB when the game
    // textures cannot be viewed directly. Refuse larger contracts before allocation.
    constexpr UINT64 budget = Dx11Transport::BudgetBytes;
    if (s.copyGuides && (requested > budget || resident - slot.depth.bytes - slot.motion.bytes > budget - requested))
    {
        s.Reject("private guide transport memory budget exceeded: requested=" +
            std::to_string(requested / (1024 * 1024)) + " MiB resident=" +
            std::to_string(resident / (1024 * 1024)) + " MiB budget=1280 MiB depth=" +
            std::to_string(dd.Width) + "x" + std::to_string(dd.Height) + " format=" +
            std::to_string(dd.Format) + " motion=" + std::to_string(md.Width) + "x" +
            std::to_string(md.Height) + " format=" + std::to_string(md.Format));
        return;
    }
    constexpr UINT64 outputBudget = Dx11Transport::OutputBudgetBytes;
    if ((s.nativePostSr || s.nativePreSr) && (requestedOutput == 0 || requestedOutput > outputBudget ||
        outputResident - slot.output.bytes > outputBudget - requestedOutput))
    {
        s.Reject("private output transport memory budget exceeded or format unsupported: requested=" +
            std::to_string(requestedOutput / (1024 * 1024)) + " MiB resident=" +
            std::to_string(outputResident / (1024 * 1024)) + " MiB budget=512 MiB format=" +
            std::to_string(imageDesc.Format));
        return;
    }
    if (!slot.allocator)
    {
        if (FAILED(runtime.device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator))) ||
            FAILED(runtime.device12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                                      IID_PPV_ARGS(&slot.list))))
        { s.Reject("private capture command allocation failed"); return; }
        slot.list->Close();
    }
    if (FAILED(slot.allocator->Reset()) || FAILED(slot.list->Reset(slot.allocator.Get(), nullptr)))
    { runtime.failed = true; s.Reject("private capture command reset failed"); return; }
    slot.list->Close(); // resetting seals the previous GPU-safety recording before carrier reuse
    slot.originalDepth.Reset(); slot.originalMotion.Reset();
    if (s.copyGuides && (!slot.depth.Prepare(runtime.device11.Get(), runtime.device12.Get(), dd, true, reason, depth.Get()) ||
        !slot.motion.Prepare(runtime.device11.Get(), runtime.device12.Get(), md, false, reason, motion.Get())))
    { s.Reject(reason); return; }
    if ((s.nativePostSr || s.nativePreSr) &&
        !slot.output.Prepare(runtime.device11.Get(), runtime.device12.Get(), imageDesc, reason))
    { s.Reject(reason); return; }
    // A depth dispatch may already be queued when motion view creation fails. Retain both
    // inputs and fence even that partial operation before permitting allocation reuse.
    slot.originalDepth = depth; slot.originalMotion = motion;
    const bool copied = !s.copyGuides ||
        runtime.converter.Copy(context, slot.depth, depth.Get(), slot.motion, motion.Get(), reason);
    if (copied && s.nativePreSr)
    {
        D3D11_BOX sourceBox {};
        sourceBox.left = colorX;
        sourceBox.top = colorY;
        sourceBox.right = colorX + rw;
        sourceBox.bottom = colorY + rh;
        sourceBox.back = 1;
        context->CopySubresourceRegion(slot.output.shared.Get(), 0, 0, 0, 0, s.color.Get(), 0, &sourceBox);
    }
    slot.ready = ++runtime.nextReady;
    if (FAILED(runtime.context11->Signal(runtime.ready11.Get(), slot.ready)))
    { runtime.failed = true; s.Reject("DX11 guide producer signal failed; restart required"); return; }
    runtime.context11->Flush();
    if (!copied) { s.Reject(reason); return; }

    if (s.nativePreSr)
    {
        if (FAILED(slot.list->Reset(slot.allocator.Get(), nullptr)))
        { runtime.failed = true; s.Reject("private Pre-SR recording unavailable"); return; }
        auto ticket = GpuSafety::Record(slot.list.Get());
        if (!ticket || FAILED(runtime.queue->Wait(runtime.ready12.Get(), slot.ready)))
        { slot.list->Close(); runtime.failed = true; s.Reject("private Pre-SR producer wait/tracking failed"); return; }

        auto transition = [list = slot.list.Get()](ID3D12Resource* resource,
                                                   D3D12_RESOURCE_STATES before,
                                                   D3D12_RESOURCE_STATES after)
        {
            D3D12_RESOURCE_BARRIER barrier {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &barrier);
        };
        transition(slot.depth.resource12.Get(), D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition(slot.motion.resource12.Get(), D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition(slot.output.resource12.Get(), D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        s.preSrDelivered = EvaluateNativeDx11PreSrCommandList(slot.list.Get(), runtime.queue.Get(),
            slot.output.resource12.Get(), slot.depth.resource12.Get(), slot.motion.resource12.Get(),
            s.frame, *s.settings);
        transition(slot.output.resource12.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COMMON);
        transition(slot.motion.resource12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COMMON);
        transition(slot.depth.resource12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COMMON);

        if (FAILED(slot.list->Close()))
        { runtime.failed = true; s.Reject("private Pre-SR close failed"); return; }
        ID3D12CommandList* lists[] = {slot.list.Get()};
        runtime.queue->ExecuteCommandLists(1, lists);
        const bool recordingSealed = GpuSafety::SealOwnedRecording(slot.list.Get());
        slot.completed = ++runtime.nextCompleted;
        if (FAILED(runtime.queue->Signal(runtime.completed12.Get(), slot.completed)) ||
            !GpuSafety::OrderedOn(ticket, runtime.queue.Get()) || !recordingSealed)
        { runtime.failed = true; s.Reject("private Pre-SR completion untrackable; restart required"); return; }

        if (s.preSrDelivered)
        {
            if (FAILED(runtime.context11->Wait(runtime.completed11.Get(), slot.completed)))
            { runtime.failed = true; s.Reject("DX11 Pre-SR consumer wait failed; original color preserved"); return; }
            p->Set(NVSDK_NGX_Parameter_Color, static_cast<ID3D11Resource*>(slot.output.shared.Get()));
            if (haveColorX && colorX)
            {
                p->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, 0u);
                s.colorXChanged = true;
            }
            if (haveColorY && colorY)
            {
                p->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, 0u);
                s.colorYChanged = true;
            }
            s.privateColorActive = true;
            s.privateColorUsed = true;
        }
    }
    slot.reserved = true; s.pending = selected;
}

void Feature::Restore(NVSDK_NGX_Parameter* p)
{
    auto& s = *state;
    if (!s.privateColorActive || p == nullptr)
        return;
    p->Set(NVSDK_NGX_Parameter_Color, static_cast<ID3D11Resource*>(s.color.Get()));
    if (s.colorXChanged)
        p->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, s.colorX);
    if (s.colorYChanged)
        p->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, s.colorY);
    s.privateColorActive = false;
}

void Feature::Complete(ID3D11DeviceContext* context, bool nativeSucceeded)
{
    auto& s = *state;
    if (s.pending < 0)
    {
        s.color.Reset(); s.output.Reset(); s.exposure.Reset(); s.settings.reset();
        return;
    }
    Dx11Transport::ContextLock contextLock(context);
    auto& runtime = Transport();
    std::lock_guard lock(runtime.mutex);
    auto& slot = runtime.slots[s.pending];
    s.pending = -1;
    struct Finish
    {
        State& state;
        Slot& slot;
        ~Finish()
        {
            slot.reserved = false;
            state.color.Reset(); state.output.Reset(); state.exposure.Reset(); state.settings.reset();
        }
    } finish {s, slot};

    auto report = [&](bool delivered)
    {
        const auto guides = PresentGuides::Instance().Inspect();
        if (++s.submissions == 1 || !s.lastReason.empty() || s.evaluation % 300 == 0)
            LOG_INFO("NR native DX11 {}: feature={} evaluation={} guides={} depth={}x{} motion={}x{} render={}x{} "
                     "output={}x{} transport={}MiB directDepth={} directMotion={} producer={} completed={} retired={} "
                     "delivered={} captures={} matched={} evaluated={} nativeDLSS=success status={}",
                s.nativePreSr ? "Pre-SR" : (s.nativePostSr ? "Post-SR" : "capture"),
                s.id, s.evaluation, s.copyGuides,
                slot.depth.sourceDesc.Width, slot.depth.sourceDesc.Height,
                slot.motion.sourceDesc.Width, slot.motion.sourceDesc.Height, s.frame.RenderSubrectWidth,
                s.frame.RenderSubrectHeight, s.outWidth, s.outHeight,
                (slot.depth.bytes + slot.motion.bytes +
                 ((s.nativePostSr || s.nativePreSr) ? slot.output.bytes : 0)) / (1024 * 1024),
                slot.depth.directSource,
                slot.motion.directSource, slot.ready,
                runtime.completed12->GetCompletedValue(), slot.retired, delivered, guides.captures,
                guides.matched, guides.evaluated,
                (s.nativePostSr || s.nativePreSr)
                    ? (delivered ? "native NR delivered with transported depth/motion"
                                 : "native NR not delivered; native DLSS image retained")
                    : guides.status.c_str());
        s.lastReason.clear();
    };

    if (s.nativePreSr)
    {
        if (!context || !s.settings || !Dx11Transport::SameObject(context, runtime.context11.Get()))
        { runtime.failed = true; s.Reject("native DX11 Pre-SR completion context or snapshot changed"); return; }
        if (s.privateColorUsed)
        {
            slot.retired = ++runtime.nextReady;
            if (FAILED(runtime.context11->Signal(runtime.ready11.Get(), slot.retired)))
            { runtime.failed = true; s.Reject("DX11 Pre-SR input retirement signal failed; restart required"); return; }
            runtime.context11->Flush();
        }
        if (s.privateColorActive)
        { runtime.failed = true; s.Reject("temporary native DX11 color parameter was not restored"); return; }
    }

    if (!nativeSucceeded)
    { s.Reject("native DLSS evaluation failed; native image preserved"); return; }
    if (!s.nativePostSr && !s.nativePreSr && s.generation != PresentGuides::Instance().Inspect().generation)
    { s.Reject("native source generation changed during evaluation"); return; }
    if (s.nativePostSr && (!context || !s.output || !s.settings ||
        !Dx11Transport::SameObject(context, runtime.context11.Get())))
    { s.Reject("native DX11 Post-SR completion context or snapshot changed"); return; }

    if (s.nativePreSr)
    {
        report(s.preSrDelivered);
        return;
    }

    if (s.nativePostSr)
    {
        context->CopyResource(slot.output.shared.Get(), s.output.Get());
        slot.ready = ++runtime.nextReady;
        if (FAILED(runtime.context11->Signal(runtime.ready11.Get(), slot.ready)))
        { runtime.failed = true; s.Reject("DX11 native-output producer signal failed; restart required"); return; }
        runtime.context11->Flush();
    }

    if (FAILED(slot.list->Reset(slot.allocator.Get(), nullptr)))
    { runtime.failed = true; s.Reject("private capture recording unavailable"); return; }
    auto ticket = GpuSafety::Record(slot.list.Get());
    if (!ticket || FAILED(runtime.queue->Wait(runtime.ready12.Get(), slot.ready)))
    { slot.list->Close(); runtime.failed = true; s.Reject("private capture producer wait/tracking failed"); return; }

    bool delivered = false;
    if (s.nativePostSr)
    {
        auto transition = [list = slot.list.Get()](ID3D12Resource* resource,
                                                   D3D12_RESOURCE_STATES before,
                                                   D3D12_RESOURCE_STATES after)
        {
            D3D12_RESOURCE_BARRIER barrier {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            list->ResourceBarrier(1, &barrier);
        };
        transition(slot.depth.resource12.Get(), D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition(slot.motion.resource12.Get(), D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition(slot.output.resource12.Get(), D3D12_RESOURCE_STATE_COMMON,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        delivered = EvaluateNativeDx11PostSrCommandList(slot.list.Get(), runtime.queue.Get(),
            slot.output.resource12.Get(), slot.depth.resource12.Get(), slot.motion.resource12.Get(),
            s.frame, *s.settings);
        transition(slot.output.resource12.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                   D3D12_RESOURCE_STATE_COMMON);
        transition(slot.motion.resource12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COMMON);
        transition(slot.depth.resource12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_COMMON);
    }
    else
    {
        auto proof = std::make_shared<PresentGuides::Dx11Producer>();
        proof->ready = runtime.ready12; proof->orderedQueue = runtime.queue;
        proof->sourceDevice = runtime.device11; proof->value = slot.ready;
        proof->feature = s.id; proof->evaluation = s.evaluation;
        PresentGuides::Instance().Capture(slot.list.Get(), s.copyGuides ? slot.depth.resource12.Get() : nullptr,
            s.copyGuides ? slot.motion.resource12.Get() : nullptr, s.frame, s.identity.Get(), s.backbuffer,
            s.outWidth, s.outHeight, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON,
            s.copyGuides, nullptr, 0, proof);
    }
    if (FAILED(slot.list->Close()))
    { runtime.failed = true; s.Reject("private capture close failed"); return; }
    ID3D12CommandList* lists[] = {slot.list.Get()};
    runtime.queue->ExecuteCommandLists(1, lists);
    const bool recordingSealed = GpuSafety::SealOwnedRecording(slot.list.Get());
    slot.completed = ++runtime.nextCompleted;
    if (FAILED(runtime.queue->Signal(runtime.completed12.Get(), slot.completed)) ||
        !GpuSafety::OrderedOn(ticket, runtime.queue.Get()) || !recordingSealed)
    { runtime.failed = true; s.Reject("private capture submission completion untrackable; restart required"); return; }

    if (s.nativePostSr && delivered)
    {
        if (FAILED(runtime.context11->Wait(runtime.completed11.Get(), slot.completed)))
        { runtime.failed = true; s.Reject("DX11 native-output consumer wait failed; native image preserved"); return; }
        context->CopyResource(s.output.Get(), slot.output.shared.Get());
        slot.retired = ++runtime.nextReady;
        if (FAILED(runtime.context11->Signal(runtime.ready11.Get(), slot.retired)))
        { runtime.failed = true; s.Reject("DX11 native-output retirement signal failed; restart required"); return; }
        runtime.context11->Flush();
    }

    report(delivered);
}
}
