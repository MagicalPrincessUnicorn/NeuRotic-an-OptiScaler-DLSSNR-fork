#include "pch.h"
#include "dlssnr/RenderingOutput.h"
#include "menu/menu_common.h"
#include "dlssnr/FinalFallbackControl.h"
#include <inputs/FG/FgOwnerScope.h>
#include <dlssnr/FrameTrace.h>
#include "wrapped_swapchain.h"

#include <Util.h>
#include <Config.h>
#include <dlssnr/DlssNr_Present.h>
#include <dlssnr/DlssNrFeature_Dx12.h>
#include <dlssnr/DlssNr_Dx11.h>
#include <dlssnr/HdrObservation.h>
#include <dlssnr/NativeD3D11Guides.h>
#include <dlssnr/connections/ConnectionPolicy.h>
#include <dlssnr/DlssNr_PresentGuides.h>
#include <dlssnr/NativeD3D12Guides.h>
#include <nr/semantic/character/CharacterRuntime.h>
#include <nr/semantic/character/CharacterQueuePolicy.h>

#include <nvapi/fakenvapi.h>
#include <hooks/Reflex_Hooks.h>
#include <hooks/D3D12_Hooks.h>

#include <menu/menu_overlay_dx.h>

#include <misc/FrameLimit.h>

#include <d3d11.h>
#include <d3d12.h>
#include <misc/IdentifyGpu.h>
#include <hooks/Xell_Hooks.h>

#ifdef LOW_LATENCY_INPUTS
#include <low_latency/input/input_antilag2.h>
#endif

#ifdef DXGI_DEBUG_ENABLED
#include <magic_enum.hpp>
#include <dxgidebug.h>

#pragma comment(lib, "dxguid.lib")

#ifdef ENABLE_DEBUG_LAYER_DX12
#include <d3d12sdklayers.h>
#endif
#endif

#pragma intrinsic(_ReturnAddress)

// Used RenderDoc's wrapped object as referance
// https://github.com/baldurk/renderdoc/blob/v1.x/renderdoc/driver/dxgi/dxgi_wrapped.cpp

static int scCount = 0;
static UINT64 _frameCounter = 0;
static double _lastFrameTime = 0;
static bool _dx11Device = false;
static bool _dx12Device = false;

const GUID IID_IUnwrappedDXGISwapChain = {
    0xe8a33b4a, 0x1405, 0x424c, { 0xae, 0x88, 0xd, 0x3e, 0x9d, 0x46, 0xc9, 0x14 }
};

static bool WaitForGPUIdle(IUnknown* object)
{
    if(!object) return true;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    if(FAILED(object->QueryInterface(IID_PPV_ARGS(&queue)))) return true; // D11 has its own owner.
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    if(FAILED(queue->GetDevice(IID_PPV_ARGS(&device))) ||
       FAILED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))) return false;
    const auto event=CreateEvent(nullptr,FALSE,FALSE,nullptr);
    if(!event) return false;
    if(FAILED(queue->Signal(fence.Get(),1))) {CloseHandle(event);return false;}
    bool ready=false;
    const auto completed=fence->GetCompletedValue();
    if(completed!=UINT64_MAX && completed>=1) ready=true;
    else if(SUCCEEDED(fence->SetEventOnCompletion(1,event))) {
        ready=WaitForSingleObject(event,5000)==WAIT_OBJECT_0 &&
              fence->GetCompletedValue()!=UINT64_MAX && fence->GetCompletedValue()>=1;
        if(!ready) {
            // Event registration and pending queue work may still complete.
            (void)fence.Detach(); (void)queue.Detach();
            return false;
        }
    }
    CloseHandle(event);
    return ready;
}

#ifdef DXGI_DEBUG_ENABLED
void ReportDXGILiveObjects()
{
    IDXGIDebug1* dxgiDebug = nullptr;

    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
    {
        dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
        dxgiDebug->Release();
    }
}

void ReadDxgiInfoQueue()
{
    IDXGIInfoQueue* dxgiInfoQueue = nullptr;
    if (DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue)) == S_OK)
    {
        UINT64 msgCount = dxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
        for (UINT64 i = 0; i < msgCount; ++i)
        {
            SIZE_T msgLen = 0;
            dxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &msgLen);
            std::vector<char> buf(msgLen);
            auto* msg = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(buf.data());
            dxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, msg, &msgLen);

            auto description = std::string(msg->pDescription, msg->DescriptionByteLength);
            LOG_DEBUG("DXGI Debug Message: Category: {}, Severity: {}, ID: {}, Description: {}",
                      magic_enum::enum_name(msg->Category), magic_enum::enum_name(msg->Severity), msg->ID, description);
        }
    }
}

#ifdef ENABLE_DEBUG_LAYER_DX12
void ReportD3D12LiveObjects(ID3D12Device* device)
{
    ID3D12DebugDevice* debugDevice = nullptr;

    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&debugDevice))))
    {
        debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
        debugDevice->Release();
    }
}
#endif
#endif

static HRESULT LocalPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags,
                            const DXGI_PRESENT_PARAMETERS* pPresentParameters, IUnknown* pDevice, HWND hWnd, bool isUWP,
                            bool inspectorQueueQualified)
{
    DlssNr::FinalFallback::PresentScope nrOutputScope;
    if ((Flags & DXGI_PRESENT_TEST) == 0) DlssNr::RenderingOutput::Signal(reinterpret_cast<uintptr_t>(hWnd), MenuCommon::IsVisible());
    if (State::Instance().isShuttingDown)
    {
        if (pPresentParameters == nullptr)
            return pSwapChain->Present(SyncInterval, Flags);
        else
            return ((IDXGISwapChain1*) pSwapChain)->Present1(SyncInterval, Flags, pPresentParameters);
    }

    LOG_DEBUG("{}", _frameCounter);

    HRESULT presentResult;
    DlssNr::Connections::PresentArbitrationScope nrArbitrationScope;
    DlssNr::PresentCallIdentity nrPresentIdentity {};
    double presentFrameIntervalMs = 0.0;
    double nrAdapterCpuMs = 0.0;
    double presentHookStartMs = 0.0;

    auto willPresent = (Flags & DXGI_PRESENT_TEST) == 0;

    if (willPresent)
    {
        double ftDelta = 0.0;

        auto now = Util::MillisecondsNow();

        if (_lastFrameTime != 0)
            ftDelta = now - _lastFrameTime;

        _lastFrameTime = now;
        presentFrameIntervalMs = ftDelta;
        State::Instance().presentFrameTime = ftDelta;

        if (State::Instance().currentFG == nullptr)
            State::Instance().lastFGFrameTime = ftDelta;

        LOG_DEBUG("SyncInterval: {}, Flags: {:X}, Frametime: {:0.3f} ms", SyncInterval, Flags, ftDelta);

        // Update swapchain info evey frame
        if (pSwapChain->GetDesc(&State::Instance().currentSwapchainDesc) != S_OK)
            LOG_WARN("Can't get swapchain desc!");
    }

    ID3D11Device* device = nullptr;
    ID3D12Device* device12 = nullptr;
    ID3D12CommandQueue* cq = nullptr;

    bool isD3D11 = false;

    // try to obtain directx objects and find the path
    if (pDevice->QueryInterface(IID_PPV_ARGS(&device)) == S_OK)
    {
        isD3D11 = true;
        device->Release();

        if (!_dx11Device)
            LOG_DEBUG("D3D11Device captured");

        _dx11Device = true;
        State::Instance().swapchainApi = DX11;
        State::Instance().currentD3D11Device = device;
    }
    else if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) == S_OK)
    {
        cq->Release();

        if (!_dx12Device)
            LOG_DEBUG("D3D12CommandQueue captured");

        ID3D12CommandQueue* realQueue = nullptr;
        if (Util::CheckForRealObject(__FUNCTION__, cq, (IUnknown**) &realQueue))
            cq = realQueue;

        State::Instance().swapchainApi = DX12;

        if (State::Instance().currentCommandQueue == nullptr)
            State::Instance().currentCommandQueue = cq;

        if (cq->GetDevice(IID_PPV_ARGS(&device12)) == S_OK)
        {
            device12->Release();

            if (!_dx12Device)
                LOG_DEBUG("D3D12Device captured");

            _dx12Device = true;

            State::Instance().currentD3D12Device = device12;
            D3D12Hooks::HookDevice(device12);
        }
    }

    if (willPresent)
    {
        bool qualified = false;
        if (device) qualified = DlssNr::NativeDx11::QualifiedPresentInputs(pSwapChain);
        else if (cq)
        {
            DXGI_SWAP_CHAIN_DESC desc {};
            auto identity = DlssNr::PresentGuides::Identity(pSwapChain);
            if (SUCCEEDED(pSwapChain->GetDesc(&desc)))
                qualified = DlssNr::PresentGuides::Instance().CurrentQualified(cq, identity.Get(),
                    desc.BufferDesc.Width, desc.BufferDesc.Height);
        }
        DlssNr::Connections::SetNativeUsable(qualified);
    }
    const bool nrOwnedBeforeFg = willPresent && DlssNr::PreFg::BypassLate(pSwapChain);
    if (willPresent && device)
        DlssNr::NativeD3D11Guides::Present(pSwapChain, device, DlssNr::FinalFallback::InGameAllowed() && Config::Instance()->DlssNrEnabled.value_or_default() && Config::Instance()->DlssNrRoute.value_or_default() != 0 && !nrOwnedBeforeFg);
    if (willPresent && cq)
        DlssNr::NativeD3D12Guides::Present(pSwapChain, cq, DlssNr::FinalFallback::InGameAllowed() && Config::Instance()->DlssNrEnabled.value_or_default() && Config::Instance()->DlssNrRoute.value_or_default() != 0 && !nrOwnedBeforeFg);
    auto fg = State::Instance().currentFG;
    if (willPresent && fg != nullptr)
        ReflexHooks::update(fg->IsActive(), false);
    else
        ReflexHooks::update(false, false);

    XellHooks::update();

    // Upscaler GPU time computation
    if (willPresent && (fg == nullptr || !fg->IsActive() || fg->IsPaused()))
    {
        if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
        {
            std::optional<double> upscalerTimeOpt {};

            if (cq != nullptr && currentFeature->Api() == API::DX12 && !currentFeature->IsWithDx12())
            {
                if (upscalerTimeOpt = currentFeature->ReadUpscalerTime(cq); upscalerTimeOpt.has_value())
                    currentFeature->ReadDetailedGpuTimes(cq, State::Instance().detailedGpuTimes);
            }
            else if (device != nullptr)
            {
                ID3D11DeviceContext* context = nullptr;
                device->GetImmediateContext(&context);

                if (upscalerTimeOpt = currentFeature->ReadUpscalerTime(context); upscalerTimeOpt.has_value())
                    currentFeature->ReadDetailedGpuTimes(context, State::Instance().detailedGpuTimes);

                context->Release();
            }
            if (State::Instance().swapchainInteropApi == SwapchainInteropApi::Dx11wDx12 &&
                State::Instance().currentD3D11Device != nullptr)
            {
                ID3D11DeviceContext* context = nullptr;
                State::Instance().currentD3D11Device->GetImmediateContext(&context);

                if (upscalerTimeOpt = currentFeature->ReadUpscalerTime(context); upscalerTimeOpt.has_value())
                    currentFeature->ReadDetailedGpuTimes(context, State::Instance().detailedGpuTimes);

                context->Release();
            }

            if (upscalerTimeOpt.has_value())
            {
                auto upscalerTime = upscalerTimeOpt.value();
                // filter out possibly wrong measured high values
                if (upscalerTime < 100.0)
                {
                    State::Instance().frameTimeMutex.lock();
                    State::Instance().upscaleTimes.push_back(upscalerTime);
                    State::Instance().upscaleTimes.pop_front();
                    State::Instance().frameTimeMutex.unlock();
                }
            }
        }
    }

    // Fallback when FGPresent is not hooked for V-sync
    if (willPresent && Config::Instance()->ForceVsync.has_value())
    {
        LOG_DEBUG("ForceVsync: {}, VsyncInterval: {}, SCAllowTearing: {}, realExclusiveFullscreen: {}",
                  Config::Instance()->ForceVsync.value(), Config::Instance()->VsyncInterval.value_or_default(),
                  State::Instance().SCAllowTearing, State::Instance().realExclusiveFullscreen);

        if (!Config::Instance()->ForceVsync.value())
        {
            SyncInterval = 0;

            if (State::Instance().SCAllowTearing && !State::Instance().realExclusiveFullscreen)
            {
                LOG_DEBUG("Adding DXGI_PRESENT_ALLOW_TEARING");
                Flags |= DXGI_PRESENT_ALLOW_TEARING;
            }
        }
        else
        {
            // Remove allow tearing
            SyncInterval = Config::Instance()->VsyncInterval.value_or_default();

            if (SyncInterval < 1)
                SyncInterval = 1;

            LOG_DEBUG("Removing DXGI_PRESENT_ALLOW_TEARING");
            Flags &= ~DXGI_PRESENT_ALLOW_TEARING;
        }

        LOG_DEBUG("Final SyncInterval: {}", SyncInterval);
    }

    // DXVK check, it's here because of upscaler time calculations
    if (IdentifyGpu::getPrimaryGpu().usesDxvk)
    {
        DlssNr::ReportPresentUnavailable(DlssNr::PresentApi::Vulkan,
                                         "DXVK/cross-API Present is unsupported");
        if (pPresentParameters == nullptr)
            presentResult = pSwapChain->Present(SyncInterval, Flags);
        else
            presentResult = ((IDXGISwapChain1*) pSwapChain)->Present1(SyncInterval, Flags, pPresentParameters);

        if (presentResult == S_OK)
        {
            LOG_TRACE("3 {}", (UINT) presentResult);
        }
        else if (presentResult == DXGI_ERROR_DEVICE_REMOVED)
        {
            if (isD3D11)
            {
                if (State::Instance().currentD3D11Device != nullptr)
                    Util::GetDeviceRemovedReason(State::Instance().currentD3D11Device);
            }
            else
            {
                if (State::Instance().currentD3D12Device != nullptr)
                    Util::GetDeviceRemovedReason(State::Instance().currentD3D12Device);
            }
        }
        else
        {
            LOG_ERROR("3 {:X}", (UINT) presentResult);
        }

        return presentResult;
    }

    if (willPresent)
    {
        // Tick feature to let it know if it's frozen
        if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
            currentFeature->TickFrozenCheck();

        // v9.6 owns no Present call. It may enqueue same-queue DX12 work, then this function continues
        // to the one original Present/Present1 invocation with the existing arguments and return path.
        presentHookStartMs = Util::MillisecondsNow();
        if (!nrOwnedBeforeFg)
            nrPresentIdentity =
                DlssNr::EvaluatePresentImageOnly(pSwapChain, pDevice, Flags, pPresentParameters);
        else
            NR_FRAME_TRACE("nr-provider-output-bypass", "swapchain={:p} owner=streamline-real-present",
                static_cast<void*>(pSwapChain));
        nrAdapterCpuMs = Util::MillisecondsNow() - presentHookStartMs;

        DlssNr::CaptureComparisonOutput(pSwapChain, pDevice, Flags);

        // Draw overlay
        MenuOverlayDx::Present(pSwapChain, SyncInterval, Flags, pPresentParameters, pDevice, hWnd, isUWP, inspectorQueueQualified);

#ifdef LOW_LATENCY_INPUTS
        if (State::Instance().activeFgOutput == FGOutput::FSRFG)
        {
            auto fgIsActive = fg != nullptr && fg->IsActive() && !fg->IsPaused();
            InputAntiLag2::injectAl2Context(pSwapChain, fgIsActive);
        }
#else
        if (State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::XeFG)
        {
            LOG_DEBUG("Calling fakenvapi");

            static UINT64 fgPresentFrame = 0;
            auto fgIsActive = fg != nullptr && fg->IsActive() && !fg->IsPaused();

            if (State::Instance().fgPresentIsCalled)
            {
                State::Instance().fgPresentIsCalled = false;
                fgPresentFrame = _frameCounter;
            }

            auto isInterpolated = fgIsActive && (_frameCounter - fgPresentFrame) > 0;

            fakenvapi::reportFGPresent(pSwapChain, fgIsActive, isInterpolated);
        }
#endif

        _frameCounter++;
        State::Instance().frameCount = _frameCounter;
    }

    // NR-DIAG-001 BEGIN: final pre-call values already held by the Presentation owner.
    const auto m0Publisher = DlssNr::FrameTrace::WithM0Publisher([&]() noexcept {
        using SourceSnapshot = Neurotic::Diagnostics::M0::SourceSnapshot;
        using OwnerDomain = Neurotic::Contracts::OwnerDomain;
        auto source = SourceSnapshot::OwnerPublication(OwnerDomain::Presentation,
            "Alpha.Swapchain.OriginalPresent", "wrapped_swapchain", 1, "OriginalPresent");
        source.Add("alpha.presentFlags", static_cast<std::uint64_t>(Flags));
        source.Add("alpha.syncInterval", static_cast<std::uint64_t>(SyncInterval));
        source.Add("alpha.present1", pPresentParameters != nullptr);
        source.Add("alpha.legacyPresentAttempt", nrPresentIdentity.presentAttempt);
        source.Add("alpha.completedOutput", nrPresentIdentity.completedOutput);
        source.Add("alpha.activeFgOutput",
                   static_cast<std::uint64_t>(State::Instance().activeFgOutput));
        return source;
    });
    (void) m0Publisher;
    // NR-DIAG-001 END
    const auto tracePresent = DlssNr::FrameTrace::Event("original-present-enter",
        "swapchain={:p} flags={} sync={} present1={} attempt={} outputSubmitted={} fgOutput={}",
        static_cast<void*>(pSwapChain), Flags, SyncInterval, pPresentParameters != nullptr,
        nrPresentIdentity.presentAttempt, nrPresentIdentity.completedOutput,
        static_cast<unsigned int>(State::Instance().activeFgOutput));
    LOG_DEBUG("Calling original present");

    // swapchain present
    const auto originalPresentStart = Util::MillisecondsNow();
    if (pPresentParameters == nullptr)
        presentResult = pSwapChain->Present(SyncInterval, Flags);
    else
        presentResult = ((IDXGISwapChain1*) pSwapChain)->Present1(SyncInterval, Flags, pPresentParameters);
    const double originalPresentCpuMs = Util::MillisecondsNow() - originalPresentStart;
    NR_FRAME_TRACE("original-present-return", "call={} swapchain={:p} result={}", tracePresent,
        static_cast<void*>(pSwapChain), static_cast<unsigned int>(presentResult));
    if (willPresent && !DlssNr::PreFg::GetOwner(pSwapChain))
    {
        DlssNr::ReportPresentCallTiming({nrPresentIdentity, presentFrameIntervalMs, nrAdapterCpuMs,
                                        Util::MillisecondsNow() - presentHookStartMs,
                                        originalPresentCpuMs, presentResult});
    }

    if (presentResult == S_OK)
    {
        LOG_DEBUG("Original present result: {:X}", (UINT) presentResult);
    }
    else
    {
        LOG_ERROR("Original present result: {:X}", (UINT) presentResult);

        if (presentResult == DXGI_ERROR_DEVICE_REMOVED && State::Instance().currentD3D12Device != nullptr)
            Util::GetDeviceRemovedReason(State::Instance().currentD3D12Device);
    }

    return presentResult;
}

WrappedIDXGISwapChain4::WrappedIDXGISwapChain4(IDXGISwapChain* real, IUnknown* pDevice, HWND hWnd, UINT flags,
                                               bool isUWP)
    : _real(real), _device(pDevice), _handle(hWnd), _refcount(1), _uwp(isUWP)
{
    DlssNr::PreFg::RememberQueue(real, pDevice);
    _id = ++scCount;
    _lastFlags = flags;

    _real->QueryInterface(IID_PPV_ARGS(&_real1));
    if (_real1 != nullptr)
        _real1->Release();

    _real->QueryInterface(IID_PPV_ARGS(&_real2));
    if (_real2 != nullptr)
        _real2->Release();

    _real->QueryInterface(IID_PPV_ARGS(&_real3));
    if (_real3 != nullptr)
        _real3->Release();

    _real->QueryInterface(IID_PPV_ARGS(&_real4));
    if (_real4 != nullptr)
        _real4->Release();

    _real->AddRef();
    auto refCount = _real->Release();

    _device2 = _device;

    DlssNr::NativeDx11::RegisterSwapchain(_real);

    DXGI_SWAP_CHAIN_DESC1 observationDesc {};
    const DXGI_FORMAT observationFormat =
        _real1 != nullptr && SUCCEEDED(_real1->GetDesc1(&observationDesc))
            ? observationDesc.Format : DXGI_FORMAT_UNKNOWN;
    const auto observation = DlssNr::HdrObservation::Registry::Instance().Register(_real, observationFormat);

    LOG_INFO("{} created, real: {:X}, refCount: {}, HDR observation {} generation {} format {}",
             _id, (UINT64) real, refCount, observation.observationSequence, observation.generation,
             (UINT) observation.format);
}

WrappedIDXGISwapChain4::~WrappedIDXGISwapChain4() {}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::QueryInterface(REFIID riid, void** ppvObject)
{
    LOG_TRACE("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    if (riid == DlssNr::NativeIdentity::neuroticSwapchainIdentityBase)
    {
        if (ppvObject == nullptr) return E_POINTER;
        // Observation identity only; normal DXGI/Streamline rendering queries
        // keep the wrapper and all of its presentation/overlay callbacks.
        return _real->QueryInterface(__uuidof(IUnknown), ppvObject);
    }

    if (riid == __uuidof(IDXGISwapChain))
    {
        AddRef();
        *ppvObject = (IDXGISwapChain*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGISwapChain1))
    {
        if (_real1)
        {
            AddRef();
            *ppvObject = (IDXGISwapChain1*) this;
            return S_OK;
        }
        else
        {
            return E_NOINTERFACE;
        }
    }
    else if (riid == __uuidof(IDXGISwapChain2))
    {
        if (_real2)
        {
            AddRef();
            *ppvObject = (IDXGISwapChain2*) this;
            return S_OK;
        }
        else
        {
            return E_NOINTERFACE;
        }
    }
    else if (riid == __uuidof(IDXGISwapChain3))
    {
        if (_real3)
        {
            AddRef();
            *ppvObject = (IDXGISwapChain3*) this;
            return S_OK;
        }
        else
        {
            return E_NOINTERFACE;
        }
    }
    else if (riid == __uuidof(IDXGISwapChain4))
    {
        if (_real4)
        {
            AddRef();
            *ppvObject = (IDXGISwapChain4*) this;
            return S_OK;
        }
        else
        {
            return E_NOINTERFACE;
        }
    }
    else if (riid == __uuidof(WrappedIDXGISwapChain4))
    {
        AddRef();
        *ppvObject = this;
        return S_OK;
    }
    else if (riid == __uuidof(IUnknown))
    {
        AddRef();
        *ppvObject = (IUnknown*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIObject))
    {
        AddRef();
        *ppvObject = (IDXGIObject*) this;
        return S_OK;
    }
    else if (riid == __uuidof(IDXGIDeviceSubObject))
    {
        AddRef();
        *ppvObject = (IDXGIDeviceSubObject*) this;
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE WrappedIDXGISwapChain4::AddRef()
{
    InterlockedIncrement(&_refcount);
    LOG_TRACE("Count: {}, caller: {}", _refcount, Util::WhoIsTheCaller(_ReturnAddress()));
    return _refcount;
}

ULONG STDMETHODCALLTYPE WrappedIDXGISwapChain4::Release()
{
    ULONG ret = InterlockedDecrement(&_refcount);

    LOG_TRACE("Count: {}, caller: {}", _refcount, Util::WhoIsTheCaller(_ReturnAddress()));

    if (ret == 0)
    {
        Neurotic::Semantic::Character::CharacterSwapchainReleased(_real, _device);
        DlssNr::NativeDx11::UnregisterSwapchain(_real);
        DlssNr::HdrObservation::Registry::Instance().Unregister(_real);
#ifdef USE_LOCAL_MUTEX
        OwnedLockGuard lock(_localMutex, 999);
#endif

        MenuOverlayDx::CleanupRenderTarget(true, _handle);

        if (State::Instance().currentSwapchain == this)
            State::Instance().currentSwapchain = nullptr;

        // These aliases borrow the caller/wrapper references; clear them before
        // this wrapper and its owned real-swapchain reference are retired.
        if (State::Instance().currentWrappedSwapchain == this)
            State::Instance().currentWrappedSwapchain = nullptr;

        if (State::Instance().currentRealSwapchain == this || State::Instance().currentRealSwapchain == _real)
            State::Instance().currentRealSwapchain = nullptr;

        Neurotic::Runtime::FgOwnerTransition ownerTransition;
        auto fg = ownerTransition ? State::Instance().currentFG : nullptr;
        if (fg != nullptr && fg->Mutex.getOwner() != 1 && fg->SwapchainContext() != nullptr && fg->OwnsSwapchain(_real))
        {
            if (fg->ReleaseSwapchain(_handle))
                State::Instance().currentFGSwapchain = nullptr;
        }

        auto refCount = _real->Release();

        // Disabled for now, cause issues with some games
        /*
        IDXGISwapChain* skSC = nullptr;
        if (_real->QueryInterface(IID_IUnwrappedDXGISwapChain, (void**) &skSC) == S_OK && skSC != nullptr)
        {
            skSC->Release();
            LOG_DEBUG("Found SK swapchain, skip releasing of main swapchain");
        }
        else
        {
            // Release real swapchain, otherwise it can cause issues when re-creating swapchain with same handle
            while (refCount > 0)
            {
                LOG_DEBUG("Waiting for real swapchain to be released, refCount: {}", refCount);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                refCount = _real->Release();
            }
        }
        */

        LOG_DEBUG("Real swapchain released, refCount: {}", refCount);

        delete this;
    }

    return ret;
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetPrivateData(REFGUID Name, UINT DataSize, const void* pData)
{
    return _real->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnknown)
{
    return _real->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData)
{
    return _real->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetParent(REFIID riid, void** ppParent)
{
    return _real->GetParent(riid, ppParent);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetDevice(REFIID riid, void** ppDevice)
{
    return _real->GetDevice(riid, ppDevice);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::Present(UINT SyncInterval, UINT Flags)
{
    if (_real == nullptr)
        return DXGI_ERROR_DEVICE_REMOVED;

#ifdef USE_LOCAL_MUTEX
    OwnedLockGuard lock(_localMutex, 4);
#endif

    HRESULT result;

    if ((Flags & DXGI_PRESENT_TEST) == 0)
    {
        result = LocalPresent(_real, SyncInterval, Flags, nullptr, _device, _handle, _uwp, _inspectorQueueQualified);

        // When Reflex can't be used to limit, sleep in present
        if (!State::Instance().reflexLimitsFps && State::Instance().activeFgOutput == FGOutput::NoFG &&
            !IdentifyGpu::getPrimaryGpu().usesDxvk && !XellHooks::canLimit())
            FrameLimit::sleep(false);
    }
    else
    {
        result = _real->Present(SyncInterval, Flags);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface)
{
    auto result = _real->GetBuffer(Buffer, riid, ppSurface);
    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget)
{
    LOG_DEBUG("Fullscreen: {}, pTarget: {:X}, Caller: {}", Fullscreen, (size_t) pTarget,
              Util::WhoIsTheCaller(_ReturnAddress()));

    HRESULT result = S_OK;

    bool ffxLock = false;

    {
#ifdef USE_LOCAL_MUTEX
        // dlssg calls this from present it seems
        // don't try to get a mutex when present owns it while dlssg mod is enabled
        if (!(_localMutex.getOwner() == 4 && State::Instance().activeFgNvngx != FGNvngxReplacement::None))
        {
            OwnedLockGuard lock(_localMutex, 3);
        }
#endif
        if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
        {

            if (State::Instance().currentFG != nullptr && State::Instance().currentFG->IsActive() &&
                State::Instance().currentFG->Mutex.getOwner() != 3)
            {
                LOG_TRACE("Waiting ffxMutex 3, current: {}", State::Instance().currentFG->Mutex.getOwner());
                State::Instance().currentFG->Mutex.lock(3);
                ffxLock = true;
                LOG_TRACE("Accuired ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
            }
            else
            {
                LOG_TRACE("Skipping ffxMutex, owner is already 3");
            }
        }

        State::Instance().realExclusiveFullscreen = Fullscreen;

        result = _real->SetFullscreenState(Fullscreen, pTarget);

        if (result != S_OK)
            LOG_ERROR("result: {:X}", (UINT) result);
        else
            LOG_DEBUG("result: {:X}", result);
    }

    if (ffxLock)
    {
        LOG_TRACE("Releasing ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
        State::Instance().currentFG->Mutex.unlockThis(3);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget)
{
    return _real->GetFullscreenState(pFullscreen, ppTarget);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) { return _real->GetDesc(pDesc); }

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                                DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    DlssNr::NativeDx11::ResizeSwapchain(_real);
    DlssNr::HdrObservation::Registry::Instance().BeginResize(_real);
    if (!Neurotic::Semantic::Character::CharacterBeforeResize(_device))
    {
        DlssNr::HdrObservation::Registry::Instance().CompleteResize(_real, DXGI_ERROR_WAS_STILL_DRAWING,
                                                                  DXGI_FORMAT_UNKNOWN);
        return DXGI_ERROR_WAS_STILL_DRAWING;
    }
    LOG_DEBUG("");

#ifdef USE_LOCAL_MUTEX
    // dlssg calls this from present it seems
    // don't try to get a mutex when present owns it while dlssg mod is enabled
    if (!(_localMutex.getOwner() == 4 && State::Instance().activeFgNvngx != FGNvngxReplacement::None))
    {
        OwnedLockGuard lock(_localMutex, 1);
    }
#endif

    if (State::Instance().currentFG != nullptr && Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Waiting ffxMutex 3, current: {}", State::Instance().currentFG->Mutex.getOwner());
        State::Instance().currentFG->Mutex.lock(3);
        LOG_TRACE("Accuired ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
    }

    HRESULT result;
    DXGI_SWAP_CHAIN_DESC desc {};
    _real->GetDesc(&desc);

    if (Config::Instance()->FGEnabled.value_or_default())
    {
        State::Instance().fgResetCapturedResources = true;
        State::Instance().fgOnlyUseCapturedResources = false;
        State::Instance().fgChanged = true;
    }

    MenuOverlayDx::CleanupRenderTarget(true, _handle);

    State::Instance().scChanged = true;

    if (Config::Instance()->OverrideVsync.value_or_default() && !State::Instance().SCExclusiveFullscreen &&
        State::Instance().currentFG == nullptr)
    {
        LOG_DEBUG("Overriding flags");
        SwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        if (BufferCount < 2)
            BufferCount = 2;
    }

    State::Instance().SCAllowTearing = (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;

    LOG_DEBUG("BufferCount: {0}, Width: {1}, Height: {2}, NewFormat: {3}, SwapChainFlags: {4:X}", BufferCount, Width,
              Height, (UINT) NewFormat, SwapChainFlags);

    if (!WaitForGPUIdle(_device)) {
        if (State::Instance().currentFG && Config::Instance()->FGUseMutexForSwapchain.value_or_default())
            State::Instance().currentFG->Mutex.unlockThis(3);
        return DXGI_ERROR_WAS_STILL_DRAWING;
    }

    // Release swapchain backbuffers to prevent errors when resizing
    /*

    const bool outputRequiresRelease =
        State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::XeFG;

    if (outputRequiresRelease && State::Instance().currentFG != nullptr)
    {
        IDXGISwapChain* skSC = nullptr;
        if (_real->QueryInterface(IID_IUnwrappedDXGISwapChain, (void**) &skSC) == S_OK && skSC != nullptr)
        {
            skSC->Release();
            LOG_DEBUG("Found SK swapchain, skip releasing backbuffers of main swapchain");
        }
        else
        {
            LOG_DEBUG("Releasing backbuffers, count: {}", desc.BufferCount);

            for (UINT i = 0; i < desc.BufferCount; i++)
            {
                ID3D12Resource* backBuffer = nullptr;
                auto bbResult = _real->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

                if (bbResult == S_OK)
                {
                    backBuffer->AddRef();

                    auto refCount = backBuffer->Release();
                    refCount = backBuffer->Release();
                    LOG_DEBUG("Current backbuffer {}, RefCount {}", i, refCount);

                    while (refCount > 1 && refCount < 4294967200ul)
                    {
                        refCount = backBuffer->Release();
                        LOG_DEBUG("Releasing backbuffer {}, RefCount {}", i, refCount);
                    }

                    LOG_DEBUG("Backbuffer {}, RefCount {}", i, refCount);
                }
                else
                {
                    LOG_DEBUG("GetBuffer failed for index {}: {:X}", i, (UINT) bbResult);
                    break;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    */

#ifdef DXGI_DEBUG_ENABLED
    ReportDXGILiveObjects();

#ifdef ENABLE_DEBUG_LAYER_DX12
    ReportD3D12LiveObjects(State::Instance().currentD3D12Device);
#endif
#endif

    if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
    {
        ScopedSkipHeapCapture skipHeapCapture {};

        _lastFlags = SwapChainFlags;
        result = _real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }
    else
    {
        _lastFlags = SwapChainFlags;
        result = _real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    DXGI_SWAP_CHAIN_DESC1 observationDesc {};
    const DXGI_FORMAT observationFormat = SUCCEEDED(result) && _real1 != nullptr &&
        SUCCEEDED(_real1->GetDesc1(&observationDesc)) ? observationDesc.Format : DXGI_FORMAT_UNKNOWN;
    const auto resizeObservation =
        DlssNr::HdrObservation::Registry::Instance().CompleteResize(_real, result, observationFormat);
    LOG_INFO("DLSS-NR HDR diagnostic: swapchain {:X} resize result {:X}, observation {}, generation {}, "
             "resize generation {}, format {}",
             (UINT64) _real, (UINT) result, resizeObservation.observationSequence,
             resizeObservation.generation, resizeObservation.resizeGeneration,
             (UINT) resizeObservation.format);

    if (result == DXGI_ERROR_DEVICE_REMOVED && State::Instance().currentD3D12Device != nullptr)
        Util::GetDeviceRemovedReason(State::Instance().currentD3D12Device);

#ifdef DXGI_DEBUG_ENABLED
    if (result != S_OK)
        ReadDxgiInfoQueue();
#endif

    if (result == S_OK && State::Instance().currentFeature == nullptr)
    {
        State::Instance().screenWidth = static_cast<float>(Width);
        State::Instance().screenHeight = static_cast<float>(Height);
        State::Instance().lastMipBias = 100.0f;
        State::Instance().lastMipBiasMax = -100.0f;
    }

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (Config::Instance()->ForceHDR.value_or_default())
    {
        LOG_INFO("Force HDR on");

        do
        {
            if (_real3 == nullptr)
                break;

            NewFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

            if (Config::Instance()->UseHDR10.value_or_default())
            {
                NewFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
                hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            }

            if (!Config::Instance()->SkipColorSpace.value_or_default())
            {
                UINT css = 0;

                result = _real3->CheckColorSpaceSupport(hdrCS, &css);

                if (result != S_OK)
                {
                    LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                    break;
                }

                if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
                {
                    result = _real3->SetColorSpace1(hdrCS);

                    const auto hdrObservation =
                        DlssNr::HdrObservation::Registry::Instance().RecordColorSpace(_real, hdrCS, result);
                    LOG_INFO("DLSS-NR HDR diagnostic: ForceHDR color-space request {} result {:X}, observation {}, "
                             "generation {}, class {}",
                             (UINT) hdrCS, (UINT) result, hdrObservation.observationSequence,
                             hdrObservation.generation,
                             DlssNr::HdrObservation::ColorClassName(
                                 DlssNr::HdrObservation::Classify(hdrObservation.colorSpace)));

                    if (result != S_OK)
                    {
                        LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                        break;
                    }
                }

                LOG_INFO("HDR format and color space are set");
            }

        } while (false);
    }

    State::Instance().scBuffers.clear();
    UINT bc = BufferCount;
    if (bc == 0 && _real1 != nullptr)
    {
        DXGI_SWAP_CHAIN_DESC1 desc {};

        if (_real1->GetDesc1(&desc) == S_OK)
            bc = desc.BufferCount;
    }

    for (UINT i = 0; i < bc; i++)
    {
        IUnknown* buffer;

        if (_real->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
        {
            State::Instance().scBuffers.push_back(buffer);
            buffer->Release();
        }
    }

    LOG_DEBUG("result: {0:X}", (UINT) result);

    if (State::Instance().currentFG != nullptr && Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Releasing ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
        State::Instance().currentFG->Mutex.unlockThis(3);
    }

    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters)
{
    return _real->ResizeTarget(pNewTargetParameters);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetContainingOutput(IDXGIOutput** ppOutput)
{
    return _real->GetContainingOutput(ppOutput);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats)
{
    return _real->GetFrameStatistics(pStats);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetLastPresentCount(UINT* pLastPresentCount)
{
    return _real->GetLastPresentCount(pLastPresentCount);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc)
{
    return _real1->GetDesc1(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
    return _real1->GetFullscreenDesc(pDesc);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetHwnd(HWND* pHwnd) { return _real1->GetHwnd(pHwnd); }

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetCoreWindow(REFIID refiid, void** ppUnk)
{
    return _real1->GetCoreWindow(refiid, ppUnk);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::Present1(UINT SyncInterval, UINT Flags,
                                                           const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    if (_real1 == nullptr)
        return DXGI_ERROR_DEVICE_REMOVED;

#ifdef USE_LOCAL_MUTEX
    OwnedLockGuard lock(_localMutex, 5);
#endif

    HRESULT result;

    if ((Flags & DXGI_PRESENT_TEST) == 0)
    {
        result = LocalPresent(_real1, SyncInterval, Flags, pPresentParameters, _device, _handle, _uwp, _inspectorQueueQualified);

        // When Reflex can't be used to limit, sleep in present
        if (!State::Instance().reflexLimitsFps && State::Instance().activeFgOutput == FGOutput::NoFG &&
            !IdentifyGpu::getPrimaryGpu().usesDxvk && !XellHooks::canLimit())
            FrameLimit::sleep(false);
    }
    else
    {
        result = _real1->Present1(SyncInterval, Flags, pPresentParameters);
    }

    return result;
}

BOOL STDMETHODCALLTYPE WrappedIDXGISwapChain4::IsTemporaryMonoSupported(void)
{
    return _real1->IsTemporaryMonoSupported();
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput)
{
    return _real1->GetRestrictToOutput(ppRestrictToOutput);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetBackgroundColor(const DXGI_RGBA* pColor)
{
    return _real1->SetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetBackgroundColor(DXGI_RGBA* pColor)
{
    return _real1->GetBackgroundColor(pColor);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetRotation(DXGI_MODE_ROTATION Rotation)
{
    return _real1->SetRotation(Rotation);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetRotation(DXGI_MODE_ROTATION* pRotation)
{
    return _real1->GetRotation(pRotation);
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetSourceSize(UINT Width, UINT Height)
{
    return _real2->SetSourceSize(Width, Height);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetSourceSize(UINT* pWidth, UINT* pHeight)
{
    return _real2->GetSourceSize(pWidth, pHeight);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetMaximumFrameLatency(UINT MaxLatency)
{
    return _real2->SetMaximumFrameLatency(MaxLatency);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetMaximumFrameLatency(UINT* pMaxLatency)
{
    return _real2->GetMaximumFrameLatency(pMaxLatency);
}

HANDLE STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetFrameLatencyWaitableObject(void)
{
    return _real2->GetFrameLatencyWaitableObject();
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix)
{
    return _real2->SetMatrixTransform(pMatrix);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix)
{
    return _real2->GetMatrixTransform(pMatrix);
}

UINT STDMETHODCALLTYPE WrappedIDXGISwapChain4::GetCurrentBackBufferIndex(void)
{
    auto index = _real3->GetCurrentBackBufferIndex();
    // LOG_TRACE("index: {}", index);
    return index;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                                         UINT* pColorSpaceSupport)
{
    return _real3->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    const HRESULT result = _real3 != nullptr ? _real3->SetColorSpace1(ColorSpace)
                                              : DXGI_ERROR_DEVICE_REMOVED;
    const auto observation =
        DlssNr::HdrObservation::Registry::Instance().RecordColorSpace(_real, ColorSpace, result);
    if (SUCCEEDED(result))
        State::Instance().isHdrActive = DlssNr::HdrObservation::IsHdr(
            DlssNr::HdrObservation::Classify(ColorSpace));

    LOG_INFO("DLSS-NR HDR diagnostic: swapchain {:X} color-space request {} result {:X}, observation {}, "
             "generation {}, active {} class {}. PQ values are encoded ST.2084 signals; scRGB uses "
             "linear 80-nit reference units.",
             (UINT64) _real, (UINT) ColorSpace, (UINT) result, observation.observationSequence,
             observation.generation, SUCCEEDED(result),
             DlssNr::HdrObservation::ColorClassName(DlssNr::HdrObservation::Classify(ColorSpace)));
    return result;
}

HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
                                                                 DXGI_FORMAT Format, UINT SwapChainFlags,
                                                                 const UINT* pCreationNodeMask,
                                                                 IUnknown* const* ppPresentQueue)
{
    DlssNr::NativeDx11::ResizeSwapchain(_real);
    DlssNr::HdrObservation::Registry::Instance().BeginResize(_real);
    if (!Neurotic::Semantic::Character::CharacterBeforeResize(_device))
    {
        DlssNr::HdrObservation::Registry::Instance().CompleteResize(_real, DXGI_ERROR_WAS_STILL_DRAWING,
                                                                  DXGI_FORMAT_UNKNOWN);
        return DXGI_ERROR_WAS_STILL_DRAWING;
    }
    LOG_DEBUG("");

#ifdef USE_LOCAL_MUTEX
    // dlssg calls this from present it seems
    // don't try to get a mutex when present owns it while dlssg mod is enabled
    if (!(_localMutex.getOwner() == 4 && State::Instance().activeFgNvngx != FGNvngxReplacement::None))
    {
        OwnedLockGuard lock(_localMutex, 2);
    }
#endif

    if (State::Instance().activeFgOutput == FGOutput::FSRFG &&
        Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Waiting ffxMutex 3, current: {}", State::Instance().currentFG->Mutex.getOwner());
        State::Instance().currentFG->Mutex.lock(3);
        LOG_TRACE("Accuired ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
    }

    HRESULT result = E_FAIL;
    DXGI_SWAP_CHAIN_DESC desc {};
    _real->GetDesc(&desc);

    if (Config::Instance()->FGEnabled.value_or_default())
    {
        State::Instance().fgResetCapturedResources = true;
        State::Instance().fgOnlyUseCapturedResources = false;
        State::Instance().fgChanged = true;
    }

    MenuOverlayDx::CleanupRenderTarget(true, _handle);

    State::Instance().scChanged = true;

    if (Config::Instance()->OverrideVsync.value_or_default() && !State::Instance().SCExclusiveFullscreen &&
        State::Instance().currentFG == nullptr)
    {
        LOG_DEBUG("Overriding flags");
        SwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        BufferCount = Neurotic::Semantic::Character::CharacterResizeBufferCount(BufferCount, pCreationNodeMask || ppPresentQueue);
    }

    State::Instance().SCAllowTearing = (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;

    LOG_DEBUG("BufferCount: {}, Width: {}, Height: {}, NewFormat: {}, SwapChainFlags: {:X}", BufferCount, Width, Height,
              (UINT) Format, SwapChainFlags);

    if (!WaitForGPUIdle(_device)) {
        if (State::Instance().currentFG && Config::Instance()->FGUseMutexForSwapchain.value_or_default())
            State::Instance().currentFG->Mutex.unlockThis(3);
        return DXGI_ERROR_WAS_STILL_DRAWING;
    }

    // Release swapchain backbuffers to prevent errors when resizing
    const bool isUsingOptiFgFeature =
        State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::XeFG;

    if (isUsingOptiFgFeature && State::Instance().currentFG != nullptr)
    {
        IDXGISwapChain* skSC = nullptr;
        if (_real->QueryInterface(IID_IUnwrappedDXGISwapChain, (void**) &skSC) == S_OK && skSC != nullptr)
        {
            skSC->Release();
            LOG_DEBUG(
                "Found SK swapchain, skip releasing backbuffersand using ResizeBuffers instead of ResizeBuffers1");

#ifdef DXGI_DEBUG_ENABLED
            ReportDXGILiveObjects();

#ifdef ENABLE_DEBUG_LAYER_DX12
            ReportD3D12LiveObjects(State::Instance().currentD3D12Device);
#endif
#endif

            if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            {
                ScopedSkipHeapCapture skipHeapCapture {};

                _lastFlags = SwapChainFlags;
                result = _real3->ResizeBuffers(BufferCount, Width, Height, Format, SwapChainFlags);
            }
            else
            {
                _lastFlags = SwapChainFlags;
                result = _real3->ResizeBuffers(BufferCount, Width, Height, Format, SwapChainFlags);
            }
        }
        /*
        else
        {
            LOG_DEBUG("Releasing backbuffers, count: {}", desc.BufferCount);

            for (UINT i = 0; i < desc.BufferCount; i++)
            {
                ID3D12Resource* backBuffer = nullptr;
                auto bbResult = _real->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

                if (bbResult == S_OK)
                {
                    backBuffer->AddRef();

                    auto refCount = backBuffer->Release();
                    refCount = backBuffer->Release();
                    LOG_DEBUG("Current backbuffer {}, RefCount {}", i, refCount);

                    while (refCount > 1 && refCount < 4294967200ul)
                    {
                        refCount = backBuffer->Release();
                        LOG_DEBUG("Releasing backbuffer {}, RefCount {}", i, refCount);
                    }

                    LOG_DEBUG("Backbuffer {}, RefCount {}", i, refCount);
                }
                else
                {
                    LOG_DEBUG("GetBuffer failed for index {}: {:X}", i, (UINT) bbResult);
                    break;
                }
            }
        }
        */

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

#ifdef DXGI_DEBUG_ENABLED
    ReportDXGILiveObjects();

#ifdef ENABLE_DEBUG_LAYER_DX12
    ReportD3D12LiveObjects(State::Instance().currentD3D12Device);
#endif
#endif

    if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
    {
        ScopedSkipHeapCapture skipHeapCapture {};

        _lastFlags = SwapChainFlags;
        result = _real3->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask,
                                        ppPresentQueue);
    }
    else
    {
        _lastFlags = SwapChainFlags;
        result = _real3->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask,
                                        ppPresentQueue);
    }

    DXGI_SWAP_CHAIN_DESC1 observationDesc {};
    const DXGI_FORMAT observationFormat = SUCCEEDED(result) && _real1 != nullptr &&
        SUCCEEDED(_real1->GetDesc1(&observationDesc)) ? observationDesc.Format : DXGI_FORMAT_UNKNOWN;
    // Failed resize retains its old queue. Heterogeneous per-buffer queues
    // cannot be ordered by Inspector's single-queue physical-output path.
    using namespace Neurotic::Semantic::Character;
    const auto queueUpdate=CharacterQueueUpdate(SUCCEEDED(result),BufferCount?BufferCount:desc.BufferCount,ppPresentQueue);
    if(queueUpdate!=QueueUpdate::Keep){
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> replacement;
        const bool queueValid=ppPresentQueue[0]&&
            SUCCEEDED(ppPresentQueue[0]->QueryInterface(IID_PPV_ARGS(&replacement)))&&
            replacement->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT;
        _inspectorQueueQualified=queueUpdate==QueueUpdate::Uniform&&queueValid;
        // Other inherited overlay users still select the first new queue.
        // Inspector conservatively refuses the mixed-queue case separately.
        if(queueValid){_resizedQueue=replacement;_device=_resizedQueue.Get();State::Instance().currentCommandQueue=_resizedQueue.Get();}
    }
    const auto resizeObservation =
        DlssNr::HdrObservation::Registry::Instance().CompleteResize(_real, result, observationFormat);
    LOG_INFO("DLSS-NR HDR diagnostic: swapchain {:X} ResizeBuffers1 result {:X}, observation {}, generation {}, "
             "resize generation {}, format {}",
             (UINT64) _real, (UINT) result, resizeObservation.observationSequence,
             resizeObservation.generation, resizeObservation.resizeGeneration,
             (UINT) resizeObservation.format);

    if (result == DXGI_ERROR_DEVICE_REMOVED && State::Instance().currentD3D12Device != nullptr)
        Util::GetDeviceRemovedReason(State::Instance().currentD3D12Device);

#ifdef DXGI_DEBUG_ENABLED
    if (result != S_OK)
        ReadDxgiInfoQueue();
#endif

    if (result == S_OK && State::Instance().currentFeature == nullptr)
    {
        State::Instance().screenWidth = static_cast<float>(Width);
        State::Instance().screenHeight = static_cast<float>(Height);
        State::Instance().lastMipBias = 100.0f;
        State::Instance().lastMipBiasMax = -100.0f;
    }

    // Crude implementation of EndlesslyFlowering's AutoHDR-ReShade
    // https://github.com/EndlesslyFlowering/AutoHDR-ReShade
    if (Config::Instance()->ForceHDR.value_or_default())
    {
        LOG_INFO("Force HDR on");

        do
        {
            Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            DXGI_COLOR_SPACE_TYPE hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

            if (Config::Instance()->UseHDR10.value_or_default())
            {
                Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                hdrCS = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            }

            if (!Config::Instance()->SkipColorSpace.value_or_default())
            {
                UINT css = 0;

                auto result = _real3->CheckColorSpaceSupport(hdrCS, &css);

                if (result != S_OK)
                {
                    LOG_ERROR("CheckColorSpaceSupport error: {:X}", (UINT) result);
                    break;
                }

                if (DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT & css)
                {
                    result = _real3->SetColorSpace1(hdrCS);

                    const auto hdrObservation =
                        DlssNr::HdrObservation::Registry::Instance().RecordColorSpace(_real, hdrCS, result);
                    LOG_INFO("DLSS-NR HDR diagnostic: ForceHDR color-space request {} result {:X}, observation {}, "
                             "generation {}, class {}",
                             (UINT) hdrCS, (UINT) result, hdrObservation.observationSequence,
                             hdrObservation.generation,
                             DlssNr::HdrObservation::ColorClassName(
                                 DlssNr::HdrObservation::Classify(hdrObservation.colorSpace)));

                    if (result != S_OK)
                    {
                        LOG_ERROR("SetColorSpace1 error: {:X}", (UINT) result);
                        break;
                    }
                }

                LOG_INFO("HDR format and color space are set");
            }

        } while (false);
    }

    State::Instance().scBuffers.clear();
    UINT bc = BufferCount;
    if (bc == 0 && _real1 != nullptr)
    {
        DXGI_SWAP_CHAIN_DESC1 desc {};

        if (_real1->GetDesc1(&desc) == S_OK)
            bc = desc.BufferCount;
    }

    for (UINT i = 0; i < bc; i++)
    {
        IUnknown* buffer;

        if (_real->GetBuffer(i, IID_PPV_ARGS(&buffer)) == S_OK)
        {
            State::Instance().scBuffers.push_back(buffer);
            buffer->Release();
        }
    }

    LOG_DEBUG("result: {0:X}", (UINT) result);

    if (State::Instance().activeFgOutput == FGOutput::FSRFG &&
        Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Releasing ffxMutex: {}", State::Instance().currentFG->Mutex.getOwner());
        State::Instance().currentFG->Mutex.unlockThis(3);
    }

    return result;
}

//
HRESULT STDMETHODCALLTYPE WrappedIDXGISwapChain4::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size,
                                                                 void* pMetaData)
{
    const HRESULT result = _real4 != nullptr ? _real4->SetHDRMetaData(Type, Size, pMetaData)
                                              : DXGI_ERROR_DEVICE_REMOVED;
    const auto observation =
        DlssNr::HdrObservation::Registry::Instance().RecordMetadata(_real, Type, Size, pMetaData, result);
    LOG_INFO("DLSS-NR HDR diagnostic: swapchain {:X} metadata type {} size {} result {:X}, observation {}, "
             "generation {}, metadata generation {}, bounded hash {:X}",
             (UINT64) _real, (UINT) Type, Size, (UINT) result, observation.observationSequence,
             observation.generation, observation.metadataGeneration, observation.requestedMetadataHash);
    return result;
}
