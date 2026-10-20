// GPL-3.0. FP16/linear709 format and resize color-space handling adapted from
// Win32CaptureSample/SimpleCapture.cpp, Robert Mikhayelyan, MIT, commit
// 49fefe79fd9b11025f0b5eb91783a98888516070. Capability/set pattern adapted from
// Microsoft DirectX-Graphics-Samples D3D12HDR.cpp, MIT, commit
// e5975f9b0744fc5096dd593ed72bf8f5c004164f. Full notices:
// Licenses/Win32CaptureSample-LICENSE.txt; Licenses/DirectXGraphicsSamples-LICENSE.txt.
// NeuRotic owns resources/retirement separately. No HDR metadata policy imported.
#pragma once
#include "WorkerColor.h"
#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <climits>
#include <sstream>
#include <string>
#include <vector>
namespace nrw::color {
inline DXGI_FORMAT TextureFormat(const Representation& value){
    return value.storage==Storage::Bgra8?DXGI_FORMAT_B8G8R8A8_UNORM:
        value.storage==Storage::Rgba16Float?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_UNKNOWN;
}
inline Storage ActualStorage(DXGI_FORMAT format){return format==DXGI_FORMAT_B8G8R8A8_UNORM?Storage::Bgra8:format==DXGI_FORMAT_R16G16B16A16_FLOAT?Storage::Rgba16Float:Storage::Unknown;}
inline DXGI_FORMAT SharedOutputFormat(const Representation& value){return Hdr(value)?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;}
inline std::string ApiFailure(const char* phase,HRESULT hr){std::ostringstream out;out<<phase<<" (HRESULT 0x"<<std::hex<<uint32_t(hr)<<')';return out.str();}
struct Display {
    std::uintptr_t monitor=0;bool spaceKnown=false;std::uint32_t space=0;
    std::int32_t observationCode=0;
    bool operator==(const Display&)const=default;
};
struct DesktopObservation {
    // Unknown display wire state chooses precision-preserving FP16 WGC, without
    // claiming the display or original application is HDR. Only a known SDR
    // desktop keeps the legacy 8-bit path. WGC defines the requested surface's
    // composed domain separately from the original game's unknown encoding.
    bool scRgb=true;std::vector<Display> displays;
    std::uintptr_t destination=0;std::optional<float> sdrWhiteScale;
    std::int32_t whiteObservationCode=0;
    bool SameSourceConfiguration(const DesktopObservation& other)const{return scRgb==other.scRgb&&displays==other.displays;}
    bool SameOutputConfiguration(const DesktopObservation& other)const{return destination==other.destination&&sdrWhiteScale==other.sdrWhiteScale&&whiteObservationCode==other.whiteObservationCode&&SameSourceConfiguration(other);}
    Representation Capture(std::uint64_t epoch)const{return scRgb?WgcScRgb(epoch):WgcSdr(epoch);}
};
// Read the OS SDR-UI reference white, solely for newly generated SDR preview/UI.
// Captured composed HUD/color is already in WGC's domain and must not use it.
inline HRESULT SdrWhiteForGdiName(const wchar_t* name,float& scale){
    for(unsigned attempt=0;attempt<3;++attempt){
        UINT32 pathsCount=0,modesCount=0;LONG error=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathsCount,&modesCount);
        if(error!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(error);
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathsCount);std::vector<DISPLAYCONFIG_MODE_INFO> modes(modesCount);
        error=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathsCount,paths.data(),&modesCount,modes.data(),nullptr);
        if(error==ERROR_INSUFFICIENT_BUFFER)continue;
        if(error!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(error);
        for(UINT32 i=0;i<pathsCount;++i){
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),paths[i].sourceInfo.adapterId,paths[i].sourceInfo.id};
            error=DisplayConfigGetDeviceInfo(&source.header);if(error!=ERROR_SUCCESS)continue;
            if(_wcsicmp(name,source.viewGdiDeviceName)!=0)continue;
            DISPLAYCONFIG_SDR_WHITE_LEVEL white{};white.header={DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL,sizeof(white),paths[i].targetInfo.adapterId,paths[i].targetInfo.id};
            error=DisplayConfigGetDeviceInfo(&white.header);if(error!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(error);
            if(!white.SDRWhiteLevel)return E_INVALIDARG;
            scale=float(white.SDRWhiteLevel)/1000.f;return S_OK;
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
}
class DesktopObserver {
    template<class T>using ComPtr=Microsoft::WRL::ComPtr<T>;
    ComPtr<IDXGIFactory1> factory;std::vector<ComPtr<IDXGIOutput>> outputs;
    bool Rebuild(std::string& reason){
        outputs.clear();factory.Reset();HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if(FAILED(hr)){reason=ApiFailure("Capture color output factory",hr);return false;}
        for(UINT index=0;;++index){ComPtr<IDXGIAdapter1> adapter;hr=factory->EnumAdapters1(index,&adapter);
            if(hr==DXGI_ERROR_NOT_FOUND)break;if(FAILED(hr)){reason=ApiFailure("Capture color adapter enumeration",hr);return false;}
            for(UINT oi=0;;++oi){ComPtr<IDXGIOutput> output;hr=adapter->EnumOutputs(oi,&output);
                if(hr==DXGI_ERROR_NOT_FOUND)break;if(FAILED(hr)){reason=ApiFailure("Capture color output enumeration",hr);return false;}
                outputs.push_back(std::move(output));if(oi==UINT_MAX){reason="Capture color output enumeration exhausted";return false;}}
            if(index==UINT_MAX){reason="Capture color adapter enumeration exhausted";return false;}
        }
        return true;
    }
    struct Monitors {std::vector<HMONITOR> values;bool failed=false;};
    static BOOL CALLBACK Collect(HMONITOR monitor,HDC,LPRECT,LPARAM parameter)noexcept{
        auto& list=*reinterpret_cast<Monitors*>(parameter);try{list.values.push_back(monitor);return TRUE;}catch(...){list.failed=true;return FALSE;}
    }
public:
    bool Observe(HWND window,DesktopObservation& result,std::string& reason){
        result={};reason.clear();RECT rect{};
        if(!GetClientRect(window,&rect)){reason=ApiFailure("Capture color client rectangle",HRESULT_FROM_WIN32(GetLastError()));return false;}
        POINT points[2]{{rect.left,rect.top},{rect.right,rect.bottom}};
        if(!ClientToScreen(window,&points[0])||!ClientToScreen(window,&points[1])){reason=ApiFailure("Capture color client coordinates",HRESULT_FROM_WIN32(GetLastError()));return false;}
        rect={points[0].x,points[0].y,points[1].x,points[1].y};Monitors wanted;
        if(!EnumDisplayMonitors(nullptr,&rect,Collect,reinterpret_cast<LPARAM>(&wanted))){reason=ApiFailure("Capture color monitor observation",wanted.failed?E_OUTOFMEMORY:HRESULT_FROM_WIN32(GetLastError()));return false;}
        const auto destination=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);result.destination=reinterpret_cast<std::uintptr_t>(destination);
        if(wanted.values.empty()&&destination)wanted.values.push_back(destination);
        if(wanted.values.empty()){reason="Capture color has no current desktop monitor";return false;}
        // One cache refresh can recover a real display topology change. These
        // are read-only OS observations, never capture/provider probing.
        for(unsigned refresh=0;refresh<2;++refresh){
            if(!factory||!factory->IsCurrent()||refresh)if(!Rebuild(reason))return false;
            result.displays.clear();bool allKnownSdr=true;std::wstring destinationName;
            for(const auto& output:outputs){DXGI_OUTPUT_DESC desc{};if(FAILED(output->GetDesc(&desc))||!desc.AttachedToDesktop)continue;
                if(std::find(wanted.values.begin(),wanted.values.end(),desc.Monitor)==wanted.values.end())continue;
                Display record;record.monitor=reinterpret_cast<std::uintptr_t>(desc.Monitor);
                ComPtr<IDXGIOutput6> modern;HRESULT hr=output.As(&modern);DXGI_OUTPUT_DESC1 actual{};
                if(SUCCEEDED(hr))hr=modern->GetDesc1(&actual);
                record.observationCode=hr;record.spaceKnown=SUCCEEDED(hr);
                if(record.spaceKnown)record.space=uint32_t(actual.ColorSpace);
                allKnownSdr&=record.spaceKnown&&record.space==uint32_t(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
                result.displays.push_back(record);
                if(desc.Monitor==destination)destinationName=desc.DeviceName;
            }
            std::sort(result.displays.begin(),result.displays.end(),[](const auto& a,const auto& b){return a.monitor<b.monitor;});
            result.displays.erase(std::unique(result.displays.begin(),result.displays.end(),[](const auto& a,const auto& b){return a.monitor==b.monitor;}),result.displays.end());
            if(result.displays.size()!=wanted.values.size())continue;
            result.scRgb=!allKnownSdr;
            if(!result.scRgb){result.sdrWhiteScale=1.f;return true;}
            float scale=0;const HRESULT white=destinationName.empty()?HRESULT_FROM_WIN32(ERROR_NOT_FOUND):SdrWhiteForGdiName(destinationName.c_str(),scale);
            result.whiteObservationCode=white;if(SUCCEEDED(white))result.sdrWhiteScale=scale;
            // Missing UI white does not reject authentic WGC FP16 rendering.
            // A requested generated SDR preview needs its own exact conversion.
            return true;
        }
        reason="Capture color could not identify every current intersecting desktop output";return false;
    }
};
inline bool ConfigureOutputColor(IDXGISwapChain1* swapchain,const Representation& representation,std::string& reason){
    if(!swapchain||!Valid(representation)){reason="Output color requires an actual swapchain and known frame representation";return false;}
    Microsoft::WRL::ComPtr<IDXGISwapChain3> actual;HRESULT hr=swapchain->QueryInterface(IID_PPV_ARGS(&actual));
    if(FAILED(hr)){reason=ApiFailure("Output color-space interface",hr);return false;}
    const auto space=Hdr(representation)?DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    UINT support=0;hr=actual->CheckColorSpaceSupport(space,&support);
    if(FAILED(hr)){reason=ApiFailure("Output color-space capability",hr);return false;}
    if(!(support&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)){reason="Actual output swapchain cannot present the requested color space";return false;}
    hr=actual->SetColorSpace1(space);if(FAILED(hr)){reason=ApiFailure("Output color-space configuration",hr);return false;}
    return true;
}
}
