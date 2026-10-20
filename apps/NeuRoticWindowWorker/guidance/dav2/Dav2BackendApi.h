// GPL-3.0. ABI shared by the optional CUDA module and dependency-free worker.
#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstddef>
namespace nrw::depth {
inline constexpr uint32_t BackendVersion=1;
struct CacheIdentity {uint8_t uuid[16]{};int32_t driver=0,runtime=0,tensorRT=0,computeMajor=0,computeMinor=0;uint64_t driverFileVersion=0;};
struct BackendCreate {
 uint32_t size=sizeof(BackendCreate),version=BackendVersion;
 const void* engine=nullptr;size_t engineBytes=0;
 uint32_t width=518,height=518;
 LUID adapter{};
 CacheIdentity identity{};
 const wchar_t* runtimeRoot=nullptr;
 HANDLE input=nullptr,output=nullptr,preparedFence=nullptr,outputFence=nullptr;
 uint64_t inputAllocation=0,outputAllocation=0,inputBytes=0,outputBytes=0;
};
enum class BackendPoll:uint32_t {Idle,Pending,Complete,Faulted};
struct BackendApi {
 uint32_t size=sizeof(BackendApi),version=BackendVersion;
 void* (__cdecl *create)(const BackendCreate*,char*,size_t)=nullptr;
 bool (__cdecl *submit)(void*,uint64_t,uint64_t,char*,size_t)=nullptr;
 BackendPoll (__cdecl *poll)(void*,char*,size_t)=nullptr;
 // Close must refuse possible unfinished work. Host retains module and all D3D owners on false.
 bool (__cdecl *close)(void*)=nullptr;
};
inline bool ValidApi(const BackendApi* a)noexcept{return a&&a->size==sizeof(BackendApi)&&a->version==BackendVersion&&a->create&&a->submit&&a->poll&&a->close;}
using GetBackendApi=const BackendApi* (__cdecl *)(uint32_t);
}
