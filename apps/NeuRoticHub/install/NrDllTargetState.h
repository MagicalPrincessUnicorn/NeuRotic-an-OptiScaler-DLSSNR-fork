#pragma once
#include "NrDllInstallChoice.h"
#include <windows.h>
#include <winver.h>
#pragma comment(lib,"version.lib")

namespace nh {
inline bool NrDllKnownLoader(const std::filesystem::path& path){
 DWORD ignored=0;const auto bytes=GetFileVersionInfoSizeW(path.c_str(),&ignored);
 if(!bytes||bytes>4*1024*1024)return false;
 std::vector<unsigned char> data(bytes);if(!GetFileVersionInfoW(path.c_str(),0,bytes,data.data()))return false;
 struct Translation {WORD language,codePage;};Translation* translations=nullptr;UINT count=0;
 if(!VerQueryValueW(data.data(),L"\\VarFileInfo\\Translation",reinterpret_cast<void**>(&translations),&count))return false;
 for(UINT i=0;i<count/sizeof(Translation);++i){
  wchar_t key[96]{};swprintf_s(key,L"\\StringFileInfo\\%04x%04x\\OriginalFilename",translations[i].language,translations[i].codePage);
  wchar_t* filename=nullptr;UINT length=0;
  if(VerQueryValueW(data.data(),key,reinterpret_cast<void**>(&filename),&length)&&length&&_wcsicmp(filename,L"OptiScaler.dll")==0)return true;
 }
 return false;
}
inline NrDllTargetState InspectNrDllTarget(const std::filesystem::path& root){
 // This is admission at click time only. The installer repeats classification
 // under its target mutex before generating any optional provider write.
 std::error_code error;
 if(std::filesystem::exists(root/L"NeuRotic/Installer/Current-Install.json",error))return NrDllTargetState::Existing;
 if(error)return NrDllTargetState::Uncertain;
 for(const auto* proxy:{L"dxgi.dll",L"winmm.dll",L"version.dll",L"dbghelp.dll",L"d3d12.dll",L"wininet.dll",L"winhttp.dll",L"OptiScaler.asi",L"OptiScaler.dll"})
  if(NrDllKnownLoader(root/proxy))return NrDllTargetState::Existing;
 if(std::filesystem::exists(root/L"NeuRotic/Installer",error)||error)return NrDllTargetState::Uncertain;
 if(std::filesystem::exists(root/L"OptiScaler.ini",error)||error)return NrDllTargetState::Uncertain;
 return NrDllTargetState::Initial;
}
}
