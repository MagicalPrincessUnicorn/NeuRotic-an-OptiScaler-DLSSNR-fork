#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <dxgi1_4.h>
#include <dbghelp.h>
#include <cstdio>
#include <vector>
#include <nvsdk_ngx.h>
#include <detours/detours.h>
static ID3D11InfoQueue* diagnostics = nullptr;
static HRESULT WINAPI DebugDevice(IDXGIAdapter* a, D3D_DRIVER_TYPE t, HMODULE s, UINT f,
    const D3D_FEATURE_LEVEL* levels, UINT count, UINT sdk, ID3D11Device** d,
    D3D_FEATURE_LEVEL* got, ID3D11DeviceContext** c)
{
    // Public OptiScaler's D3D11 device hook does not support debug-layer creation.
    // Transport pixel tests enable both debug layers independently of that hook.
    HRESULT hr = D3D11CreateDevice(a,t,s,f,levels,count,sdk,d,got,c);
    if (SUCCEEDED(hr)) (*d)->QueryInterface(IID_PPV_ARGS(&diagnostics));
    return hr;
}
static void PrintDiagnostics()
{
    if (!diagnostics) return;
    for(UINT64 i=0;i<diagnostics->GetNumStoredMessages();++i)
    {
        SIZE_T size=0; diagnostics->GetMessage(i,nullptr,&size); std::vector<char> data(size);
        auto* m=reinterpret_cast<D3D11_MESSAGE*>(data.data()); diagnostics->GetMessage(i,m,&size);
        if(m->Severity<=D3D11_MESSAGE_SEVERITY_WARNING) std::printf("D3D11: %s\n",m->pDescription);
    }
    diagnostics->ClearStoredMessages();
}
static LONG Crash(EXCEPTION_POINTERS* e)
{
    std::printf("EXCEPTION %08lx at %p\n", e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress);
    SymInitialize(GetCurrentProcess(),nullptr,TRUE);
    CONTEXT ctx=*e->ContextRecord; STACKFRAME64 frame={};
    frame.AddrPC={ctx.Rip,0,AddrModeFlat}; frame.AddrStack={ctx.Rsp,0,AddrModeFlat}; frame.AddrFrame={ctx.Rbp,0,AddrModeFlat};
    for(int i=0;i<24;++i)
    {
        char buffer[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]={}; auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct=sizeof(SYMBOL_INFO); symbol->MaxNameLen=MAX_SYM_NAME; DWORD64 displacement=0;
        if(SymFromAddr(GetCurrentProcess(),frame.AddrPC.Offset,&displacement,symbol))
            std::printf("STACK %llx %s+%llx\n",frame.AddrPC.Offset,symbol->Name,displacement);
        else std::printf("STACK %llx\n",frame.AddrPC.Offset);
        if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,GetCurrentProcess(),GetCurrentThread(),&frame,&ctx,nullptr,
            SymFunctionTableAccess64,SymGetModuleBase64,nullptr)) break;
    }
    PrintDiagnostics();
    return EXCEPTION_EXECUTE_HANDLER;
}
static HRESULT (STDMETHODCALLTYPE* originalPresent)(IDXGISwapChain*,UINT,UINT)=nullptr;
static ID3D11DeviceContext* sampleContext=nullptr;
static std::vector<unsigned char> sampleBefore;
static int sampleFrame=0;
static std::vector<unsigned char> ReadTarget(IDXGISwapChain* sc, ID3D11DeviceContext* ctx)
{
    ID3D11Texture2D* back=nullptr; ID3D11Texture2D* staging=nullptr;
    std::vector<unsigned char> pixels;
    D3D11_TEXTURE2D_DESC d={};
    if(SUCCEEDED(sc->GetBuffer(0,IID_PPV_ARGS(&back))))
    {
        back->GetDesc(&d);
        if(d.Format==DXGI_FORMAT_R8G8B8A8_UNORM)
        {
            ID3D11Device* device=nullptr; ctx->GetDevice(&device);
            d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.MiscFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            if(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&staging)))
            {
                ctx->CopyResource(staging,back); D3D11_MAPPED_SUBRESOURCE m={};
                if(SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_READ,0,&m)))
                {
                    pixels.resize(size_t(d.Width)*d.Height*4);
                    for(UINT y=0;y<d.Height;++y) std::memcpy(pixels.data()+size_t(y)*d.Width*4,
                        static_cast<unsigned char*>(m.pData)+size_t(y)*m.RowPitch,size_t(d.Width)*4);
                    ctx->Unmap(staging,0);
                }
            }
            device->Release();
        }
    }
    if(staging) staging->Release(); if(back) back->Release();
    return pixels;
}
static HRESULT STDMETHODCALLTYPE FinalPresent(IDXGISwapChain* sc,UINT sync,UINT flags)
{
    if(sampleContext && !sampleBefore.empty())
    {
        auto after=ReadTarget(sc,sampleContext);
        if(after.size()==sampleBefore.size())
        {
            size_t changed=0; unsigned long long original=14695981039346656037ull, delivered=original;
            for(size_t i=0;i<sampleBefore.size();++i)
            { changed+=sampleBefore[i]!=after[i]; original=(original^sampleBefore[i])*1099511628211ull; delivered=(delivered^after[i])*1099511628211ull; }
            std::printf("DELIVERY frame=%d bytes=%zu changed=%zu before=%016llx after=%016llx point=before-original-Present\n",
                sampleFrame,sampleBefore.size(),changed,original,delivered);
        }
        sampleBefore.clear();
    }
    return originalPresent(sc,sync,flags);
}
static bool InstallObserver(ID3D11DeviceContext* ctx)
{
    // OptiScaler intentionally leaves tiny auxiliary overlay chains unwrapped.
    // Use one only to discover the system Present function, then destroy it.
    HWND window=CreateWindowExW(0,L"ngxGym",L"NR test observer",0,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    IDXGIFactory2* factory=nullptr; IDXGISwapChain1* probe=nullptr; ID3D11Device* device=nullptr;
    ctx->GetDevice(&device);
    DXGI_SWAP_CHAIN_DESC1 d={}; d.Width=32; d.Height=32; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count=1; d.BufferCount=2; d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if(SUCCEEDED(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateSwapChainForHwnd(device,window,&d,nullptr,nullptr,&probe)))
        originalPresent=reinterpret_cast<decltype(originalPresent)>((*reinterpret_cast<void***>(probe))[8]);
    if(probe) probe->Release(); if(factory) factory->Release(); device->Release();
    // Avoid posting WM_QUIT through the host window procedure.
    SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(DefWindowProcW)); DestroyWindow(window);
    if(!originalPresent) return false;
    DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<void**>(&originalPresent),FinalPresent);
    return DetourTransactionCommit()==NO_ERROR;
}
static HRESULT ObservePresent(IDXGISwapChain1* sc, ID3D11DeviceContext* ctx, int frame)
{
    if(!originalPresent && !InstallObserver(ctx)) { std::puts("FAIL delivery observer unavailable"); return E_FAIL; }
    if(frame==60 || frame==120 || frame==240)
    { sampleBefore=ReadTarget(sc,ctx); sampleContext=ctx; sampleFrame=frame; }
    const HRESULT hr=sc->Present(0,0);
    if(!sampleBefore.empty()) std::puts("FAIL delivery observer did not see original Present");
    sampleBefore.clear(); sampleContext=nullptr;
    return hr;
}
#define D3D11CreateDevice DebugDevice
#define main UpstreamMain
#include "nr_ngxgym_upstream.cpp"
#undef main
#undef D3D11CreateDevice
int main(int argc,char** argv)
{
    __try { const int result=UpstreamMain(argc,argv); PrintDiagnostics(); return result; }
    __except(Crash(GetExceptionInformation())) { return 97; }
}
