#pragma once
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <vector>
#include <stdexcept>
namespace nrw {
inline std::filesystem::path SnapshotFolderPath(){
 PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))throw std::runtime_error("Snapshot folder unavailable");
 std::filesystem::path folder=std::filesystem::path(local)/L"NeuRotic"/L"Snapshots";CoTaskMemFree(local);return folder;
}
// Shared by the worker writer and the desktop folder action. Pins every ordinary
// ancestor before descending, so neither path follows a junction during creation.
struct SnapshotFolderLease {
 std::filesystem::path path;
 std::vector<HANDLE> handles;
 SnapshotFolderLease():path(SnapshotFolderPath()){
  try {
   if(!path.is_absolute()||path.wstring().starts_with(L"\\\\"))throw std::runtime_error("Snapshot folder must be local");
   auto pin=[&](const std::filesystem::path& folder){
    auto attributes=GetFileAttributesW(folder.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES&&!CreateDirectoryW(folder.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error("Snapshot folder could not be created");
    HANDLE h=CreateFileW(folder.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Snapshot folder could not be pinned");handles.push_back(h);BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(h,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error("Snapshot folder links are refused");
   };
   auto walk=path.root_path();pin(walk);for(const auto& part:path.relative_path()){walk/=part;pin(walk);}
  }catch(...){Close();throw;}
 }
 SnapshotFolderLease(const SnapshotFolderLease&)=delete;
 void Close(){for(auto h:handles)CloseHandle(h);handles.clear();}
 ~SnapshotFolderLease(){Close();}
};
}
