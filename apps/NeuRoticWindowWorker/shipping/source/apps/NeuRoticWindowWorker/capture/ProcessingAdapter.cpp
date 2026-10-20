#include "ProcessingAdapter.h"
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <sstream>
#include <utility>
#include <limits>
namespace nrw::adapters {
namespace {
using Microsoft::WRL::ComPtr;
Identity From(LUID id){return {id.LowPart,id.HighPart,true};}
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}Handle()=default;Handle(const Handle&)=delete;};
std::string Failure(const std::string& phase,std::int32_t code){
    std::ostringstream out;out<<phase<<" (HRESULT 0x"<<std::hex<<static_cast<std::uint32_t>(code)<<')';return out.str();
}
ProbeResult Probe(IDXGIAdapter1* adapter,Identity expected,std::uint64_t session,
                  unsigned width,unsigned height,bool inference,Binding& selected){
    // Every object below is local until success. No command recording,
    // submission, signal, WGC capture or provider entry occurs in this probe.
    // Thus failed creation leaves no application GPU work to retire, and all
    // local COM/NT-handle owners are destroyed before Choose can advance.
    const auto fail=[](const char* phase,HRESULT hr){return ProbeResult{false,true,false,static_cast<std::int32_t>(hr),phase};};
    Binding trial;trial.adapter=adapter;trial.session=session;trial.id=expected;
    D3D_FEATURE_LEVEL level{};
    HRESULT hr=D3D11CreateDevice(adapter,D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&trial.device,&level,&trial.context);
    if(FAILED(hr))return fail("D11 device",hr);
    Identity actual;hr=ReadIdentity(trial.device.Get(),actual);
    if(FAILED(hr))return fail("D11 adapter identity",hr);
    if(!PinnedMatch(expected,actual,session,session))return fail("D11 selected adapter changed",E_INVALIDARG);
    ComPtr<ID3D11Device5> d115;ComPtr<ID3D11DeviceContext4> c114;
    hr=trial.device.As(&d115);if(FAILED(hr))return fail("D11 completion-fence device",hr);
    hr=trial.context.As(&c114);if(FAILED(hr))return fail("D11 completion-fence context",hr);
    ComPtr<ID3D11Fence> captureFence;
    hr=d115->CreateFence(0,D3D11_FENCE_FLAG_NONE,IID_PPV_ARGS(&captureFence));
    if(FAILED(hr))return fail("D11 capture completion fence",hr);
    if(inference){
        ComPtr<ID3D12Device> d12;hr=D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&d12));
        if(FAILED(hr))return fail("D12 same-adapter device",hr);
        if(!PinnedMatch(expected,From(d12->GetAdapterLuid()),session,session))return fail("D12 selected adapter changed",E_INVALIDARG);
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;ComPtr<ID3D12CommandQueue> queue;
        hr=d12->CreateCommandQueue(&q,IID_PPV_ARGS(&queue));if(FAILED(hr))return fail("D12 direct queue",hr);
        ComPtr<ID3D12Fence> localFence;hr=d12->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&localFence));
        if(FAILED(hr))return fail("D12 completion fence",hr);
        ComPtr<ID3D11Fence> sharedFence;hr=d115->CreateFence(0,D3D11_FENCE_FLAG_SHARED,IID_PPV_ARGS(&sharedFence));
        if(FAILED(hr))return fail("D11 shared ingress fence",hr);
        Handle fenceHandle;hr=sharedFence->CreateSharedHandle(nullptr,GENERIC_ALL,nullptr,&fenceHandle.value);
        if(FAILED(hr))return fail("D11 fence NT handle",hr);
        ComPtr<ID3D12Fence> importedFence;hr=d12->OpenSharedHandle(fenceHandle.value,IID_PPV_ARGS(&importedFence));
        if(FAILED(hr))return fail("D12 imported ingress fence",hr);
        // Exact existing SDR ingress/output formats and sharing rights. These
        // creation controls do not qualify later content, extents or providers.
        for(unsigned output=0;output<2;++output){
            D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
            desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
            desc.Format=output?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|(output?D3D11_BIND_UNORDERED_ACCESS:0);
            desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED;
            ComPtr<ID3D11Texture2D> texture;hr=trial.device->CreateTexture2D(&desc,nullptr,&texture);
            if(FAILED(hr))return fail(output?"D11 shared SDR output":"D11 shared SDR input",hr);
            ComPtr<IDXGIResource1> resource;hr=texture.As(&resource);if(FAILED(hr))return fail("Shared texture interface",hr);
            Handle handle;hr=resource->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle.value);
            if(FAILED(hr))return fail("Texture NT handle",hr);
            ComPtr<ID3D12Resource> imported;hr=d12->OpenSharedHandle(handle.value,IID_PPV_ARGS(&imported));
            if(FAILED(hr))return fail(output?"D12 shared SDR output":"D12 shared SDR input",hr);
            const auto actualDesc=imported->GetDesc();
            if(actualDesc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||actualDesc.Format!=desc.Format||actualDesc.Width!=width||actualDesc.Height!=height)
                return fail("Imported texture representation",E_INVALIDARG);
            if(output){
                if(!(actualDesc.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return fail("Shared output UAV rights",E_INVALIDARG);
                ComPtr<ID3D11ShaderResourceView> view;hr=trial.device->CreateShaderResourceView(texture.Get(),nullptr,&view);
                if(FAILED(hr))return fail("D11 output shader view",hr);
            }
        }
        hr=d12->GetDeviceRemovedReason();if(FAILED(hr))return fail("D12 device unavailable",hr);
    }
    hr=trial.device->GetDeviceRemovedReason();if(FAILED(hr))return fail("D11 device unavailable",hr);
    selected=std::move(trial);return {true,true,false,0,"device-ready"};
}
}
HRESULT ReadIdentity(ID3D11Device* device,Identity& identity) noexcept {
    identity={};if(!device)return E_POINTER;
    ComPtr<IDXGIDevice> dxgi;HRESULT hr=device->QueryInterface(IID_PPV_ARGS(&dxgi));if(FAILED(hr))return hr;
    ComPtr<IDXGIAdapter> adapter;hr=dxgi->GetAdapter(&adapter);if(FAILED(hr))return hr;
    DXGI_ADAPTER_DESC desc{};hr=adapter->GetDesc(&desc);if(FAILED(hr))return hr;
    identity=From(desc.AdapterLuid);return S_OK;
}
std::string IdentityText(Identity identity){
    if(!identity.known)return {};
    return std::to_string(static_cast<std::uint32_t>(identity.high))+":"+std::to_string(identity.low);
}
bool Select(std::uint64_t session,unsigned width,unsigned height,bool inference,
            Binding& binding,Report& report,std::string& reason){
    binding={};report={};reason.clear();
    if(!session||!width||!height){report.failurePhase="session-shape";reason="A current session and target extent are required for processing-device selection.";return false;}
    ComPtr<IDXGIFactory1> factory;HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if(FAILED(hr)){report.failurePhase="adapter-factory";reason=Failure(report.failurePhase,hr);return false;}
    ComPtr<IDXGIFactory6> ranked;hr=factory.As(&ranked);
    if(FAILED(hr)&&hr!=E_NOINTERFACE){report.failurePhase="adapter-preference";reason=Failure(report.failurePhase,hr);return false;}
    report.ordering=ranked?"windows-high-performance":"windows-dxgi-order";
    std::vector<ComPtr<IDXGIAdapter1>> adapters;std::vector<Candidate> candidates;
    for(UINT index=0;;++index){
        ComPtr<IDXGIAdapter1> adapter;
        hr=ranked?ranked->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)):
            factory->EnumAdapters1(index,&adapter);
        if(hr==DXGI_ERROR_NOT_FOUND)break;
        if(FAILED(hr)){report.enumerationCode=hr;break;}
        if(!adapter){report.enumerationCode=E_POINTER;break;}
        DXGI_ADAPTER_DESC1 desc{};const auto description=adapter->GetDesc1(&desc);
        candidates.push_back({SUCCEEDED(description)?From(desc.AdapterLuid):Identity{},SUCCEEDED(description)&&(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0,description});
        adapters.push_back(std::move(adapter));
        if(index==(std::numeric_limits<UINT>::max)()){report.enumerationCode=E_BOUNDS;break;}
    }
    auto choice=Choose(candidates,[&](std::size_t index){return Probe(adapters[index].Get(),candidates[index].id,session,width,height,inference,binding);});
    report.attempts=std::move(choice.attempts);
    if(choice.index!=NoCandidate){report.selected=binding.id;return true;}
    if(choice.quarantined){report.failurePhase="device-retirement";reason="Processing-device ownership is uncertain. Restart the worker process.";return false;}
    if(!report.attempts.empty()){
        const auto& last=report.attempts.back();report.failurePhase=last.result.phase;
        reason="No hardware adapter could create the requested processing path. "+Failure(last.result.phase,last.result.code);
    }else if(FAILED(report.enumerationCode)){
        report.failurePhase="adapter-enumeration";reason=Failure(report.failurePhase,report.enumerationCode);
    }else{report.failurePhase="hardware-adapter";reason="No hardware processing adapter is available. Software inference is not selected.";}
    return false;
}
}
