// GPL-3.0. Optional analysis only; original capture and NR resolve stay separate.
#pragma once
#include "../WorkerContracts.h"
#include "../capture/CaptureColor.h"
#include "../nr/OwnedFrameRing.h"
#include "../../../OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader.h"
#include <d3dcompiler.h>
#include <winrt/base.h>
#include <cstddef>
namespace nrw {
// Exactly the existing core cbuffer, including zero reversible/exposure flags.
// Recipe revision 1 is not reversible-mode 1. The same core bytecode produces
// the full-raster proxy for NR and analysis; no alternate HDR curve is embedded.
struct AnalysisConstants {
    uint32_t mode=0;float white=1;uint32_t width=0,height=0;
    float transfer=1,colour=1;uint32_t debug=0;float maxRatio=2;
    uint32_t passthrough=1;float mvX=1,mvY=1;uint32_t guideWidth=0,guideHeight=0,compare=0;
    float split=0,zoom=1;uint32_t swap=0,transferMode=1;float debugScale=1;
    uint32_t reversible=0,apply=1,gameExposure=0;float preMul=1;uint32_t signedPresent=0,padding[40]{};
};
static_assert(sizeof(AnalysisConstants)==256&&offsetof(AnalysisConstants,signedPresent)==92);
inline uint32_t AnalysisRecipe(const color::Representation& source){return color::Hdr(source)?1:0;}
inline bool AnalysisMatches(const CapturedFrame& frame){
    if(!frame.analysis)return !color::Hdr(frame.color)&&color::Valid(frame.color);
    const auto& a=*frame.analysis;const auto& x=a.stamp;const auto& y=frame.stamp;
    return a.texture&&a.ownership&&a.completed&&a.recipe==AnalysisRecipe(frame.color)&&
        color::SameDomain(a.sourceColor,frame.color)&&x.session==y.session&&x.sequence==y.sequence&&
        x.timestampQpc==y.timestampQpc&&x.reset==y.reset&&x.width==y.width&&x.height==y.height&&
        x.streamEpoch==y.streamEpoch&&x.geometryEpoch==y.geometryEpoch&&x.configEpoch==y.configEpoch&&x.colorEpoch==y.colorEpoch;
}
class AnalysisProxy {
#ifdef NRW_NATIVE_TEST
    friend struct NrOwnedGpuFixture;
#endif
    struct Slot {
        bool retired=true;ComPtr<ID3D11Texture2D> proxy,original,encoded;
        ComPtr<ID3D11ShaderResourceView> proxyView;
        ComPtr<ID3D11UnorderedAccessView> proxyWrite,originalWrite;
        ComPtr<ID3D11RenderTargetView> encodedTarget;
    };
    OwnedFrameRing<Slot> slots;
    ComPtr<ID3D11ComputeShader> encode;
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;ComPtr<ID3D11Query> completion;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11ShaderResourceView> input;
    CapturedFrame pendingSource;std::shared_ptr<Slot> pending;
    bool unknown=false;
    static void Check(HRESULT hr,const char* phase){if(FAILED(hr))throw std::runtime_error(color::ApiFailure(phase,hr));}
    void Initialize(ID3D11Device* d,ID3D11DeviceContext* c){
        if(device){if(device.Get()!=d||context.Get()!=c)throw std::runtime_error("Analysis proxy device changed");return;}
        // Commit owner only after all setup succeeds, before any recording.
        ComPtr<ID3D11ComputeShader> cs;Check(d->CreateComputeShader(DlssNr_cso,sizeof(DlssNr_cso),nullptr,&cs),"Core analysis encode shader");
        constexpr char blit[]="Texture2D<float4> proxy:register(t0);float4 VS(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}float4 PS(float4 p:SV_Position):SV_Target{return proxy.Load(int3(int2(p.xy),0));}";
        ComPtr<ID3DBlob> vs,ps,error;Check(D3DCompile(blit,sizeof(blit)-1,nullptr,nullptr,nullptr,"VS","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error),"Analysis copy vertex");
        Check(D3DCompile(blit,sizeof(blit)-1,nullptr,nullptr,nullptr,"PS","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error),"Analysis copy pixel");
        Check(d->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"Analysis copy vertex object");
        Check(d->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel),"Analysis copy pixel object");
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(AnalysisConstants);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;Check(d->CreateBuffer(&cb,nullptr,&constants),"Analysis constants");
        D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};Check(d->CreateQuery(&q,&completion),"Analysis completion query");
        encode=std::move(cs);device=d;context=c;
    }
public:
    bool RequiresRestart()const{return unknown;}
    // Off consumers never invoke this method. SDR is a byte-preserving bypass.
    CapturedFrame Prepare(const CapturedFrame& frame,ID3D11Device* d,ID3D11DeviceContext* c){
        if(unknown)throw std::runtime_error("Analysis proxy completion unknown; restart worker required");
        if(!frame.texture||!frame.ownership||!frame.stamp.sequence||!frame.stamp.session||!frame.stamp.timestampQpc||(!color::Valid(frame.color)||frame.color.provenance==color::Provenance::GeneratedSdrPreview))throw std::runtime_error("Analysis requires an owned known-color capture");
        D3D11_TEXTURE2D_DESC desc{};frame.texture->GetDesc(&desc);ComPtr<ID3D11Device> parent;frame.texture->GetDevice(&parent);
        ComPtr<ID3D11Device> contextDevice;if(c)c->GetDevice(&contextDevice);
        if(!d||!c||contextDevice.Get()!=d||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||parent.Get()!=d||desc.Width!=frame.stamp.width||desc.Height!=frame.stamp.height||desc.SampleDesc.Count!=1||desc.MipLevels!=1||desc.ArraySize!=1||!color::Matches(frame.color,color::ActualStorage(desc.Format),frame.stamp.colorEpoch))throw std::runtime_error("Analysis capture device/extent/color mismatch");
        if(!color::Hdr(frame.color))return frame;
        if(desc.Width>3840||desc.Height>2160)throw std::runtime_error("HDR analysis exceeds the owned 3840 x 2160 profile; image-only NR remains available");
        const auto started=MeasureClock::now();
        Initialize(d,c);auto slot=slots.TryAcquire();if(!slot)throw std::runtime_error("Analysis slots busy; estimate dropped");
        try{
            D3D11_TEXTURE2D_DESC old{};if(slot->encoded)slot->encoded->GetDesc(&old);
            if(old.Width!=desc.Width||old.Height!=desc.Height||!slot->proxy||!slot->original||!slot->encoded||!slot->proxyView||!slot->proxyWrite||!slot->originalWrite||!slot->encodedTarget){
                Slot replacement;replacement.retired=false; // publish only a complete bundle
                desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=desc.MiscFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
                Check(d->CreateTexture2D(&desc,nullptr,&replacement.proxy),"Analysis FP16 proxy");Check(d->CreateTexture2D(&desc,nullptr,&replacement.original),"Analysis discarded signed-original scratch");
                Check(d->CreateShaderResourceView(replacement.proxy.Get(),nullptr,&replacement.proxyView),"Analysis proxy view");Check(d->CreateUnorderedAccessView(replacement.proxy.Get(),nullptr,&replacement.proxyWrite),"Analysis proxy write view");Check(d->CreateUnorderedAccessView(replacement.original.Get(),nullptr,&replacement.originalWrite),"Analysis original scratch write view");
                desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;Check(d->CreateTexture2D(&desc,nullptr,&replacement.encoded),"Analysis encoded full-raster sidecar");Check(d->CreateRenderTargetView(replacement.encoded.Get(),nullptr,&replacement.encodedTarget),"Analysis encoded target");
                *slot=std::move(replacement);
            }
            input.Reset();Check(d->CreateShaderResourceView(frame.texture.Get(),nullptr,&input),"Analysis captured view");
            pendingSource=frame;pending=slot; // retain exact source lease before dispatch
            AnalysisConstants values;values.width=frame.stamp.width;values.height=frame.stamp.height;const auto recipe=color::Model(frame.color);values.white=recipe->whitePoint;values.passthrough=recipe->passthrough;values.signedPresent=recipe->signedOriginal;
            unknown=true;context->ClearState();context->UpdateSubresource(constants.Get(),0,nullptr,&values,0,0);
            auto* cb=constants.Get();ID3D11ShaderResourceView* srvs[]{input.Get(),nullptr,nullptr,nullptr,nullptr};ID3D11UnorderedAccessView* uavs[]{slot->proxyWrite.Get(),slot->originalWrite.Get()};
            context->CSSetConstantBuffers(0,1,&cb);context->CSSetShaderResources(0,5,srvs);context->CSSetUnorderedAccessViews(0,2,uavs,nullptr);context->CSSetShader(encode.Get(),nullptr,0);context->Dispatch((values.width+7)/8,(values.height+7)/8,1);context->ClearState();
            auto* rtv=slot->encodedTarget.Get();auto* srv=slot->proxyView.Get();context->OMSetRenderTargets(1,&rtv,nullptr);context->PSSetShaderResources(0,1,&srv);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);D3D11_VIEWPORT viewport{0,0,float(values.width),float(values.height),0,1};context->RSSetViewports(1,&viewport);context->Draw(3,0);context->ClearState();context->End(completion.Get());context->Flush();
            const auto start=GetTickCount64();for(;;){BOOL done=FALSE;HRESULT hr=context->GetData(completion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(hr==S_OK&&done)break;if(FAILED(hr)||GetTickCount64()-start>=5000)throw std::runtime_error(color::ApiFailure("Analysis completion unproved; exact owners quarantined",FAILED(hr)?hr:HRESULT_FROM_WIN32(WAIT_TIMEOUT)));Sleep(1);}
            unknown=false;slot->retired=true;CapturedFrame result=frame;
            auto sidecar=std::make_shared<AnalysisFrame>();sidecar->stamp=frame.stamp;sidecar->sourceColor=frame.color;sidecar->recipe=1;sidecar->texture=slot->encoded;sidecar->ownership=slot;sidecar->completed=true;sidecar->hostMilliseconds=ElapsedMs(started);sidecar->ownedBytes=uint64_t(values.width)*values.height*20;result.analysis=std::move(sidecar);
            pending.reset();pendingSource={};input.Reset();return result;
        }catch(...){if(!unknown)slot->retired=true;throw;}
    }
};
}
