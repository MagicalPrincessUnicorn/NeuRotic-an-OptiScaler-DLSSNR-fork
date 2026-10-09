#include "ThemeTransition.h"
#include <d3dcompiler.h>
#include <cmath>
#include <algorithm>
#include <cstring>
namespace nh {
float WaterSurface(float x,float progress,float time){float p=std::clamp(progress,0.f,1.f),a=std::sin(p*3.14159265f);return 1.12f-1.24f*p+a*(.023f*std::sin(x*18.f-time*5.f)+.012f*std::sin(x*33.f+time*3.f)-.06f*(std::exp(-x*9.f)+std::exp(-(1-x)*9.f)));}
static const char* shader=R"(
cbuffer Params:register(b0){float width;float height;float progress;float time;float targetLight;float3 padding;}
Texture2D oldImage:register(t0);Texture2D newImage:register(t1);SamplerState linearClamp:register(s0);
struct Out{float4 position:SV_Position;float2 uv:TEXCOORD0;};
Out VS(uint id:SV_VertexID){Out o;float2 p=float2((id<<1)&2,id&2);o.uv=p;o.position=float4(p.x*2-1,1-p.y*2,0,1);return o;}
float4 PS(Out o):SV_Target{
 // Match the game's light-water level: light grows below a rising surface,
 // while dark replaces it above a falling surface.
 float level=targetLight>0.5?progress:1-progress;
 float a=sin(progress*3.14159265);float surface=1.12-1.24*level+a*(.023*sin(o.uv.x*18-time*5)+.012*sin(o.uv.x*33+time*3)-.06*(exp(-o.uv.x*9)+exp(-(1-o.uv.x)*9)));
 float distance=o.uv.y-surface;float wet=smoothstep(-1.5/height,1.5/height,distance);
 float ripple=a*exp(-abs(distance)*45);float2 refracted=o.uv+float2(sin(o.uv.y*55+time*6)*.0025,sin(o.uv.x*25-time*7)*.002)*ripple;
 float4 fresh=newImage.Sample(linearClamp,refracted);float4 old=oldImage.Sample(linearClamp,o.uv);float4 color=lerp(old,fresh,targetLight>0.5?wet:1-wet);
 float foam=a*exp(-abs(distance)*height*.27);color.rgb=lerp(color.rgb,float3(.48,.72,.88),foam*.27);return color;
})";
bool ThemeTransition::Attach(ID3D11Device* d,ID3D11DeviceContext* c){
 device=d;context=c;ComPtr<ID3DBlob> vs,ps,error;if(FAILED(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"VS","vs_4_0",0,0,&vs,&error))||FAILED(D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"PS","ps_4_0",0,0,&ps,&error)))return false;
 if(FAILED(d->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex))||FAILED(d->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel)))return false;
 D3D11_BUFFER_DESC b{};b.ByteWidth=32;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
 D3D11_BLEND_DESC bl{};bl.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
 D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
 D3D11_DEPTH_STENCIL_DESC z{};return SUCCEEDED(d->CreateBuffer(&b,nullptr,&constants))&&SUCCEEDED(d->CreateSamplerState(&s,&sampler))&&SUCCEEDED(d->CreateBlendState(&bl,&blend))&&SUCCEEDED(d->CreateRasterizerState(&r,&raster))&&SUCCEEDED(d->CreateDepthStencilState(&z,&depth));
}
void ThemeTransition::Cancel(){active=false;oldView.Reset();newView.Reset();oldImage.Reset();newImage.Reset();ID3D11ShaderResourceView* views[2]{};if(context)context->PSSetShaderResources(0,2,views);}
void ThemeTransition::Begin(ID3D11Texture2D* source,double now,bool light){
 Cancel();if(!vertex||!pixel||!source)return;D3D11_TEXTURE2D_DESC desc;source->GetDesc(&desc);width=desc.Width;height=desc.Height;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=desc.MiscFlags=0;
 if(FAILED(device->CreateTexture2D(&desc,nullptr,&oldImage))||FAILED(device->CreateTexture2D(&desc,nullptr,&newImage))||FAILED(device->CreateShaderResourceView(oldImage.Get(),nullptr,&oldView))||FAILED(device->CreateShaderResourceView(newImage.Get(),nullptr,&newView))){Cancel();return;}
 ID3D11Texture2D* previous=source;if(remembered){D3D11_TEXTURE2D_DESC saved;remembered->GetDesc(&saved);if(saved.Width==width&&saved.Height==height)previous=remembered.Get();}context->CopyResource(oldImage.Get(),previous);started=now;targetLight=light;active=true;
}
void ThemeTransition::Remember(ID3D11Texture2D* source){D3D11_TEXTURE2D_DESC desc;source->GetDesc(&desc);if(remembered){D3D11_TEXTURE2D_DESC saved;remembered->GetDesc(&saved);if(saved.Width!=desc.Width||saved.Height!=desc.Height)remembered.Reset();}if(!remembered){desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=desc.CPUAccessFlags=desc.MiscFlags=0;if(FAILED(device->CreateTexture2D(&desc,nullptr,&remembered)))return;}context->CopyResource(remembered.Get(),source);}
void ThemeTransition::Render(ID3D11Texture2D* source,ID3D11RenderTargetView* target,double now){
 if(!active)return;float progress=(float)((now-started)/1.15);D3D11_TEXTURE2D_DESC desc;source->GetDesc(&desc);if(progress>=1||desc.Width!=width||desc.Height!=height){Cancel();return;}
 context->OMSetRenderTargets(0,nullptr,nullptr);context->CopyResource(newImage.Get(),source);context->OMSetRenderTargets(1,&target,nullptr);
 float values[8]={(float)width,(float)height,std::clamp(progress,0.f,1.f),(float)(now-started),targetLight?1.f:0.f,0,0,0};context->UpdateSubresource(constants.Get(),0,nullptr,values,0,0);
 D3D11_VIEWPORT viewport{0,0,(float)width,(float)height,0,1};context->RSSetViewports(1,&viewport);context->RSSetState(raster.Get());context->OMSetBlendState(blend.Get(),nullptr,0xffffffff);context->OMSetDepthStencilState(depth.Get(),0);
 context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);
 auto buffer=constants.Get();auto sample=sampler.Get();ID3D11ShaderResourceView* views[]={oldView.Get(),newView.Get()};context->PSSetConstantBuffers(0,1,&buffer);context->PSSetSamplers(0,1,&sample);context->PSSetShaderResources(0,2,views);context->Draw(3,0);views[0]=views[1]=nullptr;context->PSSetShaderResources(0,2,views);
}
}
