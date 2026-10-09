#pragma once
#include <dlssnr/CanonicalProviderIdentity.h>
#include <json.hpp>
#include <atomic>
#include <cwctype>
#include <winver.h>
#pragma comment(lib,"version.lib")

namespace Neurotic::Diagnostics::RuntimeProvenance
{
using Json=nlohmann::json;
inline std::atomic<std::uint64_t> nextCall{0};
struct LastError {DWORD value=GetLastError();~LastError(){SetLastError(value);}};
inline bool Enabled()noexcept
{
    LastError preserve;char value[3]{};
    return GetEnvironmentVariableA("NEUROTIC_W03_PROVENANCE",value,sizeof(value))==1&&value[0]=='1';
}
inline Json Module(HMODULE module)
{
    LastError preserve;
    Json j={{"observed",false},{"base",reinterpret_cast<std::uintptr_t>(module)},
        {"path",nullptr},{"sha256",nullptr},{"bytes",nullptr},{"file_version",nullptr},
        {"hash_scope","loaded_path_backing_file_at_snapshot_not_memory_attestation"}};
    if(!module)return j;
    try
    {
        std::wstring path(32768,L'\0');const auto n=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
        if(!n||n>=path.size())throw std::runtime_error("MODULE_PATH_UNAVAILABLE");
        path.resize(n);j["observed"]=true;j["path"]=DlssNr::Canonical::Utf8(path);
        DlssNr::Canonical::LockedFile file(path);j["sha256"]=file.hash;j["bytes"]=file.bytes;
        DWORD ignored=0;const auto size=GetFileVersionInfoSizeW(path.c_str(),&ignored);
        if(size&&size<=1024*1024)
        {
            std::vector<unsigned char> data(size);VS_FIXEDFILEINFO* info=nullptr;UINT length=0;
            if(GetFileVersionInfoW(path.c_str(),0,size,data.data())&&VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&length)&&
               info&&length>=sizeof(*info)&&info->dwSignature==0xfeef04bd)
                j["file_version"]=std::to_string(HIWORD(info->dwFileVersionMS))+"."+std::to_string(LOWORD(info->dwFileVersionMS))+"."+
                    std::to_string(HIWORD(info->dwFileVersionLS))+"."+std::to_string(LOWORD(info->dwFileVersionLS));
        }
    }
    catch(const std::exception& e){j["error"]=e.what();}
    return j;
}
inline Json Address(const void* address)
{
    LastError preserve;HMODULE module=nullptr;
    if(address)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address),&module);
    return {{"address",reinterpret_cast<std::uintptr_t>(address)},{"module",Module(module)}};
}
inline Json LoadedModules()
{
    LastError preserve;Json list=Json::array();
    DlssNr::Canonical::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId()));
    if(snapshot.value==INVALID_HANDLE_VALUE)return {{"error","MODULE_SNAPSHOT_FAILED"},{"modules",list}};
    MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(!Module32FirstW(snapshot.value,&entry))return {{"error","MODULE_ENUMERATION_FAILED"},{"modules",list}};
    do
    {
        std::wstring name=entry.szModule;for(auto& c:name)c=static_cast<wchar_t>(towlower(c));
        if(name.starts_with(L"sl.")||name.find(L"nvngx")!=std::wstring::npos||name.starts_with(L"amd_fidelityfx"))list.push_back(Module(entry.hModule));
    }while(Module32NextW(snapshot.value,&entry));
    return {{"coverage","resident_snapshot_only_transient_loads_may_be_missing"},{"modules",list}};
}
inline Json Process()
{
    LastError preserve;std::wstring cwd(32768,L'\0');const auto n=GetCurrentDirectoryW(static_cast<DWORD>(cwd.size()),cwd.data());
    Json j={{"executable",Module(GetModuleHandleW(nullptr))},{"cwd",nullptr},{"pid",GetCurrentProcessId()}};
    if(n&&n<cwd.size()){cwd.resize(n);j["cwd"]=DlssNr::Canonical::Utf8(cwd);}return j;
}
template<class Sink,class Invoke> auto Call(Sink&& sink,const char* stage,const char* api,Invoke&& invoke)
{
    const auto callId=nextCall.fetch_add(1);
    const auto threadId=GetCurrentThreadId();
    const auto emit=[&](Json event)noexcept{LastError preserve;try{sink(std::move(event));}catch(...){}};
    // Allocation and diagnostic errors are also contained before API entry.
    {LastError preserve;try{emit({{"schema","NeuRotic.RuntimeProvenance/1"},{"stage",stage},{"api",api},{"phase","enter"},
        {"call_id",callId},{"thread_id",threadId}});}catch(...){}}
    auto result=std::forward<Invoke>(invoke)();
    LastError preserve;
    try{emit({{"schema","NeuRotic.RuntimeProvenance/1"},{"stage",stage},{"api",api},{"phase","return"},
        {"call_id",callId},{"thread_id",threadId},
        {"result",static_cast<std::uint64_t>(result)},{"interpretation",static_cast<std::uint64_t>(result)==0?"API_OK":"NONZERO_API_RESULT"}});}catch(...){}
    return result;
}
}
