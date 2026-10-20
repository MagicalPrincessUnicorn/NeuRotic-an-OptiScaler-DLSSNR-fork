// SPDX-License-Identifier: MIT
// Deterministic memory/API doubles for provider_state_test ONLY. Never included
// by the production addon; these do not validate the Windows ABI or loader.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <set>
#include <functional>
using DWORD=uint32_t; using WORD=uint16_t; using BOOL=int;
using SIZE_T=size_t;
using HMODULE=void*;using HANDLE=void*;using LPCWSTR=const wchar_t*;using FARPROC=void(*)();
constexpr BOOL FALSE=0;
constexpr DWORD MEM_IMAGE=0x1000000,MEM_MAPPED=0x40000,MEM_COMMIT=0x1000;
constexpr DWORD PAGE_NOACCESS=1,PAGE_READONLY=2,PAGE_READWRITE=4,PAGE_WRITECOPY=8;
constexpr DWORD PAGE_EXECUTE_READ=32,PAGE_EXECUTE_READWRITE=64,PAGE_EXECUTE_WRITECOPY=128,PAGE_GUARD=256;
constexpr WORD IMAGE_DOS_SIGNATURE=0x5a4d,IMAGE_FILE_MACHINE_AMD64=0x8664,IMAGE_NT_OPTIONAL_HDR64_MAGIC=0x20b;
constexpr DWORD IMAGE_NT_SIGNATURE=0x4550,IMAGE_SCN_MEM_READ=0x40000000,IMAGE_SCN_MEM_EXECUTE=0x20000000;
constexpr DWORD GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS=4;
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
struct IMAGE_DOS_HEADER {WORD e_magic;unsigned char reserved[58];int32_t e_lfanew;};
struct IMAGE_FILE_HEADER {WORD Machine,NumberOfSections;DWORD TimeDateStamp,PointerToSymbolTable,NumberOfSymbols;WORD SizeOfOptionalHeader,Characteristics;};
struct IMAGE_OPTIONAL_HEADER64 {WORD Magic;unsigned char reserved[54];DWORD SizeOfImage,SizeOfHeaders;unsigned char rest[176];};
struct IMAGE_NT_HEADERS64 {DWORD Signature;IMAGE_FILE_HEADER FileHeader;IMAGE_OPTIONAL_HEADER64 OptionalHeader;};
struct IMAGE_SECTION_HEADER {unsigned char Name[8];union {DWORD PhysicalAddress;DWORD VirtualSize;} Misc;DWORD VirtualAddress,SizeOfRawData,PointerToRawData,PointerToRelocations,PointerToLinenumbers;WORD NumberOfRelocations,NumberOfLinenumbers;DWORD Characteristics;};
static_assert(sizeof(IMAGE_DOS_HEADER)==64&&sizeof(IMAGE_NT_HEADERS64)==264&&sizeof(IMAGE_SECTION_HEADER)==40);
struct MEMORY_BASIC_INFORMATION {void* BaseAddress=nullptr;void* AllocationBase=nullptr;size_t RegionSize=0;DWORD State=0,Protect=0,Type=0;};
struct SRWLOCK {bool exclusive=false;unsigned shared=0;};
#define SRWLOCK_INIT {}
namespace provider_mock {
struct Region {HMODULE base;size_t bytes;DWORD type=MEM_IMAGE,protect=PAGE_READONLY;};
inline std::vector<Region> regions;
inline std::map<std::pair<HMODULE,std::string>,FARPROC> exports;
inline std::map<HMODULE,std::string> paths;
inline bool retain_ok=true,force_lock_busy=false;
inline unsigned protection_calls=0,fail_protection_call=0,retains=0;
inline unsigned module_releases=0;
inline bool fail_read=false, partial_read=false;
inline std::set<unsigned> fail_protection_calls;
inline std::set<void*> allocations;
inline unsigned frees = 0;
inline bool fail_free = false;
inline std::function<void(void*,DWORD)> on_protect;
}
inline BOOL TryAcquireSRWLockExclusive(SRWLOCK* l){if(provider_mock::force_lock_busy||l->exclusive||l->shared)return 0;l->exclusive=true;return 1;}
inline void AcquireSRWLockExclusive(SRWLOCK* l){l->exclusive=true;}
inline void ReleaseSRWLockExclusive(SRWLOCK* l){l->exclusive=false;}
inline BOOL TryAcquireSRWLockShared(SRWLOCK* l){if(provider_mock::force_lock_busy||l->exclusive)return 0;++l->shared;return 1;}
inline void ReleaseSRWLockShared(SRWLOCK* l){--l->shared;}
inline size_t VirtualQuery(const void* pointer,MEMORY_BASIC_INFORMATION* out,size_t size){
 auto at=reinterpret_cast<uintptr_t>(pointer);
 for(const auto& r:provider_mock::regions){auto base=reinterpret_cast<uintptr_t>(r.base);
  if(at>=base&&at-base<r.bytes){*out={r.base,r.base,r.bytes,MEM_COMMIT,r.protect,r.type};return size;}}
 return 0;
}
inline BOOL VirtualProtect(void* pointer,size_t,DWORD desired,DWORD* old){
 ++provider_mock::protection_calls;
 if(provider_mock::on_protect)provider_mock::on_protect(pointer,desired);
 if(provider_mock::protection_calls==provider_mock::fail_protection_call ||
    provider_mock::fail_protection_calls.contains(provider_mock::protection_calls))return 0;
 auto at=reinterpret_cast<uintptr_t>(pointer);
 for(auto& r:provider_mock::regions){auto base=reinterpret_cast<uintptr_t>(r.base);
  if(at>=base&&at-base<r.bytes){*old=r.protect;r.protect=desired;return 1;}}
 return 0;
}
inline HANDLE GetCurrentProcess(){return nullptr;}
inline BOOL FreeLibrary(HMODULE){++provider_mock::module_releases;return 1;}
inline BOOL ReadProcessMemory(HANDLE,const void* source,void* destination,SIZE_T bytes,SIZE_T* copied){
 *copied=0;if(provider_mock::fail_read)return 0;
 MEMORY_BASIC_INFORMATION memory{};
 if(!VirtualQuery(source,&memory,sizeof(memory)) || (memory.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return 0;
 const auto offset=reinterpret_cast<uintptr_t>(source)-reinterpret_cast<uintptr_t>(memory.BaseAddress);
 if(bytes>memory.RegionSize-offset)return 0;
 *copied=provider_mock::partial_read?bytes/2:bytes;std::memcpy(destination,source,*copied);return 1;
}
inline BOOL FlushInstructionCache(HANDLE,const void*,size_t){return 1;}
inline FARPROC GetProcAddress(HMODULE module,const char* name){auto it=provider_mock::exports.find({module,name});return it==provider_mock::exports.end()?nullptr:it->second;}
inline BOOL GetModuleHandleExW(DWORD,LPCWSTR address,HMODULE* module){
 if(!provider_mock::retain_ok)return 0;
 *module=reinterpret_cast<void*>(const_cast<wchar_t*>(address));++provider_mock::retains;return 1;
}
inline DWORD GetModuleFileNameA(HMODULE module,char* output,DWORD capacity){
 auto it=provider_mock::paths.find(module);if(it==provider_mock::paths.end())return 0;
 if(it->second.size()+1>capacity)return capacity;
 std::memcpy(output,it->second.c_str(),it->second.size()+1);return static_cast<DWORD>(it->second.size());
}

// The temporal composition test includes midpoint.hpp as well as provider.hpp.
constexpr DWORD MEM_RESERVE = 0x2000, MEM_RELEASE = 0x8000;
inline const IMAGE_SECTION_HEADER* IMAGE_FIRST_SECTION(const IMAGE_NT_HEADERS64* nt) {
  return reinterpret_cast<const IMAGE_SECTION_HEADER*>(
      reinterpret_cast<const unsigned char*>(&nt->OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader);
}
inline void* VirtualAlloc(void*, size_t bytes, DWORD, DWORD protection) {
  void* result = std::malloc(bytes);
  if (result) {
    provider_mock::allocations.insert(result);
    provider_mock::regions.push_back({result, bytes, MEM_COMMIT, protection});
  }
  return result;
}
inline BOOL VirtualFree(void* memory, size_t, DWORD) {
  if (provider_mock::fail_free) return 0;
  ++provider_mock::frees;
  provider_mock::allocations.erase(memory);
  std::erase_if(provider_mock::regions, [memory](const auto& region) { return region.base == memory; });
  std::free(memory);
  return 1;
}
