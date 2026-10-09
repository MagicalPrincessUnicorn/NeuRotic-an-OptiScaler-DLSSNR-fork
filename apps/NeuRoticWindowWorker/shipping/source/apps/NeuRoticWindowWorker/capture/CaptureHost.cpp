#include "../nr/OwnedFrameRing.h"
// Magpie GPL v3 source adaptation; exact donor license preserved in references/window-nr.
// Adapted from Magpie 69d6105d; see references/window-nr/capture-provenance.json.
#include "../WorkerContracts.h"
#include "../diagnostics/FrameProbe.h"
#include "ComparisonOutput.h"
#include "SnapshotFile.h"
#include "InteractionPolicy.h"
#include "../control/WorkerWake.h"
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <d3d11_4.h>
#include <dcomp.h>
#include <roapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <sstream>

namespace nrw {
namespace {
using namespace winrt::Windows::Graphics::Capture;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::SizeInt32;
constexpr wchar_t OutputClass[] = L"NeuRoticWindowWorkerOutput";
constexpr DWORD MaxGpuWaitMs = 500;
constexpr int DisengageHotkey = 0x4E52;
std::string Failure(const char* what, HRESULT hr) {
    std::ostringstream out; out << what << " (HRESULT 0x" << std::hex << static_cast<uint32_t>(hr) << ')'; return out.str();
}
bool Candidate(HWND hwnd, WindowIdentity& id, std::string& reason, bool allowUnavailable=false) {
    id = {}; reason.clear();
    auto reject = [&](const char* text) { reason = text; return false; };
    if (!IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return reject("source is not a valid top-level window");
    if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) return reject("desktop/shell capture is excluded");
    if (!allowUnavailable && (!IsWindowVisible(hwnd) || IsIconic(hwnd))) return reject("source is hidden or minimized");
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & (WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return reject("transparent, tool and nonactivating windows are excluded");
    if (GetWindow(hwnd, GW_OWNER) && !(ex & WS_EX_APPWINDOW)) return reject("owned popup windows are excluded");
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId()) return reject("worker/self windows are excluded");
    DWORD cloaked = 0;
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) || (!allowUnavailable&&cloaked)) return reject("source is cloaked or unavailable to DWM");
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect))) || rect.right <= rect.left || rect.bottom <= rect.top || (rect.right - rect.left < 50 && rect.bottom - rect.top < 50)) return reject("source has no usable DWM extent");
    wchar_t title[4096]{}, cls[256]{};
    if (!GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)))) return reject("source title is empty or unavailable");
    if (!GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)))) return reject("source class unavailable");
    if (wcscmp(cls, OutputClass) == 0 || wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Xaml_WindowedPopupClass") == 0) return reject("worker, shell and popup classes are excluded");
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return reject("source process identity is inaccessible");
    FILETIME created{}, exited{}, kernel{}, user{};
    wchar_t path[32768]{}; DWORD length = static_cast<DWORD>(std::size(path));
    bool valid = GetProcessTimes(process, &created, &exited, &kernel, &user) && QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    ULARGE_INTEGER creation{}; creation.LowPart = created.dwLowDateTime; creation.HighPart = created.dwHighDateTime;
    DWORD checkPid = 0; GetWindowThreadProcessId(hwnd, &checkPid);
    if (!valid || !creation.QuadPart || !IsWindow(hwnd) || checkPid != pid) return reject("source identity changed or process creation is unreadable");
    id.hwnd = reinterpret_cast<uint64_t>(hwnd); id.pid = pid; id.processCreation = creation.QuadPart;
    id.title = title; id.windowClass = cls; id.executable.assign(path, length);
    id.x = rect.left; id.y = rect.top; id.width = static_cast<uint32_t>(rect.right - rect.left); id.height = static_cast<uint32_t>(rect.bottom - rect.top);
    return true;
}
ComPtr<IDXGIAdapter> SourceAdapter(HWND hwnd) {
    ComPtr<IDXGIFactory1> factory; ComPtr<IDXGIAdapter> selected;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return selected;
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    for (UINT ai = 0; ; ++ai) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(ai, &adapter) == DXGI_ERROR_NOT_FOUND || !adapter) break;
        for (UINT oi = 0; ; ++oi) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(oi, &output) == DXGI_ERROR_NOT_FOUND || !output) break;
            DXGI_OUTPUT_DESC desc{}; if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) { adapter.As(&selected); return selected; }
        }
    }
    return selected;
}
bool SameDevice(ID3D11Texture2D* texture, ID3D11Device* device) {
    ComPtr<ID3D11Device> owner; texture->GetDevice(&owner); return owner.Get() == device;
}
bool Identity(const WindowIdentity& expected,std::string& reason,bool allowUnavailable=false){
    WindowIdentity current;
    if(!expected.hwnd||!expected.pid||!expected.processCreation){reason="complete stable source identity is required";return false;}
    if(!Candidate(reinterpret_cast<HWND>(expected.hwnd),current,reason,allowUnavailable))return false;
    if(current.pid!=expected.pid||current.processCreation!=expected.processCreation||current.windowClass!=expected.windowClass||current.executable!=expected.executable){reason="source HWND/PID/process creation identity changed";return false;}
    reason.clear();return true;
}
// Adapt Magpie CalcWindowCapturedFrameBounds/_CaptureWindow into a read-only
// client crop. Match the actual WGC content size before assuming any origin.
bool ClientCrop(HWND hwnd, SizeInt32 content, D3D11_BOX& crop, std::string& reason) {
    RECT client{}, bounds{};
    if (!GetClientRect(hwnd, &client)) { reason = "client geometry unavailable"; return false; }
    SetLastError(ERROR_SUCCESS);
    if (!MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&client), 2) && GetLastError() != ERROR_SUCCESS) { reason = "client screen geometry unavailable"; return false; }
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) { reason = "DWM capture bounds unavailable"; return false; }
    auto matches = [&](const RECT& r) { return r.right - r.left == content.Width && r.bottom - r.top == content.Height; };
    if (!matches(bounds)) {
        // Magpie's pre-24H2 maximized-window correction: off-screen client or
        // intersection with monitor work area. Use only if confirmed by WGC size.
        bool matched = false;
        if (IsZoomed(hwnd)) {
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
                RECT alternate{};
                if (client.top < monitor.rcWork.top) alternate = client;
                else IntersectRect(&alternate, &bounds, &monitor.rcWork);
                if (matches(alternate)) { bounds = alternate; matched = true; }
            }
        }
        BOOL border = TRUE;
        if (!matched && SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_NCRENDERING_ENABLED, &border, sizeof(border))) && !border && matches(client)) { bounds = client; matched = true; }
        if (!matched) { reason = "WGC content extent does not establish a known client crop origin"; return false; }
    }
    if (client.left < bounds.left || client.top < bounds.top || client.right > bounds.right || client.bottom > bounds.bottom || client.right <= client.left || client.bottom <= client.top) { reason = "client crop is outside confirmed WGC bounds"; return false; }
    crop = {static_cast<UINT>(client.left - bounds.left), static_cast<UINT>(client.top - bounds.top), 0,
        static_cast<UINT>(client.right - bounds.left), static_cast<UINT>(client.bottom - bounds.top), 1};
    return true;
}
}

struct CaptureHost::Impl {
    CaptureOptions options;
    mutable CaptureStatistics statistics;
    std::shared_ptr<std::atomic_uint64_t> arrivals=std::make_shared<std::atomic_uint64_t>(0);
    // Callbacks retain their own event owner through unsubscribe/Stop races.
    std::shared_ptr<WakeEvent> wake=std::make_shared<WakeEvent>();
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Fence> completion;
    ComPtr<ID3D11DeviceContext4> completionContext;
    winrt::handle completionEvent;
    uint64_t completionValue=0;
    uint64_t outputEpoch=0;
    FrameProbe* inFlightProbe=nullptr; // owner-thread borrower; worker probe outlives this backend
#ifdef NRW_CAPTURE_TEST
    int completionFault=0;
#endif
    void InitializeCompletion() {
        ComPtr<ID3D11Device5> fenceDevice;
        winrt::check_hresult(device.As(&fenceDevice));
        winrt::check_hresult(context.As(&completionContext));
        winrt::check_hresult(fenceDevice->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&completion)));
        completionEvent.attach(CreateEventW(nullptr,FALSE,FALSE,nullptr));
        if(!completionEvent)winrt::throw_last_error();
    }
    ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<IDCompositionDevice> composition;
    ComPtr<IDCompositionTarget> compositionTarget;
    ComPtr<IDCompositionVisual> compositionVisual;
    ComPtr<ID3D11Texture2D> inFlightSnapshot, inFlightSource, inFlightProcessed, inFlightBackbuffer;
    struct CaptureSlot {bool retired=true;FrameStamp stamp;ComPtr<ID3D11Texture2D> texture;};
    OwnedFrameRing<CaptureSlot> snapshots;
    ComPtr<ID3D11Texture2D> snapshotReadback; // retained with Impl if GPU completion is unproved
    std::shared_ptr<void> inFlightCaptureLease,inFlightProcessedLease;
    ComparisonOutput comparison;
    IDirect3DDevice wrapped{nullptr};
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    Direct3D11CaptureFrame inFlightFrame{nullptr};
    std::shared_ptr<std::atomic_bool> itemClosed = std::make_shared<std::atomic_bool>(false);
    winrt::event_token closedToken{}, frameToken{};
    bool closedSubscribed = false, frameSubscribed = false;
    HWND output = nullptr;
    SizeInt32 extent{};
    D3D11_BOX crop{};
    UINT clientWidth = 0, clientHeight = 0;
    UINT swapWidth = 0, swapHeight = 0;
    uint64_t sequence = 0, latestTimestamp = 0, geometryEpoch=1, streamEpoch=1;
    ULONGLONG geometryWaitStarted = 0;
    DWORD thread = 0;
    bool active = false, closed = false, reset = true, unknownGpu = false, apartment = false, pendingGpu = false, paused=false;
    bool cursorCaptured=false,hotkeyRegistered=false;
    std::string fatal;
    bool InteractionAvailable() const {
        auto source=reinterpret_cast<HWND>(options.target.hwnd);
        auto foreground=GetForegroundWindow();DWORD foregroundPid=0;
        GetWindowThreadProcessId(foreground,&foregroundPid);
        statistics.foregroundHwnd=reinterpret_cast<uint64_t>(foreground);
        statistics.foregroundPid=foregroundPid;statistics.guiFlags=0;
        auto result=[&](const char* why,bool available){statistics.interactionReason=why;return available;};
        if(!options.overlay)return result("preview",true);
        GUITHREADINFO gui{sizeof(gui)};
        const bool guiKnown=GetGUIThreadInfo(GetWindowThreadProcessId(source,nullptr),&gui)!=FALSE;
        statistics.guiFlags=gui.flags;
        const bool ownedDialog=foreground&&foreground!=source&&foreground!=output&&GetAncestor(foreground,GA_ROOTOWNER)==source;
        if(auto why=InteractionBlockReason(options.overlay,IsWindowEnabled(source)!=FALSE,ownedDialog,guiKnown,gui.flags))return result(why,false);
        // Foreground changes alone do not revoke capture. The owned, non-topmost,
        // nonactivating output stays with its source behind other applications.
        return result(foreground==source?"source-active":foregroundPid==options.controlHostPid?"app-controls-active":"source-background",true);
    }
    void PauseInteraction() {if(!paused)++streamEpoch;paused=true;reset=true;if(options.overlay&&output)ShowWindow(output,SW_HIDE);}
    void EndInteraction() {
        active=false;closed=true;
        if(output&&IsWindow(output)) {
            ShowWindow(output,SW_HIDE);
            if(hotkeyRegistered)UnregisterHotKey(output,DisengageHotkey);
            // Retained GPU objects may outlive Stop. A source restore must never reshow this popup.
            if(options.overlay)SetWindowLongPtrW(output,GWLP_HWNDPARENT,0);
        }
        hotkeyRegistered=false;
    }
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) { self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        if(self && self->options.overlay && msg==WM_NCHITTEST)return HTTRANSPARENT;
        if(self && self->options.overlay && msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if (self && (msg == WM_CLOSE || (msg == WM_KEYDOWN && wp == VK_ESCAPE) ||
            (msg==WM_HOTKEY && wp==DisengageHotkey && self->hotkeyRegistered))) {self->EndInteraction();return 0;}
        if (self && msg == WM_DESTROY) { self->closed = true; self->output = nullptr; return 0; }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    bool Owner(std::string& reason) const {
        if (thread && thread != GetCurrentThreadId()) { reason = "capture API must run on its owning thread"; return false; } return true;
    }
    bool Follow(std::string& reason,UINT expectedWidth=0,UINT expectedHeight=0) {
        if (!options.overlay || !output) return true;
        auto sourceWindow=reinterpret_cast<HWND>(options.target.hwnd);
        RECT client{};
        if (!GetClientRect(sourceWindow,&client)) { reason="overlay source geometry unavailable"; return false; }
        SetLastError(ERROR_SUCCESS);
        if (!MapWindowPoints(sourceWindow,nullptr,reinterpret_cast<POINT*>(&client),2) && GetLastError()!=ERROR_SUCCESS) { reason="overlay source screen geometry unavailable"; return false; }
        if(expectedWidth && (client.right-client.left!=LONG(expectedWidth)||client.bottom-client.top!=LONG(expectedHeight))){reset=true;ShowWindow(output,SW_HIDE);reason.clear();return false;}
        // Owned, non-topmost output stays above its source without covering unrelated foreground apps.
        if (!SetWindowPos(output,nullptr,client.left,client.top,client.right-client.left,client.bottom-client.top,SWP_NOZORDER|SWP_NOACTIVATE)) { reason="overlay geometry update failed"; return false; }
        if(swapWidth && (client.right-client.left!=LONG(swapWidth)||client.bottom-client.top!=LONG(swapHeight)))ShowWindow(output,SW_HIDE);
        return true;
    }
    bool Drain(std::string& reason) {
        if (unknownGpu) { reason=fatal; return false; }
        if (!pendingGpu) return true;
        auto fail=[&](const char* message,HRESULT hr) {
            if(inFlightProbe)inFlightProbe->Unknown("Comparison GPU completion is unproved");
            unknownGpu=true;active=false;fatal=reason=Failure(message,hr);return false;
        };
        if (!completion || !completionContext || !completionEvent || completionValue>=UINT64_MAX-1)
            return fail("GPU completion unavailable; resources quarantined",E_FAIL);
        const uint64_t required=++completionValue;
        if(inFlightProbe)inFlightProbe->RequireComparison(completion.Get(),required);
        HRESULT hr;
#ifdef NRW_CAPTURE_TEST
        hr=completionFault==1 ? E_FAIL : completionContext->Signal(completion.Get(),required);
#else
        hr=completionContext->Signal(completion.Get(),required);
#endif
        if(FAILED(hr))return fail("GPU completion signal failed; resources quarantined",hr);
        context->Flush();
        auto completed=[&]() -> uint64_t {
#ifdef NRW_CAPTURE_TEST
            if(completionFault==4)return UINT64_MAX;
            if(completionFault>=2)return 0;
#endif
            return completion->GetCompletedValue();
        };
        auto value=completed();
        if(value==UINT64_MAX)return fail("GPU device removed; resources quarantined",DXGI_ERROR_DEVICE_REMOVED);
        if(value<required) {
            if(!ResetEvent(completionEvent.get()))return fail("GPU completion event reset failed; resources quarantined",HRESULT_FROM_WIN32(GetLastError()));
#ifdef NRW_CAPTURE_TEST
            hr=completionFault==2 ? E_FAIL : completion->SetEventOnCompletion(required,completionEvent.get());
#else
            hr=completion->SetEventOnCompletion(required,completionEvent.get());
#endif
            if(FAILED(hr))return fail("GPU completion event failed; resources quarantined",hr);
            DWORD wait;
#ifdef NRW_CAPTURE_TEST
            wait=completionFault==3 ? WAIT_TIMEOUT : completionFault==5 ? WAIT_OBJECT_0 : completionFault==6 ? WAIT_FAILED : WaitForSingleObject(completionEvent.get(),MaxGpuWaitMs);
#else
            wait=WaitForSingleObject(completionEvent.get(),MaxGpuWaitMs);
#endif
            if(wait!=WAIT_OBJECT_0)return fail("GPU completion wait failed or timed out; resources quarantined",wait==WAIT_TIMEOUT ? HRESULT_FROM_WIN32(WAIT_TIMEOUT) : E_FAIL);
            value=completed();
            if(value==UINT64_MAX || value<required)return fail("GPU completion unproved after wake; resources quarantined",E_FAIL);
        }
        // Only this exact fence value establishes retirement. A wake alone never does.
        if(inFlightProbe){inFlightProbe->ObserveComparison(value);inFlightProbe=nullptr;}
        pendingGpu=false;
        comparison.ReleaseFrameViews();
        inFlightSnapshot.Reset();inFlightSource.Reset();inFlightProcessed.Reset();inFlightBackbuffer.Reset();inFlightCaptureLease.reset();inFlightProcessedLease.reset();
        if(inFlightFrame) {
            try {inFlightFrame.Close();inFlightFrame=nullptr;}
            catch(const winrt::hresult_error& e) {return fail("capture frame closure failed; resources quarantined",e.code());}
        }
        return true;
    }
    bool Output(std::string& reason) {
        WNDCLASSW cls{}; cls.lpfnWndProc = WindowProc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = OutputClass; cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { reason = "register output class failed"; return false; }
        RECT r{0, 0, extent.Width, extent.Height}; if(!options.overlay)AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        output = CreateWindowExW(options.overlay?WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW:0, OutputClass, L"NeuRotic window output - Stop from the App", options.overlay?WS_POPUP:WS_OVERLAPPEDWINDOW,
            options.overlay?0:CW_USEDEFAULT, options.overlay?0:CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, options.overlay?reinterpret_cast<HWND>(options.target.hwnd):nullptr, nullptr, cls.hInstance, this);
        if (!output) { reason = "create output window failed"; return false; }
        if(options.overlay)hotkeyRegistered=RegisterHotKey(output,DisengageHotkey,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F10)!=FALSE;
        if(options.overlay && (!SetLayeredWindowAttributes(output,0,255,LWA_ALPHA)||!Follow(reason))) { if(reason.empty())reason="overlay mouse pass-through unavailable"; return false; }
        if (!SetWindowDisplayAffinity(output, WDA_EXCLUDEFROMCAPTURE)) { reason = "output capture exclusion unavailable"; return false; }
        ComPtr<IDXGIDevice> dxgiDevice; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
        HRESULT hr = device.As(&dxgiDevice);
        if (SUCCEEDED(hr)) hr = dxgiDevice->GetAdapter(&adapter);
        if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
        DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = extent.Width; desc.Height = extent.Height; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2;
        desc.SwapEffect = options.overlay?DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:DXGI_SWAP_EFFECT_FLIP_DISCARD; desc.Scaling = DXGI_SCALING_STRETCH;
        if (SUCCEEDED(hr)) hr = options.overlay?factory->CreateSwapChainForComposition(device.Get(),&desc,nullptr,&swapchain):factory->CreateSwapChainForHwnd(device.Get(), output, &desc, nullptr, nullptr, &swapchain);
        if (FAILED(hr)) { reason = Failure("output swapchain creation failed", hr); return false; }
        if(options.overlay){
            hr=DCompositionCreateDevice(dxgiDevice.Get(),IID_PPV_ARGS(&composition));
            if(SUCCEEDED(hr))hr=composition->CreateTargetForHwnd(output,TRUE,&compositionTarget);
            if(SUCCEEDED(hr))hr=composition->CreateVisual(&compositionVisual);
            if(SUCCEEDED(hr))hr=compositionVisual->SetContent(swapchain.Get());
            if(SUCCEEDED(hr))hr=compositionTarget->SetRoot(compositionVisual.Get());
            if(SUCCEEDED(hr))hr=composition->Commit();
            if(FAILED(hr)){reason=Failure("overlay composition creation failed",hr);return false;}
        }
        factory->MakeWindowAssociation(output, DXGI_MWA_NO_ALT_ENTER);
        swapWidth = desc.Width; swapHeight = desc.Height;++outputEpoch;
        // Overlay becomes visible only after an accepted matching frame, never during model setup.
        if(!options.overlay){ShowWindow(output,SW_SHOWNOACTIVATE);UpdateWindow(output);}return true;
    }
};

CaptureHost::CaptureHost() : impl_(std::make_unique<Impl>()) {}
CaptureHost::~CaptureHost() {
    try { Stop(); } catch (...) { if (impl_) impl_->unknownGpu = true; }
    if (impl_ && impl_->unknownGpu) {
        // Deliberate process-lifetime quarantine: do not run COM/WGC destructors on unknown work.
        (void)impl_.release();
    }
}
bool CaptureHost::Inspect(HWND hwnd, WindowIdentity& id, std::string& reason) { return Candidate(hwnd, id, reason); }
bool CaptureHost::Validate(const WindowIdentity& expected, std::string& reason) {
    return Identity(expected,reason);
}
std::vector<WindowIdentity> CaptureHost::Enumerate() {
    std::vector<WindowIdentity> windows;
    EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL {
        WindowIdentity id; std::string reason;
        try { if (Candidate(hwnd, id, reason)) reinterpret_cast<std::vector<WindowIdentity>*>(parameter)->push_back(std::move(id)); } catch (...) {}
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows));
    std::sort(windows.begin(), windows.end(), [](const auto& a, const auto& b) { return a.title == b.title ? a.hwnd < b.hwnd : a.title < b.title; });
    return windows;
}
bool CaptureHost::Start(const CaptureOptions& options, std::string& reason) {
    reason.clear();
    if (!impl_->Owner(reason)) return false;
    if (impl_->unknownGpu) { reason = impl_->fatal; return false; }
    Stop();
    auto& p = *impl_; p.thread = GetCurrentThreadId();
    if (!options.session) { reason = "nonzero capture session is required"; return false; }
    if (!Validate(options.target, reason)) return false;
    p.options = options;
    HRESULT apartment = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) { reason = Failure("WinRT initialization failed", apartment); return false; }
    p.apartment = SUCCEEDED(apartment);
    const char* phase = "support probe";
    try {
        if (!GraphicsCaptureSession::IsSupported()) { reason = "Windows Graphics Capture is unsupported"; Stop(); return false; }
        phase = "D3D11 device";
        auto adapter = SourceAdapter(reinterpret_cast<HWND>(options.target.hwnd)); D3D_FEATURE_LEVEL level{};
        winrt::check_hresult(D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &p.device, &level, &p.context));
        phase = "completion fence";
        p.InitializeCompletion();
        phase = "WinRT D3D11 device wrapping";
        ComPtr<IDXGIDevice> dxgi; winrt::check_hresult(p.device.As(&dxgi));
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), reinterpret_cast<IInspectable**>(winrt::put_abi(p.wrapped))));
        phase = "capture item activation";
        auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        phase = "capture item for selected HWND";
        winrt::check_hresult(interop->CreateForWindow(reinterpret_cast<HWND>(options.target.hwnd), winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(p.item)));
        p.closedToken = p.item.Closed([closed = p.itemClosed,wake=p.wake](const auto&, const auto&) { closed->store(true, std::memory_order_release);wake->Signal(); });
        p.closedSubscribed = true;
        if (!Validate(options.target, reason)) { Stop(); return false; }
        p.extent = p.item.Size();
        if (p.extent.Width <= 0 || p.extent.Height <= 0) { reason = "capture item extent is empty"; Stop(); return false; }
        phase = "WGC frame pool";
        p.pool = Direct3D11CaptureFramePool::CreateFreeThreaded(p.wrapped, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, p.extent);
        // Magpie subscribes even when the callback has no frame-copy work.
        // Free-threaded delivery likewise needs an event consumer; copy remains owner-thread only.
        p.frameToken = p.pool.FrameArrived([count=p.arrivals,wake=p.wake](const auto&, const auto&) {count->fetch_add(1,std::memory_order_relaxed);wake->Signal();}); p.frameSubscribed = true;
        phase = "WGC capture session";
        p.session = p.pool.CreateCaptureSession(p.item);
        // Source owns the live system cursor; never process a delayed duplicate in an overlay.
        p.cursorCaptured=options.cursor&&!options.overlay;
        if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(winrt::name_of<GraphicsCaptureSession>(), L"IsCursorCaptureEnabled")) p.session.IsCursorCaptureEnabled(p.cursorCaptured);
        else if(options.overlay)throw std::runtime_error("Overlay requires cursor capture control on Windows 10 2004 or later");
        if (!p.Output(reason)) { Stop(); return false; }
        phase = "WGC StartCapture";
        p.session.StartCapture(); p.active = true; return true;
    } catch (const winrt::hresult_error& e) { reason = Failure(phase, e.code()); Stop(); return false; }
      catch (const std::exception& e) { reason = e.what(); Stop(); return false; }
}
bool CaptureHost::Next(CapturedFrame& result, std::string& reason) {
    result = {}; reason.clear(); auto& p = *impl_;
    if (!p.Owner(reason)) return false;
    if (!p.active) { reason = p.fatal.empty() ? "capture is stopped" : p.fatal; return false; }
    if(p.paused)return false;
    if(!p.InteractionAvailable()){p.PauseInteraction();return false;}
    if (p.itemClosed->load(std::memory_order_acquire)) { reason = p.fatal = "capture item closed"; p.active = false; return false; }
    if (!Validate(p.options.target, reason)) {
        if(Identity(p.options.target,reason,true)){p.PauseInteraction();reason.clear();return false;}
        p.active = false; p.fatal = reason; return false;
    }
    try {
        // At most two polls: keep the newest of the bounded two-buffer WGC pool.
        // Consume before polling: arrivals racing with/after the poll stay signaled.
        p.wake->Consume();
        auto frame = p.pool.TryGetNextFrame(); if (!frame) return false;
        ++p.statistics.dequeued;
        auto newer = p.pool.TryGetNextFrame(); if (newer) { ++p.statistics.dequeued;++p.statistics.superseded;frame.Close(); frame = std::move(newer); p.reset=true; }
        SizeInt32 size = frame.ContentSize();
        if (size.Width <= 0 || size.Height <= 0) { frame.Close(); reason = "source frame has empty extent"; p.active = false; return false; }
        if (size.Width != p.extent.Width || size.Height != p.extent.Height) {
            frame.Close(); p.pool.Recreate(p.wrapped, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size); p.extent = size; ++p.geometryEpoch; p.reset = true; return false;
        }
        D3D11_BOX crop{};
        if (!ClientCrop(reinterpret_cast<HWND>(p.options.target.hwnd), size, crop, reason)) {
            frame.Close();
            // WGC may deliver the old raster after a Win32 resize already changed
            // client coordinates. Drop that transitional frame, never guess its origin.
            if (!p.geometryWaitStarted) p.geometryWaitStarted = GetTickCount64();
            if (GetTickCount64() - p.geometryWaitStarted < 1000) { p.reset = true; reason.clear(); return false; }
            p.fatal = reason; p.active = false; return false;
        }
        p.geometryWaitStarted = 0;
        if (crop.left != p.crop.left || crop.top != p.crop.top || crop.right != p.crop.right || crop.bottom != p.crop.bottom) {++p.geometryEpoch;p.reset = true;}
        p.crop = crop; p.clientWidth = crop.right - crop.left; p.clientHeight = crop.bottom - crop.top;
        const auto sourceTime=frame.SystemRelativeTime().count();
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        if(sourceTime<=0||frequency.QuadPart<=0) {frame.Close();reason="source compositor clock unavailable";p.active=false;return false;}
        const uint64_t sourceQpc=uint64_t(sourceTime/10000000)*uint64_t(frequency.QuadPart)+uint64_t(sourceTime%10000000)*uint64_t(frequency.QuadPart)/10000000;
        if(sourceQpc<=p.latestTimestamp){frame.Close();++p.streamEpoch;p.reset=true;return false;}
        if(p.latestTimestamp&&sourceQpc-p.latestTimestamp>uint64_t(frequency.QuadPart)/10){++p.streamEpoch;p.reset=true;}
        p.inFlightFrame = std::move(frame);
        auto access = p.inFlightFrame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&p.inFlightSource)));
        D3D11_TEXTURE2D_DESC sourceDesc{}; p.inFlightSource->GetDesc(&sourceDesc);
        if (sourceDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM || sourceDesc.Width < static_cast<UINT>(size.Width) || sourceDesc.Height < static_cast<UINT>(size.Height)) winrt::throw_hresult(E_INVALIDARG);
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = p.clientWidth; desc.Height = p.clientHeight; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        auto slot=p.snapshots.TryAcquire();
        if(!slot){p.inFlightFrame.Close();p.inFlightFrame=nullptr;p.inFlightSource.Reset();p.reset=true;reason.clear();return false;}
        if(slot->texture){D3D11_TEXTURE2D_DESC previous{};slot->texture->GetDesc(&previous);
            if(previous.Width!=desc.Width||previous.Height!=desc.Height)slot->texture.Reset();}
        if(!slot->texture)winrt::check_hresult(p.device->CreateTexture2D(&desc,nullptr,&slot->texture));
        p.inFlightSnapshot=slot->texture;p.inFlightCaptureLease=slot;
        p.pendingGpu = true; p.context->CopySubresourceRegion(p.inFlightSnapshot.Get(), 0, 0, 0, 0, p.inFlightSource.Get(), 0, &p.crop);
        auto snapshot = p.inFlightSnapshot;
        if (!p.Drain(reason)) return false;
        LARGE_INTEGER qpc{}; QueryPerformanceCounter(&qpc);
        p.latestTimestamp = sourceQpc;result.acquiredQpc=static_cast<uint64_t>(qpc.QuadPart);
        result.stamp = {p.options.session, ++p.sequence, p.latestTimestamp, desc.Width, desc.Height, p.reset,p.streamEpoch,p.geometryEpoch,1};
        slot->stamp=result.stamp;slot->retired=true;result.ownership=slot;
        result.texture = std::move(snapshot); p.reset = false; return true;
    } catch (const winrt::hresult_error& e) { reason = p.fatal = Failure("capture frame failed", e.code()); p.active = false; return false; }
}
uint64_t CaptureHost::OutputEpoch() const {return impl_&&impl_->swapchain?impl_->outputEpoch:0;}
bool CaptureHost::Present(const CapturedFrame& original, ID3D11Texture2D* processed, float split, std::string& reason, int stripes, SnapshotResult* snapshot, int direction, std::shared_ptr<void> processedOwnership,FrameProbe* probe) {
    reason.clear(); auto& p = *impl_;
    struct MeasurementFrameScope {
        FrameProbe* probe;bool accepted=false;
        ~MeasurementFrameScope(){if(probe&&!accepted)probe->Invalidate("Output frame, source geometry or presentation changed before acceptance");}
    } measurementFrame{probe};
    p.statistics.presentCallMs.reset();
    if (!p.Owner(reason)) return false;
    if (!p.active || p.closed || !p.output || !IsWindow(p.output)) { reason = p.fatal.empty() ? "output is stopped" : p.fatal; return false; }
    if(p.paused||!p.InteractionAvailable()){p.PauseInteraction();return false;}
    if (!Validate(p.options.target, reason)) {
        if(Identity(p.options.target,reason,true)){p.PauseInteraction();reason.clear();return false;}
        p.active = false; p.fatal = reason; return false;
    }
    if (p.itemClosed->load(std::memory_order_acquire)) { reason = p.fatal = "capture item closed"; p.active = false; return false; }
    if (!original.texture || original.stamp.session != p.options.session || !original.stamp.sequence || (original.pairedDepthOutput ? (original.stamp.sequence>p.sequence||original.stamp.timestampQpc>p.latestTimestamp||original.stamp.streamEpoch!=p.streamEpoch||original.stamp.geometryEpoch!=p.geometryEpoch) : (original.stamp.sequence!=p.sequence||original.stamp.timestampQpc!=p.latestTimestamp)) || original.stamp.width != p.clientWidth || original.stamp.height != p.clientHeight) { reason = original.pairedDepthOutput?"":"original frame has stale session, sequence, timestamp or extent"; return false; }
    if(original.pairedDepthOutput){LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);if(!depth::PublicationFresh(original.stamp.timestampQpc,uint64_t(now.QuadPart),uint64_t(frequency.QuadPart)))return false;D3D11_BOX currentCrop{};if(!ClientCrop(reinterpret_cast<HWND>(p.options.target.hwnd),p.extent,currentCrop,reason)||currentCrop.left!=p.crop.left||currentCrop.top!=p.crop.top||currentCrop.right!=p.crop.right||currentCrop.bottom!=p.crop.bottom){++p.geometryEpoch;p.reset=true;reason.clear();return false;}}
    if(!p.Follow(reason,original.stamp.width,original.stamp.height))return false;
    if(stripes<0||stripes==1||stripes>32){reason="stripes must be zero or 2 to 32";return false;}
    if(direction<0||direction>7){reason="comparison direction must be zero to seven";return false;}
    if (!std::isfinite(split) || split > 1 || split < -1) { reason = "split must be between -1 and 1"; return false; }
    auto compatible = [&](ID3D11Texture2D* texture) {
        if (!texture || !SameDevice(texture, p.device.Get())) return false;
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
        return desc.Width == original.stamp.width && desc.Height == original.stamp.height && desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM && desc.SampleDesc.Count == 1 && desc.ArraySize == 1 && desc.MipLevels == 1;
    };
    if (!compatible(original.texture.Get()) || (processed && !compatible(processed))) { reason = "present requires completed BGRA8 textures of matching extent on the capture device"; return false; }
    if (!processed && (split < 1 || stripes)) { reason = "processed output is unavailable"; return false; }
    try {
        if (p.swapWidth != original.stamp.width || p.swapHeight != original.stamp.height) {
            p.context->ClearState(); p.pendingGpu = true; if (!p.Drain(reason)) return false;
            winrt::check_hresult(p.swapchain->ResizeBuffers(2, original.stamp.width, original.stamp.height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
            p.swapWidth = original.stamp.width; p.swapHeight = original.stamp.height;++p.outputEpoch;
            RECT window{0, 0, static_cast<LONG>(p.swapWidth), static_cast<LONG>(p.swapHeight)};
            if (!p.options.overlay && AdjustWindowRect(&window, WS_OVERLAPPEDWINDOW, FALSE)) SetWindowPos(p.output, nullptr, 0, 0, window.right - window.left, window.bottom - window.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        winrt::check_hresult(p.swapchain->GetBuffer(0, IID_PPV_ARGS(&p.inFlightBackbuffer)));
        p.inFlightSnapshot = original.texture; p.inFlightCaptureLease=original.ownership; p.inFlightProcessedLease=std::move(processedOwnership); p.inFlightProcessed = processed; p.pendingGpu = true;
        p.comparison.Compose(p.context.Get(),p.inFlightBackbuffer.Get(),original.texture.Get(),processed,original.stamp.width,original.stamp.height,split,stripes,direction);
        if(probe&&probe->Pending()) {
            const auto& id=probe->Identity();
            if(id.frame.session!=original.stamp.session||id.frame.sequence!=original.stamp.sequence||id.frame.timestampQpc!=original.stamp.timestampQpc||id.frame.streamEpoch!=p.streamEpoch||id.frame.geometryEpoch!=p.geometryEpoch||id.frame.configEpoch!=original.stamp.configEpoch||id.comparisonDirection!=direction||split!=0||stripes!=0)
                probe->Invalidate("Comparison frame or effective configuration changed");
            else {
                DXGI_SWAP_CHAIN_DESC1 desc{};const int alphaMode=SUCCEEDED(p.swapchain->GetDesc1(&desc))?int(desc.AlphaMode):-1;
                if(probe->RecordComparison(p.context.Get(),p.inFlightBackbuffer.Get(),processed,p.outputEpoch,alphaMode,p.options.overlay))p.inFlightProbe=probe;
            }
        }
        if(snapshot) {
            D3D11_TEXTURE2D_DESC desc{};p.inFlightBackbuffer->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            auto hr=p.device->CreateTexture2D(&desc,nullptr,&p.snapshotReadback);
            if(FAILED(hr))snapshot->error="Snapshot readback allocation failed";
            else p.context->CopyResource(p.snapshotReadback.Get(),p.inFlightBackbuffer.Get());
        }
        if (!p.Drain(reason)) return false;
        ++p.statistics.presentCalls;
        const auto presentBegin=MeasureClock::now();
        HRESULT hr = p.swapchain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
        p.statistics.presentCallMs=ElapsedMs(presentBegin);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING || hr == DXGI_STATUS_OCCLUDED) { p.pendingGpu = true; p.Drain(reason); return false; }
        if (FAILED(hr)) { reason = p.fatal = Failure("output Present failed", hr); p.active = false; return false; }
        p.pendingGpu = true;const bool finished=p.Drain(reason);
        if(finished&&p.options.overlay){
            // Win32 source geometry may change while copy/present completion waits.
            if(!Validate(p.options.target,reason)){
                if(Identity(p.options.target,reason,true)){p.PauseInteraction();reason.clear();return false;}
                p.active=false;p.fatal=reason;return false;
            }
            if(!p.InteractionAvailable()){p.PauseInteraction();reason.clear();return false;}
            if(!p.Follow(reason,original.stamp.width,original.stamp.height))return false;
            if(!p.paused)ShowWindow(p.output,SW_SHOWNOACTIVATE);
        }
        if(finished&&snapshot&&p.snapshotReadback) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if(FAILED(p.context->Map(p.snapshotReadback.Get(),0,D3D11_MAP_READ,0,&mapped)))snapshot->error="Snapshot readback failed";
            else {
                try{snapshot->path=SaveSnapshot(mapped.pData,original.stamp.width,original.stamp.height,mapped.RowPitch);}
                catch(const std::exception& error){snapshot->error=error.what();}
                p.context->Unmap(p.snapshotReadback.Get(),0);
            }
            p.snapshotReadback.Reset();
        }
        measurementFrame.accepted=finished;return finished;
    } catch (const winrt::hresult_error& e) { reason = p.fatal = Failure("output presentation failed", e.code()); p.active = false; return false; }
}
bool CaptureHost::Pump() {
    auto& p = *impl_; std::string reason;
    if (!p.Owner(reason) || !p.active) return false;
    MSG msg{}; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { p.closed = true; break; }
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (p.itemClosed->load(std::memory_order_acquire) || p.closed || !p.output || !IsWindow(p.output) || !Identity(p.options.target, reason,true)) { p.active = false; if (!reason.empty()) p.fatal = reason; return false; }
    auto source=reinterpret_cast<HWND>(p.options.target.hwnd);DWORD cloaked=0;
    const bool unavailable=!IsWindowVisible(source)||IsIconic(source)||FAILED(DwmGetWindowAttribute(source,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))||cloaked||!p.InteractionAvailable();
    if(unavailable){if(!IsWindowVisible(source)||IsIconic(source)||cloaked)p.statistics.interactionReason="source-hidden-minimized-or-cloaked";p.PauseInteraction();return true;}
    if(p.paused){
        try{p.pool.Recreate(p.wrapped,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,p.extent);}
        catch(const winrt::hresult_error& e){p.fatal=Failure("resume frame pool failed",e.code());p.active=false;return false;}
        // Keep the pre-pause raster hidden until a new matching Present completes.
        p.paused=false;p.reset=true;
        if(p.options.overlay)ShowWindow(p.output,SW_HIDE); // owner restore can automatically reshow owned popups
    }
    if(!p.Follow(reason)){p.fatal=reason;p.active=false;return false;}
    return true;
}
void CaptureHost::Stop() {
    if (!impl_) return;
    auto& p = *impl_; std::string ignored;
    if (!p.Owner(ignored)) { p.unknownGpu = true; p.fatal = ignored; p.active = false; return; }
    p.EndInteraction();
    if (p.unknownGpu) return;
    if (p.context && p.completion) { p.pendingGpu = true; if (!p.Drain(ignored)) return; }
    else if (p.pendingGpu && !p.Drain(ignored)) return;
    try {
        if (p.closedSubscribed && p.item) { p.item.Closed(p.closedToken); p.closedSubscribed = false; }
        if (p.frameSubscribed && p.pool) { p.pool.FrameArrived(p.frameToken); p.frameSubscribed = false; }
        if (p.session) { p.session.Close(); p.session = nullptr; }
        if (p.pool) { p.pool.Close(); p.pool = nullptr; }
        if (p.inFlightFrame) { p.inFlightFrame.Close(); p.inFlightFrame = nullptr; }
    } catch (...) { p.unknownGpu = true; p.fatal = "capture closure failed; resources quarantined"; return; }
    p.item = nullptr; p.wrapped = nullptr;
    if (p.output && IsWindow(p.output)) DestroyWindow(p.output);
    p.snapshotReadback.Reset();p.compositionTarget.Reset();p.compositionVisual.Reset();p.composition.Reset();p.swapchain.Reset(); p.inFlightSnapshot.Reset(); p.inFlightSource.Reset(); p.inFlightProcessed.Reset(); p.inFlightBackbuffer.Reset();
    if (p.context) p.context->ClearState();
    p.completion.Reset(); p.completionContext.Reset(); p.context.Reset(); p.device.Reset();
    if (p.apartment) RoUninitialize();
    impl_ = std::make_unique<Impl>();
}
ID3D11Device* CaptureHost::Device() const { return impl_->device.Get(); }
bool CaptureHost::Paused()const{return impl_->paused;}
HANDLE CaptureHost::WakeHandle()const{return impl_->wake->Handle();}
CaptureStatistics CaptureHost::Statistics()const{auto result=impl_->statistics;result.arrivals=impl_->arrivals->load(std::memory_order_relaxed);result.outputVisible=impl_->output&&IsWindowVisible(impl_->output);return result;}
bool CaptureHost::CursorCaptured()const{return impl_->cursorCaptured;}
FrameStamp CaptureHost::ObservedIdentity()const{const auto& p=*impl_;return {p.options.session,p.sequence,p.latestTimestamp,p.clientWidth,p.clientHeight,p.reset,p.streamEpoch,p.geometryEpoch,1};}
bool CaptureHost::DisengageHotkeyAvailable()const{return impl_->hotkeyRegistered;}
void CaptureHost::Disengage(){if(!impl_)return;std::string ignored;if(impl_->Owner(ignored))impl_->EndInteraction();}
ID3D11DeviceContext* CaptureHost::Context() const { return impl_->context.Get(); }
#ifdef NRW_CAPTURE_TEST
bool TestCaptureCompletionFailures() {
    for(int fault=0;fault<=6;++fault) {
        auto p=std::make_unique<CaptureHost::Impl>();
        winrt::check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&p->device,nullptr,&p->context));
        p->InitializeCompletion();
        D3D11_TEXTURE2D_DESC desc{};desc.Width=17;desc.Height=9;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        winrt::check_hresult(p->device->CreateTexture2D(&desc,nullptr,&p->inFlightSource));
        winrt::check_hresult(p->device->CreateTexture2D(&desc,nullptr,&p->inFlightSnapshot));
        p->inFlightProcessed=p->inFlightSource;p->inFlightBackbuffer=p->inFlightSnapshot;p->snapshotReadback=p->inFlightSnapshot;
        p->comparison.Compose(p->context.Get(),p->inFlightSnapshot.Get(),p->inFlightSource.Get(),p->inFlightSource.Get(),17,9,.5f,0,4);
        if(!p->comparison.HasFrameViews())return false;
        p->pendingGpu=true;p->active=true;p->completionFault=fault;
        std::string reason;const bool drained=p->Drain(reason);
        if(fault==0) {
            if(!drained || p->unknownGpu || p->pendingGpu || p->inFlightSource || p->inFlightSnapshot || p->inFlightProcessed || p->inFlightBackbuffer || p->comparison.HasFrameViews())return false;
        } else {
            if(drained || !p->unknownGpu || p->active || !p->pendingGpu || !p->inFlightSource || !p->inFlightSnapshot || !p->inFlightProcessed || !p->inFlightBackbuffer || !p->snapshotReadback || !p->comparison.HasFrameViews() || reason.empty())return false;
            p->completionFault=0;
            if(p->Drain(reason) || !p->inFlightSource || !p->inFlightSnapshot)return false;
            // Deliberate test quarantine has the same process-exit lifetime as production.
            p.release();
        }
    }
    return true;
}
#endif
}
