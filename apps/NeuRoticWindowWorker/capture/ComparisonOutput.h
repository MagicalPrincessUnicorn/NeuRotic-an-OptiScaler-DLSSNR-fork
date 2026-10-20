#pragma once
#include "ComparisonBands.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <winrt/base.h>
#pragma comment(lib,"d3dcompiler.lib")
namespace nrw {
// Cached on the capture owner. Frame views remain owned until its existing fence
// proves completion (including failure/quarantine paths), like the source pair.
class ComparisonOutput {
 Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex;
 Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel;
 Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
 Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;
 Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sourceView;
 Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targetView;
 void Initialize(ID3D11Device* device) {
  if(pixel)return;
  constexpr char shader[]=R"(
cbuffer Settings : register(b0) { float2 extent; float split; uint direction; };
Texture2D<float4> source : register(t0);
float4 VS(uint id : SV_VertexID) : SV_Position {
 float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);
}
float4 PS(float4 position : SV_Position) : SV_Target {
 float2 uv=position.xy/extent;
 if(direction==5||direction==7)uv.x=1-uv.x;
 if(direction==6||direction==7)uv.y=1-uv.y;
 if((uv.x+uv.y)*0.5>=split)discard;
 return source.Load(int3(int2(position.xy),0));
})";
  Microsoft::WRL::ComPtr<ID3DBlob> code,error;
  winrt::check_hresult(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"VS","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error));
  winrt::check_hresult(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex));
  code.Reset();error.Reset();winrt::check_hresult(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"PS","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error));
  Microsoft::WRL::ComPtr<ID3D11PixelShader> readyPixel;winrt::check_hresult(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&readyPixel));
  D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=16;buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;winrt::check_hresult(device->CreateBuffer(&buffer,nullptr,&constants));
  D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;winrt::check_hresult(device->CreateRasterizerState(&raster,&rasterizer));pixel=std::move(readyPixel);
 }
public:
 void ReleaseFrameViews(){sourceView.Reset();targetView.Reset();}
#ifdef NRW_CAPTURE_TEST
 bool HasFrameViews()const{return sourceView&&targetView;}
#endif
 void Compose(ID3D11DeviceContext* context,ID3D11Texture2D* output,ID3D11Texture2D* original,ID3D11Texture2D* enhanced,uint32_t width,uint32_t height,float split,int stripes,int direction=0) {
 if(split>=1&&!stripes)context->CopyResource(output,original);
 else {
  context->CopyResource(output,enhanced);
  if(stripes||direction==0)for(auto [left,right]:ComparisonBands(width,split,stripes)) {
    D3D11_BOX band{left,0,0,right,height,1};context->CopySubresourceRegion(output,0,left,0,0,original,0,&band);
  }
  else if(split>0&&direction<4){
   const auto count=uint32_t((direction==1?width:height)*split);
   if(count){D3D11_BOX box{0,0,0,width,height,1};if(direction==1)box.left=width-count;else if(direction==2)box.bottom=count;else box.top=height-count;context->CopySubresourceRegion(output,0,box.left,box.top,0,original,0,&box);}
  }
  else if(split>0){
   Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);Initialize(device.Get());
   winrt::check_hresult(device->CreateShaderResourceView(original,nullptr,&sourceView));winrt::check_hresult(device->CreateRenderTargetView(output,nullptr,&targetView));
   struct Settings{float width,height,split;uint32_t direction;} settings{float(width),float(height),split,uint32_t(direction)};
   context->ClearState();context->UpdateSubresource(constants.Get(),0,nullptr,&settings,0,0);
   D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);context->RSSetState(rasterizer.Get());
   context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
   auto cb=constants.Get();auto srv=sourceView.Get();auto rtv=targetView.Get();context->PSSetConstantBuffers(0,1,&cb);context->PSSetShaderResources(0,1,&srv);context->OMSetRenderTargets(1,&rtv,nullptr);context->Draw(3,0);context->ClearState();
  }
 }
}
};
}
