#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>
#include <filesystem>
namespace nrw::depth {
inline uint64_t CudaDriverFileVersion(){
 wchar_t system[32768]{};const auto n=GetSystemDirectoryW(system,32768);if(!n||n>=32768)return 0;
 const auto path=std::filesystem::path(system)/L"nvcuda.dll";DWORD unused=0;const auto size=GetFileVersionInfoSizeW(path.c_str(),&unused);if(!size)return 0;
 std::vector<uint8_t> bytes(size);if(!GetFileVersionInfoW(path.c_str(),0,size,bytes.data()))return 0;
 VS_FIXEDFILEINFO* info=nullptr;UINT length=0;if(!VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&length)||length<sizeof(*info)||info->dwSignature!=0xfeef04bd)return 0;
 return (uint64_t(info->dwFileVersionMS)<<32)|info->dwFileVersionLS;
}
}
