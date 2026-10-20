"""Exercise production V-Sync UI binding with real ImGui and injected user edits.

Does not load the game, renderer, provider or production singleton.
"""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1];out=root/'builds/ui-release/ingame-settings';out.mkdir(parents=True,exist_ok=True)
source=(root/'OptiScaler/menu/menu_common.cpp').read_text(encoding='utf-8')
fn=source[source.index('void MenuCommon::RenderVsyncSettings('):source.index('void MenuCommon::RenderMipmapBiasSettings(')]
fn=fn.replace('ImGui::Combo(', 'EditCombo(').replace('ImGui::Button(', 'EditButton(').replace('ImGui::SliderInt(', 'EditSlider(')
cpp=r'''
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <imgui/imgui_impl_dx11.h>
#include <algorithm>
#include <string>
#include <cstdio>
#include <cassert>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include "CustomOptional.h"
enum API { DX12, Vulkan }; enum class FGOutput { NoFG, XeFG };
struct Config { CustomOptional<bool,NoDefault> ForceVsync; CustomOptional<unsigned> VsyncInterval{0}; };
struct State { API swapchainApi=DX12; FGOutput activeFgOutput=FGOutput::NoFG; void* currentFG=nullptr; bool WAR_xefgRequestFGToggle=false; };
struct RenderMenuContext { State& state; Config* config; float menuResScale=1; };
namespace MenuCommon { void RenderVsyncSettings(RenderMenuContext&); }
#define LOG_DEBUG(...) ((void)0)
static std::string action; static int selection=0,visits=0;static bool fits=true;
static void Measure(){fits &= ImGui::GetItemRectMax().x<=ImGui::GetCurrentWindow()->WorkRect.Max.x+1;}
static bool EditCombo(const char* label,int* value,const char*const* choices,int count){
 ++visits;ImGui::Combo(label,value,choices,count);Measure();if(action==label){*value=selection;return true;}return false;
}
static bool EditButton(const char* label){ImGui::Button(label);Measure();return action==label;}
static bool EditSlider(const char* label,int* value,int lo,int hi){ImGui::SliderInt(label,value,lo,hi);Measure();if(action==label){*value=selection;return true;}return false;}
@@FUNCTION@@
int main(){
 Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> renderer;
 assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&renderer)));
 D3D11_TEXTURE2D_DESC desc{};desc.Width=1600;desc.Height=1200;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
 Microsoft::WRL::ComPtr<ID3D11Texture2D> target,staging;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
 Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;assert(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&view)));
 desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));
 ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=io.LogFilename=nullptr;io.DisplaySize={1600,1200};io.DeltaTime=1.f/60;io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.Fonts->AddFontDefault();
 assert(ImGui_ImplDX11_Init(device.Get(),renderer.Get()));
 Config config;State state;RenderMenuContext ctx{state,&config};int checks=0;
 auto frame=[&](const char* edit,int choice,float width=500,float scale=1){
   action=edit;selection=choice;ImGui_ImplDX11_NewFrame();ImGui::NewFrame();ImGui::PushFont(nullptr,16*scale);ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,500});ImGui::Begin("Settings fixture",nullptr,ImGuiWindowFlags_NoSavedSettings);MenuCommon::RenderVsyncSettings(ctx);ImGui::End();ImGui::PopFont();ImGui::Render();++checks;
   const float clear[]={.025f,.03f,.042f,1};renderer->ClearRenderTargetView(view.Get(),clear);auto* rtv=view.Get();renderer->OMSetRenderTargets(1,&rtv,nullptr);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
   if(checks<=18 && checks%3==0){
     renderer->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(renderer->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)));
     const int w=int(width),h=220;BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+w*h*4;info.biSize=sizeof(info);info.biWidth=w;info.biHeight=-h;info.biPlanes=1;info.biBitCount=32;
     char name[160];std::snprintf(name,sizeof(name),"builds/ui-release/ingame-settings/vsync-%d-%d.bmp",w,int(scale*100));std::ofstream image(name,std::ios::binary);image.write((char*)&file,sizeof(file));image.write((char*)&info,sizeof(info));std::vector<unsigned char> row(w*4);
     for(int y=0;y<h;++y){auto* src=(unsigned char*)mapped.pData+y*mapped.RowPitch;for(int x=0;x<w;++x){row[x*4]=src[x*4+2];row[x*4+1]=src[x*4+1];row[x*4+2]=src[x*4];row[x*4+3]=255;}image.write((char*)row.data(),row.size());}renderer->Unmap(staging.Get(),0);
   }
 };
 for(float width:{420.f,720.f,1040.f})for(float scale:{1.f,1.5f})for(int pass=0;pass<3;++pass)frame("",0,width,scale);
 assert(!config.ForceVsync.has_value());assert(!config.VsyncInterval.has_value());
 frame("V-Sync",1);assert(config.ForceVsync==true);assert(!config.VsyncInterval.has_value());
 frame("",0);assert(config.VsyncInterval.value_or_default()==0); // legacy zero is displayed as effective one, never rewritten
 frame("Sync interval",3);assert(config.VsyncInterval==3u);
 frame("V-Sync",2);assert(config.ForceVsync==false);assert(config.VsyncInterval==3u);
 state.activeFgOutput=FGOutput::XeFG;state.currentFG=&state;frame("V-Sync",0);assert(!config.ForceVsync.has_value());assert(state.WAR_xefgRequestFGToggle);
 state.WAR_xefgRequestFGToggle=false;frame("",0);assert(!state.WAR_xefgRequestFGToggle);
 frame("V-Sync",1);frame("Reset##VsyncInterval",0);assert(config.VsyncInterval.value_or_default()==0);assert(config.ForceVsync==true);
 frame("Reset##10",0);assert(!config.ForceVsync.has_value());
 const int before=visits;state.swapchainApi=Vulkan;frame("V-Sync",1);assert(visits==before);assert(!config.ForceVsync.has_value());
 assert(fits);ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();std::printf("PASS: %d production V-Sync frames; Game/On/Off, resets, idle persistence, API gate, XeFG notification and layout.\n",checks);
}
'''
(out/'vsync.cpp').write_text(cpp.replace('@@FUNCTION@@',fn),encoding='utf-8')
batch=r'''@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /utf-8 /DNOMINMAX /I OptiScaler /I OptiScaler\include /I external\freetype builds\ui-release\ingame-settings\vsync.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\imgui_impl_dx11.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:builds\ui-release\ingame-settings\vsync.exe /Fo:builds\ui-release\ingame-settings\ /link /INCREMENTAL:NO d3d11.lib d3dcompiler.lib dxgi.lib user32.lib shell32.lib external\freetype\freetype.lib
if errorlevel 1 exit /b 1
builds\ui-release\ingame-settings\vsync.exe
exit /b %errorlevel%
'''
(out/'vsync.cmd').write_text(batch)
r=subprocess.run(['cmd','/c',str(out/'vsync.cmd')],cwd=root,capture_output=True,text=True)
print(r.stdout+r.stderr);sys.exit(r.returncode)
