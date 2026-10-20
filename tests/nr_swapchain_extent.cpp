#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <hooks/DxgiSwapchainExtent.h>
#include <cstdio>
#include <stdexcept>
#include <initializer_list>

using Microsoft::WRL::ComPtr;
static unsigned checks=0;
static void Check(bool condition,const char* name)
{
    ++checks;
    if(!condition){std::printf("FAIL: %s\n",name);throw std::runtime_error(name);}
}
static void Require(HRESULT result,const char* name)
{
    if(FAILED(result))std::printf("HRESULT %08X: %s\n",static_cast<unsigned>(result),name);
    Check(SUCCEEDED(result),name);
}
struct Window
{
    HWND handle=nullptr;
    Window(UINT width,UINT height)
    {
        RECT rect{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
        Check(AdjustWindowRect(&rect,WS_POPUP,FALSE)!=FALSE,"adjust owned window");
        handle=CreateWindowExW(0,L"STATIC",L"NeuRotic extent fixture",WS_POPUP,
            0,0,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(handle!=nullptr,"create owned hidden window");
        RECT actual{};Check(GetClientRect(handle,&actual)!=FALSE,"measure actual owned client");
        std::printf("OWNED_WINDOW request=%ux%u actualClient=%ldx%ld style=WS_POPUP\n",
            width,height,actual.right-actual.left,actual.bottom-actual.top);
        Check(actual.right-actual.left==static_cast<LONG>(width)&&actual.bottom-actual.top==static_cast<LONG>(height),
            "owned popup establishes exact fixture client dimensions");
    }
    void Resize(UINT width,UINT height)
    {
        RECT rect{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
        Check(AdjustWindowRect(&rect,WS_POPUP,FALSE)!=FALSE,"adjust resized window");
        Check(SetWindowPos(handle,nullptr,0,0,rect.right-rect.left,rect.bottom-rect.top,
            SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"resize owned window");
    }
    ~Window(){if(handle)DestroyWindow(handle);}
};
static void ObserveAndCheck(IDXGISwapChain* chain,UINT width,UINT height)
{
    const auto observed=DxgiSwapchainExtent::ObserveCreated(S_OK,chain);
    Check(observed.PositiveExtent(),"actual GetDesc positive extent");
    Check(observed.actual.BufferDesc.Width==width&&observed.actual.BufferDesc.Height==height,
        "DXGI resolved requested automatic axes");
    DXGI_SWAP_CHAIN_DESC saved{};saved.BufferDesc.Width=7;saved.BufferDesc.Height=9;
    float stateWidth=7,stateHeight=9;
    Check(DxgiSwapchainExtent::ApplyObservedExtent(observed,saved,stateWidth,stateHeight),"apply actual extent");
    Check(stateWidth==static_cast<float>(width)&&stateHeight==static_cast<float>(height)&&
        saved.BufferDesc.Width==width&&saved.BufferDesc.Height==height,"state uses actual buffer dimensions");
}
static void RunWindowCase(ID3D11Device* device,IDXGIFactory2* factory,bool modern,UINT width,UINT height,
    UINT windowWidth=640,UINT windowHeight=480)
{
    Window window(windowWidth,windowHeight);
    ComPtr<IDXGISwapChain> chain;
    if(modern){
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=width;desc.Height=height;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain1;
        Require(factory->CreateSwapChainForHwnd(device,window.handle,&desc,nullptr,nullptr,&chain1),"public HWND creation");
        Require(chain1.As(&chain),"query base swapchain");
    }else{
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=width;desc.BufferDesc.Height=height;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        desc.OutputWindow=window.handle;desc.Windowed=TRUE;
        Require(factory->CreateSwapChain(device,&desc,&chain),"public legacy creation");
    }
    ObserveAndCheck(chain.Get(),width?width:windowWidth,height?height:windowHeight);
    window.Resize(800,600);
    Require(chain->ResizeBuffers(0,0,0,DXGI_FORMAT_UNKNOWN,0),"public automatic ResizeBuffers");
    ObserveAndCheck(chain.Get(),800,600);
    chain.Reset();
    std::printf("PASS public %s request=%ux%u initialWindow=%ux%u; actual creation/resize dimensions\n",
        modern?"HWND":"legacy",width,height,windowWidth,windowHeight);
}
static void RunD3D12Resize1Cases()
{
    ComPtr<IDXGIFactory4> factory;ComPtr<IDXGIAdapter> warp;ComPtr<ID3D12Device> device;
    Require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"D3D12 public DXGI factory4");
    Require(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"D3D12 public WARP adapter");
    Require(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"public D3D12 WARP device");
    D3D12_COMMAND_QUEUE_DESC queueDesc{};queueDesc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;Require(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)),"public D3D12 direct queue");
    for(unsigned scenario=0;scenario<4;++scenario){
        const UINT windowWidth=scenario==3?32:640,windowHeight=scenario==3?32:480;
        Window window(windowWidth,windowHeight);
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=scenario==1?640:0;desc.Height=scenario==0?480:0;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferCount=2;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain;ComPtr<IDXGISwapChain3> chain3;
        Require(factory->CreateSwapChainForHwnd(queue.Get(),window.handle,&desc,nullptr,nullptr,&chain),"public D3D12 queue-owned HWND chain");
        Require(chain.As(&chain3),"D3D12 queue-owned ResizeBuffers1 interface");
        ObserveAndCheck(chain.Get(),desc.Width?desc.Width:windowWidth,desc.Height?desc.Height:windowHeight);
        window.Resize(720,540);
        // ResizeBuffers1 is only valid for a D3D12 queue-created chain. Supply
        // genuine per-buffer queues and matching node-zero masks (bit 1).
        const UINT masks[2]={1,1};IUnknown* queues[2]={queue.Get(),queue.Get()};
        Require(chain3->ResizeBuffers1(2,0,0,DXGI_FORMAT_UNKNOWN,0,masks,queues),"public D3D12 automatic ResizeBuffers1");
        ObserveAndCheck(chain.Get(),720,540);
        std::printf("PASS public D3D12 queue-owned request=%ux%u initialClient=%ux%u automatic ResizeBuffers1=720x540 debugLayer=0 submission=0\n",
            desc.Width,desc.Height,windowWidth,windowHeight);
    }
}
static void RunHiddenPairCase(ID3D11Device* device,IDXGIFactory2* factory)
{
    Window hiddenWindow(32,32),visibleWindow(640,480);
    auto create=[&](HWND window,ComPtr<IDXGISwapChain>& chain){
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;desc.Windowed=TRUE;desc.OutputWindow=window;
        Require(factory->CreateSwapChain(device,&desc,&chain),"public automatic pair creation");
    };
    ComPtr<IDXGISwapChain> hidden,visible;create(hiddenWindow.handle,hidden);create(visibleWindow.handle,visible);
    ObserveAndCheck(hidden.Get(),32,32);ObserveAndCheck(visible.Get(),640,480);
    Check(FAILED(DxgiSwapchainExtent::RequireMatchingBufferExtent(hidden.Get(),visible.Get())),
        "Present pair proof refuses actual mismatched buffers before any copy");
    ComPtr<ID3D11Texture2D> outstanding;
    Require(hidden->GetBuffer(0,IID_PPV_ARGS(&outstanding)),"retain actual hidden backbuffer");
    Check(FAILED(DxgiSwapchainExtent::MatchHiddenBufferExtent(hidden.Get(),visible.Get())),
        "actual outstanding buffer prevents unsafe hidden resize");
    ObserveAndCheck(hidden.Get(),32,32);
    Check(FAILED(DxgiSwapchainExtent::RequireMatchingBufferExtent(hidden.Get(),visible.Get())),
        "failed correction cannot authorize interop copying");
    outstanding.Reset();
    Require(DxgiSwapchainExtent::MatchHiddenBufferExtent(hidden.Get(),visible.Get()),"match hidden to visible actual extent");
    ObserveAndCheck(hidden.Get(),640,480);
    Require(DxgiSwapchainExtent::RequireMatchingBufferExtent(hidden.Get(),visible.Get()),"actual positive pair enables copying");
    auto matched=DxgiSwapchainExtent::ObserveCreated(S_OK,hidden.Get());
    Check(matched.actual.BufferDesc.Format==DXGI_FORMAT_R8G8B8A8_UNORM&&matched.actual.BufferCount==2&&
        matched.actual.OutputWindow==hiddenWindow.handle,"matching preserves format count and owned hidden HWND");
    Require(DxgiSwapchainExtent::MatchHiddenBufferExtent(hidden.Get(),visible.Get()),"already matched pair retained");
    visibleWindow.Resize(800,600);
    const HRESULT visibleResize=visible->ResizeBuffers(0,0,0,DXGI_FORMAT_UNKNOWN,0);
    const HRESULT hiddenResize=hidden->ResizeBuffers(0,0,0,DXGI_FORMAT_UNKNOWN,0);
    Require(visibleResize,"visible automatic pair resize");
    Require(hiddenResize,"hidden independently resolves its own HWND");
    ObserveAndCheck(hidden.Get(),32,32);
    Require(hidden->GetBuffer(0,IID_PPV_ARGS(&outstanding)),"retain newly resized actual buffer");
    Check(FAILED(DxgiSwapchainExtent::MatchHiddenBufferExtent(hidden.Get(),visible.Get()))&&
        FAILED(DxgiSwapchainExtent::RequireMatchingBufferExtent(hidden.Get(),visible.Get())),
        "both native resizes may succeed but failed correction keeps Present copy unavailable");
    Check(visibleResize==S_OK&&hiddenResize==S_OK,"original per-chain resize results remain successful");
    outstanding.Reset();
    Require(DxgiSwapchainExtent::MatchHiddenBufferExtent(hidden.Get(),visible.Get()),"pair refresh uses actual visible resize");
    ObserveAndCheck(hidden.Get(),800,600);
    Require(DxgiSwapchainExtent::RequireMatchingBufferExtent(hidden.Get(),visible.Get()),"later positive observation recovers pair without permanent denial");
    Check(DxgiSwapchainExtent::MatchHiddenBufferExtent(nullptr,visible.Get())==E_POINTER,"missing hidden object cannot be accepted");
    Check(DxgiSwapchainExtent::MatchHiddenBufferExtent(hidden.Get(),nullptr)==E_POINTER,"missing visible object cannot be accepted");
    std::printf("PASS public D3D11 hidden/visible pair and outstanding-buffer refusal; no D3D12/FG integration runtime\n");
}
int main()
{
    const auto priorDpi=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try{
        {
            // Shared predicate unit cases. Four actual factory call sites are
            // source-reviewed separately, not simulated by repeating this block.
            Check(!DxgiSwapchainExtent::BypassSmallExplicitWindowExtent(0,480),"automatic width admitted");
            Check(!DxgiSwapchainExtent::BypassSmallExplicitWindowExtent(640,0),"automatic height admitted");
            Check(!DxgiSwapchainExtent::BypassSmallExplicitWindowExtent(0,0),"both automatic admitted");
            Check(!DxgiSwapchainExtent::BypassSmallExplicitWindowExtent(640,480),"explicit normal retained");
            Check(!DxgiSwapchainExtent::BypassSmallExplicitWindowExtent(100,100),"explicit boundary retained");
            Check(DxgiSwapchainExtent::BypassSmallExplicitWindowExtent(99,480),"explicit small width policy retained");
            Check(DxgiSwapchainExtent::BypassSmallExplicitWindowExtent(640,99),"explicit small height policy retained");
        }
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
        Require(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
            &device,&level,&context),"public D3D11 WARP device");
        ComPtr<IDXGIDevice> dxgiDevice;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory2> factory;
        Require(device.As(&dxgiDevice),"DXGI device");Require(dxgiDevice->GetAdapter(&adapter),"WARP adapter");
        Require(adapter->GetParent(IID_PPV_ARGS(&factory)),"public DXGI factory2");
        for(bool modern:{false,true}){
            RunWindowCase(device.Get(),factory.Get(),modern,0,480);
            RunWindowCase(device.Get(),factory.Get(),modern,640,0);
            RunWindowCase(device.Get(),factory.Get(),modern,0,0);
            RunWindowCase(device.Get(),factory.Get(),modern,640,480);
            RunWindowCase(device.Get(),factory.Get(),modern,0,0,32,32);
        }
        RunHiddenPairCase(device.Get(),factory.Get());
        RunD3D12Resize1Cases();
        DXGI_SWAP_CHAIN_DESC1 invalid{};invalid.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        invalid.SampleDesc.Count=1;invalid.BufferCount=2;invalid.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        invalid.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> missing;
        const HRESULT rejected=factory->CreateSwapChainForHwnd(device.Get(),nullptr,&invalid,nullptr,nullptr,&missing);
        Check(FAILED(rejected),"real invalid HWND rejected");
        const auto failure=DxgiSwapchainExtent::ObserveCreated(rejected,missing.Get());
        Check(failure.creationResult==rejected&&failure.descriptionResult==E_PENDING,"original API failure retained; no GetDesc");
        DXGI_SWAP_CHAIN_DESC saved{};saved.BufferDesc.Width=800;saved.BufferDesc.Height=600;
        float stateWidth=800,stateHeight=600;
        Check(!DxgiSwapchainExtent::ApplyObservedExtent(failure,saved,stateWidth,stateHeight),"failed API cannot refresh state");
        Check(!DxgiSwapchainExtent::ApplyObservedDescription(failure,saved),"failed observation cannot overwrite prior descriptor");
        Check(saved.BufferDesc.Width==800&&saved.BufferDesc.Height==600&&stateWidth==800&&stateHeight==600,"failure preserves previous state");
        const auto absent=DxgiSwapchainExtent::ObserveCreated(S_OK,nullptr);
        Check(absent.descriptionResult==E_POINTER&&!absent.PositiveExtent(),"missing successful object is unknown");
        DxgiSwapchainExtent::Observation zero;zero.creationResult=S_OK;zero.descriptionResult=S_OK;
        Check(!DxgiSwapchainExtent::ApplyObservedExtent(zero,saved,stateWidth,stateHeight),"zero description does not replace actual state");
        std::atomic<unsigned> budget{0};
        for(unsigned i=0;i<64;++i)Check(DxgiSwapchainExtent::TakeReceipt(budget),"bounded diagnostic receipt");
        Check(!DxgiSwapchainExtent::TakeReceipt(budget),"diagnostic receipt saturates");
        context->ClearState();context->Flush();
        std::printf("PASS %u checks; WARP featureLevel=%X debugLayer=0; helper+public DXGI only, no product factory/private provider/game execution\n",
            checks,static_cast<unsigned>(level));
        if(priorDpi)SetThreadDpiAwarenessContext(priorDpi);
        return 0;
    }catch(const std::exception&){if(priorDpi)SetThreadDpiAwarenessContext(priorDpi);return 1;}
}
