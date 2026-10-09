#include <menu/Localization.h>
#pragma once
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <cstring>

namespace nh {
// Preferences/caches only, never installer-owned game files. Keep every parent
// pinned until an exclusively owned sibling is flushed and renamed by HANDLE.
// No predictable temporary leaf is opened or truncated, including hardlinks.
inline void SaveUserFile(const std::filesystem::path& path,std::string_view bytes) {
    auto name=path.wstring();
    if(!path.is_absolute()||name.starts_with(L"\\\\")||name.find(L':',2)!=std::wstring::npos||
       name.find(L'\0')!=std::wstring::npos||path.lexically_normal()!=path||path.filename().empty()||bytes.size()>16777216)
        throw std::runtime_error(Neurotic::UiMessage("desktop.userfile.user_data_save_requires_an_ordinary_local_path_a_c5329ab2", "User data save requires an ordinary local path and bounded content"));
    struct Handles {
        std::vector<HANDLE> values;
        ~Handles(){for(auto it=values.rbegin();it!=values.rend();++it)CloseHandle(*it);}
    } pins;
    auto pin=[&](const std::filesystem::path& folder){
        if(GetFileAttributesW(folder.c_str())==INVALID_FILE_ATTRIBUTES){
            auto error=GetLastError();
            if((error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)||
               (!CreateDirectoryW(folder.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS))
                throw std::runtime_error(Neurotic::UiMessage("desktop.userfile.user_data_save_could_not_create_its_folder_1870580f", "User data save could not create its folder"));
        }
        auto handle=CreateFileW(folder.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
                                OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.userfile.user_data_save_could_not_pin_its_folder_761150b3", "User data save could not pin its folder"));
        pins.values.push_back(handle);BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||
           !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error(Neurotic::UiMessage("desktop.userfile.user_data_save_refused_a_linked_folder_e5974806", "User data save refused a linked folder"));
    };
    auto parent=path.parent_path(),walk=parent.root_path();pin(walk);
    for(auto& part:parent.relative_path()){walk/=part;pin(walk);}
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error(Neurotic::UiMessage("desktop.userfile.user_data_save_identifier_unavailable_703c66df", "User data save identifier unavailable"));
    wchar_t suffix[40]{};StringFromGUID2(id,suffix,40);
    auto temporary=parent/(std::wstring(L".neurotic-")+suffix+L".pending");
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error(Neurotic::UiMessage("desktop.userfile.user_data_save_could_not_create_an_exclusive_tem_46b9127c", "User data save could not create an exclusive temporary file"));
    struct Pending {
        HANDLE handle;bool committed=false;
        ~Pending(){if(!committed){FILE_DISPOSITION_INFO disposition{TRUE};SetFileInformationByHandle(handle,FileDispositionInfo,&disposition,sizeof(disposition));}CloseHandle(handle);}
    } pending{file};
    DWORD written=0;
    if(!WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(file))
        throw std::runtime_error(Neurotic::UiMessage("desktop.userfile.user_data_save_failed_the_previous_file_is_uncha_61234ca8", "User data save failed; the previous file is unchanged"));
    // Rename replaces the destination directory entry; it never follows an
    // existing destination hardlink/symlink to overwrite its referent.
    const auto nameBytes=name.size()*sizeof(wchar_t);
    std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO)+nameBytes);
    auto rename=reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename->ReplaceIfExists=TRUE;rename->RootDirectory=nullptr;rename->FileNameLength=DWORD(nameBytes);
    std::memcpy(rename->FileName,name.data(),nameBytes);
    if(!SetFileInformationByHandle(file,FileRenameInfo,rename,DWORD(buffer.size())))
        throw std::runtime_error(Neurotic::UiMessage("desktop.userfile.user_data_save_could_not_replace_the_previous_fi_124672d4", "User data save could not replace the previous file; it is unchanged"));
    pending.committed=true;
}
}
