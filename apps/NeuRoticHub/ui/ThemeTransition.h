#pragma once
#include <d3d11.h>
#include <wrl/client.h>
namespace nh {
using Microsoft::WRL::ComPtr;
float WaterSurface(float x,float progress,float time);
class ThemeTransition {
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 ComPtr<ID3D11Texture2D> oldImage,newImage,remembered;
 ComPtr<ID3D11ShaderResourceView> oldView,newView;
 ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
 ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11Buffer> constants;
 ComPtr<ID3D11BlendState> blend;ComPtr<ID3D11RasterizerState> raster;ComPtr<ID3D11DepthStencilState> depth;
 double started=0;bool active=false,targetLight=false;unsigned width=0,height=0;
public:
 bool Attach(ID3D11Device*,ID3D11DeviceContext*);
 void Begin(ID3D11Texture2D*,double,bool light);
 void Render(ID3D11Texture2D*,ID3D11RenderTargetView*,double);
 void Remember(ID3D11Texture2D*);
 void Cancel();
 bool Active() const{return active;}
};
}
