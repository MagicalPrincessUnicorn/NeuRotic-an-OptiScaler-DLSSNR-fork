// GPL-3.0. Managed import destination namespace lifetime.
#pragma once
#include <Windows.h>
#include <winternl.h>
#include <filesystem>
#include <vector>
#include <string>
#include <cstring>
namespace nrw::detail {
class ImportDirectoryLeases {
    std::vector<HANDLE> directories_;
    std::filesystem::path root_;
    bool Hold(const std::filesystem::path& path,bool create,std::string& reason) {
        constexpr DWORD flags=FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT;
        HANDLE directory=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,flags,nullptr);
        if(directory==INVALID_HANDLE_VALUE && create) {
            auto error=GetLastError();
            if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND) {
                // The immediate parent and every earlier ancestor remain leased.
                // Never create a whole unchecked chain or follow an existing reparse.
                if(!CreateDirectoryW(path.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) {reason="Cannot create managed Models directory";return false;}
                directory=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,flags,nullptr);
            }
        }
        if(directory==INVALID_HANDLE_VALUE){reason="Cannot freeze managed Models directory namespace";return false;}
        directories_.push_back(directory);BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(directory,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {reason="Managed Models path contains reparse/non-directory ancestor";return false;}
        wchar_t final[32768]{};auto size=GetFinalPathNameByHandleW(directory,final,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!size || size>=32768){reason="Managed Models canonical directory identity unavailable";return false;}
        std::wstring canonical(final,size);if(canonical.starts_with(L"\\\\?\\"))canonical.erase(0,4);
        auto expected=path.wstring();
        if(CompareStringOrdinal(canonical.c_str(),int(canonical.size()),expected.c_str(),int(expected.size()),TRUE)!=CSTR_EQUAL) {reason="Managed Models canonical path differs from selected local namespace";return false;}
        root_=std::move(canonical);return true;
    }
public:
    ImportDirectoryLeases()=default;ImportDirectoryLeases(const ImportDirectoryLeases&)=delete;
    ~ImportDirectoryLeases(){for(auto i=directories_.rbegin();i!=directories_.rend();++i)CloseHandle(*i);}
    const std::filesystem::path& Root()const{return root_;}
    bool Publish(HANDLE temporary,const std::wstring& name,std::string& reason)const {
        if(directories_.empty() || temporary==INVALID_HANDLE_VALUE || name.empty() || name==L"." || name==L".." || name.find_first_of(L"/\\:")!=std::wstring::npos) {reason="Invalid owned import publication";return false;}
        wchar_t final[32768]{};auto size=GetFinalPathNameByHandleW(temporary,final,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
        if(!size || size>=32768){reason="Owned import parent identity unavailable";return false;}
        std::wstring path(final,size);if(path.starts_with(L"\\\\?\\"))path.erase(0,4);
        auto parent=std::filesystem::path(path).parent_path().wstring(),expected=root_.wstring();
        if(CompareStringOrdinal(parent.c_str(),int(parent.size()),expected.c_str(),int(expected.size()),TRUE)!=CSTR_EQUAL){reason="Owned import file is outside leased publication directory";return false;}
        std::vector<uint8_t> bytes(sizeof(FILE_RENAME_INFO)+(name.size()+1)*sizeof(wchar_t));
        auto* info=reinterpret_cast<FILE_RENAME_INFO*>(bytes.data());info->ReplaceIfExists=FALSE;
        // Native same-directory rename uses a simple leaf and no target-directory
        // reopen. Win32 SetFileInformationByHandle rejects non-null RootDirectory
        // here; path MoveFileEx reopens the parent and conflicts with its lease.
        info->RootDirectory=nullptr;info->FileNameLength=DWORD(name.size()*sizeof(wchar_t));
        std::memcpy(info->FileName,name.c_str(),(name.size()+1)*sizeof(wchar_t));
        // Microsoft NtSetInformationFile / FILE_RENAME_INFORMATION contract:
        // https://learn.microsoft.com/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information
        using SetInformation=NTSTATUS (NTAPI*)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
        auto set=reinterpret_cast<SetInformation>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtSetInformationFile"));
        IO_STATUS_BLOCK status{};
        const auto result=set?set(temporary,&status,info,ULONG(bytes.size()),static_cast<FILE_INFORMATION_CLASS>(10)):LONG(0xc0000002);
        if(result<0){reason="Cannot publish owned model import (NTSTATUS "+std::to_string(static_cast<uint32_t>(result))+")";return false;}
        return true;
    }
    static bool Rollback(HANDLE temporary,std::string& reason) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        if(!SetFileInformationByHandle(temporary,FileDispositionInfo,&disposition,sizeof(disposition))) {reason="Cannot retire owned import file (Windows "+std::to_string(GetLastError())+")";return false;}
        return true;
    }
    bool Open(const std::filesystem::path& requested,std::string& reason) {
        if(!directories_.empty()){reason="Managed Models namespace already leased";return false;}
        if(!requested.is_absolute()){reason="Managed Models require an absolute local drive path";return false;}
        wchar_t full[32768]{};auto size=GetFullPathNameW(requested.c_str(),32768,full,nullptr);
        if(!size || size>=32768){reason="Managed Models absolute path unavailable";return false;}
        auto normalized=std::filesystem::path(full).lexically_normal();auto drive=normalized.root_name().wstring();
        if(drive.size()!=2 || drive[1]!=L':' || !((drive[0]>=L'A' && drive[0]<=L'Z') || (drive[0]>=L'a' && drive[0]<=L'z'))) {reason="Managed Models require an ordinary local drive path";return false;}
        auto path=normalized.root_path();if(!Hold(path,false,reason))return false;
        for(const auto& component:normalized.relative_path()) {if(component.empty() || component==L".")continue;path/=component;if(!Hold(path,true,reason))return false;}
        return true;
    }
};
}
