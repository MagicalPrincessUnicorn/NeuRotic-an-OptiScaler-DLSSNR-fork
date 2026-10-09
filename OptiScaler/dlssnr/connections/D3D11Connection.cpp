#include "GpuBudget.h"
#include "D3D11Connection.h"
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <array>
#include <cstring>
namespace DlssNr::Connections {
namespace {
DXGI_FORMAT SharedMotion(DXGI_FORMAT f){return f==DXGI_FORMAT_R32G32_FLOAT?DXGI_FORMAT_R16G16_FLOAT:f;}
bool Fail(std::string& r,const char* why){r=why;return false;}
bool Same11(IUnknown* a,IUnknown* b){ComPtr<IUnknown>x,y;return a&&b&&SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x)))&&SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y)))&&x==y;}
bool Wait11(ID3D12Fence* f,UINT64 n){auto v=f->GetCompletedValue();if(v==UINT64_MAX)return false;if(v>=n)return true;
    HANDLE e=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!e)return false;bool ok=SUCCEEDED(f->SetEventOnCompletion(n,e))&&WaitForSingleObject(e,2000)==WAIT_OBJECT_0;
    CloseHandle(e);v=f->GetCompletedValue();return ok&&v!=UINT64_MAX&&v>=n;}
DXGI_FORMAT SharedDepth(DXGI_FORMAT f){switch(f){case DXGI_FORMAT_D32_FLOAT:case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R32_TYPELESS:return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D16_UNORM:case DXGI_FORMAT_R16_UNORM:case DXGI_FORMAT_R16_TYPELESS:return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:case DXGI_FORMAT_R24G8_TYPELESS:return DXGI_FORMAT_R32_FLOAT;default:return DXGI_FORMAT_UNKNOWN;}}
}
struct D3D11Connection::Impl {
    struct Ticket {D3D11ObservedFrame source;PendingFrame inner;bool live=true;};
    std::shared_ptr<GpuBudget::Reservation> bridgeBudget,depthBudget,motionBudget;
    ComPtr<ID3D12Device> renderer;D3D12Connection transport;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11Device5> d5;ComPtr<ID3D11DeviceContext4> context;
    ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> producer,output;
    ComPtr<ID3D11Fence> producer11,output11;
    std::array<ComPtr<ID3D11Texture2D>,5> shared;
    std::array<ComPtr<ID3D12Resource>,5> imported;
    ComPtr<ID3D11Texture2D> rawDepth,rawMotion;
    ComPtr<ID3D11ShaderResourceView> motionSrv;ComPtr<ID3D11RenderTargetView> motionRtv;ComPtr<ID3D11PixelShader> motionPs;
    ComPtr<ID3D11ShaderResourceView> depthSrv;ComPtr<ID3D11RenderTargetView> depthRtv;
    ComPtr<ID3D11VertexShader> depthVs;ComPtr<ID3D11PixelShader> depthPs;ComPtr<ID3DDeviceContextState> privateState;
    ComPtr<ID3D11RasterizerState> depthRaster;
    UINT w=0,h=0;DXGI_FORMAT cf=DXGI_FORMAT_UNKNOWN,df=DXGI_FORMAT_UNKNOWN;UINT64 value=0;DXGI_FORMAT mf=DXGI_FORMAT_UNKNOWN,kf=DXGI_FORMAT_UNKNOWN;
    std::shared_ptr<Ticket> active;nrpg::CpuMainlineClient::GpuOutput held;bool restart=false;
    explicit Impl(ID3D12Device* d):renderer(d),transport(d){}
    bool Init(ID3D11Device* d,ID3D11DeviceContext* c,UINT width,UINT height,DXGI_FORMAT color,DXGI_FORMAT depth,DXGI_FORMAT motion,DXGI_FORMAT mask,std::string& r){
        if(device&&!Same11(device.Get(),d))return Fail(r,"D3D11 logical device changed; create a new session");
        if(context&&!Same11(context.Get(),c))return Fail(r,"D3D11 immediate context changed");
        if(!context){
            ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
            if(!renderer||FAILED(d->QueryInterface(IID_PPV_ARGS(&dx)))||FAILED(dx->GetAdapter(&adapter))||FAILED(adapter->GetDesc(&desc)))return Fail(r,"D3D11 adapter identity unavailable");
            auto luid=renderer->GetAdapterLuid();if(std::memcmp(&luid,&desc.AdapterLuid,sizeof(luid)))return Fail(r,"D3D11 renderer adapter mismatch");
            ComPtr<ID3D11Device5> device5;ComPtr<ID3D11DeviceContext4> context4;
            if(FAILED(d->QueryInterface(IID_PPV_ARGS(&device5)))||FAILED(c->QueryInterface(IID_PPV_ARGS(&context4))))return Fail(r,"D3D11 shared fence interfaces unavailable");
            D3D12_COMMAND_QUEUE_DESC q{};if(FAILED(renderer->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))))return Fail(r,"D3D11 bridge queue unavailable");
            auto fence=[&](ComPtr<ID3D12Fence>& f,ComPtr<ID3D11Fence>& f11){HANDLE handle=nullptr;
                if(FAILED(renderer->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&f)))||FAILED(renderer->CreateSharedHandle(f.Get(),nullptr,GENERIC_ALL,nullptr,&handle)))return false;
                const auto hr=device5->OpenSharedFence(handle,IID_PPV_ARGS(&f11));CloseHandle(handle);return SUCCEEDED(hr);};
            if(!fence(producer,producer11)||!fence(output,output11))return Fail(r,"D3D11 shared fence roundtrip unavailable");
            device=d;d5=device5;context=context4;
        }
        if(w==width&&h==height&&cf==color&&df==depth&&mf==motion&&kf==mask)return true;
        std::uint64_t bytes=0;for(UINT n=0;n<(motion==DXGI_FORMAT_UNKNOWN?3u:5u);++n){auto amount=GpuBudget::ImageBytes(renderer.Get(),width,height,n==1?depth:n==3?SharedMotion(motion):n==4?mask:color,(n==1||n==2||n==3)?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE);if(!amount)return Fail(r,"D3D11 bridge size unavailable");bytes+=amount;}
        auto budget=GpuBudget::Reserve(bytes);if(!budget)return Fail(r,"D3D11 aggregate GPU budget exceeded");
        std::array<ComPtr<ID3D11Texture2D>,5> textures;std::array<ComPtr<ID3D12Resource>,5> aliases;
        for(UINT n=0;n<(motion==DXGI_FORMAT_UNKNOWN?3u:5u);++n){D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
            desc.Format=n==1?depth:n==3?SharedMotion(motion):n==4?mask:color;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|((n==1||n==2||n==3)?D3D11_BIND_RENDER_TARGET:0);
            desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE|D3D11_RESOURCE_MISC_SHARED;
            if(FAILED(device->CreateTexture2D(&desc,nullptr,&textures[n])))return Fail(r,"D3D11 exact shared texture format unavailable");
            ComPtr<IDXGIResource1> dx;HANDLE handle=nullptr;
            if(FAILED(textures[n].As(&dx))||FAILED(dx->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&handle)))return Fail(r,"D3D11 NT texture export unavailable");
            const auto hr=renderer->OpenSharedHandle(handle,IID_PPV_ARGS(&aliases[n]));CloseHandle(handle);
            if(FAILED(hr))return Fail(r,"D3D12 NT texture import unavailable");
        }
        if(privateState){ComPtr<ID3DDeviceContextState> previous;context->SwapDeviceContextState(privateState.Get(),&previous);ID3D11ShaderResourceView* empty=nullptr;context->PSSetShaderResources(0,1,&empty);context->OMSetRenderTargets(0,nullptr,nullptr);context->SwapDeviceContextState(previous.Get(),nullptr);}
        rawDepth.Reset();depthSrv.Reset();depthRtv.Reset();depthBudget.reset();rawMotion.Reset();motionSrv.Reset();motionRtv.Reset();motionBudget.reset();shared=std::move(textures);imported=std::move(aliases);bridgeBudget=std::move(budget);w=width;h=height;cf=color;df=depth;mf=motion;kf=mask;return true;
    }
    bool PreparePrivateState(std::string& r){
        if(!depthRaster){D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
            if(FAILED(device->CreateRasterizerState(&rd,&depthRaster)))return Fail(r,"D3D11 depth raster state unavailable");}
        if(!privateState){D3D_FEATURE_LEVEL selected{};const auto level=device->GetFeatureLevel();
            if(FAILED(d5->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&selected,&privateState)))return Fail(r,"D3D11 complete context-state preservation unavailable");}
        if(!depthVs||!depthPs){static constexpr char shader[]=R"(
Texture2D<float> depth:register(t0);
float4 vs(uint id:SV_VertexID):SV_Position{return float4(id==2?3:-1,id==1?3:-1,0,1);}
float ps(float4 p:SV_Position):SV_Target{return depth.Load(int3(p.xy,0));})";
            ComPtr<ID3DBlob> vs,ps,error;
            if(FAILED(D3DCompile(shader,sizeof(shader),"depth-normalization",nullptr,nullptr,"vs","vs_5_0",0,0,&vs,&error))||
               FAILED(D3DCompile(shader,sizeof(shader),"depth-normalization",nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error))||
               FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&depthVs))||
               FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&depthPs)))return Fail(r,"D3D11 depth conversion shader unavailable");}
        return true;
    }
    bool PrepareDepthConversion(std::string& r){
        if(rawDepth)return true;if(!PreparePrivateState(r))return false;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R24G8_TYPELESS;
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        auto budget=GpuBudget::Reserve(GpuBudget::ImageBytes(renderer.Get(),w,h,DXGI_FORMAT_R24G8_TYPELESS));if(!budget)return Fail(r,"D3D11 depth conversion budget exceeded");
        ComPtr<ID3D11Texture2D> raw;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11RenderTargetView> rtv;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sd.Texture2D.MipLevels=1;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&raw))||FAILED(device->CreateShaderResourceView(raw.Get(),&sd,&srv))||
           FAILED(device->CreateRenderTargetView(shared[1].Get(),nullptr,&rtv)))return Fail(r,"D3D11 legal D24 view conversion unavailable");
        rawDepth=raw;depthSrv=srv;depthRtv=rtv;depthBudget=std::move(budget);return true;
    }
    bool PrepareMotionConversion(std::string& r){
        if(rawMotion)return true;if(!PreparePrivateState(r))return false;
        if(!motionPs){static constexpr char shader[]="Texture2D<float2> motion:register(t0);float2 ps(float4 p:SV_Position):SV_Target{return motion.Load(int3(p.xy,0));}";ComPtr<ID3DBlob> ps,error;
            if(FAILED(D3DCompile(shader,sizeof(shader),"motion-normalization",nullptr,nullptr,"ps","ps_5_0",0,0,&ps,&error))||FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&motionPs)))return Fail(r,"D3D11 motion conversion shader unavailable");}
        auto budget=GpuBudget::Reserve(GpuBudget::ImageBytes(renderer.Get(),w,h,mf));if(!budget)return Fail(r,"D3D11 motion conversion budget exceeded");
        D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=1;desc.Format=mf;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> raw;ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11RenderTargetView> rtv;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&raw))||FAILED(device->CreateShaderResourceView(raw.Get(),nullptr,&srv))||FAILED(device->CreateRenderTargetView(shared[3].Get(),nullptr,&rtv)))return Fail(r,"D3D11 motion conversion views unavailable");
        rawMotion=raw;motionSrv=srv;motionRtv=rtv;motionBudget=std::move(budget);return true;
    }
    void Convert(ID3D11Texture2D* source,ID3D11Texture2D* raw,ID3D11ShaderResourceView* srv,ID3D11RenderTargetView* rtv,ID3D11PixelShader* shader){
        context->CopyResource(raw,source);
        ComPtr<ID3DDeviceContextState> previous;context->SwapDeviceContextState(privateState.Get(),&previous);
        context->PSSetShaderResources(0,1,&srv);context->OMSetRenderTargets(1,&rtv,nullptr);
        context->VSSetShader(depthVs.Get(),nullptr,0);context->PSSetShader(shader,nullptr,0);context->GSSetShader(nullptr,nullptr,0);
        context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1};context->RSSetViewports(1,&vp);context->RSSetState(depthRaster.Get());
        context->OMSetBlendState(nullptr,nullptr,UINT_MAX);context->OMSetDepthStencilState(nullptr,0);context->Draw(3,0);
        // Returning the entire context state also restores resources unbound by hazards.
        context->SwapDeviceContextState(previous.Get(),nullptr);
    }
};
D3D11Connection::D3D11Connection(ID3D12Device* d):impl_(std::make_unique<Impl>(d)){}
D3D11Connection::~D3D11Connection(){if(RequiresRestart())(void)impl_.release();}
bool D3D11Connection::RequiresRestart()const{return impl_->restart||impl_->transport.RequiresRestart();}
bool D3D11Connection::Capture(const D3D11ObservedFrame& f,PendingFrame& out,std::string& r){
    auto& i=*impl_;if(RequiresRestart()||i.active||out.ownerLease)return Fail(r,"D3D11 capture busy or quarantined");
    if(!GpuBudget::AdmitRaster(f.description.color.width,f.description.color.height,f.deriveMotion))return Fail(r,"D3D11 raster exceeds bounded GPU admission");
    if(!f.context||f.context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||!f.color||!f.depth||!f.lease)return Fail(r,"D3D11 requires leased textures and immediate context");
    ComPtr<ID3D11Device>d,cd,dd,td;f.context->GetDevice(&d);f.color->GetDevice(&cd);f.depth->GetDevice(&dd);auto target=f.outputTarget?f.outputTarget:f.color;target->GetDevice(&td);
    if(!Same11(d.Get(),cd.Get())||!Same11(d.Get(),dd.Get())||!Same11(d.Get(),td.Get()))return Fail(r,"D3D11 foreign device texture");
    D3D11_TEXTURE2D_DESC color{},depth{},dest{};f.color->GetDesc(&color);f.depth->GetDesc(&depth);target->GetDesc(&dest);
    auto shape=[&](const D3D11_TEXTURE2D_DESC& x){return x.Width==f.description.color.width&&x.Height==f.description.color.height&&x.MipLevels==1&&x.ArraySize==1&&x.SampleDesc.Count==1;};
    if(!shape(color)||!shape(depth)||!shape(dest)||dest.Format!=color.Format||SharedDepth(depth.Format)==DXGI_FORMAT_UNKNOWN||
        (color.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&color.Format!=DXGI_FORMAT_B8G8R8A8_UNORM))return Fail(r,"D3D11 unsupported raster or copy format");
    DXGI_FORMAT mf=DXGI_FORMAT_UNKNOWN,kf=DXGI_FORMAT_UNKNOWN;
    if(!f.deriveMotion){
        if(!f.motion||!f.distrust)return Fail(r,"D3D11 supplied guides missing");
        ComPtr<ID3D11Device> md,kd;f.motion->GetDevice(&md);f.distrust->GetDevice(&kd);
        D3D11_TEXTURE2D_DESC m{},k{};f.motion->GetDesc(&m);f.distrust->GetDesc(&k);
        if(!Same11(d.Get(),md.Get())||!Same11(d.Get(),kd.Get())||!shape(m)||!shape(k))return Fail(r,"D3D11 supplied guides foreign device/raster");
        mf=m.Format;kf=k.Format;
        if((mf!=DXGI_FORMAT_R16G16_FLOAT&&mf!=DXGI_FORMAT_R32G32_FLOAT)||(kf!=DXGI_FORMAT_R8_UNORM&&kf!=DXGI_FORMAT_R32_FLOAT))return Fail(r,"D3D11 supplied guide format unavailable");
    }
    if(!i.Init(d.Get(),f.context.Get(),color.Width,color.Height,color.Format,SharedDepth(depth.Format),mf,kf,r))return false;
    const bool convertDepth=depth.Format==DXGI_FORMAT_D24_UNORM_S8_UINT||depth.Format==DXGI_FORMAT_R24G8_TYPELESS;
    if(convertDepth&&!i.PrepareDepthConversion(r))return false;
    const bool convertMotion=!f.deriveMotion&&mf==DXGI_FORMAT_R32G32_FLOAT;if(convertMotion&&!i.PrepareMotionConversion(r))return false;
    auto t=std::make_shared<Impl::Ticket>();t->source=f;i.active=t;
    i.context->CopyResource(i.shared[0].Get(),f.color.Get());if(convertDepth)i.Convert(f.depth.Get(),i.rawDepth.Get(),i.depthSrv.Get(),i.depthRtv.Get(),i.depthPs.Get());else i.context->CopyResource(i.shared[1].Get(),f.depth.Get());
    if(!f.deriveMotion){if(convertMotion)i.Convert(f.motion.Get(),i.rawMotion.Get(),i.motionSrv.Get(),i.motionRtv.Get(),i.motionPs.Get());else i.context->CopyResource(i.shared[3].Get(),f.motion.Get());i.context->CopyResource(i.shared[4].Get(),f.distrust.Get());}
    if(FAILED(i.context->Signal(i.producer11.Get(),++i.value))){i.restart=true;return Fail(r,"D3D11 producer signal failed after references");}
    i.context->Flush();if(!Wait11(i.producer.Get(),i.value)){i.restart=true;return Fail(r,"D3D11 producer completion unknown");}
    D3D12ObservedFrame inner{};inner.description=f.description;inner.color=i.imported[0];inner.depth=i.imported[1];inner.outputTarget=i.imported[2];
    inner.deriveMotion=f.deriveMotion;if(!f.deriveMotion){inner.motion=i.imported[3];inner.distrust=i.imported[4];}
    inner.sourceQueue=i.queue;inner.producer=i.producer;inner.producerValue=i.value;inner.lease=t->source.lease;inner.statesKnown=true;
    if(!i.transport.Capture(inner,t->inner,r)){if(!RequiresRestart())i.active.reset();return false;}
    out=t->inner;out.ownerLease=t;r.clear();return true;
}
bool D3D11Connection::Finish(PendingFrame& p,nrpg::CpuMainlineClient::GpuOutput& output,CopybackReceipt& receipt,std::string& r){
    receipt={};auto& i=*impl_;auto t=i.active;if(RequiresRestart()||!t||p.ownerLease.get()!=t.get()||!t->live)return Fail(r,"invalid D3D11 copyback ticket");
    i.held=output;auto local=output;CopybackReceipt inner;
    if(!i.transport.Finish(t->inner,local,inner,r)){if(!RequiresRestart())i.held={};return false;}
    if(FAILED(i.queue->Signal(i.output.Get(),i.value))||!Wait11(i.output.Get(),i.value)||FAILED(i.context->Wait(i.output11.Get(),i.value))){i.restart=true;return Fail(r,"D3D11 output handoff failed");}
    auto target=t->source.outputTarget?t->source.outputTarget:t->source.color;i.context->CopyResource(target.Get(),i.shared[2].Get());
    if(FAILED(i.context->Signal(i.producer11.Get(),++i.value))){i.restart=true;return Fail(r,"D3D11 copyback signal failed");}
    i.context->Flush();if(!Wait11(i.producer.Get(),i.value)){i.restart=true;return Fail(r,"D3D11 copyback completion unknown");}
    receipt=inner;t->live=false;t->source={};i.active.reset();p={};output={};i.held={};r.clear();return true;
}
bool D3D11Connection::Cancel(PendingFrame& p,std::string& r){auto& i=*impl_;auto t=i.active;
    if(RequiresRestart()||!t||p.ownerLease.get()!=t.get()||!t->live)return Fail(r,"invalid D3D11 cancellation");
    if(!i.transport.Cancel(t->inner,r))return false;t->live=false;t->source={};i.active.reset();p={};return true;}
}
