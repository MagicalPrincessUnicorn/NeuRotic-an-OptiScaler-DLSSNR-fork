#include <d3d10_1.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <initializer_list>
using Microsoft::WRL::ComPtr;
int main(){ComPtr<IDXGIFactory4> factory;CreateDXGIFactory1(IID_PPV_ARGS(&factory));ComPtr<IDXGIAdapter1>a;factory->EnumAdapters1(0,&a);ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;D3D11CreateDevice(a.Get(),D3D_DRIVER_TYPE_UNKNOWN,0,0,0,0,D3D11_SDK_VERSION,&d,0,&c);ComPtr<ID3D10Device>d10;D3D10CreateDevice(a.Get(),D3D10_DRIVER_TYPE_HARDWARE,0,0,D3D10_SDK_VERSION,&d10);
for(auto format:{DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32G32_FLOAT,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R8_UNORM})for(bool rt:{false,true}){D3D11_TEXTURE2D_DESC x{};x.Width=256;x.Height=64;x.Format=format;x.MipLevels=x.ArraySize=x.SampleDesc.Count=1;x.BindFlags=D3D11_BIND_SHADER_RESOURCE|(rt?D3D11_BIND_RENDER_TARGET:0);x.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;ComPtr<ID3D11Texture2D>t;auto hr=d->CreateTexture2D(&x,0,&t);D3D10_TEXTURE2D_DESC y{};y.Width=256;y.Height=64;y.Format=format;y.MipLevels=y.ArraySize=y.SampleDesc.Count=1;y.BindFlags=D3D10_BIND_SHADER_RESOURCE|(rt?D3D10_BIND_RENDER_TARGET:0);y.MiscFlags=D3D10_RESOURCE_MISC_SHARED;ComPtr<ID3D10Texture2D>u;auto h10=d10->CreateTexture2D(&y,0,&u);printf("format %u RTV %u D11 %08lx D10 %08lx\n",unsigned(format),unsigned(rt),static_cast<unsigned long>(hr),static_cast<unsigned long>(h10));}}
