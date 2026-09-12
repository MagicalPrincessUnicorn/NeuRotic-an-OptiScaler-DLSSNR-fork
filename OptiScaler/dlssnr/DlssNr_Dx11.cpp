#include <pch.h>
#include "DlssNr_Dx11.h"
#include "DlssNr_Dx11Transport.h"
#include "DlssNr_PresentGuides.h"
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
    ComPtr<ID3D11Texture2D> originalDepth, originalMotion;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    UINT64 ready = 0, completed = 0;
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
    ComPtr<ID3D12Fence> ready12, completed12;
    Dx11Transport::Converter converter;
    std::array<Slot, 8> slots;
    UINT64 nextReady = 0, nextCompleted = 0;
    bool failed = false;

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
        HANDLE handle = nullptr;
        HRESULT hr = d11->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&f11));
        if (SUCCEEDED(hr)) hr = f11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &handle);
        if (SUCCEEDED(hr)) hr = d12->OpenSharedHandle(handle, IID_PPV_ARGS(&f12));
        if (handle) CloseHandle(handle);
        if (SUCCEEDED(hr)) hr = d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&done));
        if (FAILED(hr) || !converter.Initialize(d11.Get(), reason))
        { if (reason.empty()) reason = "DX11 producer fence creation failed"; return false; }
        device11 = d11; context11 = c11; device12 = d12; queue = q;
        ready11 = f11; ready12 = f12; completed12 = done;
        LOG_INFO("NR native DX11: same-adapter guide transport initialized; native DLSS remains DX11");
        return true;
    }
    bool Available(Slot& slot)
    {
        if (slot.reserved || failed) return false;
        const auto ready = ready11->GetCompletedValue(), completed = completed12->GetCompletedValue();
        if (ready == UINT64_MAX || completed == UINT64_MAX) { failed = true; return false; }
        return ready >= slot.ready && completed >= slot.completed;
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
    UINT64 id = ++nextFeature, evaluation = 0;
    unsigned int flags = 0, width = 0, height = 0, outWidth = 0, outHeight = 0;
    int quality = 0;
    bool created = false, copyGuides = false;
    int pending = -1;
    UINT backbuffer = 0;
    UINT64 generation = 0;
    DlssNrFrameInfo frame;
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
    const auto* config = Config::Instance();
    const auto settings = TryNrConfigSnapshot(*config);
    if (!settings) return;
    const auto route = settings->DlssNrRoute.value_or_default();
    const bool observe = route == 2 || (route == 1 &&
        PresentResolution::Selected(*settings).mode == PresentResolution::FollowNative);
    PresentGuides::Instance().Enable(config->GetDlssNrRuntimeSnapshot().enabled && observe,
        PresentResolution::CaptureKey(*settings));
    if (!config->GetDlssNrRuntimeSnapshot().enabled || !observe) return;
    if (!s.created || !context || !p || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
    { s.Reject("native feature contract or immediate context unavailable"); return; }
    s.copyGuides = route == 2;
    if (s.pending >= 0) { s.Reject("overlapping native feature evaluations"); return; }
    if (s.copyGuides && (s.flags & NVSDK_NGX_DLSS_Feature_Flags_MVJittered))
    { s.Reject("jittered motion-vector convention is not validated"); return; }
    if (s.copyGuides && !(s.flags & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) &&
        (s.width != s.outWidth || s.height != s.outHeight))
    { s.Reject("display-resolution motion vectors with sub-native color are not validated"); return; }
    Dx11Transport::ContextLock contextLock(context);
    auto& runtime = Transport();
    std::lock_guard lock(runtime.mutex);
    std::string reason;
    if (!runtime.Initialize(context, reason)) { s.Reject(reason); return; }
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
    p->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, &colorX);
    p->Get(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, &colorY);
    if (outputX || outputY || outputDesc.Width != s.outWidth || outputDesc.Height != s.outHeight ||
        outputDesc.Width != chainDesc.Width || outputDesc.Height != chainDesc.Height ||
        !Dx11Transport::SupportedShape(outputDesc))
    { s.Reject("partial/ambiguous output does not match the full Present target"); return; }
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
    if (!rw || !rh || rw > s.outWidth || rh > s.outHeight || colorX > colorDesc.Width || colorY > colorDesc.Height ||
        rw > colorDesc.Width - colorX || rh > colorDesc.Height - colorY)
    { s.Reject("render subrect exceeds native color/output dimensions"); return; }
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
        rw > md.Width - s.frame.MotionSubrectX || rh > md.Height - s.frame.MotionSubrectY))
    { s.Reject("native guide shape/subrect unsupported"); return; }
    int selected = -1;
    UINT64 resident = 0;
    for (unsigned int i = 0; i < runtime.slots.size(); ++i)
    {
        auto& slot = runtime.slots[i];
        resident += slot.depth.bytes + slot.motion.bytes;
        if (runtime.Available(slot) && selected < 0) selected = static_cast<int>(i);
    }
    if (selected < 0) { s.Reject("private guide transport slots are still in flight"); return; }
    auto& slot = runtime.slots[selected];
    const UINT64 requested = UINT64(dd.Width) * dd.Height * (4 + Dx11Transport::DepthType(dd.Format).bytes) +
        UINT64(md.Width) * md.Height * Dx11Transport::MotionBytes(md.Format) * 3;
    constexpr UINT64 budget = 256ull * 1024 * 1024;
    if (s.copyGuides && (requested > budget || resident - slot.depth.bytes - slot.motion.bytes > budget - requested))
    { s.Reject("private guide transport memory budget exceeded"); return; }
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
    if (s.copyGuides && (!slot.depth.Prepare(runtime.device11.Get(), runtime.device12.Get(), dd, true, reason) ||
        !slot.motion.Prepare(runtime.device11.Get(), runtime.device12.Get(), md, false, reason)))
    { s.Reject(reason); return; }
    if (s.copyGuides && !runtime.converter.Copy(context, slot.depth, depth.Get(), slot.motion, motion.Get(), reason))
    { s.Reject(reason); return; }
    slot.originalDepth = depth; slot.originalMotion = motion;
    slot.ready = ++runtime.nextReady;
    if (FAILED(runtime.context11->Signal(runtime.ready11.Get(), slot.ready)))
    { runtime.failed = true; s.Reject("DX11 guide producer signal failed; restart required"); return; }
    runtime.context11->Flush();
    slot.reserved = true; s.pending = selected;
}
void Feature::Complete(bool nativeSucceeded)
{
    auto& s = *state;
    s.color.Reset(); s.output.Reset(); s.exposure.Reset();
    if (s.pending < 0) return;
    auto& runtime = Transport(); std::lock_guard lock(runtime.mutex);
    auto& slot = runtime.slots[s.pending];
    s.pending = -1; slot.reserved = false;
    if (!nativeSucceeded) { s.Reject("native DLSS evaluation failed; guides not published"); return; }
    if (s.generation != PresentGuides::Instance().Inspect().generation)
    { s.Reject("native source generation changed during evaluation"); return; }
    if (FAILED(slot.list->Reset(slot.allocator.Get(), nullptr)))
    { runtime.failed = true; s.Reject("private capture recording unavailable"); return; }
    auto ticket = GpuSafety::Record(slot.list.Get());
    if (!ticket || FAILED(runtime.queue->Wait(runtime.ready12.Get(), slot.ready)))
    { slot.list->Close(); runtime.failed = true; s.Reject("private capture producer wait/tracking failed"); return; }
    auto proof = std::make_shared<PresentGuides::Dx11Producer>();
    proof->ready = runtime.ready12; proof->orderedQueue = runtime.queue;
    proof->sourceDevice = runtime.device11; proof->value = slot.ready;
    proof->feature = s.id; proof->evaluation = s.evaluation;
    PresentGuides::Instance().Capture(slot.list.Get(), s.copyGuides ? slot.depth.resource12.Get() : nullptr,
        s.copyGuides ? slot.motion.resource12.Get() : nullptr, s.frame, s.identity.Get(), s.backbuffer,
        s.outWidth, s.outHeight, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON,
        s.copyGuides, nullptr, proof);
    if (FAILED(slot.list->Close()))
    { runtime.failed = true; s.Reject("private capture close failed"); return; }
    ID3D12CommandList* lists[] = {slot.list.Get()};
    runtime.queue->ExecuteCommandLists(1, lists);
    slot.completed = ++runtime.nextCompleted;
    if (FAILED(runtime.queue->Signal(runtime.completed12.Get(), slot.completed)) ||
        !GpuSafety::OrderedOn(ticket, runtime.queue.Get()))
    { runtime.failed = true; s.Reject("private capture submission completion untrackable; restart required"); return; }
    if (!s.lastReason.empty() || s.evaluation == 1 || s.evaluation % 300 == 0)
        LOG_INFO("NR native DX11 capture: feature={} evaluation={} guides={} depth={}x{} motion={}x{} render={}x{} "
                 "output={}x{} producer={} nativeDLSS=success",
            s.id, s.evaluation, s.copyGuides, slot.depth.sourceDesc.Width, slot.depth.sourceDesc.Height,
            slot.motion.sourceDesc.Width, slot.motion.sourceDesc.Height, s.frame.RenderSubrectWidth,
            s.frame.RenderSubrectHeight, s.outWidth, s.outHeight, slot.ready);
    s.lastReason.clear();
}
}
