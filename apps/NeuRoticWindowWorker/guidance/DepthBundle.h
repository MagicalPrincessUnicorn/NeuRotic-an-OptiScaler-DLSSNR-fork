// GPL-3.0. Adapted from NeuRotic IndependentDepthBundle.h; provenance in references/window-nr/guidance.
#pragma once
#include <Windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <array>
#include <vector>
#include <string>
namespace nrw::guidance {
inline std::string HashFile(HANDLE file) {
    LARGE_INTEGER start{};if(!SetFilePointerEx(file,start,nullptr,FILE_BEGIN))return {};
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    struct Algorithm {BCRYPT_ALG_HANDLE a;~Algorithm(){BCryptCloseAlgorithmProvider(a,0);}} a{algorithm};
    DWORD size=0,read=0;
    if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&read,0)<0 || !size)return {};
    std::vector<uint8_t> object(size);BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)<0)return {};
    struct Hash {BCRYPT_HASH_HANDLE h;~Hash(){BCryptDestroyHash(h);}} h{hash};
    std::array<uint8_t,65536> buffer{};
    for(;;) {if(!ReadFile(file,buffer.data(),DWORD(buffer.size()),&read,nullptr))return {};if(!read)break;if(BCryptHashData(hash,buffer.data(),read,0)<0)return {};}
    std::array<uint8_t,32> digest{};if(BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)<0)return {};
    std::string result;constexpr char hex[]="0123456789abcdef";
    for(auto byte:digest){result.push_back(hex[byte>>4]);result.push_back(hex[byte&15]);}return result;
}
// Freeze the directory namespace before file verification. Each ancestor is opened
// top-down without following a reparse point and without write/delete sharing.
// Holding only the verified files does not prevent retargeting their parent path.
class DirectoryLeases {
    std::vector<HANDLE> directories_;
    std::filesystem::path root_;
    bool Hold(const std::filesystem::path& path,std::string& reason) {
        // Attribute-only opens do not participate in Windows sharing checks.
        // GENERIC_READ includes directory list access and makes deny-write/delete
        // sharing effective against rename and reparse mutation handles.
        HANDLE directory=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(directory==INVALID_HANDLE_VALUE){reason="Depth directory lease unavailable; path cannot be frozen";return false;}
        directories_.push_back(directory);BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(directory,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
            reason="Depth bundle rejects reparse/non-directory ancestors";return false;
        }
        wchar_t final[32768]{};auto size=GetFinalPathNameByHandleW(directory,final,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!size || size>=32768){reason="Depth directory canonical identity unavailable";return false;}
        std::wstring canonical(final,size);
        if(canonical.starts_with(L"\\\\?\\"))canonical.erase(0,4);
        const auto expected=path.wstring();
        // Reject substituted drives and ambiguous namespace aliases as well as
        // reparses. The loader will use precisely this verified canonical path.
        if(CompareStringOrdinal(canonical.c_str(),int(canonical.size()),expected.c_str(),int(expected.size()),TRUE)!=CSTR_EQUAL) {
            reason="Depth directory canonical path differs from selected local namespace";return false;
        }
        root_=std::move(canonical);return true;
    }
public:
    DirectoryLeases()=default;DirectoryLeases(const DirectoryLeases&)=delete;
    ~DirectoryLeases(){for(auto i=directories_.rbegin();i!=directories_.rend();++i)CloseHandle(*i);}
    const std::filesystem::path& Root()const{return root_;}
    bool Open(const std::filesystem::path& requested,std::string& reason) {
        if(!directories_.empty()){reason="Depth directory namespace already leased";return false;}
        if(!requested.is_absolute()){reason="Depth bundle requires an explicit absolute local path";return false;}
        wchar_t full[32768]{};auto size=GetFullPathNameW(requested.c_str(),32768,full,nullptr);
        if(!size || size>=32768){reason="Depth bundle absolute path unavailable";return false;}
        auto normalized=std::filesystem::path(full).lexically_normal();
        auto drive=normalized.root_name().wstring();
        if(drive.size()!=2 || drive[1]!=L':' || !((drive[0]>=L'A' && drive[0]<=L'Z') || (drive[0]>=L'a' && drive[0]<=L'z'))) {
            reason="Depth bundle requires an ordinary local drive path; remote/device namespaces refused";return false;
        }
        auto path=normalized.root_path();if(!Hold(path,reason))return false;
        for(const auto& component:normalized.relative_path()) {
            if(component.empty() || component==L".")continue;
            path/=component;if(!Hold(path,reason))return false;
        }
        return true;
    }
};
// Directory and file leases both survive verification through module/session
// destruction, preventing rename, junction retargeting, and pinned-file writes.
class DepthBundle {
    DirectoryLeases directories_;
    std::array<HANDLE,4> files_{INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE,INVALID_HANDLE_VALUE};
public:
    DepthBundle()=default;DepthBundle(const DepthBundle&)=delete;
    ~DepthBundle(){for(auto file:files_)if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    const std::filesystem::path& Root()const{return directories_.Root();}
    bool Open(const std::filesystem::path& selected,std::string& reason) {
        if(!directories_.Open(selected,reason))return false;
        const auto& root=directories_.Root();
        constexpr const wchar_t* names[]{L"model.onnx",L"DirectML.dll",L"onnxruntime.dll",L"onnxruntime_providers_shared.dll"};
        constexpr const char* hashes[]{"e60095bfb8270a8672d0165f92e311a38103499fb68d2bfe0c20d19b58632059","9c9e6d822561c6c41b90e6994b3e8857cf1d66dbfb1e0c4c799c7c89b4e92da1","e7eedec6a6f26dc39dc948276a75ef6d2bee3fff944d874ceed0bbd3b97bff40","265c8daf29637cb259cac8be9f08f2cd45f3883f0f0e4949cbfddd5b4cbec3b6"};
        for(size_t i=0;i<files_.size();++i) {
            if(files_[i]!=INVALID_HANDLE_VALUE){reason="Depth bundle already opened";return false;}
            files_[i]=CreateFileW((root/names[i]).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            BY_HANDLE_FILE_INFORMATION info{};
            if(files_[i]==INVALID_HANDLE_VALUE || !GetFileInformationByHandle(files_[i],&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) || HashFile(files_[i])!=hashes[i]) {reason="Depth bundle missing, reparse file or hash mismatch: "+std::string(i==0?"model.onnx":i==1?"DirectML.dll":i==2?"onnxruntime.dll":"onnxruntime_providers_shared.dll");return false;}
        }
        return true;
    }
};
inline bool LoadedFrom(HMODULE module,const std::filesystem::path& expected) {
    wchar_t path[32768]{};auto size=GetModuleFileNameW(module,path,32768);
    if(!size || size>=32768)return false;std::error_code error;
    return std::filesystem::equivalent(expected,path,error) && !error;
}
}
