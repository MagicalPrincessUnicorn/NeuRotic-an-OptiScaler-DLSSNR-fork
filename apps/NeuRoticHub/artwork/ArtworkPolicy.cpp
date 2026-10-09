#include "ArtworkPolicy.h"
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
using Microsoft::WRL::ComPtr;
namespace nh {
bool AllowedArtworkUrl(const std::wstring& url){
 if(url.size()>4096||url.find_first_of(L"\r\n")!=std::wstring::npos)return false;
 URL_COMPONENTS parts{sizeof(parts)};parts.dwHostNameLength=parts.dwUserNameLength=parts.dwPasswordLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=(DWORD)-1;
 if(!WinHttpCrackUrl(url.c_str(),(DWORD)url.size(),0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS||parts.nPort!=443||parts.dwUserNameLength||parts.dwPasswordLength)return false;
 std::wstring host(parts.lpszHostName,parts.dwHostNameLength);std::transform(host.begin(),host.end(),host.begin(),[](wchar_t c){return (wchar_t)towlower(c);});
 return host==L"api.steampowered.com"||host==L"shared.fastly.steamstatic.com"||host==L"shared.steamstatic.com"||host==L"shared.akamai.steamstatic.com"||host==L"cdn.akamai.steamstatic.com"||host==L"steamcdn-a.akamaihd.net";
}
ArtworkPixels DecodeArtwork(const std::vector<uint8_t>& bytes,const std::string& role){
 ArtworkPixels image;
 if(bytes.size()<8||bytes.size()>8388608)return image;
 const bool png=bytes[0]==0x89&&bytes[1]=='P'&&bytes[2]=='N'&&bytes[3]=='G';const bool jpeg=bytes[0]==0xff&&bytes[1]==0xd8&&bytes[2]==0xff;
 const unsigned iconFrames=bytes[4]|((unsigned)bytes[5]<<8);
 const bool ico=role=="icon"&&bytes[0]==0&&bytes[1]==0&&bytes[2]==1&&bytes[3]==0&&iconFrames>0&&iconFrames<=64;
 if(!png&&!jpeg&&!ico)return image;
 ComPtr<IWICImagingFactory> factory;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return image;
 ComPtr<IWICStream> stream;if(FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),(DWORD)bytes.size())))return image;
 ComPtr<IWICBitmapDecoder> decoder;if(FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)))return image;
 ComPtr<IWICBitmapFrameDecode> frame;if(FAILED(decoder->GetFrame(0,&frame)))return image;
 UINT width=0,height=0;if(FAILED(frame->GetSize(&width,&height))||!width||!height||width>4096||height>4096)return image;
 unsigned maximum=role=="icon"?64:role=="logo"?1280:1600;
 float factor=std::min(1.f,(float)maximum/(float)std::max(width,height));image.width=std::max(1u,(unsigned)(width*factor));image.height=std::max(1u,(unsigned)(height*factor));
 ComPtr<IWICBitmapScaler> scaler;if(FAILED(factory->CreateBitmapScaler(&scaler))||FAILED(scaler->Initialize(frame.Get(),image.width,image.height,WICBitmapInterpolationModeFant)))return {};
 ComPtr<IWICFormatConverter> converter;if(FAILED(factory->CreateFormatConverter(&converter))||FAILED(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return {};
 image.rgba.resize((size_t)image.width*image.height*4);if(FAILED(converter->CopyPixels(nullptr,image.width*4,(UINT)image.rgba.size(),image.rgba.data())))return {};return image;
}
ArtworkPixels ExtractGameIcon(const std::wstring& path){
 HICON icon=nullptr;if(!ExtractIconExW(path.c_str(),0,&icon,nullptr,1)||!icon)return {};
 ArtworkPixels image;image.width=image.height=64;image.rgba.resize(64*64*4);
 auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=64;info.bmiHeader.biHeight=-64;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* raw=nullptr;
 auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
 if(!dc||!bitmap||!raw){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);DestroyIcon(icon);return {};}
 memset(raw,0,64*64*4);auto previous=SelectObject(dc,bitmap);bool drawn=DrawIconEx(dc,0,0,icon,64,64,0,nullptr,DI_NORMAL)!=FALSE;
 auto pixels=(uint8_t*)raw;bool alpha=false;for(size_t i=3;i<image.rgba.size();i+=4)if(pixels[i]){alpha=true;break;}
 for(size_t i=0;i<image.rgba.size();i+=4){auto a=alpha?pixels[i+3]:(pixels[i]||pixels[i+1]||pixels[i+2]?255:0);image.rgba[i+3]=(uint8_t)a;for(int c=0;c<3;c++){unsigned color=pixels[i+2-c];image.rgba[i+c]=(uint8_t)(a&&alpha?std::min(255u,color*255/a):color);}}
 SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);DestroyIcon(icon);return drawn?image:ArtworkPixels{};
}
}
